#include "lsd/core.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <numbers>
#include <sstream>
#include <stdexcept>

namespace lsd {
namespace {
constexpr float rad = std::numbers::pi_v<float> / 180.0f;
float axis(Vec3 v, int i) {
    return i == 0 ? v.x : (i == 1 ? v.y : v.z);
}
Vec3 minv(Vec3 a, Vec3 b) {
    return {std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z)};
}
Vec3 maxv(Vec3 a, Vec3 b) {
    return {std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z)};
}
bool triangle_hit(const Triangle& t, Vec3 origin, Vec3 dir) {
    const Vec3 e1 = t.b - t.a, e2 = t.c - t.a, p = cross(dir, e2);
    const float determinant = dot(e1, p);
    if (std::abs(determinant) < 1e-7f)
        return false;
    const float inv = 1.0f / determinant;
    const Vec3 s = origin - t.a;
    const float u = dot(s, p) * inv;
    if (u < 0 || u > 1)
        return false;
    const Vec3 q = cross(s, e1);
    const float v = dot(dir, q) * inv;
    if (v < 0 || u + v > 1)
        return false;
    const float distance = dot(e2, q) * inv;
    return distance > 1e-5f && distance < 1.0f - 1e-5f;
}
} // namespace
float dot(Vec3 a, Vec3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
Vec3 cross(Vec3 a, Vec3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
float length(Vec3 v) {
    return std::sqrt(dot(v, v));
}
bool finite(Vec3 v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}
float wrap(float degrees) {
    return std::remainder(degrees, 360.0f);
}
Angles look_at(Vec3 from, Vec3 to) {
    const auto d = to - from;
    return {-std::atan2(d.z, std::hypot(d.x, d.y)) / rad, std::atan2(d.y, d.x) / rad, 0};
}
float angular_distance(Angles a, Angles b) {
    // Angle between unit forward vectors: actual circular cone, including at high pitch.
    const float d = std::sin(a.pitch * rad) * std::sin(b.pitch * rad) +
                    std::cos(a.pitch * rad) * std::cos(b.pitch * rad) * std::cos(wrap(a.yaw - b.yaw) * rad);
    return std::acos(std::clamp(d, -1.0f, 1.0f)) / rad;
}
Angles smooth(Angles current, Angles target, float tau, float dt) {
    const float alpha = tau <= 0 ? 1.0f : 1.0f - std::exp(-std::clamp(dt, 0.0f, 0.1f) / tau);
    return {std::clamp(current.pitch + wrap(target.pitch - current.pitch) * alpha, -89.0f, 89.0f),
            wrap(current.yaw + wrap(target.yaw - current.yaw) * alpha), 0};
}
std::optional<Vec2> project(Vec3 v, const Matrix& m, Vec2 size) {
    if (!finite(v) || size.x <= 0 || size.y <= 0)
        return {};
    const auto row = [&](int r) { return m.m[r][0] * v.x + m.m[r][1] * v.y + m.m[r][2] * v.z + m.m[r][3]; };
    const float w = row(3), x = row(0), y = row(1);
    if (!std::isfinite(w) || !std::isfinite(x) || !std::isfinite(y) || w <= 0.01f)
        return {};
    const Vec2 out{size.x * (1 + x / w) / 2, size.y * (1 - y / w) / 2};
    if (!std::isfinite(out.x) || !std::isfinite(out.y))
        return {};
    return out;
}
Matrix camera_matrix(Vec3 eye, Angles a, float aspect, float fov) {
    const float p = a.pitch * rad, y = a.yaw * rad, t = std::tan(fov * rad / 2);
    const Vec3 forward{std::cos(p) * std::cos(y), std::cos(p) * std::sin(y), -std::sin(p)};
    const Vec3 right{std::sin(y), -std::cos(y), 0}, up = cross(right, forward);
    Matrix m;
    const auto set = [&](int r, Vec3 v) {
        m.m[r][0] = v.x;
        m.m[r][1] = v.y;
        m.m[r][2] = v.z;
        m.m[r][3] = -dot(v, eye);
    };
    set(0, right * (1 / t));
    set(1, up * (aspect / t));
    set(2, forward);
    set(3, forward);
    return m;
}
std::vector<int> parse_pattern(const std::string& signature) {
    std::istringstream s(signature);
    std::string token;
    std::vector<int> result;
    while (s >> token) {
        if (token == "?" || token == "??")
            result.push_back(-1);
        else {
            if (token.size() != 2 || token.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos)
                throw std::runtime_error("Invalid signature token: " + token);
            result.push_back(std::stoi(token, nullptr, 16));
        }
    }
    if (result.empty() || std::all_of(result.begin(), result.end(), [](int x) { return x < 0; }))
        throw std::runtime_error("Empty/all-wildcard signature");
    return result;
}
std::optional<std::size_t> unique_pattern(std::span<const std::uint8_t> b, std::span<const int> p) {
    if (p.empty() || b.size() < p.size())
        return {};
    std::optional<std::size_t> found;
    for (std::size_t i = 0; i <= b.size() - p.size(); ++i) {
        bool match = true;
        for (std::size_t j = 0; j < p.size(); ++j)
            if (p[j] >= 0 && b[i + j] != p[j]) {
                match = false;
                break;
            }
        if (match) {
            if (found)
                throw std::runtime_error("Ambiguous signature");
            found = i;
        }
    }
    return found;
}
void CollisionScene::set(std::vector<Triangle> triangles, std::string map) {
    if (triangles.empty() || triangles.size() > 2'000'000 || map.empty())
        throw std::runtime_error("Empty/oversized collision scene or missing map name");
    for (const auto& t : triangles)
        if (!finite(t.a) || !finite(t.b) || !finite(t.c))
            throw std::runtime_error("Nonfinite collision vertex");
    CollisionScene next;
    next.triangles_ = std::move(triangles);
    next.map_ = std::move(map);
    next.nodes_.reserve(next.triangles_.size() * 2);
    next.build(0, next.triangles_.size());
    *this = std::move(next);
}
int CollisionScene::build(std::size_t begin, std::size_t end) {
    const float inf = std::numeric_limits<float>::infinity();
    Node n{{inf, inf, inf}, {-inf, -inf, -inf}, -1, -1, begin, end};
    for (auto i = begin; i < end; ++i)
        for (const auto v : {triangles_[i].a, triangles_[i].b, triangles_[i].c}) {
            n.low = minv(n.low, v);
            n.high = maxv(n.high, v);
        }
    const int index = static_cast<int>(nodes_.size());
    nodes_.push_back(n);
    if (end - begin > 8) {
        const auto d = n.high - n.low;
        const int a = d.x > d.y ? (d.x > d.z ? 0 : 2) : (d.y > d.z ? 1 : 2);
        const auto mid = begin + (end - begin) / 2;
        std::nth_element(triangles_.begin() + begin, triangles_.begin() + mid, triangles_.begin() + end,
                         [a](const Triangle& x, const Triangle& y) {
                             return axis(x.a + x.b + x.c, a) < axis(y.a + y.b + y.c, a);
                         });
        const int left = build(begin, mid), right = build(mid, end);
        nodes_[index].left = left;
        nodes_[index].right = right;
    }
    return index;
}
bool CollisionScene::hit(int index, Vec3 from, Vec3 dir) const {
    const auto& n = nodes_[index];
    float near = 0, far = 1;
    for (int a = 0; a < 3; ++a) {
        const float o = axis(from, a), d = axis(dir, a), lo = axis(n.low, a), hi = axis(n.high, a);
        if (std::abs(d) < 1e-8f) {
            if (o < lo || o > hi)
                return false;
        } else {
            float t0 = (lo - o) / d, t1 = (hi - o) / d;
            if (t0 > t1)
                std::swap(t0, t1);
            near = std::max(near, t0);
            far = std::min(far, t1);
            if (near > far)
                return false;
        }
    }
    if (n.left >= 0)
        return hit(n.left, from, dir) || hit(n.right, from, dir);
    for (auto i = n.begin; i < n.end; ++i)
        if (triangle_hit(triangles_[i], from, dir))
            return true;
    return false;
}
Visibility CollisionScene::visibility(const std::string& map, Vec3 from, Vec3 to) const {
    if (nodes_.empty() || map != map_ || !finite(from) || !finite(to) || length(to - from) < 0.001f)
        return Visibility::Unknown;
    return hit(0, from, to - from) ? Visibility::Occluded : Visibility::Visible;
}
void CollisionScene::load_obj(const std::filesystem::path& path, std::string map) {
    std::ifstream f(path);
    if (!f)
        throw std::runtime_error("Cannot open collision OBJ");
    std::vector<Vec3> vertices;
    std::vector<Triangle> triangles;
    std::string line;
    while (std::getline(f, line)) {
        if (line.size() > 1'000'000)
            throw std::runtime_error("Oversized OBJ line");
        std::istringstream s(line);
        std::string kind;
        s >> kind;
        if (kind == "v") {
            Vec3 v;
            if (!(s >> v.x >> v.y >> v.z) || !finite(v))
                throw std::runtime_error("Invalid OBJ vertex");
            vertices.push_back(v);
        } else if (kind == "f") {
            std::vector<std::size_t> face;
            std::string token;
            while (s >> token) {
                if (token[0] == '#')
                    break;
                const auto raw = token.substr(0, token.find('/'));
                std::size_t used = 0;
                auto i = std::stoll(raw, &used);
                if (used != raw.size() || i == 0)
                    throw std::runtime_error("Invalid OBJ index");
                i = i > 0 ? i - 1 : static_cast<long long>(vertices.size()) + i;
                if (i < 0 || static_cast<std::size_t>(i) >= vertices.size())
                    throw std::runtime_error("OBJ index out of bounds");
                face.push_back(static_cast<std::size_t>(i));
            }
            if (face.size() < 3)
                throw std::runtime_error("Invalid OBJ face");
            for (std::size_t i = 1; i + 1 < face.size(); ++i)
                triangles.push_back({vertices[face[0]], vertices[face[i]], vertices[face[i + 1]]});
        }
        if (vertices.size() > 4'000'000 || triangles.size() > 2'000'000)
            throw std::runtime_error("Collision OBJ too large");
    }
    if (f.bad())
        throw std::runtime_error("Collision OBJ read failed");
    set(std::move(triangles), std::move(map));
}
std::optional<Target> select_target(const Frame& f, Bone bone, float fov, bool visible,
                                    const CollisionScene& scene) {
    if (!f.alive || !finite(f.eye) || !std::isfinite(fov) || fov <= 0)
        return {};
    const auto bi = static_cast<std::size_t>(bone);
    if (bi >= 3)
        return {};
    std::optional<Target> best;
    for (std::size_t i = 0; i < f.players.size(); ++i) {
        const auto& p = f.players[i];
        const auto& point = p.bones[bi];
        if (p.pawn == f.local_pawn || p.team == f.team || p.team < 2 || p.health <= 0 || !point ||
            !finite(*point))
            continue;
        const auto a = look_at(f.eye, *point);
        const float error = angular_distance(f.angles, a);
        if (!std::isfinite(error) || error > fov || (best && error >= best->error))
            continue;
        const auto v = scene.visibility(f.map, f.eye, *point);
        if (visible && v != Visibility::Visible)
            continue;
        best = Target{i, *point, a, error, v};
    }
    return best;
}
std::uint64_t trigger_candidate(const Frame& f, Bone bone, bool visible, const CollisionScene& scene) {
    const auto b = static_cast<std::size_t>(bone);
    if (!f.alive || !f.crosshair_pawn || b >= 3 || !finite(f.eye))
        return 0;
    for (const auto& p : f.players) {
        if (p.pawn != f.crosshair_pawn || p.pawn == f.local_pawn || p.health <= 0 || p.team == f.team ||
            (p.team != 2 && p.team != 3))
            continue;
        if (visible && (!p.bones[b] || scene.visibility(f.map, f.eye, *p.bones[b]) != Visibility::Visible))
            return 0;
        return p.pawn;
    }
    return 0;
}
bool TriggerTimer::update(std::uint64_t candidate, double now_ms, int delay_ms, int interval_ms) {
    if (!candidate || !std::isfinite(now_ms) || delay_ms < 0 || interval_ms < 50) {
        candidate_ = 0;
        return false;
    }
    if (candidate != candidate_) {
        candidate_ = candidate;
        entered_ms_ = now_ms;
    }
    if (now_ms - entered_ms_ < delay_ms || now_ms < next_shot_ms_)
        return false;
    next_shot_ms_ = now_ms + interval_ms;
    return true;
}
Frame demo_frame(double seconds, Angles angles, Vec2 viewport) {
    Frame f;
    f.eye = {0, 0, 64};
    f.angles = angles;
    f.team = 2;
    f.local_pawn = 1;
    f.alive = true;
    f.map = "training";
    f.matrix = camera_matrix(f.eye, angles, viewport.x / viewport.y, 90);
    for (int i = 0; i < 5; ++i) {
        Player p;
        p.pawn = static_cast<std::uint64_t>(i + 2);
        p.team = i == 4 ? 2 : 3;
        p.health = 100 - i * 12;
        p.name = "Target " + std::to_string(i + 1);
        p.weapon = i % 2 ? "AK-47" : "M4A1-S";
        p.origin = {350.0f + static_cast<float>(i) * 100,
                    static_cast<float>(i - 2) * 90 + std::sin(static_cast<float>(seconds) + i) * 35, 0};
        p.bones = {p.origin + Vec3{0, 0, 68}, p.origin + Vec3{0, 0, 62}, p.origin + Vec3{0, 0, 48}};
        f.players.push_back(p);
    }
    // Synthetic demo hit volumes: 4-unit spheres around the displayed bone points.
    const float pitch = angles.pitch * rad, yaw = angles.yaw * rad;
    const Vec3 forward{std::cos(pitch) * std::cos(yaw), std::cos(pitch) * std::sin(yaw), -std::sin(pitch)};
    float nearest = std::numeric_limits<float>::infinity();
    for (const auto& player : f.players)
        for (const auto& bone : player.bones) {
            if (!bone)
                continue;
            const Vec3 delta = *bone - f.eye;
            const float along = dot(delta, forward);
            if (along > 0 && along < nearest && length(delta - forward * along) <= 4) {
                nearest = along;
                f.crosshair_pawn = player.pawn;
            }
        }
    return f;
}
} // namespace lsd
