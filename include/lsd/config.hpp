#pragma once
#include "lsd/core.hpp"
#include <nlohmann/json.hpp>
namespace lsd {
struct Config {
    bool boxes{true}, names{true}, health{true}, distance{true}, weapons{true}, teammates{true};
    bool aim{false}, require_visibility{true};
    bool show_fov{false}, show_aim_diagnostics{false}, trigger{false};
    int trigger_key{0x06}, trigger_delay_ms{80}, trigger_interval_ms{150};
    float fov{5}, smoothing{0.15f};
    Bone bone{Bone::Head};
    int menu_key{0x2D}, aim_key{0x05}, exit_key{0x23};
    std::string collision_file, collision_map;
    void validate() const;
    static Config load(const std::filesystem::path& path);
    void save(const std::filesystem::path& path) const;
};
void to_json(nlohmann::json& j, const Config& c);
void from_json(const nlohmann::json& j, Config& c);
} // namespace lsd
