#pragma once

#include <cstddef>
#include <vector>

namespace mm {

struct Vec2 {
    double x{};
    double y{};
};

struct Vec3 {
    double x{};
    double y{};
    double z{};

    Vec3 operator+(const Vec3& other) const noexcept;
    Vec3 operator-(const Vec3& other) const noexcept;
    Vec3 operator*(double scalar) const noexcept;
    bool operator==(const Vec3& other) const noexcept = default;
};

struct Edge {
    std::size_t from{};
    std::size_t to{};
    bool operator==(const Edge& other) const noexcept = default;
};

class WireframeModel {
public:
    WireframeModel() = default;
    WireframeModel(std::vector<Vec3> vertices, std::vector<Edge> edges);

    static WireframeModel line(Vec3 from, Vec3 to);
    static WireframeModel rectangle(Vec3 firstCorner, Vec3 oppositeCorner);
    static WireframeModel circle(Vec3 center, double radius, std::size_t segments = 64);
    static WireframeModel cube(double size);
    static WireframeModel pyramid(double baseSize, double height);

    const std::vector<Vec3>& vertices() const noexcept;
    const std::vector<Edge>& edges() const noexcept;
    void translate(const Vec3& offset) noexcept;

private:
    std::vector<Vec3> vertices_;
    std::vector<Edge> edges_;
};

} // namespace mm
