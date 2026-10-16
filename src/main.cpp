#include "lsd/config.hpp"
#include "lsd/overlay_win.hpp"
#include "lsd/process_win.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <imgui.h>
#include <iostream>
#include <memory>
#include <numbers>
#include <stdexcept>

namespace {
using namespace lsd;
using Clock = std::chrono::steady_clock;
class Keys {
  public:
    void update() {
        old_ = now_;
        for (int k = 1; k < 255; ++k)
            now_[k] = (GetAsyncKeyState(k) & 0x8000) != 0;
    }
    bool down(int k) const {
        return k > 0 && k < 255 && now_[k];
    }
    bool pressed(int k) const {
        return down(k) && !old_[k];
    }

  private:
    std::array<bool, 256> now_{}, old_{};
};
class TriggerClick {
  public:
    ~TriggerClick() {
        release();
    }
    bool held() const {
        return held_;
    }
    bool expired() const {
        return held_ && Clock::now() >= release_at_;
    }
    bool press() {
        INPUT input{};
        input.type = INPUT_MOUSE;
        input.mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
        if (held_ || SendInput(1, &input, sizeof(input)) != 1)
            return false;
        held_ = true;
        release_at_ = Clock::now() + std::chrono::milliseconds(30);
        return true;
    }
    bool release() {
        if (!held_)
            return true;
        INPUT input{};
        input.type = INPUT_MOUSE;
        input.mi.dwFlags = MOUSEEVENTF_LEFTUP;
        if (SendInput(1, &input, sizeof(input)) != 1)
            return false;
        held_ = false;
        return true;
    }

  private:
    bool held_{};
    Clock::time_point release_at_{};
};
struct Options {
    bool demo{};
    bool write{true};
    std::filesystem::path config, profile;
    std::string expected_steam_build;
};
std::filesystem::path utf8_path(const std::string& value) {
    return std::filesystem::path(std::u8string(value.begin(), value.end()));
}
std::filesystem::path executable_directory() {
    std::wstring buffer(32768, L'\0');
    const auto n = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (!n || n == buffer.size())
        throw std::runtime_error("Cannot locate executable directory");
    buffer.resize(n);
    return std::filesystem::path(buffer).parent_path();
}
Options parse(int argc, wchar_t** argv) {
    const auto root = executable_directory();
    Options o;
    o.config = root / "config.json";
    o.profile = root / "data/cs2-14191.json";
    for (int i = 1; i < argc; ++i) {
        const std::wstring arg = argv[i];
        if (arg == L"--demo")
            o.demo = true;
        else if (arg == L"--write-angles")
            o.write = true;
        else if (arg == L"--read-only")
            o.write = false;
        else if ((arg == L"--config" || arg == L"--profile" || arg == L"--steam-build") && i + 1 < argc) {
            const std::wstring value = argv[++i];
            if (arg == L"--config")
                o.config = value;
            else if (arg == L"--profile")
                o.profile = value;
            else {
                if (value.empty() || value.find_first_not_of(L"0123456789") != std::wstring::npos)
                    throw std::runtime_error("Steam BuildID must contain decimal digits");
                o.expected_steam_build.assign(value.begin(), value.end());
            }
        } else
            throw std::runtime_error(
                "Usage: lsdware [--demo] [--read-only] [--write-angles] [--config file] [--profile "
                "file] [--steam-build BuildID]");
    }
    return o;
}
void label(ImDrawList* draw, Vec2 p, ImU32 color, const std::string& text, bool centered = false) {
    if (centered)
        p.x -= ImGui::CalcTextSize(text.c_str()).x / 2;
    draw->AddText({p.x + 1, p.y + 1}, IM_COL32(0, 0, 0, 255), text.c_str());
    draw->AddText({p.x, p.y}, color, text.c_str());
}
void draw_fov(ImDrawList* draw, const Frame& f, Vec2 viewport, float fov) {
    constexpr float rad = std::numbers::pi_v<float> / 180;
    const float p = f.angles.pitch * rad, y = f.angles.yaw * rad;
    const Vec3 forward{std::cos(p) * std::cos(y), std::cos(p) * std::sin(y), -std::sin(p)};
    const Vec3 right{std::sin(y), -std::cos(y), 0}, up = cross(right, forward);
    std::optional<Vec2> previous;
    for (int i = 0; i <= 96; ++i) {
        const float t = 2 * std::numbers::pi_v<float> * static_cast<float>(i) / 96;
        const auto direction =
            forward * std::cos(fov * rad) + (right * std::cos(t) + up * std::sin(t)) * std::sin(fov * rad);
        auto point = project(f.eye + direction * 100, f.matrix, viewport);
        if (previous && point)
            draw->AddLine({previous->x, previous->y}, {point->x, point->y}, IM_COL32(230, 230, 230, 130));
        previous = point;
    }
}
void draw_frame(const Frame& f, const Config& c, const CollisionScene& scene, Vec2 size,
                const std::optional<Target>& target) {
    auto* draw = ImGui::GetBackgroundDrawList();
    if (c.show_fov)
        draw_fov(draw, f, size, c.fov);
    for (const auto& p : f.players) {
        if (!c.teammates && p.team == f.team)
            continue;
        const auto feet = project(p.origin, f.matrix, size);
        // Missing bone data is never replaced by a guessed aim point.
        if (!feet || !p.bones[0])
            continue;
        const auto top = project(*p.bones[0] + Vec3{0, 0, 5}, f.matrix, size);
        if (!top)
            continue;
        const float h = feet->y - top->y, w = h * 0.45f;
        if (h < 2 || h > size.y * 4)
            continue;
        if (feet->x + w < 0 || feet->x - w > size.x || feet->y < 0 || top->y > size.y)
            continue;
        const ImU32 color = p.team == 2 ? IM_COL32(242, 182, 74, 255) : IM_COL32(89, 174, 255, 255);
        const float left = top->x - w / 2, right = top->x + w / 2;
        if (c.boxes) {
            draw->AddRect({left - 1, top->y - 1}, {right + 1, feet->y + 1}, IM_COL32(0, 0, 0, 230), 0, 0, 3);
            draw->AddRect({left, top->y}, {right, feet->y}, color);
        }
        if (c.names)
            label(draw, {top->x, top->y - 19}, color, p.name, true);
        if (c.health) {
            const float fraction =
                std::clamp(static_cast<float>(p.health) / std::max(1, p.max_health), 0.0f, 1.0f);
            draw->AddRectFilled({left - 7, top->y - 1}, {left - 3, feet->y + 1}, IM_COL32(0, 0, 0, 255));
            draw->AddRectFilled({left - 6, feet->y - h * fraction}, {left - 4, feet->y},
                                IM_COL32(90, 225, 115, 255));
        }
        std::string bottom;
        if (c.distance)
            bottom = std::to_string(static_cast<int>(length(p.origin - f.eye) * 0.0254f)) + " m";
        if (c.weapons) {
            if (!bottom.empty())
                bottom += " | ";
            bottom += p.weapon;
        }
        if (!bottom.empty())
            label(draw, {feet->x, feet->y + 3}, IM_COL32_WHITE, bottom, true);
        const auto point = p.bones[static_cast<std::size_t>(c.bone)];
        if (c.show_los && point) {
            const auto v = scene.visibility(f.map, f.eye, *point);
            const char* text = v == Visibility::Visible
                                   ? "LOS static"
                                   : (v == Visibility::Occluded ? "Blocked" : "LOS unknown");
            label(draw, {right + 5, top->y}, IM_COL32(210, 210, 210, 255), text);
        }
    }
    if (c.show_target_marker && target)
        if (auto p = project(target->point, f.matrix, size))
            draw->AddCircle({p->x, p->y}, 6, IM_COL32(110, 255, 160, 255), 24, 2);
}
void load_scene(const Config& c, const Options& o, CollisionScene& scene) {
    CollisionScene next;
    if (!c.collision_file.empty()) {
        auto path = utf8_path(c.collision_file);
        if (path.is_relative())
            path = o.config.parent_path() / path;
        next.load_obj(path, c.collision_map);
    } else if (o.demo)
        next.load_obj(executable_directory() / "data/training.obj", "training");
    scene = std::move(next);
}
void key_editor(const char* label, int& key) {
    ImGui::InputInt(label, &key, 1, 16);
}
int run(const Options& options) {
    Config config;
    std::string message;
    if (std::filesystem::exists(options.config)) {
        try {
            config = Config::load(options.config);
        } catch (const std::exception& e) {
            message = std::string("Config rejected; defaults active: ") + e.what();
        }
    }
    CollisionScene scene;
    try {
        load_scene(config, options, scene);
    } catch (const std::exception& e) {
        message = e.what();
    }
    std::optional<Profile> profile;
    if (!options.demo)
        profile = Profile::load(options.profile);
    Overlay overlay;
    Keys keys;
    bool menu = true;
    Angles demo_angles{};
    int edit_menu = config.menu_key, edit_aim = config.aim_key, edit_exit = config.exit_key;
    int edit_trigger = config.trigger_key;
    std::array<char, 1024> collision_path{};
    std::array<char, 256> collision_map{};
    const auto copy_text = [&] {
        std::snprintf(collision_path.data(), collision_path.size(), "%s", config.collision_file.c_str());
        std::snprintf(collision_map.data(), collision_map.size(), "%s", config.collision_map.c_str());
    };
    copy_text();
    std::unique_ptr<Process> process;
    std::unique_ptr<Source2> source;
    std::string status = "Starting", steam_build;
    auto started = Clock::now(), previous = started, next_connect = started;
    float response_ms = 0;
    std::optional<Clock::time_point> target_since;
    std::optional<Address> previous_target;
    bool diag_target = false, diag_focused = false, diag_hotkey = false, diag_local_pawn = false;
    std::optional<Angles> diag_current_angles, diag_target_angles, diag_output_angles;
    std::uint64_t successful_writes = 0;
    DWORD last_write_error = ERROR_SUCCESS;
    TriggerTimer trigger_timer;
    TriggerClick trigger_click;
    std::uint64_t trigger_shots = 0;
    const auto release_trigger = [&] {
        if (!trigger_click.release()) {
            config.trigger = false;
            message = "SendInput mouse release failed; trigger disabled (release will be retried)";
        }
    };
    while (overlay.pump()) {
        keys.update();
        if (trigger_click.expired() || !config.trigger || !keys.down(config.trigger_key) || menu)
            release_trigger();
        if (keys.pressed(config.exit_key))
            break;
        if (keys.pressed(config.menu_key))
            menu = !menu;
        const auto now = Clock::now();
        const float dt = std::chrono::duration<float>(now - previous).count();
        previous = now;
        if (process && !process->alive()) {
            source.reset();
            process.reset();
            status = "CS2 exited";
        }
        if (!options.demo && !source && now >= next_connect) {
            next_connect = now + std::chrono::seconds(5);
            try {
                auto p = std::make_unique<Process>(options.write);
                steam_build = p->steam_build_id();
                if (!options.expected_steam_build.empty() && steam_build != options.expected_steam_build)
                    throw std::runtime_error("Steam BuildID mismatch/unavailable. Expected " +
                                             options.expected_steam_build + ", read " +
                                             (steam_build.empty() ? "unknown" : steam_build));
                auto s = std::make_unique<Source2>(*p, p->module(L"client.dll"), p->module(L"engine2.dll"),
                                                   p->module(L"schemasystem.dll"), *profile);
                process = std::move(p);
                source = std::move(s);
                successful_writes = 0;
                last_write_error = ERROR_SUCCESS;
                status = "Connected";
            } catch (const std::exception& e) {
                status = e.what();
            }
        }
        const HWND game = process ? process->window() : nullptr;
        overlay.align(game, menu || options.demo);
        overlay.begin();
        const auto size = overlay.size();
        std::optional<Frame> frame;
        if (options.demo) {
            const auto* io = &ImGui::GetIO();
            if (!menu && GetForegroundWindow() == overlay.window()) {
                demo_angles.yaw = wrap(demo_angles.yaw - io->MouseDelta.x * 0.08f);
                demo_angles.pitch = std::clamp(demo_angles.pitch + io->MouseDelta.y * 0.08f, -89.0f, 89.0f);
            }
            frame = demo_frame(std::chrono::duration<double>(now - started).count(), demo_angles, size);
            ImGui::GetBackgroundDrawList()->AddRectFilled({0, 0}, {size.x, size.y},
                                                          IM_COL32(18, 24, 33, 255));
            // Show the sample wall at its real projected position.
            const Vec3 wall[] = {{250, -40, 0}, {250, 40, 0}, {250, 40, 120}, {250, -40, 120}};
            ImVec2 points[4];
            bool valid = true;
            for (int i = 0; i < 4; ++i) {
                auto p = project(wall[i], frame->matrix, size);
                if (!p) {
                    valid = false;
                    break;
                }
                points[i] = {p->x, p->y};
            }
            if (valid)
                ImGui::GetBackgroundDrawList()->AddConvexPolyFilled(points, 4, IM_COL32(54, 65, 80, 255));
            status = "DEMO - synthetic targets, no game memory";
        } else if (source && game) {
            try {
                frame = source->sample();
                status = "Connected | internal build " + std::to_string(source->build()) + " | Steam " +
                         (steam_build.empty() ? "unknown" : steam_build);
            } catch (const std::exception& e) {
                status = e.what();
            }
        }
        std::optional<Target> target;
        diag_target = false;
        diag_focused = false;
        diag_hotkey = false;
        diag_local_pawn = false;
        diag_current_angles.reset();
        diag_target_angles.reset();
        diag_output_angles.reset();
        if (frame) {
            target = select_target(*frame, config.bone, config.fov, config.require_visibility, scene);
            diag_target = target.has_value();
            diag_current_angles = frame->angles;
            if (target)
                diag_target_angles = target->angles;
            draw_frame(*frame, config, scene, size, target);
            const auto id =
                target ? std::optional<Address>{frame->players[target->player].pawn} : std::nullopt;
            if (id != previous_target) {
                target_since = id ? std::optional<Clock::time_point>{Clock::now()} : std::nullopt;
                previous_target = id;
            }
            if (!menu && target_since && keys.pressed(config.aim_key)) {
                response_ms = std::chrono::duration<float, std::milli>(Clock::now() - *target_since).count();
                target_since.reset();
            }
            const bool focused = options.demo ? GetForegroundWindow() == overlay.window()
                                              : game && GetForegroundWindow() == game;
            diag_focused = focused;
            diag_hotkey = keys.down(config.aim_key);
            diag_local_pawn = options.demo ? true : (source && source->same_local_pawn(frame->local_pawn));
            if (config.aim && target && !menu && focused && diag_hotkey) {
                const auto angles = smooth(frame->angles, target->angles, config.smoothing, dt);
                diag_output_angles = angles;
                if (options.demo)
                    demo_angles = angles;
                else if (process && process->writable() && source && diag_local_pawn) {
                    // Sample was taken in this iteration, never reuse the previous frame after an RPM
                    // failure.
                    if (std::chrono::duration<float>(Clock::now() - now).count() < 0.1f) {
                        if (process->write_angles(source->view_angles_address(), angles)) {
                            ++successful_writes;
                            last_write_error = ERROR_SUCCESS;
                        } else {
                            last_write_error = GetLastError();
                            config.aim = false;
                            message = "WriteProcessMemory failed, Windows error " +
                                      std::to_string(last_write_error) + "; aim disabled";
                        }
                    }
                }
            }
        }
        // Trigger uses the crosshair entity, not the larger aim-selection FOV.
        const bool trigger_focus =
            options.demo ? GetForegroundWindow() == overlay.window() : game && GetForegroundWindow() == game;
        const bool trigger_active =
            config.trigger && !menu && trigger_focus && keys.down(config.trigger_key) && frame &&
            frame->alive && std::chrono::duration<float>(Clock::now() - now).count() < 0.1f &&
            (options.demo ||
             (process && process->writable() && source && source->same_local_pawn(frame->local_pawn)));
        const Address trigger_pawn =
            trigger_active ? trigger_candidate(*frame, config.bone, config.require_visibility, scene) : 0;
        if (!trigger_pawn || trigger_click.expired())
            release_trigger();
        const bool manual_fire = !trigger_click.held() && (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
        const bool fire =
            trigger_timer.update(manual_fire ? 0 : trigger_pawn,
                                 std::chrono::duration<double, std::milli>(Clock::now() - started).count(),
                                 config.trigger_delay_ms, config.trigger_interval_ms);
        if (fire && !trigger_click.held()) {
            if (options.demo) {
                ++trigger_shots; // Demo never injects mouse input into another program.
            } else if (GetForegroundWindow() == game && source && frame &&
                       source->same_local_pawn(frame->local_pawn) &&
                       source->crosshair_pawn(frame->local_pawn) == trigger_pawn) {
                if (trigger_click.press())
                    ++trigger_shots;
                else {
                    config.trigger = false;
                    message = "SendInput mouse press failed; trigger disabled";
                }
            }
        }
        label(ImGui::GetForegroundDrawList(), {14, 12}, IM_COL32_WHITE, "LSDWare | " + status);
        if (menu) {
            ImGui::SetNextWindowSize({580, 650}, ImGuiCond_FirstUseEver);
            ImGui::SetNextWindowPos({25, 45}, ImGuiCond_FirstUseEver);
            ImGui::Begin("LSDWare - visual training", &menu, ImGuiWindowFlags_NoCollapse);
            ImGui::TextWrapped("%s", status.c_str());
            ImGui::TextWrapped(
                "%s",
                options.demo
                    ? "Demo mode"
                    : (!process
                           ? (options.write ? "Waiting for a process with write access"
                                            : "Read-only requested by --read-only")
                           : (process->writable()
                                  ? "Angle writes enabled; enable assistance and hold the hotkey"
                                  : "Read-only process handle; restart without --read-only to use aim")));
            ImGui::SeparatorText("Visual");
            ImGui::Checkbox("Boxes", &config.boxes);
            ImGui::SameLine();
            ImGui::Checkbox("Names", &config.names);
            ImGui::SameLine();
            ImGui::Checkbox("Health", &config.health);
            ImGui::Checkbox("Distance", &config.distance);
            ImGui::SameLine();
            ImGui::Checkbox("Weapons", &config.weapons);
            ImGui::SameLine();
            ImGui::Checkbox("Teammates", &config.teammates);
            ImGui::Checkbox("Show LOS labels", &config.show_los);
            ImGui::SameLine();
            ImGui::Checkbox("Show target marker (green)", &config.show_target_marker);
            ImGui::SeparatorText("Aim training");
            ImGui::Checkbox("Show FOV circle", &config.show_fov);
            ImGui::SameLine();
            ImGui::Checkbox("Show aim diagnostics", &config.show_aim_diagnostics);
            ImGui::Checkbox("Enable assistance while hotkey held", &config.aim);
            ImGui::SliderFloat("FOV half-angle (degrees)", &config.fov, 0.1f, 45, "%.1f");
            ImGui::SliderFloat("Smoothing time constant (s)", &config.smoothing, 0, 2, "%.3f");
            int bone = static_cast<int>(config.bone);
            if (ImGui::Combo("Bone", &bone, "Head\0Neck\0Chest\0"))
                config.bone = static_cast<Bone>(bone);
            ImGui::Checkbox("Require static geometry visibility", &config.require_visibility);
            ImGui::Text("Last activation latency: %.0f ms", response_ms);
            ImGui::TextWrapped(
                "Latency is target entering the eligible cone -> hotkey press, not a CS2 hit measurement.");
            ImGui::Checkbox("Enable triggerbot while hotkey held", &config.trigger);
            ImGui::SliderInt("Trigger delay (ms)", &config.trigger_delay_ms, 0, 1000);
            ImGui::SliderInt("Trigger interval (ms)", &config.trigger_interval_ms, 50, 2000);
            ImGui::Text("Trigger key: VK %d (default Mouse 5) | %s: %llu", config.trigger_key,
                        options.demo ? "Simulated clicks" : "Submitted clicks",
                        static_cast<unsigned long long>(trigger_shots));
            ImGui::TextWrapped("Trigger uses the enemy under the crosshair and the visibility filter above; "
                               "independent of aim assistance.");
            if (config.show_aim_diagnostics &&
                ImGui::CollapsingHeader("Aim diagnostics", ImGuiTreeNodeFlags_DefaultOpen)) {
                const auto yn = [](bool value) { return value ? "YES" : "NO"; };
                ImGui::Text("Assistance enabled: %s", yn(config.aim));
                ImGui::Text("Target found: %s", yn(diag_target));
                ImGui::Text("Menu closed: %s", yn(!menu));
                ImGui::Text("Game/overlay focused: %s", yn(diag_focused));
                ImGui::Text("Hotkey held (VK %d): %s", config.aim_key, yn(diag_hotkey));
                ImGui::Text("Local pawn matches sample: %s", yn(diag_local_pawn));
                ImGui::Text("Mode: %s", options.demo ? "DEMO"
                                                     : (!process ? "NOT CONNECTED"
                                                                 : (process->writable() ? "WRITE ENABLED"
                                                                                        : "READ ONLY")));
                ImGui::Text("Successful angle writes: %llu",
                            static_cast<unsigned long long>(successful_writes));
                if (last_write_error != ERROR_SUCCESS)
                    ImGui::Text("Last write error (Windows): %lu",
                                static_cast<unsigned long>(last_write_error));
                if (diag_current_angles)
                    ImGui::Text("Current angles: pitch %.2f | yaw %.2f", diag_current_angles->pitch,
                                diag_current_angles->yaw);
                else
                    ImGui::TextUnformatted("Current angles: unavailable");
                if (diag_target_angles)
                    ImGui::Text("Target angles: pitch %.2f | yaw %.2f", diag_target_angles->pitch,
                                diag_target_angles->yaw);
                else
                    ImGui::TextUnformatted("Target angles: unavailable");
                if (diag_output_angles)
                    ImGui::Text("Calculated output: pitch %.2f | yaw %.2f", diag_output_angles->pitch,
                                diag_output_angles->yaw);
                else
                    ImGui::TextUnformatted("Calculated output: inactive");
            }
            ImGui::SeparatorText("Collision geometry");
            ImGui::InputText("OBJ file", collision_path.data(), collision_path.size());
            ImGui::InputText("Map name", collision_map.data(), collision_map.size());
            if (ImGui::Button("Load collision"))
                try {
                    auto next = config;
                    next.collision_file = collision_path.data();
                    next.collision_map = collision_map.data();
                    next.validate();
                    load_scene(next, options, scene);
                    config = next;
                    message = "Collision loaded";
                } catch (const std::exception& e) {
                    message = e.what();
                }
            ImGui::Text("Triangles: %llu | scene: %s", static_cast<unsigned long long>(scene.size()),
                        scene.map().c_str());
            ImGui::Text("Game map: %s", frame ? frame->map.c_str() : "unknown");
            ImGui::TextWrapped("OBJ: Source world coordinates, Z up, no unit conversion. Static surfaces "
                               "only; not engine trace.");
            ImGui::SeparatorText("Hotkeys (decimal Windows VK codes)");
            key_editor("Menu", edit_menu);
            key_editor("Hold aim", edit_aim);
            key_editor("Hold trigger", edit_trigger);
            key_editor("Exit", edit_exit);
            if (ImGui::Button("Apply keys"))
                try {
                    auto next = config;
                    next.menu_key = edit_menu;
                    next.aim_key = edit_aim;
                    next.trigger_key = edit_trigger;
                    next.exit_key = edit_exit;
                    next.validate();
                    config = next;
                    message = "Keys applied";
                } catch (const std::exception& e) {
                    message = e.what();
                }
            ImGui::Separator();
            if (ImGui::Button("Save JSON"))
                try {
                    config.save(options.config);
                    message = "Config saved";
                } catch (const std::exception& e) {
                    message = e.what();
                }
            ImGui::SameLine();
            if (ImGui::Button("Load JSON"))
                try {
                    auto next = Config::load(options.config);
                    load_scene(next, options, scene);
                    config = next;
                    edit_menu = config.menu_key;
                    edit_aim = config.aim_key;
                    edit_trigger = config.trigger_key;
                    edit_exit = config.exit_key;
                    copy_text();
                    message = "Config loaded";
                } catch (const std::exception& e) {
                    message = e.what();
                }
            ImGui::SameLine();
            if (ImGui::Button("Reconnect")) {
                release_trigger();
                trigger_timer.update(0, 0, 0, 50);
                source.reset();
                process.reset();
                next_connect = Clock::now();
            }
            if (options.demo) {
                ImGui::SameLine();
                if (ImGui::Button("Reset demo view"))
                    demo_angles = {};
            }
            if (source && ImGui::CollapsingHeader("Offset diagnostics"))
                ImGui::TextWrapped("%s", source->diagnostics().c_str());
            if (!message.empty())
                ImGui::TextWrapped("%s", message.c_str());
            ImGui::Text("%.1f FPS", ImGui::GetIO().Framerate);
            ImGui::End();
        }
        overlay.end();
        if (!frame)
            Sleep(25);
    }
    return 0;
}
} // namespace
int wmain(int argc, wchar_t** argv) {
    // Native UTF-16 command line preserves paths containing non-ASCII characters.
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    int result = 1;
    try {
        result = run(parse(argc, argv));
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        MessageBoxA(nullptr, e.what(), "LSDWare", MB_OK | MB_ICONERROR);
    }
    if (SUCCEEDED(com))
        CoUninitialize();
    return result;
}
