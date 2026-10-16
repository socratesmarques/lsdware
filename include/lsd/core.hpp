#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace lsd {
struct Vec2 {
    float x{}, y{};
};
struct Vec3 {
    float x{}, y{}, z{};
    Vec3 operator+(Vec3 b) const {
        return {x + b.x, y + b.y, z + b.z};
    }
    Vec3 operator-(Vec3 b) const {
        return {x - b.x, y - b.y, z - b.z};
    }
    Vec3 operator*(float f) const {
        return {x * f, y * f, z * f};
    }
};
float dot(Vec3 a, Vec3 b);
Vec3 cross(Vec3 a, Vec3 b);
float length(Vec3 v);
bool finite(Vec3 v);
struct Angles {
    float pitch{}, yaw{}, roll{};
};
struct Matrix {
    float m[4][4]{};
};
float wrap(float degrees);
Angles look_at(Vec3 from, Vec3 to);
float angular_distance(Angles a, Angles b);
Angles smooth(Angles current, Angles target, float time_constant, float dt);
std::optional<Vec2> project(Vec3 v, const Matrix& m, Vec2 viewport);
Matrix camera_matrix(Vec3 eye, Angles angles, float aspect, float horizontal_fov);
std::vector<int> parse_pattern(const std::string& signature);
// A non-unique signature is an error, never an arbitrary first match.
std::optional<std::size_t> unique_pattern(std::span<const std::uint8_t> bytes, std::span<const int> pattern);
enum class Bone : int { Head, Neck, Chest };
enum class Visibility { Unknown, Visible, Occluded };
struct Player {
    std::uint64_t pawn{};
    int team{}, health{}, max_health{100};
    std::string name, weapon;
    Vec3 origin{};
    std::array<std::optional<Vec3>, 3> bones;
};
struct Frame {
    Matrix matrix;
    Vec3 eye;
    Angles angles;
    int team{};
    std::uint64_t local_pawn{};
    std::uint64_t crosshair_pawn{};
    bool alive{};
    std::string map;
    std::vector<Player> players;
};
struct Triangle {
    Vec3 a, b, c;
};
class CollisionScene {
  public:
    void load_obj(const std::filesystem::path& path, std::string map);
    void set(std::vector<Triangle> triangles, std::string map);
    Visibility visibility(const std::string& map, Vec3 from, Vec3 to) const;
    std::size_t size() const {
        return triangles_.size();
    }
    const std::string& map() const {
        return map_;
    }

  private:
    struct Node {
        Vec3 low, high;
        int left{-1}, right{-1};
        std::size_t begin{}, end{};
    };
    int build(std::size_t begin, std::size_t end);
    bool hit(int node, Vec3 from, Vec3 direction) const;
    std::vector<Triangle> triangles_;
    std::vector<Node> nodes_;
    std::string map_;
};
struct Target {
    std::size_t player{};
    Vec3 point;
    Angles angles;
    float error{};
    Visibility visibility{};
};
std::optional<Target> select_target(const Frame& frame, Bone bone, float fov, bool require_visibility,
                                    const CollisionScene& scene);
Frame demo_frame(double seconds, Angles angles, Vec2 viewport);
std::uint64_t trigger_candidate(const Frame& frame, Bone bone, bool require_visibility,
                                const CollisionScene& scene);
// Called every frame, including with candidate=0 on any lost activation condition.
class TriggerTimer {
  public:
    bool update(std::uint64_t candidate, double now_ms, int delay_ms, int interval_ms);

  private:
    std::uint64_t candidate_{};
    double entered_ms_{}, next_shot_ms_{};
};
} // namespace lsd
