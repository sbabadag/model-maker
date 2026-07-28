#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
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

struct WorkPlane {
    Vec3 origin{};
    Vec3 u{1.0, 0.0, 0.0};
    Vec3 v{0.0, 1.0, 0.0};
    Vec3 normal{0.0, 0.0, 1.0};

    static std::optional<WorkPlane> fromThreePoints(Vec3 first, Vec3 second, Vec3 third) noexcept;
    Vec3 fromPlane(Vec2 point) const noexcept;
    Vec2 toPlane(Vec3 point) const noexcept;
};

struct Edge {
    std::size_t from{};
    std::size_t to{};
    bool operator==(const Edge& other) const noexcept = default;
};

struct EntityProperties {
    std::string layer{"0"};
    std::string lineType{"BYLAYER"};
    int colorIndex{256};
    std::optional<std::uint32_t> trueColor;
    int lineWeight{-1};
    double thickness{};
    double lineTypeScale{1.0};
    int transparency{};
    bool visible{true};
    std::uint32_t effectiveColor{0x68CAFF};
    int effectiveLineWeight{25};
    std::string effectiveLineType{"CONTINUOUS"};
    bool operator==(const EntityProperties&) const = default;
};

class WireframeModel {
public:
    WireframeModel() = default;
    WireframeModel(std::vector<Vec3> vertices, std::vector<Edge> edges);

    static WireframeModel line(Vec3 from, Vec3 to);
    static WireframeModel point(Vec3 position);
    static WireframeModel rectangle(Vec3 firstCorner, Vec3 oppositeCorner);
    static WireframeModel circle(Vec3 center, double radius, std::size_t segments = 64);
    static WireframeModel rectangleOnPlane(const WorkPlane& plane, Vec2 firstCorner, Vec2 oppositeCorner);
    static WireframeModel circleOnPlane(const WorkPlane& plane, Vec2 center, double radius,
                                        std::size_t segments = 64);
    static WireframeModel cube(double size);
    static WireframeModel pyramid(double baseSize, double height);

    const std::vector<Vec3>& vertices() const noexcept;
    const std::vector<Edge>& edges() const noexcept;
    bool isPointEntity() const noexcept;
    std::optional<Vec3> insertionPoint() const noexcept;
    std::optional<Vec3> analyticCenter() const noexcept;
    std::optional<double> analyticRadius() const noexcept;
    const EntityProperties& properties() const noexcept;
    void setProperties(EntityProperties properties);
    void translate(const Vec3& offset) noexcept;

private:
    std::vector<Vec3> vertices_;
    std::vector<Edge> edges_;
    bool pointEntity_{};
    std::optional<Vec3> insertionPoint_;
    std::optional<Vec3> analyticCenter_;
    std::optional<double> analyticRadius_;
    EntityProperties properties_;
};

} // namespace mm
