#include "model_maker/geometry.hpp"

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

WireframeModel::WireframeModel(std::vector<Vec3> vertices, std::vector<Edge> edges)
    : vertices_(std::move(vertices)), edges_(std::move(edges)) {
    for (const auto& edge : edges_) {
        if (edge.from >= vertices_.size() || edge.to >= vertices_.size()) {
            throw std::invalid_argument("Edge references a missing vertex");
        }
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
         {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}}
    };
    model.insertionPoint_ = Vec3{};
    return model;
}

WireframeModel WireframeModel::pyramid(double baseSize, double height) {
    if (baseSize <= 0.0 || height <= 0.0) throw std::invalid_argument("Pyramid dimensions must be positive");
    const double h = baseSize / 2.0;
    WireframeModel model{
        {{-h, -h, 0.0}, {h, -h, 0.0}, {h, h, 0.0}, {-h, h, 0.0}, {0.0, 0.0, height}},
        {{0, 1}, {1, 2}, {2, 3}, {3, 0}, {0, 4}, {1, 4}, {2, 4}, {3, 4}}
    };
    model.insertionPoint_ = Vec3{};
    return model;
}

const std::vector<Vec3>& WireframeModel::vertices() const noexcept { return vertices_; }
const std::vector<Edge>& WireframeModel::edges() const noexcept { return edges_; }
bool WireframeModel::isPointEntity() const noexcept { return pointEntity_; }
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

} // namespace mm
