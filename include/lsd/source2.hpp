#pragma once
#include "lsd/core.hpp"
#include <cstring>
#include <map>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <type_traits>
namespace lsd {
using Address = std::uint64_t;
struct Module {
    Address base{};
    std::uint32_t size{};
};
class Memory {
  public:
    virtual ~Memory() = default;
    virtual bool read_bytes(Address address, void* out, std::size_t size) const = 0;
    template <class T> std::optional<T> read(Address address) const {
        static_assert(std::is_trivially_copyable_v<T>);
        T v{};
        if (!read_bytes(address, &v, sizeof(v)))
            return {};
        return v;
    }
    template <class T> T need(Address address) const {
        const auto v = read<T>(address);
        if (!v)
            throw std::runtime_error("Remote read failed");
        return *v;
    }
    std::optional<std::string> string(Address address, std::size_t max = 128) const;
};
using Fields = std::map<std::string, std::map<std::string, std::uint32_t>>;
struct Profile {
    int build{};
    nlohmann::json offsets, layout;
    Fields fields;
    static Profile load(const std::filesystem::path& path);
    std::uint32_t offset(const std::string& module, const std::string& name) const;
    std::uint32_t field(const std::string& type, const std::string& name) const;
};
// No remote vtable calls: inspect the exported registration list instead.
Address find_interface(const Memory& memory, Module module, const std::string& name);
Fields read_schema(const Memory& memory, Address schema_system, const nlohmann::json& layout);
std::optional<Address> scan_rip(const Memory& memory, Module module, const std::string& pattern,
                                std::uint32_t displacement = 3);
class Source2 {
  public:
    Source2(const Memory& memory, Module client, Module engine, Module schema, Profile profile);
    Frame sample() const;
    Address view_angles_address() const {
        return view_angles_;
    }
    int build() const {
        return profile_.build;
    }
    const std::string& diagnostics() const {
        return diagnostics_;
    }
    bool same_local_pawn(Address pawn) const;

  private:
    Address entity(Address list, std::uint32_t index) const;
    Address handle_entity(Address list, std::uint32_t handle) const;
    std::uint32_t field(const std::string& type, const std::string& name) const;
    std::optional<Vec3> origin(Address pawn) const;
    Address local_pawn(Address list) const;
    const Memory& memory_;
    Module client_, engine_;
    Profile profile_;
    Fields fields_;
    Address entity_list_{}, controller_{}, matrix_{}, view_angles_{}, globals_{};
    std::string diagnostics_;
};
} // namespace lsd
