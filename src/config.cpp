#include "lsd/config.hpp"
#include <cmath>
#include <fstream>
#include <stdexcept>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif
namespace lsd {
void Config::validate() const {
    if (!std::isfinite(fov) || fov < 0.1f || fov > 45)
        throw std::runtime_error("FOV must be 0.1..45 degrees (cone half-angle)");
    if (!std::isfinite(smoothing) || smoothing < 0 || smoothing > 2)
        throw std::runtime_error("Smoothing must be 0..2 seconds");
    if (static_cast<int>(bone) < 0 || static_cast<int>(bone) > 2)
        throw std::runtime_error("Invalid bone");
    for (const auto k : {menu_key, aim_key, exit_key})
        if (k < 1 || k > 254)
            throw std::runtime_error("Hotkey must be a Windows VK code in 1..254");
    if (menu_key == aim_key || menu_key == exit_key || aim_key == exit_key)
        throw std::runtime_error("Hotkeys must be distinct");
    if (collision_file.empty() != collision_map.empty())
        throw std::runtime_error("Collision file and map must be set together");
}
void to_json(nlohmann::json& j, const Config& c) {
    j = {{"version", 1},
         {"visual",
          {{"boxes", c.boxes},
           {"names", c.names},
           {"health", c.health},
           {"distance", c.distance},
           {"weapons", c.weapons},
           {"teammates", c.teammates}}},
         {"aim",
          {{"enabled", c.aim},
           {"fov_degrees", c.fov},
           {"smoothing_seconds", c.smoothing},
           {"bone", static_cast<int>(c.bone)},
           {"require_visibility", c.require_visibility}}},
         {"hotkeys", {{"menu", c.menu_key}, {"aim", c.aim_key}, {"exit", c.exit_key}}},
         {"collision", {{"file", c.collision_file}, {"map", c.collision_map}}}};
}
void from_json(const nlohmann::json& j, Config& c) {
    if (j.at("version").get<int>() != 1)
        throw std::runtime_error("Unsupported config version");
    Config n;
    const auto& v = j.at("visual");
    const auto& a = j.at("aim");
    const auto& k = j.at("hotkeys");
    const auto& g = j.at("collision");
    n.boxes = v.at("boxes");
    n.names = v.at("names");
    n.health = v.at("health");
    n.distance = v.at("distance");
    n.weapons = v.at("weapons");
    n.teammates = v.at("teammates");
    n.aim = a.at("enabled");
    n.fov = a.at("fov_degrees");
    n.smoothing = a.at("smoothing_seconds");
    n.bone = static_cast<Bone>(a.at("bone").get<int>());
    n.require_visibility = a.at("require_visibility");
    n.menu_key = k.at("menu");
    n.aim_key = k.at("aim");
    n.exit_key = k.at("exit");
    n.collision_file = g.at("file");
    n.collision_map = g.at("map");
    n.validate();
    c = std::move(n);
}
Config Config::load(const std::filesystem::path& path) {
    if (std::filesystem::file_size(path) > 65536)
        throw std::runtime_error("Config exceeds 64 KiB");
    std::ifstream f(path);
    if (!f)
        throw std::runtime_error("Cannot open config");
    nlohmann::json j;
    f >> j;
    return j.get<Config>();
}
void Config::save(const std::filesystem::path& path) const {
    validate();
    auto temp = path;
    temp += ".tmp";
    {
        std::ofstream f(temp, std::ios::trunc);
        if (!f)
            throw std::runtime_error("Cannot write temporary config");
        f << nlohmann::json(*this).dump(2) << '\n';
        f.flush();
        if (!f)
            throw std::runtime_error("Config write failed");
    }
#ifdef _WIN32
    if (!MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error("Cannot replace config");
#else
    std::filesystem::rename(temp, path);
#endif
}
} // namespace lsd
