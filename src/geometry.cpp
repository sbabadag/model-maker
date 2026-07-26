#include "model_maker/geometry.hpp"

#include <cmath>
#include <numbers>
#include <stdexcept>
#include <utility>

namespace mm {

Vec3 Vec3::operator+(const Vec3& other) const noexcept { return {x + other.x, y + other.y, z + other.z}; }
Vec3 Vec3::operator-(const Vec3& other) const noexcept { return {x - other.x, y - other.y, z - other.z}; }
Vec3 Vec3::operator*(double scalar) const noexcept { return {x * scalar, y * scalar, z * scalar}; }

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

WireframeModel WireframeModel::rectangle(Vec3 firstCorner, Vec3 oppositeCorner) {
    return {{{firstCorner.x, firstCorner.y, firstCorner.z},
             {oppositeCorner.x, firstCorner.y, firstCorner.z},
             {oppositeCorner.x, oppositeCorner.y, firstCorner.z},
             {firstCorner.x, oppositeCorner.y, firstCorner.z}},
            {{0, 1}, {1, 2}, {2, 3}, {3, 0}}};
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
    return {std::move(vertices), std::move(edges)};
}

WireframeModel WireframeModel::cube(double size) {
    if (size <= 0.0) throw std::invalid_argument("Cube size must be positive");
    const double h = size / 2.0;
    return {
        {{-h, -h, -h}, {h, -h, -h}, {h, h, -h}, {-h, h, -h},
         {-h, -h, h},  {h, -h, h},  {h, h, h},  {-h, h, h}},
        {{0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6},
         {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}}
    };
}

WireframeModel WireframeModel::pyramid(double baseSize, double height) {
    if (baseSize <= 0.0 || height <= 0.0) throw std::invalid_argument("Pyramid dimensions must be positive");
    const double h = baseSize / 2.0;
    return {
        {{-h, -h, 0.0}, {h, -h, 0.0}, {h, h, 0.0}, {-h, h, 0.0}, {0.0, 0.0, height}},
        {{0, 1}, {1, 2}, {2, 3}, {3, 0}, {0, 4}, {1, 4}, {2, 4}, {3, 4}}
    };
}

const std::vector<Vec3>& WireframeModel::vertices() const noexcept { return vertices_; }
const std::vector<Edge>& WireframeModel::edges() const noexcept { return edges_; }

void WireframeModel::translate(const Vec3& offset) noexcept {
    for (auto& vertex : vertices_) vertex = vertex + offset;
}

} // namespace mm
