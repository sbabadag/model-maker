#include "model_maker/geometry.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>
#include <utility>

namespace mm {

namespace {
double dot3(const Vec3& a, const Vec3& b) noexcept {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

Vec3 cross3(const Vec3& a, const Vec3& b) noexcept {
    return {a.y * b.z - a.z * b.y,
            a.z * b.x - a.x * b.z,
            a.x * b.y - a.y * b.x};
}

std::optional<Vec3> normalized(Vec3 value) noexcept {
    const double length = std::sqrt(dot3(value, value));
    if (length <= 1e-9) return std::nullopt;
    return value * (1.0 / length);
}
}

Vec3 Vec3::operator+(const Vec3& other) const noexcept { return {x + other.x, y + other.y, z + other.z}; }
Vec3 Vec3::operator-(const Vec3& other) const noexcept { return {x - other.x, y - other.y, z - other.z}; }
Vec3 Vec3::operator*(double scalar) const noexcept { return {x * scalar, y * scalar, z * scalar}; }

std::optional<WorkPlane> WorkPlane::fromThreePoints(Vec3 first, Vec3 second, Vec3 third) noexcept {
    const auto uAxis = normalized(second - first);
    if (!uAxis) return std::nullopt;
    const auto normalAxis = normalized(cross3(*uAxis, third - first));
    if (!normalAxis) return std::nullopt;
    const auto vAxis = normalized(cross3(*normalAxis, *uAxis));
    if (!vAxis) return std::nullopt;
    return WorkPlane{first, *uAxis, *vAxis, *normalAxis};
}

Vec3 WorkPlane::fromPlane(Vec2 point) const noexcept {
    return origin + u * point.x + v * point.y;
}

Vec2 WorkPlane::toPlane(Vec3 point) const noexcept {
    const Vec3 relative = point - origin;
    return {dot3(relative, u), dot3(relative, v)};
}

WorkPlane WorkPlane::world() noexcept {
    return WorkPlane{};
}

WorkPlane WorkPlane::fromAxisAndPoint(Vec3 origin, Vec3 zPoint) noexcept {
    const auto normalAxis = normalized(zPoint - origin);
    if (!normalAxis) return WorkPlane{};
    Vec3 ref{0.0, 1.0, 0.0};
    if (std::abs(dot3(*normalAxis, ref)) > 0.99) ref = {1.0, 0.0, 0.0};
    const auto uAxis = normalized(cross3(ref, *normalAxis));
    if (!uAxis) return WorkPlane{};
    const auto vAxis = normalized(cross3(*normalAxis, *uAxis));
    if (!vAxis) return WorkPlane{};
    return WorkPlane{origin, *uAxis, *vAxis, *normalAxis};
}

WorkPlane WorkPlane::fromCurrentView(Vec3 origin, const Vec3& viewDir, const Vec3& viewUp) noexcept {
    const auto normalAxis = normalized(viewDir);
    if (!normalAxis) return WorkPlane{};
    const auto uAxis = normalized(cross3(viewUp, *normalAxis));
    if (!uAxis) return WorkPlane{};
    const auto vAxis = normalized(cross3(*normalAxis, *uAxis));
    if (!vAxis) return WorkPlane{};
    return WorkPlane{origin, *uAxis, *vAxis, *normalAxis};
}

WorkPlane WorkPlane::fromViewDirection(Vec3 origin, Vec3 viewDirection) noexcept {
    const auto normalAxis = normalized(viewDirection);
    if (!normalAxis) return WorkPlane{};
    Vec3 ref{0.0, 0.0, 1.0};
    if (std::abs(dot3(*normalAxis, ref)) > 0.99) ref = {0.0, 1.0, 0.0};
    const auto uAxis = normalized(cross3(ref, *normalAxis));
    if (!uAxis) return WorkPlane{};
    const auto vAxis = normalized(cross3(*normalAxis, *uAxis));
    if (!vAxis) return WorkPlane{};
    return WorkPlane{origin, *uAxis, *vAxis, *normalAxis};
}

WorkPlane WorkPlane::rotatedX(double angleDeg) const noexcept {
    const double rad = angleDeg * 3.14159265358979323846 / 180.0;
    const double c = std::cos(rad), s = std::sin(rad);
    return WorkPlane{origin,
        u,
        v * c + normal * s,
        normal * c - v * s};
}

WorkPlane WorkPlane::rotatedY(double angleDeg) const noexcept {
    const double rad = angleDeg * 3.14159265358979323846 / 180.0;
    const double c = std::cos(rad), s = std::sin(rad);
    return WorkPlane{origin,
        u * c - normal * s,
        v,
        normal * c + u * s};
}

WorkPlane WorkPlane::rotatedZ(double angleDeg) const noexcept {
    const double rad = angleDeg * 3.14159265358979323846 / 180.0;
    const double c = std::cos(rad), s = std::sin(rad);
    return WorkPlane{origin,
        u * c + v * s,
        v * c - u * s,
        normal};
}

WireframeModel::WireframeModel(std::vector<Vec3> vertices, std::vector<Edge> edges,
                               std::vector<Face> faces)
    : vertices_(std::move(vertices)), edges_(std::move(edges)), faces_(std::move(faces)) {
    for (const auto& edge : edges_) {
        if (edge.from >= vertices_.size() || edge.to >= vertices_.size()) {
            throw std::invalid_argument("Edge references a missing vertex");
        }
    }
    for (const auto& face : faces_) {
        if (face.size() < 3 || std::any_of(face.begin(), face.end(), [&](std::size_t index) {
                return index >= vertices_.size();
            })) throw std::invalid_argument("Face references a missing vertex");
    }
}

WireframeModel WireframeModel::line(Vec3 from, Vec3 to) {
    return {{from, to}, {{0, 1}}};
}

WireframeModel WireframeModel::point(Vec3 position) {
    WireframeModel model({position}, {});
    model.pointEntity_ = true;
    return model;
}

WireframeModel WireframeModel::face3D(const std::array<Vec3, 4>& corners) {
    WireframeModel model({corners.begin(), corners.end()}, {{0, 1}, {1, 2}, {2, 3}, {3, 0}},
                         {{0, 1, 2, 3}});
    model.face3D_ = true;
    return model;
}

WireframeModel WireframeModel::rectangle(Vec3 firstCorner, Vec3 oppositeCorner) {
    return {{{firstCorner.x, firstCorner.y, firstCorner.z},
             {oppositeCorner.x, firstCorner.y, firstCorner.z},
             {oppositeCorner.x, oppositeCorner.y, firstCorner.z},
             {firstCorner.x, oppositeCorner.y, firstCorner.z}},
            {{0, 1}, {1, 2}, {2, 3}, {3, 0}}};
}

WireframeModel WireframeModel::rectangleOnPlane(const WorkPlane& plane, Vec2 firstCorner,
                                                 Vec2 oppositeCorner) {
    return {{plane.fromPlane(firstCorner),
             plane.fromPlane({oppositeCorner.x, firstCorner.y}),
             plane.fromPlane(oppositeCorner),
             plane.fromPlane({firstCorner.x, oppositeCorner.y})},
            {{0, 1}, {1, 2}, {2, 3}, {3, 0}}};
}

WireframeModel WireframeModel::circleOnPlane(const WorkPlane& plane, Vec2 center, double radius,
                                              std::size_t segments) {
    if (radius <= 0.0) throw std::invalid_argument("Circle radius must be positive");
    if (segments < 3) throw std::invalid_argument("Circle must have at least three segments");
    std::vector<Vec3> vertices;
    std::vector<Edge> edges;
    vertices.reserve(segments);
    edges.reserve(segments);
    for (std::size_t index = 0; index < segments; ++index) {
        const double angle = 2.0 * std::numbers::pi * static_cast<double>(index) / static_cast<double>(segments);
        vertices.push_back(plane.fromPlane({center.x + radius * std::cos(angle),
                                            center.y + radius * std::sin(angle)}));
        edges.push_back({index, (index + 1) % segments});
    }
    WireframeModel model(std::move(vertices), std::move(edges));
    model.analyticCenter_ = plane.fromPlane(center);
    model.analyticRadius_ = radius;
    return model;
}

WireframeModel WireframeModel::circle(Vec3 center, double radius, std::size_t segments) {
    if (radius <= 0.0) throw std::invalid_argument("Circle radius must be positive");
    if (segments < 3) throw std::invalid_argument("Circle must have at least three segments");
    std::vector<Vec3> vertices;
    std::vector<Edge> edges;
    vertices.reserve(segments);
    edges.reserve(segments);
    for (std::size_t index = 0; index < segments; ++index) {
        const double angle = 2.0 * std::numbers::pi * static_cast<double>(index) / static_cast<double>(segments);
        vertices.push_back({center.x + radius * std::cos(angle), center.y + radius * std::sin(angle), center.z});
        edges.push_back({index, (index + 1) % segments});
    }
    WireframeModel model(std::move(vertices), std::move(edges));
    model.analyticCenter_ = center;
    model.analyticRadius_ = radius;
    return model;
}

WireframeModel WireframeModel::cube(double size) {
    if (size <= 0.0) throw std::invalid_argument("Cube size must be positive");
    const double h = size / 2.0;
    WireframeModel model{
        {{-h, -h, -h}, {h, -h, -h}, {h, h, -h}, {-h, h, -h},
         {-h, -h, h},  {h, -h, h},  {h, h, h},  {-h, h, h}},
        {{0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6},
         {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}},
        {{0, 3, 2, 1}, {4, 5, 6, 7}, {0, 1, 5, 4},
         {1, 2, 6, 5}, {2, 3, 7, 6}, {3, 0, 4, 7}}
    };
    model.insertionPoint_ = Vec3{};
    return model;
}

WireframeModel WireframeModel::pyramid(double baseSize, double height) {
    if (baseSize <= 0.0 || height <= 0.0) throw std::invalid_argument("Pyramid dimensions must be positive");
    const double h = baseSize / 2.0;
    WireframeModel model{
        {{-h, -h, 0.0}, {h, -h, 0.0}, {h, h, 0.0}, {-h, h, 0.0}, {0.0, 0.0, height}},
        {{0, 1}, {1, 2}, {2, 3}, {3, 0}, {0, 4}, {1, 4}, {2, 4}, {3, 4}},
        {{0, 3, 2, 1}, {0, 1, 4}, {1, 2, 4}, {2, 3, 4}, {3, 0, 4}}
    };
    model.insertionPoint_ = Vec3{};
    return model;
}

const std::vector<Vec3>& WireframeModel::vertices() const noexcept { return vertices_; }
const std::vector<Edge>& WireframeModel::edges() const noexcept { return edges_; }
const std::vector<Face>& WireframeModel::faces() const noexcept { return faces_; }
bool WireframeModel::isPointEntity() const noexcept { return pointEntity_; }
bool WireframeModel::isFace3D() const noexcept { return face3D_; }
std::optional<Vec3> WireframeModel::insertionPoint() const noexcept { return insertionPoint_; }
std::optional<Vec3> WireframeModel::analyticCenter() const noexcept { return analyticCenter_; }
std::optional<double> WireframeModel::analyticRadius() const noexcept { return analyticRadius_; }
const EntityProperties& WireframeModel::properties() const noexcept { return properties_; }
void WireframeModel::setProperties(EntityProperties properties) { properties_ = std::move(properties); }

void WireframeModel::translate(const Vec3& offset) noexcept {
    for (auto& vertex : vertices_) vertex = vertex + offset;
    if (insertionPoint_) *insertionPoint_ = *insertionPoint_ + offset;
    if (analyticCenter_) *analyticCenter_ = *analyticCenter_ + offset;
}

void WireframeModel::rotateAroundZ(const Vec3& center, double radians) noexcept {
    const double cosine = std::cos(radians);
    const double sine = std::sin(radians);
    const auto rotatePoint = [&](Vec3 point) {
        const double x = point.x - center.x;
        const double y = point.y - center.y;
        point.x = center.x + x * cosine - y * sine;
        point.y = center.y + x * sine + y * cosine;
        return point;
    };
    for (auto& vertex : vertices_) vertex = rotatePoint(vertex);
    if (insertionPoint_) *insertionPoint_ = rotatePoint(*insertionPoint_);
    if (analyticCenter_) *analyticCenter_ = rotatePoint(*analyticCenter_);
}

} // namespace mm
