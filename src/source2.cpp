#include "lsd/source2.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <set>

namespace lsd {
namespace {
bool pointer(Address a) {
    return a >= 0x10000 && a < 0x0000800000000000ULL;
}
Address rip(const Memory& m, Address instruction, std::uint32_t displacement) {
    const auto d = m.need<std::int32_t>(instruction + displacement);
    const auto result = static_cast<std::int64_t>(instruction + displacement + 4) + d;
    if (result < 0 || !pointer(static_cast<Address>(result)))
        throw std::runtime_error("Invalid RIP target");
    return static_cast<Address>(result);
}
bool in_module(Module m, Address a, std::size_t n = 1) {
    return a >= m.base && a - m.base <= m.size && n <= m.size - (a - m.base);
}
std::string map_basename(std::string name) {
    const auto pos = name.find_last_of("/\\");
    if (pos != std::string::npos)
        name = name.substr(pos + 1);
    const auto dot = name.find('.');
    if (dot != std::string::npos)
        name.resize(dot);
    return name;
}
} // namespace
std::optional<std::string> Memory::string(Address address, std::size_t max) const {
    if (!pointer(address) || max > 4096)
        return {};
    std::string result;
    result.reserve(max);
    for (std::size_t i = 0; i < max; ++i) {
        const auto c = read<char>(address + i);
        if (!c)
            return {};
        if (*c == 0)
            return result;
        result += *c;
    }
    return {}; // Unterminated remote strings are invalid, not silently truncated.
}
Profile Profile::load(const std::filesystem::path& path) {
    if (std::filesystem::file_size(path) > 2 * 1024 * 1024)
        throw std::runtime_error("Profile exceeds 2 MiB");
    std::ifstream f(path);
    if (!f)
        throw std::runtime_error("Cannot open build profile");
    nlohmann::json j;
    f >> j;
    Profile p;
    p.build = j.at("build");
    p.offsets = j.at("offsets");
    p.layout = j.at("layout");
    p.fields = j.at("fields").get<Fields>();
    if (p.build <= 0 || p.fields.empty())
        throw std::runtime_error("Invalid build profile");
    for (const auto& [type, fields] : p.fields)
        for (const auto& [name, offset] : fields) {
            if (type.empty() || name.empty() || offset > 1'000'000)
                throw std::runtime_error("Invalid reflected field in profile");
        }
    const auto stride = p.layout.at("entities").at("stride").get<std::uint32_t>();
    const auto bone_stride = p.layout.at("bones").at("stride").get<std::uint32_t>();
    if (stride < 16 || stride > 1024 || bone_stride < sizeof(Vec3) || bone_stride > 1024)
        throw std::runtime_error("Invalid profile stride");
    const auto& indices = p.layout.at("bones").at("indices");
    if (!indices.is_array() || indices.size() != 3)
        throw std::runtime_error("Profile needs three bone indices");
    for (const auto& index : indices)
        if (index.get<std::uint32_t>() > 255)
            throw std::runtime_error("Invalid profile bone index");
    return p;
}
std::uint32_t Profile::offset(const std::string& m, const std::string& n) const {
    return offsets.at(m).at(n).get<std::uint32_t>();
}
std::uint32_t Profile::field(const std::string& t, const std::string& n) const {
    return fields.at(t).at(n);
}
std::optional<Address> scan_rip(const Memory& memory, Module module, const std::string& signature,
                                std::uint32_t displacement) {
    // Scan executable PE sections only. Never fill unreadable regions with zeroes.
    if (memory.need<std::uint16_t>(module.base) != 0x5A4D)
        throw std::runtime_error("Invalid DOS header");
    const auto pe = memory.need<std::uint32_t>(module.base + 0x3C);
    if (pe > module.size || !in_module(module, module.base + pe, 24))
        throw std::runtime_error("Invalid PE header");
    const auto nt = module.base + pe;
    if (memory.need<std::uint32_t>(nt) != 0x4550)
        throw std::runtime_error("Invalid PE signature");
    const auto count = memory.need<std::uint16_t>(nt + 6), optional = memory.need<std::uint16_t>(nt + 20);
    if (count > 96)
        throw std::runtime_error("Invalid PE sections");
    auto pattern = parse_pattern(signature);
    std::optional<Address> found;
    for (std::uint16_t i = 0; i < count; ++i) {
        const Address section = nt + 24 + optional + static_cast<Address>(i) * 40;
        if (!in_module(module, section, 40))
            throw std::runtime_error("Invalid PE section table");
        const auto flags = memory.need<std::uint32_t>(section + 36);
        if (!(flags & 0x20000000))
            continue;
        const auto size = memory.need<std::uint32_t>(section + 8),
                   rva = memory.need<std::uint32_t>(section + 12);
        if (!in_module(module, module.base + rva, size) || size > 128 * 1024 * 1024)
            throw std::runtime_error("Invalid code section");
        // Read 64 KiB blocks with overlap, including signatures crossing block boundaries.
        for (std::size_t offset = 0; offset < size; offset += 65536) {
            const auto bytes = std::min<std::size_t>(65536 + pattern.size() - 1, size - offset);
            std::vector<std::uint8_t> buffer(bytes);
            if (!memory.read_bytes(module.base + rva + offset, buffer.data(), buffer.size()))
                throw std::runtime_error("Unreadable code section");
            auto match = unique_pattern(buffer, pattern);
            if (match && *match < 65536) {
                if (found)
                    throw std::runtime_error("Ambiguous signature across sections");
                found = rip(memory, module.base + rva + offset + *match, displacement);
            }
        }
    }
    if (found && !in_module(module, *found, 4))
        throw std::runtime_error("Signature resolved outside module");
    return found;
}
Address find_interface(const Memory& m, Module module, const std::string& wanted) {
    const auto pe = m.need<std::uint32_t>(module.base + 0x3C);
    const Address nt = module.base + pe;
    if (!in_module(module, nt, 264) || m.need<std::uint32_t>(nt) != 0x4550 ||
        m.need<std::uint16_t>(nt + 24) != 0x20B)
        throw std::runtime_error("Interface module is not PE32+");
    const auto rva = m.need<std::uint32_t>(nt + 24 + 112), size = m.need<std::uint32_t>(nt + 24 + 116);
    const Address exports = module.base + rva;
    if (!rva || !in_module(module, exports, std::max<std::uint32_t>(size, 40)))
        throw std::runtime_error("No valid exports");
    const auto count = m.need<std::uint32_t>(exports + 24),
               function_count = m.need<std::uint32_t>(exports + 20);
    const auto functions = module.base + m.need<std::uint32_t>(exports + 28);
    const auto names = module.base + m.need<std::uint32_t>(exports + 32),
               ordinals = module.base + m.need<std::uint32_t>(exports + 36);
    if (count > 100000 || function_count > 100000 || !in_module(module, names, count * 4ULL) ||
        !in_module(module, ordinals, count * 2ULL) || !in_module(module, functions, function_count * 4ULL))
        throw std::runtime_error("Invalid export arrays");
    Address factory = 0;
    for (std::uint32_t i = 0; i < count; ++i) {
        const Address address = module.base + m.need<std::uint32_t>(names + i * 4ULL);
        if (!in_module(module, address))
            continue;
        if (m.string(address).value_or("") != "CreateInterface")
            continue;
        const auto ordinal = m.need<std::uint16_t>(ordinals + i * 2ULL);
        if (ordinal >= function_count)
            break;
        factory = module.base + m.need<std::uint32_t>(functions + ordinal * 4ULL);
        break;
    }
    if (!factory || !in_module(module, factory, 7) || (factory >= exports && factory < exports + size))
        throw std::runtime_error("CreateInterface export missing/forwarded");
    if (m.need<std::uint8_t>(factory) == 0xE9)
        factory = rip(m, factory, 1);
    if (!in_module(module, factory, 7))
        throw std::runtime_error("Invalid CreateInterface jump");
    const auto op = m.need<std::array<std::uint8_t, 3>>(factory);
    if (op != std::array<std::uint8_t, 3>{0x48, 0x8B, 0x0D})
        throw std::runtime_error("Unsupported CreateInterface prologue");
    Address node = m.need<Address>(rip(m, factory, 3));
    std::set<Address> seen;
    for (int i = 0; pointer(node) && i < 4096; ++i) {
        if (!seen.insert(node).second)
            throw std::runtime_error("Cyclic interface list");
        const Address fn = m.need<Address>(node), name = m.need<Address>(node + 8);
        if (m.string(name).value_or("") == wanted) {
            if (!in_module(module, fn, 7))
                throw std::runtime_error("Interface factory outside module");
            const auto code = m.need<std::array<std::uint8_t, 3>>(fn);
            const auto instance = rip(m, fn, 3);
            if (code == std::array<std::uint8_t, 3>{0x48, 0x8D, 0x05})
                return instance;
            if (code == std::array<std::uint8_t, 3>{0x48, 0x8B, 0x05})
                return m.need<Address>(instance);
            throw std::runtime_error("Unsupported interface factory");
        }
        node = m.need<Address>(node + 16);
    }
    throw std::runtime_error("SchemaSystem_001 registration not found");
}
Fields read_schema(const Memory& m, Address system, const nlohmann::json& layout) {
    const auto& s = layout.at("schema");
    const Address vector = system + s.at("scopes").get<Address>();
    const auto count = m.need<std::int32_t>(vector);
    const auto scopes = m.need<Address>(vector + 8);
    if (count < 1 || count > 128 || !pointer(scopes))
        throw std::runtime_error("Invalid schema scopes");
    Fields fields;
    std::set<Address> bindings;
    for (int i = 0; i < count; ++i) {
        const auto scope = m.need<Address>(scopes + i * 8ULL);
        if (m.string(scope + 8, 256).value_or("") != "client.dll")
            continue;
        const auto hash = scope + s.at("class_bindings").get<Address>();
        const auto consume = [&](Address binding) {
            if (!pointer(binding) || !bindings.insert(binding).second)
                return;
            const auto name = m.string(m.need<Address>(binding + 8));
            if (!name || name->empty())
                return;
            const auto size = m.need<std::int32_t>(binding + 0x20);
            const auto fc = m.need<std::int16_t>(binding + 0x24);
            const auto data = m.need<Address>(binding + 0x30);
            if (size <= 0 || size > 1'000'000 || fc < 0 || fc > 4096)
                throw std::runtime_error("Invalid schema class");
            for (int k = 0; k < fc; ++k) {
                const Address f = data + static_cast<Address>(k) * 32;
                const auto n = m.string(m.need<Address>(f));
                const auto off = m.need<std::int32_t>(f + 16);
                if (!n || n->empty() || off < 0 || off >= size)
                    throw std::runtime_error("Invalid schema field");
                fields[*name][*n] = static_cast<std::uint32_t>(off);
            }
        };
        const auto walk = [&](Address node) {
            std::set<Address> seen;
            while (pointer(node)) {
                if (seen.size() >= 65536 || !seen.insert(node).second)
                    throw std::runtime_error("Invalid schema chain");
                consume(m.need<Address>(node + 16));
                node = m.need<Address>(node + 8);
            }
        };
        for (int b = 0; b < 256; ++b) {
            const Address bucket = hash + 0x60 + static_cast<Address>(b) * 24;
            walk(m.need<Address>(bucket + 8));
            walk(m.need<Address>(bucket + 16));
        }
        // Free-block chain used by current UtlTsHash, as inspected by cs2-dumper.
        auto blob = m.need<Address>(hash + 0x20);
        std::set<Address> seen;
        while (pointer(blob)) {
            if (seen.size() >= 65536 || !seen.insert(blob).second)
                throw std::runtime_error("Invalid schema free-block chain");
            consume(m.need<Address>(blob + 16));
            blob = m.need<Address>(blob);
        }
    }
    if (fields.empty())
        throw std::runtime_error("No client.dll schema fields");
    return fields;
}
Source2::Source2(const Memory& memory, Module client, Module engine, Module schema, Profile profile)
    : memory_(memory), client_(client), engine_(engine), profile_(std::move(profile)) {
    const auto build_address =
        scan_rip(memory_, engine_, "89 05 ? ? ? ? 48 8D 0D ? ? ? ? FF 15 ? ? ? ? 48 8B 0D", 2);
    if (!build_address)
        throw std::runtime_error("Build signature missing; cannot trust fallback offsets");
    const auto actual = memory_.need<int>(*build_address);
    if (actual != profile_.build)
        throw std::runtime_error("Unsupported CS2 build " + std::to_string(actual) + "; profile is " +
                                 std::to_string(profile_.build));
    const auto resolve = [&](const char* name, const char* pattern) {
        auto result = scan_rip(memory_, client_, pattern);
        if (result)
            return *result;
        const Address fallback = client_.base + profile_.offset("client.dll", name);
        if (!in_module(client_, fallback, 8))
            throw std::runtime_error("Fallback outside client.dll");
        diagnostics_ += std::string(name) + ": build fallback; ";
        return fallback;
    };
    entity_list_ = resolve("dwEntityList", "48 89 0D ? ? ? ? E9 ? ? ? ? CC");
    controller_ = resolve("dwLocalPlayerController", "48 8B 05 ? ? ? ? 41 89 BE");
    matrix_ = resolve("dwViewMatrix", "48 8D 0D ? ? ? ? 48 C1 E0 06");
    globals_ = resolve("dwGlobalVars", "48 89 15 ? ? ? ? 48 89 42");
    view_angles_ = client_.base + profile_.offset("client.dll", "dwViewAngles");
    if (!in_module(client_, view_angles_, sizeof(Angles)))
        throw std::runtime_error("View angle offset outside module");
    diagnostics_ += "view angles: build fallback; ";
    try {
        auto system = find_interface(memory_, schema, "SchemaSystem_001");
        auto dynamic = read_schema(memory_, system, profile_.layout);
        // Require every consumed field before accepting the runtime schema as one coherent set.
        for (const auto& [type, fields] : profile_.fields)
            for (const auto& [name, off] : fields) {
                (void)off;
                (void)dynamic.at(type).at(name);
            }
        fields_ = std::move(dynamic);
        diagnostics_ += "runtime schema loaded";
    } catch (const std::exception& e) {
        fields_ = profile_.fields;
        diagnostics_ += "schema fallback: " + std::string(e.what());
    }
}
std::uint32_t Source2::field(const std::string& t, const std::string& n) const {
    return fields_.at(t).at(n);
}
Address Source2::entity(Address list, std::uint32_t index) const {
    if (index > 0x7FFF || !pointer(list))
        return 0;
    const auto& l = profile_.layout.at("entities");
    const auto chunk = memory_.read<Address>(list + l.at("chunks").get<Address>() + 8ULL * (index >> 9));
    if (!chunk || !pointer(*chunk))
        return 0;
    const auto p = memory_.read<Address>(*chunk + l.at("stride").get<Address>() * (index & 0x1FF));
    return p && pointer(*p) ? *p : 0;
}
Address Source2::handle_entity(Address list, std::uint32_t handle) const {
    if (handle == 0 || handle == 0xFFFFFFFF)
        return 0;
    const auto pawn = entity(list, handle & 0x7FFF);
    if (!pawn)
        return 0;
    const auto id = memory_.read<Address>(pawn + field("CEntityInstance", "m_pEntity"));
    if (!id || !pointer(*id))
        return 0;
    const auto actual = memory_.read<std::uint32_t>(
        *id + profile_.layout.at("entities").at("identity_handle").get<Address>());
    return actual && *actual == handle ? pawn : 0;
}
Address Source2::local_pawn(Address list) const {
    const auto controller = memory_.read<Address>(controller_);
    if (!controller || !pointer(*controller))
        return 0;
    const auto handle =
        memory_.read<std::uint32_t>(*controller + field("CCSPlayerController", "m_hPlayerPawn"));
    return handle ? handle_entity(list, *handle) : 0;
}
bool Source2::same_local_pawn(Address pawn) const {
    const auto list = memory_.read<Address>(entity_list_);
    return pawn && list && local_pawn(*list) == pawn;
}
Address Source2::crosshair_pawn(Address local) const {
    const auto type = fields_.find("C_CSPlayerPawn");
    if (!local || type == fields_.end() || !type->second.contains("m_iIDEntIndex"))
        return 0;
    const auto index = memory_.read<std::int32_t>(local + type->second.at("m_iIDEntIndex"));
    const auto list = memory_.read<Address>(entity_list_);
    if (!index || *index <= 0 || *index > 0x7FFF || !list)
        return 0;
    return entity(*list, static_cast<std::uint32_t>(*index));
}
std::optional<Vec3> Source2::origin(Address pawn) const {
    const auto scene = memory_.read<Address>(pawn + field("C_BaseEntity", "m_pGameSceneNode"));
    if (!scene || !pointer(*scene))
        return {};
    const auto v = memory_.read<Vec3>(*scene + field("CGameSceneNode", "m_vecAbsOrigin"));
    return v && finite(*v) && length(*v) < 100000 ? v : std::nullopt;
}
Frame Source2::sample() const {
    Frame f;
    const auto list = memory_.need<Address>(entity_list_);
    f.local_pawn = local_pawn(list);
    if (!f.local_pawn)
        throw std::runtime_error("No local pawn (menu, loading, spectator, or incompatible entity layout)");
    f.matrix = memory_.need<Matrix>(matrix_);
    f.angles = memory_.need<Angles>(view_angles_);
    for (const auto& row : f.matrix.m)
        for (float v : row)
            if (!std::isfinite(v))
                throw std::runtime_error("Invalid view matrix");
    if (!std::isfinite(f.angles.pitch) || !std::isfinite(f.angles.yaw) || std::abs(f.angles.pitch) > 90 ||
        std::abs(f.angles.yaw) > 360)
        throw std::runtime_error("Invalid view angles");
    const auto position = origin(f.local_pawn);
    if (!position)
        throw std::runtime_error("Invalid local origin");
    const auto eye_offset = memory_.need<Vec3>(f.local_pawn + field("C_BaseModelEntity", "m_vecViewOffset"));
    if (!finite(eye_offset) || eye_offset.z < 0 || eye_offset.z > 100)
        throw std::runtime_error("Invalid view offset");
    f.eye = *position + eye_offset;
    f.team = memory_.need<std::uint8_t>(f.local_pawn + field("C_BaseEntity", "m_iTeamNum"));
    f.alive = memory_.need<int>(f.local_pawn + field("C_BaseEntity", "m_iHealth")) > 0 &&
              memory_.need<std::uint8_t>(f.local_pawn + field("C_BaseEntity", "m_lifeState")) == 0;
    const auto globals = memory_.read<Address>(globals_);
    if (globals && pointer(*globals)) {
        const auto name = memory_.read<Address>(*globals + profile_.layout.at("map_name").get<Address>());
        if (name)
            f.map = map_basename(memory_.string(*name, 256).value_or(""));
    }
    for (std::uint32_t i = 1; i <= 64; ++i) {
        const Address controller = entity(list, i);
        if (!controller)
            continue;
        const auto handle =
            memory_.read<std::uint32_t>(controller + field("CCSPlayerController", "m_hPlayerPawn"));
        if (!handle)
            continue;
        const Address pawn = handle_entity(list, *handle);
        if (!pawn || pawn == f.local_pawn)
            continue;
        Player p;
        p.pawn = pawn;
        const auto health = memory_.read<int>(pawn + field("C_BaseEntity", "m_iHealth"));
        const auto life = memory_.read<std::uint8_t>(pawn + field("C_BaseEntity", "m_lifeState"));
        const auto team = memory_.read<std::uint8_t>(pawn + field("C_BaseEntity", "m_iTeamNum"));
        const auto pos = origin(pawn);
        if (!health || *health <= 0 || *health > 1000 || !life || *life != 0 || !team ||
            (*team != 2 && *team != 3) || !pos)
            continue;
        p.health = *health;
        p.team = *team;
        p.origin = *pos;
        p.max_health =
            std::max(p.health, memory_.read<int>(pawn + field("C_BaseEntity", "m_iMaxHealth")).value_or(100));
        p.name = memory_.string(controller + field("CBasePlayerController", "m_iszPlayerName"), 128)
                     .value_or("Unknown");
        const auto node = memory_.read<Address>(pawn + field("C_BaseEntity", "m_pGameSceneNode"));
        if (node && pointer(*node)) {
            const auto dormant = memory_.read<std::uint8_t>(*node + field("CGameSceneNode", "m_bDormant"));
            if (!dormant || *dormant)
                continue;
            const auto& layout = profile_.layout.at("bones");
            const auto data = memory_.read<Address>(*node + field("CSkeletonInstance", "m_modelState") +
                                                    layout.at("data").get<Address>());
            if (data && pointer(*data))
                for (int b = 0; b < 3; ++b) {
                    const auto index = layout.at("indices").at(b).get<Address>();
                    const auto v = memory_.read<Vec3>(*data + layout.at("stride").get<Address>() * index);
                    if (v && finite(*v) && length(*v - p.origin) < 128)
                        p.bones[b] = *v;
                }
        }
        p.weapon = "Unknown";
        const auto services = memory_.read<Address>(pawn + field("C_BasePlayerPawn", "m_pWeaponServices"));
        if (services && pointer(*services)) {
            const auto wh =
                memory_.read<std::uint32_t>(*services + field("CPlayer_WeaponServices", "m_hActiveWeapon"));
            const Address w = wh ? handle_entity(list, *wh) : 0;
            if (w) {
                const auto id = memory_.read<Address>(w + field("CEntityInstance", "m_pEntity"));
                const auto name = id ? memory_.read<Address>(*id + field("CEntityIdentity", "m_designerName"))
                                     : std::nullopt;
                if (name) {
                    p.weapon = memory_.string(*name).value_or("Unknown");
                    if (p.weapon.starts_with("weapon_"))
                        p.weapon.erase(0, 7);
                }
            }
        }
        // Reject an entity that was recycled while sampling it.
        if (handle_entity(list, *handle) == pawn)
            f.players.push_back(std::move(p));
    }
    if (!same_local_pawn(f.local_pawn))
        throw std::runtime_error("Local pawn changed during sampling");
    f.crosshair_pawn = crosshair_pawn(f.local_pawn);
    return f;
}
} // namespace lsd
