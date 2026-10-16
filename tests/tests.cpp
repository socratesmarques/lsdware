#include "lsd/config.hpp"
#include "lsd/source2.hpp"
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <stdexcept>

using namespace lsd;
namespace {
int checks = 0;
void require(bool condition, const char* why) {
    ++checks;
    if (!condition)
        throw std::runtime_error(why);
}
void near(float a, float b, float tolerance = 0.001f) {
    require(std::abs(a - b) < tolerance, "Numeric result outside tolerance");
}
template <class F> void throws(F f) {
    bool failed = false;
    try {
        f();
    } catch (const std::exception&) {
        failed = true;
    }
    require(failed, "Expected rejection");
}
class FixtureMemory : public Memory {
  public:
    static constexpr Address base = 0x100000;
    std::vector<std::uint8_t> bytes = std::vector<std::uint8_t>(0x400000);
    bool read_bytes(Address a, void* out, std::size_t size) const override {
        if (a < base || a - base > bytes.size() || size > bytes.size() - (a - base))
            return false;
        std::memcpy(out, bytes.data() + a - base, size);
        return true;
    }
    template <class T> void put(Address a, T value) {
        if (a < base || a - base + sizeof(T) > bytes.size())
            throw std::runtime_error("Fixture bounds");
        std::memcpy(bytes.data() + a - base, &value, sizeof(T));
    }
    void string(Address a, const std::string& s) {
        for (std::size_t i = 0; i <= s.size(); ++i)
            put<char>(a + i, i < s.size() ? s[i] : 0);
    }
    void pe(Module mod) {
        put<std::uint16_t>(mod.base, 0x5A4D);
        put<std::uint32_t>(mod.base + 0x3C, 0x80);
        const auto nt = mod.base + 0x80;
        put<std::uint32_t>(nt, 0x4550);
        put<std::uint16_t>(nt + 6, 1);
        put<std::uint16_t>(nt + 20, 240);
        put<std::uint16_t>(nt + 24, 0x20B);
        const auto section = nt + 24 + 240;
        put<std::uint32_t>(section + 8, 0x100);
        put<std::uint32_t>(section + 12, 0x400);
        put<std::uint32_t>(section + 36, 0x60000020);
    }
};
void math_tests() {
    auto m = camera_matrix({0, 0, 64}, {0, 0, 0}, 16.0f / 9, 90);
    auto p = project({100, 0, 64}, m, {1920, 1080});
    require(p.has_value(), "Center projects");
    near(p->x, 960);
    near(p->y, 540);
    require(!project({-100, 0, 64}, m, {1920, 1080}), "Behind camera rejected");
    p = project({100, -100, 64}, m, {1920, 1080});
    near(p->x, 1920);
    near(look_at({0, 0, 0}, {1, 1, 0}).yaw, 45);
    near(look_at({0, 0, 0}, {1, 0, 1}).pitch, -45);
    near(angular_distance({0, 179, 0}, {0, -179, 0}), 2, 0.002f);
    auto a = smooth({0, 179, 0}, {0, -179, 0}, 0.1f, 0.01f);
    require(a.yaw > 179, "Smoothing uses shortest wrap");
    const auto one = smooth({0, 0, 0}, {20, 40, 0}, 0.2f, 0.1f);
    auto many = Angles{};
    for (int i = 0; i < 10; ++i)
        many = smooth(many, {20, 40, 0}, 0.2f, 0.01f);
    near(one.pitch, many.pitch);
    near(one.yaw, many.yaw);
    near(smooth({0, 0, 0}, {25, 30, 0}, 0, 0.01f).yaw, 30);
    Frame f;
    f.alive = true;
    f.eye = {0, 0, 0};
    f.team = 2;
    f.local_pawn = 1;
    f.map = "test";
    Player target;
    target.pawn = 2;
    target.team = 3;
    target.health = 100;
    target.bones[0] = Vec3{100, 1, 0};
    target.bones[1] = Vec3{100, 1, -10};
    f.players.push_back(target);
    CollisionScene scene;
    require(select_target(f, Bone::Head, 5, false, scene).has_value(), "Enemy inside FOV selected");
    require(!select_target(f, Bone::Neck, 5, false, scene), "Bone affects cone selection");
    require(!select_target(f, Bone::Head, 5, true, scene), "Unknown LOS cannot pass visibility filter");
    f.players[0].team = 2;
    require(!select_target(f, Bone::Head, 5, false, scene), "Friendly rejected");
    f.players[0].team = 3;
    f.players[0].health = 0;
    require(!select_target(f, Bone::Head, 5, false, scene), "Dead target rejected");
    f.players[0].health = 100;
    f.alive = false;
    require(!select_target(f, Bone::Head, 5, false, scene), "Dead local player rejected");
}
void pattern_tests() {
    const std::vector<std::uint8_t> data = {0x10, 0x48, 0x8B, 0x99, 0x0D};
    const auto pattern = parse_pattern("48 8B ?? 0D");
    require(unique_pattern(data, pattern) == 1, "Wildcard matches");
    require(!unique_pattern(data, parse_pattern("48 89")), "Missing pattern");
    throws([] { parse_pattern("GG"); });
    throws([] { parse_pattern("?? ??"); });
    throws([] { parse_pattern(""); });
    throws([] {
        std::vector<std::uint8_t> b{1, 1};
        unique_pattern(b, parse_pattern("01"));
    });
    FixtureMemory m;
    Module module{0x100000, 0x2000};
    m.pe(module);
    m.put<std::uint8_t>(module.base + 0x400, 0x48);
    m.put<std::uint8_t>(module.base + 0x401, 0x8B);
    m.put<std::uint8_t>(module.base + 0x402, 0x05);
    m.put<std::int32_t>(module.base + 0x403, 0x1000 - 0x407);
    m.put<std::uint8_t>(module.base + 0x407, 0xC3);
    require(scan_rip(m, module, "48 8B 05 ? ? ? ? C3") == module.base + 0x1000, "RIP signed displacement");
    m.put<std::int32_t>(module.base + 0x403, 0x200 - 0x407);
    require(scan_rip(m, module, "48 8B 05 ? ? ? ? C3") == module.base + 0x200, "Negative RIP displacement");
}
void geometry_tests(const std::filesystem::path& data) {
    CollisionScene scene;
    scene.load_obj(data / "training.obj", "training");
    require(scene.size() == 2, "Both wall triangles loaded");
    require(scene.visibility("training", {0, 0, 64}, {500, 0, 64}) == Visibility::Occluded,
            "Wall blocks center ray");
    require(scene.visibility("training", {0, 0, 64}, {500, 200, 64}) == Visibility::Visible,
            "Ray misses wall");
    require(scene.visibility("training", {0, 0, 64}, {200, 0, 64}) == Visibility::Visible,
            "Wall beyond target does not block");
    require(scene.visibility("other", {0, 0, 64}, {500, 0, 64}) == Visibility::Unknown,
            "Wrong map is unknown");
    require(scene.visibility("training", {0, 0, 64}, {0, 0, 64}) == Visibility::Unknown,
            "Zero-length ray rejected");
    std::vector<Triangle> triangles;
    for (int i = 1; i <= 100; ++i) {
        const auto x = static_cast<float>(i * 10);
        triangles.push_back({{x, -10, 0}, {x, 10, 0}, {x, 0, 100}});
    }
    scene.set(triangles, "bvh");
    require(scene.visibility("bvh", {0, 0, 50}, {2000, 0, 50}) == Visibility::Occluded,
            "BVH internal traversal");
    require(scene.visibility("bvh", {0, 100, 50}, {2000, 100, 50}) == Visibility::Visible,
            "Parallel ray outside bounds");
    throws([&] { scene.set({}, "invalid"); });
    require(scene.size() == 100, "Failed replacement preserves loaded geometry");
    Frame f;
    f.alive = true;
    f.team = 2;
    f.map = "bvh";
    f.eye = {0, 0, 50};
    Player p;
    p.pawn = 2;
    p.team = 3;
    p.health = 100;
    p.bones[0] = Vec3{500, 0, 50};
    f.players.push_back(p);
    require(!select_target(f, Bone::Head, 5, true, scene), "Occluded aim target rejected");
}
void config_tests() {
    Config c;
    require(!c.show_fov && !c.show_aim_diagnostics && !c.trigger, "New controls default to off");
    const auto path = std::filesystem::temp_directory_path() / "lsdware-config-test.json";
    c.aim = true;
    c.fov = 12;
    c.smoothing = 0.4f;
    c.bone = Bone::Chest;
    c.show_fov = true;
    c.show_aim_diagnostics = true;
    c.trigger = true;
    c.trigger_key = 0x12;
    c.trigger_delay_ms = 125;
    c.trigger_interval_ms = 350;
    c.save(path);
    auto result = Config::load(path);
    require(result.aim && result.bone == Bone::Chest, "Config roundtrip retains values");
    require(result.show_fov && result.show_aim_diagnostics && result.trigger && result.trigger_key == 0x12 &&
                result.trigger_delay_ms == 125 && result.trigger_interval_ms == 350,
            "New options survive save/load");
    near(result.fov, 12);
    c.fov = 8;
    c.save(path);
    near(Config::load(path).fov, 8);
    auto j = nlohmann::json(c);
    j["aim"].erase("show_fov");
    j["aim"].erase("show_diagnostics");
    j["aim"].erase("trigger");
    j["hotkeys"].erase("trigger");
    auto legacy = j.get<Config>();
    require(!legacy.show_fov && !legacy.show_aim_diagnostics && !legacy.trigger && legacy.trigger_key == 6,
            "Legacy JSON loads with new controls disabled");
    require(legacy.aim && legacy.fov == c.fov, "Legacy user settings are preserved");
    j["hotkeys"]["menu"] = 6;
    legacy = j.get<Config>();
    require(legacy.menu_key == 6 && legacy.trigger_key != 6,
            "Legacy Mouse 5 binding survives trigger hotkey migration");
    j = nlohmann::json(c);
    j["aim"]["trigger"]["interval_ms"] = 0;
    throws([&] { j.get<Config>(); });
    j = nlohmann::json(c);
    j["aim"]["trigger"]["delay_ms"] = -1;
    throws([&] { j.get<Config>(); });
    j = nlohmann::json(c);
    j["hotkeys"]["trigger"] = 1;
    throws([&] { j.get<Config>(); });
    j = nlohmann::json(c);
    j["aim"]["fov_degrees"] = -1;
    throws([&] { j.get<Config>(); });
    j = nlohmann::json(c);
    j["hotkeys"]["aim"] = c.menu_key;
    throws([&] { j.get<Config>(); });
    j = nlohmann::json(c);
    j["version"] = 9;
    throws([&] { j.get<Config>(); });
    c.collision_file = "map.obj";
    throws([&] { c.validate(); });
    c.collision_map = "test";
    c.validate();
    std::filesystem::remove(path);
}
void schema_tests() {
    FixtureMemory m;
    const Address sys = 0x110000, scopes = 0x120000, scope = 0x130000, node = 0x140000, binding = 0x150000,
                  field = 0x160000;
    const nlohmann::json layout = {{"schema", {{"scopes", 0x190}, {"class_bindings", 0x560}}}};
    m.put<int>(sys + 0x190, 1);
    m.put<Address>(sys + 0x198, scopes);
    m.put<Address>(scopes, scope);
    m.string(scope + 8, "client.dll");
    m.put<Address>(scope + 0x560 + 0x60 + 16, node);
    m.put<Address>(node + 16, binding);
    m.put<Address>(binding + 8, 0x170000);
    m.string(0x170000, "C_BaseEntity");
    m.put<int>(binding + 0x20, 0x1000);
    m.put<std::int16_t>(binding + 0x24, 1);
    m.put<Address>(binding + 0x30, field);
    m.put<Address>(field, 0x170100);
    m.string(0x170100, "m_iHealth");
    m.put<int>(field + 16, 0x34C);
    require(read_schema(m, sys, layout).at("C_BaseEntity").at("m_iHealth") == 0x34C,
            "Dynamic schema field read");
    m.put<Address>(node + 8, node);
    throws([&] { read_schema(m, sys, layout); });
    m.put<Address>(node + 8, 0);
    m.put<int>(field + 16, 0x1001);
    throws([&] { read_schema(m, sys, layout); });
    m.put<int>(field + 16, 0);
    require(read_schema(m, sys, layout).at("C_BaseEntity").at("m_iHealth") == 0, "Offset zero is valid");
    m.put<int>(sys + 0x190, 129);
    throws([&] { read_schema(m, sys, layout); });
    // Synthetic PE exports / registration list exercise external CreateInterface parsing.
    Module mod{0x200000, 0x10000};
    m.pe(mod);
    const auto nt = mod.base + 0x80;
    m.put<std::uint32_t>(nt + 24 + 112, 0x1000);
    m.put<std::uint32_t>(nt + 24 + 116, 0x100);
    m.put<std::uint32_t>(mod.base + 0x1000 + 20, 1);
    m.put<std::uint32_t>(mod.base + 0x1000 + 24, 1);
    m.put<std::uint32_t>(mod.base + 0x1000 + 28, 0x1100);
    m.put<std::uint32_t>(mod.base + 0x1000 + 32, 0x1200);
    m.put<std::uint32_t>(mod.base + 0x1000 + 36, 0x1300);
    m.put<std::uint32_t>(mod.base + 0x1100, 0x400);
    m.put<std::uint32_t>(mod.base + 0x1200, 0x1400);
    m.string(mod.base + 0x1400, "CreateInterface");
    m.put<std::uint8_t>(mod.base + 0x400, 0x48);
    m.put<std::uint8_t>(mod.base + 0x401, 0x8B);
    m.put<std::uint8_t>(mod.base + 0x402, 0x0D);
    m.put<std::int32_t>(mod.base + 0x403, 0x2000 - 0x407);
    m.put<Address>(mod.base + 0x2000, mod.base + 0x2100);
    m.put<Address>(mod.base + 0x2100, mod.base + 0x500);
    m.put<Address>(mod.base + 0x2108, mod.base + 0x2200);
    m.string(mod.base + 0x2200, "SchemaSystem_001");
    m.put<std::uint8_t>(mod.base + 0x500, 0x48);
    m.put<std::uint8_t>(mod.base + 0x501, 0x8D);
    m.put<std::uint8_t>(mod.base + 0x502, 0x05);
    m.put<std::int32_t>(mod.base + 0x503, 0x3000 - 0x507);
    require(find_interface(m, mod, "SchemaSystem_001") == mod.base + 0x3000,
            "Remote interface registration resolved without execution");
    m.put<std::uint8_t>(mod.base + 0x500, 0xCC);
    throws([&] { find_interface(m, mod, "SchemaSystem_001"); });
}
void profile_tests(const std::filesystem::path& data) {
    const auto p = Profile::load(data / "cs2-14191.json");
    require(p.build == 14191, "Pinned engine build");
    require(p.field("C_BaseEntity", "m_iHealth") == 844, "Pinned dump health offset");
    require(p.offset("client.dll", "dwViewMatrix") == 39235616, "Pinned dump matrix offset");
    FixtureMemory m;
    Module engine{0x100000, 0x2000}, client{0x200000, 0x2000}, schema{0x300000, 0x2000};
    m.pe(engine);
    const auto pattern = parse_pattern("89 05 ? ? ? ? 48 8D 0D ? ? ? ? FF 15 ? ? ? ? 48 8B 0D");
    for (std::size_t i = 0; i < pattern.size(); ++i)
        m.put<std::uint8_t>(engine.base + 0x400 + i,
                            pattern[i] < 0 ? 0 : static_cast<std::uint8_t>(pattern[i]));
    m.put<std::int32_t>(engine.base + 0x402, 0x1000 - 0x406);
    m.put<int>(engine.base + 0x1000, 14190);
    throws([&] { Source2 s(m, client, engine, schema, p); });
}
} // namespace
void entity_tests(const std::filesystem::path& data) {
    // A complete remote frame, plus recycled handles, dormancy and respawn failures.
    // This tests reader behavior, not the accuracy of CS2's undocumented ABI.
    auto p = Profile::load(data / "cs2-14191.json");
    p.offsets["client.dll"] = {{"dwEntityList", 0x1000},
                               {"dwLocalPlayerController", 0x1010},
                               {"dwViewMatrix", 0x1100},
                               {"dwGlobalVars", 0x1200},
                               {"dwViewAngles", 0x1300}};
    FixtureMemory m;
    Module engine{0x100000, 0x2000}, client{0x200000, 0x4000}, schema{0x300000, 0x2000};
    m.pe(engine);
    m.pe(client);
    m.pe(schema);
    const auto pattern = parse_pattern("89 05 ? ? ? ? 48 8D 0D ? ? ? ? FF 15 ? ? ? ? 48 8B 0D");
    for (std::size_t i = 0; i < pattern.size(); ++i)
        m.put<std::uint8_t>(engine.base + 0x400 + i,
                            pattern[i] < 0 ? 0 : static_cast<std::uint8_t>(pattern[i]));
    m.put<std::int32_t>(engine.base + 0x402, 0x1000 - 0x406);
    m.put<int>(engine.base + 0x1000, 14191);
    const Address list = 0x320000, chunk = 0x330000, lc = 0x340000, ec = 0x350000;
    const Address local = 0x360000, enemy = 0x370000, weapon = 0x380000;
    const Address lid = 0x390000, eid = 0x390100, wid = 0x390200;
    const Address ln = 0x3A0000, en = 0x3A1000, bones = 0x3B0000, services = 0x3C0000;
    m.put<Address>(client.base + 0x1000, list);
    m.put<Address>(list + 16, chunk);
    m.put<Address>(client.base + 0x1010, lc);
    const auto stride = p.layout["entities"]["stride"].get<Address>();
    m.put<Address>(chunk + stride, lc);
    m.put<Address>(chunk + 2 * stride, ec);
    m.put<Address>(chunk + 3 * stride, local);
    m.put<Address>(chunk + 4 * stride, enemy);
    m.put<Address>(chunk + 5 * stride, weapon);
    m.put<std::uint32_t>(lc + p.field("CCSPlayerController", "m_hPlayerPawn"), 0x8003);
    m.put<std::uint32_t>(ec + p.field("CCSPlayerController", "m_hPlayerPawn"), 0x8004);
    m.string(ec + p.field("CBasePlayerController", "m_iszPlayerName"), "Fixture opponent");
    m.put<Address>(local + p.field("CEntityInstance", "m_pEntity"), lid);
    m.put<Address>(enemy + p.field("CEntityInstance", "m_pEntity"), eid);
    m.put<Address>(weapon + p.field("CEntityInstance", "m_pEntity"), wid);
    m.put<std::uint32_t>(lid + 16, 0x8003);
    m.put<std::uint32_t>(eid + 16, 0x8004);
    m.put<std::uint32_t>(wid + 16, 0x8005);
    for (auto pawn : {local, enemy}) {
        m.put<int>(pawn + p.field("C_BaseEntity", "m_iHealth"), 100);
        m.put<int>(pawn + p.field("C_BaseEntity", "m_iMaxHealth"), 100);
        m.put<std::uint8_t>(pawn + p.field("C_BaseEntity", "m_iTeamNum"), pawn == local ? 2 : 3);
    }
    m.put<Address>(local + p.field("C_BaseEntity", "m_pGameSceneNode"), ln);
    m.put<Address>(enemy + p.field("C_BaseEntity", "m_pGameSceneNode"), en);
    m.put<Vec3>(ln + p.field("CGameSceneNode", "m_vecAbsOrigin"), {0, 0, 0});
    m.put<Vec3>(en + p.field("CGameSceneNode", "m_vecAbsOrigin"), {500, 0, 0});
    m.put<Vec3>(local + p.field("C_BaseModelEntity", "m_vecViewOffset"), {0, 0, 64});
    m.put<Matrix>(client.base + 0x1100, camera_matrix({0, 0, 64}, {}, 16.0f / 9, 90));
    m.put<Address>(en + p.field("CSkeletonInstance", "m_modelState") + 0x80, bones);
    m.put<Vec3>(bones + 6 * 32, {500, 0, 68});
    m.put<Vec3>(bones + 5 * 32, {500, 0, 62});
    m.put<Vec3>(bones + 4 * 32, {500, 0, 48});
    m.put<Address>(enemy + p.field("C_BasePlayerPawn", "m_pWeaponServices"), services);
    m.put<std::uint32_t>(services + p.field("CPlayer_WeaponServices", "m_hActiveWeapon"), 0x8005);
    m.put<Address>(wid + p.field("CEntityIdentity", "m_designerName"), 0x3D0000);
    m.string(0x3D0000, "weapon_ak47");
    m.put<Address>(client.base + 0x1200, 0x3E0000);
    m.put<Address>(0x3E0000 + 0x180, 0x3E1000);
    m.string(0x3E1000, "maps/de_test.vpk");
    m.put<std::int32_t>(local + p.field("C_CSPlayerPawn", "m_iIDEntIndex"), 4);
    Source2 source(m, client, engine, schema, p);
    auto frame = source.sample();
    require(frame.alive && frame.team == 2 && frame.local_pawn == local, "Local pawn sampled");
    near(frame.eye.z, 64);
    require(frame.map == "de_test", "Remote map normalized");
    require(frame.players.size() == 1, "One opponent sampled");
    require(frame.crosshair_pawn == enemy, "Crosshair index resolves to the remote pawn");
    m.put<std::int32_t>(local + p.field("C_CSPlayerPawn", "m_iIDEntIndex"), -1);
    require(source.sample().crosshair_pawn == 0, "Invalid crosshair index produces no target");
    m.put<std::int32_t>(local + p.field("C_CSPlayerPawn", "m_iIDEntIndex"), 4);
    require(frame.players[0].weapon == "ak47" && frame.players[0].name == "Fixture opponent",
            "Name and weapon read");
    require(frame.players[0].bones[0].has_value(), "Head bone read");
    near(frame.players[0].bones[0]->z, 68);
    require(source.view_angles_address() == client.base + 0x1300,
            "Only profile angle address exposed for writes");
    m.put<std::uint32_t>(eid + 16, 0x10004);
    require(source.sample().players.empty(), "Recycled entity serial rejected");
    m.put<std::uint32_t>(eid + 16, 0x8004);
    m.put<std::uint8_t>(en + p.field("CGameSceneNode", "m_bDormant"), 1);
    require(source.sample().players.empty(), "Dormant entity rejected");
    m.put<std::uint32_t>(lid + 16, 0x10003);
    require(!source.same_local_pawn(local), "Respawn invalidates old local pawn");
    throws([&] { source.sample(); });
}
void trigger_tests(const std::filesystem::path& data) {
    TriggerTimer timer;
    require(!timer.update(2, 0, 80, 150), "No shot on initial acquisition");
    require(!timer.update(2, 79, 80, 150), "Wait for full dwell delay");
    require(timer.update(2, 80, 80, 150), "Fire after dwell delay");
    require(!timer.update(2, 229, 80, 150), "Interval suppresses duplicate fire");
    require(timer.update(2, 230, 80, 150), "Repeat only after interval");
    require(!timer.update(3, 400, 80, 150), "Switching targets restarts dwell");
    require(!timer.update(3, 479, 80, 150), "New target must satisfy its own delay");
    require(timer.update(3, 480, 80, 150), "New target fires after delay");
    require(!timer.update(0, 500, 80, 150), "Lost activation cancels pending targeting");
    require(!timer.update(3, 550, 80, 150), "Reactivation starts a new delay");
    require(timer.update(3, 630, 80, 150), "Reactivation respects both cooldown and delay");
    require(!timer.update(0, 640, 0, 150), "Disabled does not fire");
    require(!timer.update(2, 641, 0, 150), "Zero delay cannot bypass global interval");
    require(timer.update(2, 780, 0, 150), "Zero delay fires when cooldown expires");
    Frame f;
    f.alive = true;
    f.team = 2;
    f.local_pawn = 1;
    f.eye = {0, 0, 64};
    f.map = "training";
    Player p;
    p.pawn = 2;
    p.team = 3;
    p.health = 100;
    p.bones[0] = Vec3{500, 0, 64};
    f.players.push_back(p);
    CollisionScene scene;
    require(!trigger_candidate(f, Bone::Head, false, scene), "Aim FOV alone does not cause a trigger shot");
    f.crosshair_pawn = 2;
    require(trigger_candidate(f, Bone::Head, false, scene) == 2, "Enemy under crosshair is eligible");
    require(!trigger_candidate(f, Bone::Head, true, scene), "Unknown visibility blocks trigger");
    scene.load_obj(data / "training.obj", "training");
    require(!trigger_candidate(f, Bone::Head, true, scene), "Wall blocks trigger");
    f.players[0].bones[0] = Vec3{500, 200, 64};
    require(trigger_candidate(f, Bone::Head, true, scene) == 2, "Clear static LOS permits trigger");
    f.players[0].team = 2;
    require(!trigger_candidate(f, Bone::Head, false, scene), "No friendly fire");
    f.players[0].team = 3;
    f.players[0].health = 0;
    require(!trigger_candidate(f, Bone::Head, false, scene), "Dead target is rejected");
    f.players[0].health = 100;
    f.alive = false;
    require(!trigger_candidate(f, Bone::Head, false, scene), "Dead local player cannot trigger");
    f.alive = true;
    f.crosshair_pawn = 999;
    require(!trigger_candidate(f, Bone::Head, false, scene), "Unvalidated crosshair entity is rejected");
}
int main(int argc, char** argv) {
    try {
        if (argc != 3)
            throw std::runtime_error("Expected suite and data directory");
        const std::string suite = argv[1];
        const std::filesystem::path data = argv[2];
        if (suite == "math")
            math_tests();
        else if (suite == "pattern")
            pattern_tests();
        else if (suite == "geometry")
            geometry_tests(data);
        else if (suite == "config")
            config_tests();
        else if (suite == "schema")
            schema_tests();
        else if (suite == "profile")
            profile_tests(data);
        else if (suite == "entities")
            entity_tests(data);
        else if (suite == "trigger")
            trigger_tests(data);
        else
            throw std::runtime_error("Unknown suite");
        std::cout << suite << ": " << checks << " checks passed\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL after " << checks << " checks: " << e.what() << '\n';
        return 1;
    }
}
