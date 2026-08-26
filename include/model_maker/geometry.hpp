#pragma once

#include <cstddef>
#include <array>
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

    static WorkPlane world() noexcept;
    static std::optional<WorkPlane> fromThreePoints(Vec3 first, Vec3 second, Vec3 third) noexcept;
    static WorkPlane fromAxisAndPoint(Vec3 origin, Vec3 zPoint) noexcept;
    static WorkPlane fromCurrentView(Vec3 origin, const Vec3& viewDir, const Vec3& viewUp) noexcept;
    static WorkPlane fromViewDirection(Vec3 origin, Vec3 viewDirection) noexcept;
    WorkPlane rotatedX(double angleDeg) const noexcept;
    WorkPlane rotatedY(double angleDeg) const noexcept;
    WorkPlane rotatedZ(double angleDeg) const noexcept;
    Vec3 fromPlane(Vec2 point) const noexcept;
    Vec2 toPlane(Vec3 point) const noexcept;
};

struct Edge {
    std::size_t from{};
    std::size_t to{};
    bool operator==(const Edge& other) const noexcept = default;
};

using Face = std::vector<std::size_t>;

struct EntityProperties {
    std::string layer{"0"};
    std::string profileName;
    double profileRotation{};
    std::int64_t profileSourceLine{-1};
    std::string lineType{"BYLAYER"};
    std::string material;
    int colorIndex{256};
    std::optional<std::uint32_t> trueColor;
    int lineWeight{-1};
    double thickness{};
    double lineTypeScale{1.0};
    int transparency{};
    bool visible{true};
    bool frozen{};
    bool locked{};
    bool plottable{true};
    std::string description;
    std::uint32_t effectiveColor{0x68CAFF};
    int effectiveLineWeight{};
    std::string effectiveLineType{"CONTINUOUS"};
    bool operator==(const EntityProperties&) const = default;
};

class WireframeModel {
public:
    WireframeModel() = default;
    WireframeModel(std::vector<Vec3> vertices, std::vector<Edge> edges,
                   std::vector<Face> faces = {});

    static WireframeModel line(Vec3 from, Vec3 to);
    static WireframeModel polyline(const std::vector<Vec3>& vertices);
    static WireframeModel point(Vec3 position);
    static WireframeModel face3D(const std::array<Vec3, 4>& corners);
    static WireframeModel rectangle(Vec3 firstCorner, Vec3 oppositeCorner);
    static WireframeModel circle(Vec3 center, double radius, std::size_t segments = 64);
    static WireframeModel rectangleOnPlane(const WorkPlane& plane, Vec2 firstCorner, Vec2 oppositeCorner);
    static WireframeModel circleOnPlane(const WorkPlane& plane, Vec2 center, double radius,
                                        std::size_t segments = 64);
    static WireframeModel cube(double size);
    static WireframeModel pyramid(double baseSize, double height);

    const std::vector<Vec3>& vertices() const noexcept;
    const std::vector<Edge>& edges() const noexcept;
    const std::vector<Face>& faces() const noexcept;
    bool isPointEntity() const noexcept;
    bool isFace3D() const noexcept;
    std::optional<Vec3> insertionPoint() const noexcept;
    std::optional<Vec3> analyticCenter() const noexcept;
    std::optional<double> analyticRadius() const noexcept;
    const EntityProperties& properties() const noexcept;
    void setProperties(EntityProperties properties);
    void translate(const Vec3& offset) noexcept;
    void rotateAroundZ(const Vec3& center, double radians) noexcept;
    void rotateAroundAxis(const Vec3& center, const Vec3& axis, double radians) noexcept;

private:
    std::vector<Vec3> vertices_;
    std::vector<Edge> edges_;
    std::vector<Face> faces_;
    bool pointEntity_{};
    bool face3D_{};
    std::optional<Vec3> insertionPoint_;
    std::optional<Vec3> analyticCenter_;
    std::optional<double> analyticRadius_;
    EntityProperties properties_;
};
struct ProfileDefinition {
    const wchar_t* label;
    const char* name;
    const char* type;
    double h; // height mm
    double b; // width mm
    double tf; // flange thickness mm
    double tw; // web thickness mm
    double area; // cm²
};
struct NodeConstraint {
    bool ux{}, uy{}, uz{}, rx{}, ry{}, rz{};
    bool isPinned() const noexcept { return ux && uy && uz && !rx && !ry && !rz; }
    bool isFixed() const noexcept { return ux && uy && uz && rx && ry && rz; }
    bool isFree() const noexcept { return !ux && !uy && !uz && !rx && !ry && !rz; }
    void setPinned() noexcept { ux = uy = uz = true; rx = ry = rz = false; }
    void setFixed() noexcept { ux = uy = uz = rx = ry = rz = true; }
    void setFree() noexcept { ux = uy = uz = rx = ry = rz = false; }
};
struct BeamLoad {
    double wY{}; // vertical distributed load (kN/m, positive = -Y direction)
    double wZ{}; // lateral distributed load (kN/m)
};
} // namespace mm
