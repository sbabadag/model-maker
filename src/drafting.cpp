#include "model_maker/drafting.hpp"

#include <algorithm>
#include <array>
#include <cmath>

#include <functional>
#include <limits>
#include <numbers>
#include <string>
#include <vector>

namespace mm {
namespace {
constexpr double epsilon = 1e-9;

double distance2D(const Vec3& a, const Vec3& b) noexcept {
    return std::hypot(a.x - b.x, a.y - b.y);
}

double dot2D(const Vec3& a, const Vec3& b) noexcept { return a.x * b.x + a.y * b.y; }

int priority(SnapType type) noexcept {
    switch (type) {
    case SnapType::Endpoint: return 0;
    case SnapType::Intersection: return 1;
    case SnapType::Perpendicular: return 2;
    case SnapType::Tangent: return 3;
    case SnapType::Midpoint: return 4;
    case SnapType::Center: return 5;
    case SnapType::GeometricCenter: return 6;
    case SnapType::Quadrant: return 7;
    case SnapType::Node: return 8;
    case SnapType::Insertion: return 9;
    case SnapType::ApparentIntersection: return 10;
    case SnapType::Extension: return 11;
    case SnapType::Parallel: return 12;
    case SnapType::Nearest: return 13;
    default: return 100;
    }
}

struct CircleInfo { Vec3 center{}; double radius{}; };

std::optional<CircleInfo> detectCircle(const WireframeModel& model) noexcept {
    if (model.analyticCenter() && model.analyticRadius())
        return CircleInfo{*model.analyticCenter(), *model.analyticRadius()};
    const auto& vertices = model.vertices();
    if (vertices.size() < 8 || model.edges().size() != vertices.size()) return std::nullopt;
    Vec3 center{};
    for (const auto& vertex : vertices) center = center + vertex;
    center = center * (1.0 / static_cast<double>(vertices.size()));
    double radius{};
    for (const auto& vertex : vertices) radius += distance2D(vertex, center);
    radius /= static_cast<double>(vertices.size());
    if (radius <= epsilon) return std::nullopt;
    for (const auto& vertex : vertices) {
        if (std::abs(vertex.z - center.z) > radius * 1e-6 + epsilon ||
            std::abs(distance2D(vertex, center) - radius) > radius * 1e-5) return std::nullopt;
    }
    return CircleInfo{center, radius};
}

bool isClosed(const WireframeModel& model) {
    if (model.vertices().size() < 3 || model.edges().size() < 3) return false;
    std::vector<int> degree(model.vertices().size());
    for (const auto& edge : model.edges()) {
        if (edge.from >= degree.size() || edge.to >= degree.size()) return false;
        ++degree[edge.from]; ++degree[edge.to];
    }
    return std::all_of(degree.begin(), degree.end(), [](int value) { return value == 2; });
}

std::optional<Vec3> segmentIntersection(const Vec3& a, const Vec3& b,
                                        const Vec3& c, const Vec3& d) noexcept {
    const double abx = b.x - a.x, aby = b.y - a.y;
    const double cdx = d.x - c.x, cdy = d.y - c.y;
    const double denominator = abx * cdy - aby * cdx;
    if (std::abs(denominator) <= epsilon) return std::nullopt;
    const double acx = c.x - a.x, acy = c.y - a.y;
    const double t = (acx * cdy - acy * cdx) / denominator;
    const double u = (acx * aby - acy * abx) / denominator;
    if (t < -epsilon || t > 1.0 + epsilon || u < -epsilon || u > 1.0 + epsilon) return std::nullopt;
    const Vec3 first{a.x + t * abx, a.y + t * aby, a.z + t * (b.z - a.z)};
    const Vec3 second{c.x + u * cdx, c.y + u * cdy, c.z + u * (d.z - c.z)};
    if (std::abs(first.z - second.z) > 1e-7) return std::nullopt;
    return {(first + second) * 0.5};
}

struct Candidate { Vec3 point{}; SnapType type{SnapType::None}; double distance{}; };
using Metric = std::function<double(const Vec3&)>;

void addCandidate(std::vector<Candidate>& candidates, const Vec3& point, SnapType type,
                  double tolerance, const Metric& metric) {
    const double distance = metric(point);
    if (std::isfinite(distance) && distance <= tolerance + epsilon)
        candidates.push_back({point, type, distance});
}

std::vector<Candidate> objectCandidates(const Vec3& cursor, const Document& document,
                                        double tolerance, const Metric& metric,
                                        std::optional<Vec3> referencePoint,
                                        const std::vector<std::size_t>* candidateIndices = nullptr) {
    std::vector<Candidate> candidates;
    struct Segment { Vec3 a; Vec3 b; bool analyticCircle{}; };
    std::vector<Segment> segments;
    std::vector<std::size_t> allIndices;
    if (!candidateIndices) {
        allIndices.resize(document.models().size());
        for (std::size_t i = 0; i < allIndices.size(); ++i) allIndices[i] = i;
        candidateIndices = &allIndices;
    }
    std::size_t totalVertices{}, totalEdges{};
    for (const auto modelIndex : *candidateIndices) {
        if (modelIndex >= document.models().size()) continue;
        totalVertices += document.models()[modelIndex].vertices().size();
        totalEdges += document.models()[modelIndex].edges().size();
    }
    constexpr std::size_t candidateBudget = 20'000;
    const std::size_t vertexStride = std::max<std::size_t>(1, (totalVertices + candidateBudget - 1) /
                                                               candidateBudget);
    const std::size_t edgeStride = std::max<std::size_t>(1, (totalEdges + candidateBudget - 1) /
                                                             candidateBudget);

    for (const auto modelIndex : *candidateIndices) {
        if (modelIndex >= document.models().size()) continue;
        const auto& model = document.models()[modelIndex];
        const auto circle = detectCircle(model);
        const auto& vertices = model.vertices();
        if (model.isPointEntity() && !vertices.empty())
            addCandidate(candidates, vertices.front(), SnapType::Node, tolerance, metric);
        if (!circle && !model.isPointEntity()) {
            for (std::size_t vertexIndex = 0; vertexIndex < vertices.size(); vertexIndex += vertexStride)
                addCandidate(candidates, vertices[vertexIndex], SnapType::Endpoint, tolerance, metric);
        }

        for (std::size_t edgeIndex = 0; edgeIndex < model.edges().size(); edgeIndex += edgeStride) {
            const auto& edge = model.edges()[edgeIndex];
            const Vec3& a = vertices[edge.from];
            const Vec3& b = vertices[edge.to];
            segments.push_back({a, b, circle.has_value()});
            if (circle) continue;
            addCandidate(candidates, (a + b) * 0.5, SnapType::Midpoint, tolerance, metric);
            const Vec3 direction = b - a;
            const double lengthSquared = dot2D(direction, direction);
            if (lengthSquared <= epsilon) continue;
            const double rawT = dot2D(cursor - a, direction) / lengthSquared;
            const double clampedT = std::clamp(rawT, 0.0, 1.0);
            addCandidate(candidates, a + direction * clampedT, SnapType::Nearest, tolerance, metric);
            if (rawT < 0.0 || rawT > 1.0)
                addCandidate(candidates, a + direction * rawT, SnapType::Extension, tolerance, metric);
            if (referencePoint) {
                const double perpendicularT = dot2D(*referencePoint - a, direction) / lengthSquared;
                if (perpendicularT >= 0.0 && perpendicularT <= 1.0)
                    addCandidate(candidates, a + direction * perpendicularT,
                                 SnapType::Perpendicular, tolerance, metric);
                const double parallelT = dot2D(cursor - *referencePoint, direction) / lengthSquared;
                addCandidate(candidates, *referencePoint + direction * parallelT,
                             SnapType::Parallel, tolerance, metric);
            }
        }

        if (circle) {
            addCandidate(candidates, circle->center, SnapType::Center, tolerance, metric);
            addCandidate(candidates, {circle->center.x + circle->radius, circle->center.y, circle->center.z},
                         SnapType::Quadrant, tolerance, metric);
            addCandidate(candidates, {circle->center.x - circle->radius, circle->center.y, circle->center.z},
                         SnapType::Quadrant, tolerance, metric);
            addCandidate(candidates, {circle->center.x, circle->center.y + circle->radius, circle->center.z},
                         SnapType::Quadrant, tolerance, metric);
            addCandidate(candidates, {circle->center.x, circle->center.y - circle->radius, circle->center.z},
                         SnapType::Quadrant, tolerance, metric);
            const Vec3 radial = cursor - circle->center;
            const double radialLength = std::hypot(radial.x, radial.y);
            if (radialLength > epsilon) {
                addCandidate(candidates,
                    {circle->center.x + radial.x * circle->radius / radialLength,
                     circle->center.y + radial.y * circle->radius / radialLength, circle->center.z},
                    SnapType::Nearest, tolerance, metric);
            }
            if (referencePoint) {
                const Vec3 base = *referencePoint - circle->center;
                const double d2 = dot2D(base, base);
                const double r2 = circle->radius * circle->radius;
                if (d2 >= r2 - epsilon && d2 > epsilon) {
                    const double root = std::sqrt(std::max(0.0, d2 - r2));
                    for (double sign : {-1.0, 1.0}) {
                        const Vec3 tangent{
                            circle->center.x + (r2 * base.x - sign * circle->radius * root * base.y) / d2,
                            circle->center.y + (r2 * base.y + sign * circle->radius * root * base.x) / d2,
                            circle->center.z};
                        addCandidate(candidates, tangent, SnapType::Tangent, tolerance, metric);
                    }
                }
            }
        } else if (vertices.size() <= 4'096 && isClosed(model)) {
            Vec3 center{};
            std::size_t centerSamples{};
            for (std::size_t vertexIndex = 0; vertexIndex < vertices.size(); vertexIndex += vertexStride) {
                center = center + vertices[vertexIndex];
                ++centerSamples;
            }
            center = center * (1.0 / static_cast<double>(centerSamples));
            addCandidate(candidates, center, SnapType::GeometricCenter, tolerance, metric);
        }

        if (const auto insertion = model.insertionPoint())
            addCandidate(candidates, *insertion, SnapType::Insertion, tolerance, metric);
    }

    constexpr std::size_t intersectionSegmentBudget = 256;
    if (segments.size() > intersectionSegmentBudget) {
        std::nth_element(segments.begin(), segments.begin() + intersectionSegmentBudget, segments.end(),
            [&](const Segment& a, const Segment& b) {
                return metric((a.a + a.b) * 0.5) < metric((b.a + b.b) * 0.5);
            });
        segments.resize(intersectionSegmentBudget);
    }
    for (std::size_t first = 0; first < segments.size(); ++first) {
        if (segments[first].analyticCircle) continue;
        for (std::size_t second = first + 1; second < segments.size(); ++second) {
            if (segments[second].analyticCircle) continue;
            if (const auto point = segmentIntersection(segments[first].a, segments[first].b,
                                                       segments[second].a, segments[second].b))
                addCandidate(candidates, *point, SnapType::Intersection, tolerance, metric);
        }
    }
    return candidates;
}

SnapResult choose(std::vector<Candidate> candidates, const Vec3& raw) {
    const bool hasPrecise = std::any_of(candidates.begin(), candidates.end(), [](const Candidate& candidate) {
        return candidate.type != SnapType::Nearest && candidate.type != SnapType::Extension &&
               candidate.type != SnapType::Parallel;
    });
    if (hasPrecise) {
        std::erase_if(candidates, [](const Candidate& candidate) {
            return candidate.type == SnapType::Nearest || candidate.type == SnapType::Extension ||
                   candidate.type == SnapType::Parallel;
        });
    }
    if (candidates.empty()) return {raw, SnapType::None, 0.0};
    const auto best = std::min_element(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
        if (std::abs(a.distance - b.distance) > 1e-7) return a.distance < b.distance;
        return priority(a.type) < priority(b.type);
    });
    return {best->point, best->type, best->distance};
}

std::vector<std::size_t> projectedCandidates(const Vec2& cursor, const Document& document,
                                             const Camera& camera, int viewportWidth,
                                             int viewportHeight, double margin) {
    const auto intersects = [&](const Bounds3& bound) {
        double minimumX = std::numeric_limits<double>::infinity();
        double minimumY = std::numeric_limits<double>::infinity();
        double maximumX = -std::numeric_limits<double>::infinity();
        double maximumY = -std::numeric_limits<double>::infinity();
        for (int corner = 0; corner < 8; ++corner) {
            const Vec3 point{
                (corner & 1) ? bound.maximum.x : bound.minimum.x,
                (corner & 2) ? bound.maximum.y : bound.minimum.y,
                (corner & 4) ? bound.maximum.z : bound.minimum.z};
            const Vec2 projected = camera.project(point, viewportWidth, viewportHeight);
            minimumX = std::min(minimumX, projected.x);
            minimumY = std::min(minimumY, projected.y);
            maximumX = std::max(maximumX, projected.x);
            maximumY = std::max(maximumY, projected.y);
        }
        return cursor.x >= minimumX - margin && cursor.x <= maximumX + margin &&
               cursor.y >= minimumY - margin && cursor.y <= maximumY + margin;
    };
    return document.queryBounds(intersects);
}

std::optional<double> parseNumber(std::wstring_view text) noexcept {
    try {
        std::wstring value(text);
        std::size_t used{};
        const double result = std::stod(value, &used);
        if (used != value.size() || !std::isfinite(result)) return std::nullopt;
        return result;
    } catch (...) { return std::nullopt; }
}
}

SnapResult SnapEngine::snap(const Vec3& cursor, const Document& document,
                            double objectTolerance, double gridSpacing,
                            bool objectSnapEnabled, bool gridSnapEnabled,
                            std::optional<Vec3> referencePoint) {
    if (objectSnapEnabled) {
        const auto nearby = document.query2D({cursor.x - objectTolerance, cursor.y - objectTolerance, cursor.z},
                                             {cursor.x + objectTolerance, cursor.y + objectTolerance, cursor.z});
        auto result = choose(objectCandidates(cursor, document, objectTolerance,
            [&](const Vec3& point) { return distance2D(cursor, point); }, referencePoint, &nearby), cursor);
        if (result.type == SnapType::None && nearby.size() != document.models().size()) {
            const double trackingRange = std::max(1.0, objectTolerance * 64.0);
            const auto tracked = document.query2D({cursor.x - trackingRange, cursor.y - trackingRange, cursor.z},
                                                  {cursor.x + trackingRange, cursor.y + trackingRange, cursor.z});
            result = choose(objectCandidates(cursor, document, objectTolerance,
                [&](const Vec3& point) { return distance2D(cursor, point); }, referencePoint, &tracked), cursor);
        }
        if (result.type != SnapType::None) return result;
    }
    if (gridSnapEnabled && gridSpacing > 0.0) {
        const Vec3 grid{std::round(cursor.x / gridSpacing) * gridSpacing,
                        std::round(cursor.y / gridSpacing) * gridSpacing, cursor.z};
        return {grid, SnapType::Grid, distance2D(cursor, grid)};
    }
    return {cursor, SnapType::None, 0.0};
}

SnapResult SnapEngine::snap3D(const Vec2& screenCursor, const Document& document,
                              const Camera& camera, int viewportWidth, int viewportHeight,
                              double objectTolerancePixels, double gridSpacing, double workPlaneZ,
                              bool objectSnapEnabled, bool gridSnapEnabled,
                              std::optional<Vec3> referencePoint) {
    return snap3D(screenCursor, document, camera, viewportWidth, viewportHeight,
                  objectTolerancePixels, gridSpacing,
                  WorkPlane{{0.0, 0.0, workPlaneZ}, {1.0, 0.0, 0.0},
                            {0.0, 1.0, 0.0}, {0.0, 0.0, 1.0}},
                  objectSnapEnabled, gridSnapEnabled, referencePoint);
}

SnapResult SnapEngine::snap3D(const Vec2& screenCursor, const Document& document,
                              const Camera& camera, int viewportWidth, int viewportHeight,
                              double objectTolerancePixels, double gridSpacing, const WorkPlane& workPlane,
                              bool objectSnapEnabled, bool gridSnapEnabled,
                              std::optional<Vec3> referencePoint) {
    const auto raw = camera.unprojectToPlane(screenCursor, viewportWidth, viewportHeight, workPlane);
    if (!raw) return {workPlane.origin, SnapType::None, 0.0};
    if (objectSnapEnabled) {
        const auto metric = [&](const Vec3& point) {
            const Vec2 projected = camera.project(point, viewportWidth, viewportHeight);
            return std::hypot(screenCursor.x - projected.x, screenCursor.y - projected.y);
        };
        const auto nearby = projectedCandidates(screenCursor, document, camera, viewportWidth,
                                                viewportHeight, objectTolerancePixels);
        auto candidates = objectCandidates(*raw, document, objectTolerancePixels, metric,
                                           referencePoint, &nearby);
        struct ProjectedEdge { Vec3 a; Vec3 b; Vec2 pa; Vec2 pb; };
        std::vector<ProjectedEdge> edges;
        std::size_t drawingEdgeCount{};
        for (const auto& model : document.models()) drawingEdgeCount += model.edges().size();
        constexpr std::size_t apparentIntersectionEdgeLimit = 512;
        if (drawingEdgeCount <= apparentIntersectionEdgeLimit) {
            for (const auto& model : document.models()) {
                for (const auto& edge : model.edges()) {
                    const Vec3& a = model.vertices()[edge.from];
                    const Vec3& b = model.vertices()[edge.to];
                    edges.push_back({a, b, camera.project(a, viewportWidth, viewportHeight),
                                           camera.project(b, viewportWidth, viewportHeight)});
                }
            }
        }
        for (std::size_t first = 0; first < edges.size(); ++first) {
            for (std::size_t second = first + 1; second < edges.size(); ++second) {
                const auto& a = edges[first];
                const auto& b = edges[second];
                const double adx = a.pb.x - a.pa.x, ady = a.pb.y - a.pa.y;
                const double bdx = b.pb.x - b.pa.x, bdy = b.pb.y - b.pa.y;
                const double denominator = adx * bdy - ady * bdx;
                if (std::abs(denominator) <= epsilon) continue;
                const double dx = b.pa.x - a.pa.x, dy = b.pa.y - a.pa.y;
                const double t = (dx * bdy - dy * bdx) / denominator;
                const double u = (dx * ady - dy * adx) / denominator;
                if (t < 0.0 || t > 1.0 || u < 0.0 || u > 1.0) continue;
                const Vec3 worldA = a.a + (a.b - a.a) * t;
                const Vec3 worldB = b.a + (b.b - b.a) * u;
                if (std::hypot(std::hypot(worldA.x - worldB.x, worldA.y - worldB.y), worldA.z - worldB.z) <= 1e-7)
                    continue;
                const Vec2 crossing{a.pa.x + t * adx, a.pa.y + t * ady};
                if (const auto point = camera.unprojectToPlane(crossing, viewportWidth, viewportHeight, workPlane))
                    addCandidate(candidates, *point, SnapType::ApparentIntersection,
                                 objectTolerancePixels, metric);
            }
        }
        auto result = choose(std::move(candidates), *raw);
        if (result.type != SnapType::None) return result;
    }
    if (gridSnapEnabled && gridSpacing > 0.0) {
        const Vec2 local = workPlane.toPlane(*raw);
        const Vec3 grid = workPlane.fromPlane({std::round(local.x / gridSpacing) * gridSpacing,
                                                std::round(local.y / gridSpacing) * gridSpacing});
        const Vec2 projected = camera.project(grid, viewportWidth, viewportHeight);
        return {grid, SnapType::Grid,
                std::hypot(screenCursor.x - projected.x, screenCursor.y - projected.y)};
    }
    return {*raw, SnapType::None, 0.0};
}

std::optional<Vec3> parseDynamicPoint(std::wstring_view text, std::optional<Vec3> origin,
                                      std::optional<Vec3> directionPoint) noexcept {
    const auto comma = text.find(L',');
    if (comma != std::wstring_view::npos) {
        const auto secondComma = text.find(L',', comma + 1);
        const auto x = parseNumber(text.substr(0, comma));
        const auto y = parseNumber(text.substr(comma + 1, secondComma == std::wstring_view::npos
            ? std::wstring_view::npos : secondComma - comma - 1));
        if (!x || !y) return std::nullopt;
        if (secondComma != std::wstring_view::npos) {
            const auto z = parseNumber(text.substr(secondComma + 1));
            if (!z) return std::nullopt;
            return Vec3{*x, *y, *z};
        }
        return Vec3{*x, *y, 0.0};
    }
    const auto angleMark = text.find(L'<');
    if (angleMark != std::wstring_view::npos && origin) {
        const auto distance = parseNumber(text.substr(0, angleMark));
        const auto angle = parseNumber(text.substr(angleMark + 1));
        if (!distance || !angle || *distance < 0.0) return std::nullopt;
        const double radians = *angle * std::numbers::pi / 180.0;
        return Vec3{origin->x + *distance * std::cos(radians),
                    origin->y + *distance * std::sin(radians), origin->z};
    }
    if (origin && directionPoint) {
        const auto distance = parseNumber(text);
        if (!distance || *distance < 0.0) return std::nullopt;
        const Vec3 direction = *directionPoint - *origin;
        const double directionLength = std::hypot(std::hypot(direction.x, direction.y), direction.z);
        if (directionLength <= epsilon) return std::nullopt;
        return *origin + direction * (*distance / directionLength);
    }
    return std::nullopt;
}

const wchar_t* snapTypeLabel(SnapType type) noexcept {
    switch (type) {
    case SnapType::Grid: return L"GRID";
    case SnapType::Endpoint: return L"ENDPOINT";
    case SnapType::Midpoint: return L"MIDPOINT";
    case SnapType::Center: return L"CENTER";
    case SnapType::GeometricCenter: return L"GEOM CENTER";
    case SnapType::Node: return L"NODE";
    case SnapType::Quadrant: return L"QUADRANT";
    case SnapType::Intersection: return L"INTERSECTION";
    case SnapType::ApparentIntersection: return L"APPARENT INT";
    case SnapType::Extension: return L"EXTENSION";
    case SnapType::Insertion: return L"INSERTION";
    case SnapType::Perpendicular: return L"PERPENDICULAR";
    case SnapType::Tangent: return L"TANGENT";
    case SnapType::Nearest: return L"NEAREST";
    case SnapType::Parallel: return L"PARALLEL";
    default: return L"";
    }
}

const wchar_t* toolLabel(DrawTool tool) noexcept {
    switch (tool) {
    case DrawTool::Line: return L"LINE";
    case DrawTool::Polyline: return L"POLYLINE";
    case DrawTool::Rectangle: return L"RECTANGLE";
    case DrawTool::Circle: return L"CIRCLE";
    }
    return L"";
}

Vec3 constrainOrtho(const Vec3& anchor, const Vec3& cursor) noexcept {
    const double dx = std::abs(cursor.x - anchor.x);
    const double dy = std::abs(cursor.y - anchor.y);
    return dx >= dy
        ? Vec3{cursor.x, anchor.y, cursor.z}
        : Vec3{anchor.x, cursor.y, cursor.z};
}

SnapResult applyOrtho(const Vec3& anchor, SnapResult candidate) noexcept {
    if (candidate.type == SnapType::None || candidate.type == SnapType::Grid)
        candidate.point = constrainOrtho(anchor, candidate.point);
    return candidate;
}

Vec3 constrainOrtho3D(const Vec3& anchor, const Vec2& screenCursor, const Camera& camera,
                      int viewportWidth, int viewportHeight) noexcept {
    const Vec2 origin = camera.project(anchor, viewportWidth, viewportHeight);
    const Vec2 cursorDelta{screenCursor.x - origin.x, screenCursor.y - origin.y};
    const std::array<Vec3, 3> axes{{{1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {0.0, 0.0, 1.0}}};

    Vec3 bestPoint = anchor;
    double bestResidual = std::numeric_limits<double>::infinity();
    for (const auto& axis : axes) {
        const Vec2 projected = camera.project(anchor + axis, viewportWidth, viewportHeight);
        const Vec2 axisScreen{projected.x - origin.x, projected.y - origin.y};
        const double lengthSquared = axisScreen.x * axisScreen.x + axisScreen.y * axisScreen.y;
        if (lengthSquared <= epsilon) continue; // Axis points directly into the current view.

        const double worldDistance = (cursorDelta.x * axisScreen.x + cursorDelta.y * axisScreen.y) /
                                     lengthSquared;
        const double residualX = cursorDelta.x - worldDistance * axisScreen.x;
        const double residualY = cursorDelta.y - worldDistance * axisScreen.y;
        const double residual = residualX * residualX + residualY * residualY;
        if (residual < bestResidual) {
            bestResidual = residual;
            bestPoint = anchor + axis * worldDistance;
        }
    }
    return bestPoint;
}

SnapResult applyOrtho3D(const Vec3& anchor, const Vec2& screenCursor, SnapResult candidate,
                        const Camera& camera, int viewportWidth, int viewportHeight) noexcept {
    if (candidate.type == SnapType::None || candidate.type == SnapType::Grid)
        candidate.point = constrainOrtho3D(anchor, screenCursor, camera, viewportWidth, viewportHeight);
    return candidate;
}

namespace {
double pointSegmentDistance(const Vec2& point, const Vec2& from, const Vec2& to) noexcept {
    const double dx = to.x - from.x;
    const double dy = to.y - from.y;
    const double lengthSquared = dx * dx + dy * dy;
    if (lengthSquared <= epsilon) return std::hypot(point.x - from.x, point.y - from.y);
    const double t = std::clamp(((point.x - from.x) * dx + (point.y - from.y) * dy) /
                                lengthSquared, 0.0, 1.0);
    return std::hypot(point.x - (from.x + t * dx), point.y - (from.y + t * dy));
}

template <typename Project>
std::optional<std::size_t> hitTestModel(const Vec2& cursor, const Document& document,
                                        double tolerance, Project project,
                                        const std::vector<std::size_t>* candidates = nullptr) {
    std::optional<std::size_t> bestIndex;
    double bestDistance = tolerance;
    std::vector<std::size_t> allIndices;
    if (!candidates) {
        allIndices.resize(document.models().size());
        for (std::size_t i = 0; i < allIndices.size(); ++i) allIndices[i] = i;
        candidates = &allIndices;
    }
    for (const auto index : *candidates) {
        if (index >= document.models().size()) continue;
        const auto& model = document.models()[index];
        for (const auto& edge : model.edges()) {
            const double distance = pointSegmentDistance(cursor, project(model.vertices()[edge.from]),
                                                         project(model.vertices()[edge.to]));
            if (distance <= bestDistance) {
                bestDistance = distance;
                bestIndex = index;
            }
        }
        if (model.edges().empty() && !model.vertices().empty()) {
            const auto point = project(model.vertices().front());
            const double distance = std::hypot(cursor.x - point.x, cursor.y - point.y);
            if (distance <= bestDistance) { bestDistance = distance; bestIndex = index; }
        }
    }
    return bestIndex;
}

struct SelectionBounds { double left, top, right, bottom; };

SelectionBounds selectionBounds(const Vec2& a, const Vec2& b) noexcept {
    return {std::min(a.x, b.x), std::min(a.y, b.y), std::max(a.x, b.x), std::max(a.y, b.y)};
}

bool pointInBounds(const Vec2& point, const SelectionBounds& bounds) noexcept {
    return point.x >= bounds.left - epsilon && point.x <= bounds.right + epsilon &&
           point.y >= bounds.top - epsilon && point.y <= bounds.bottom + epsilon;
}

double orientation(const Vec2& a, const Vec2& b, const Vec2& c) noexcept {
    return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}

bool segmentsTouch(const Vec2& a, const Vec2& b, const Vec2& c, const Vec2& d) noexcept {
    const double o1 = orientation(a, b, c), o2 = orientation(a, b, d);
    const double o3 = orientation(c, d, a), o4 = orientation(c, d, b);
    const auto between = [](double value, double first, double second) {
        return value >= std::min(first, second) - epsilon && value <= std::max(first, second) + epsilon;
    };
    const auto on = [&](const Vec2& p, const Vec2& q, const Vec2& r, double o) {
        return std::abs(o) <= epsilon && between(r.x, p.x, q.x) && between(r.y, p.y, q.y);
    };
    if (on(a, b, c, o1) || on(a, b, d, o2) || on(c, d, a, o3) || on(c, d, b, o4)) return true;
    return ((o1 > 0.0) != (o2 > 0.0)) && ((o3 > 0.0) != (o4 > 0.0));
}

bool segmentTouchesBounds(const Vec2& a, const Vec2& b, const SelectionBounds& bounds) noexcept {
    if (pointInBounds(a, bounds) || pointInBounds(b, bounds)) return true;
    const Vec2 topLeft{bounds.left, bounds.top}, topRight{bounds.right, bounds.top};
    const Vec2 bottomRight{bounds.right, bounds.bottom}, bottomLeft{bounds.left, bounds.bottom};
    return segmentsTouch(a, b, topLeft, topRight) || segmentsTouch(a, b, topRight, bottomRight) ||
           segmentsTouch(a, b, bottomRight, bottomLeft) || segmentsTouch(a, b, bottomLeft, topLeft);
}

template <typename Project>
std::vector<std::size_t> selectModelsInRect(const Vec2& firstCorner, const Vec2& secondCorner,
                                            const Document& document, bool crossing, Project project,
                                            const std::vector<std::size_t>* candidates = nullptr) {
    const SelectionBounds bounds = selectionBounds(firstCorner, secondCorner);
    std::vector<std::size_t> selected;
    std::vector<std::size_t> allIndices;
    if (!candidates) {
        allIndices.resize(document.models().size());
        for (std::size_t i = 0; i < allIndices.size(); ++i) allIndices[i] = i;
        candidates = &allIndices;
    }
    for (const auto index : *candidates) {
        if (index >= document.models().size()) continue;
        const auto& model = document.models()[index];
        const bool allInside = !model.vertices().empty() &&
            std::all_of(model.vertices().begin(), model.vertices().end(), [&](const Vec3& vertex) {
                return pointInBounds(project(vertex), bounds);
            });
        bool touches = allInside;
        if (crossing && !touches) {
            touches = std::any_of(model.edges().begin(), model.edges().end(), [&](const Edge& edge) {
                return segmentTouchesBounds(project(model.vertices()[edge.from]),
                                            project(model.vertices()[edge.to]), bounds);
            });
            if (!touches && model.edges().empty()) {
                touches = std::any_of(model.vertices().begin(), model.vertices().end(), [&](const Vec3& vertex) {
                    return pointInBounds(project(vertex), bounds);
                });
            }
        }
        if (crossing ? touches : allInside) selected.push_back(index);
    }
    return selected;
}
}

std::optional<std::size_t> hitTestModel2D(const Vec3& cursor, const Document& document,
                                         double tolerance) {
    const auto nearby = document.query2D({cursor.x - tolerance, cursor.y - tolerance, cursor.z},
                                         {cursor.x + tolerance, cursor.y + tolerance, cursor.z});
    return hitTestModel({cursor.x, cursor.y}, document, tolerance,
                        [](const Vec3& point) { return Vec2{point.x, point.y}; }, &nearby);
}

std::optional<std::size_t> hitTestModel3D(const Vec2& cursor, const Document& document,
                                         const Camera& camera, int viewportWidth, int viewportHeight,
                                         double tolerancePixels) {
    return hitTestModel(cursor, document, tolerancePixels, [&](const Vec3& point) {
        return camera.project(point, viewportWidth, viewportHeight);
    });
}

std::vector<std::size_t> selectModelsInRect2D(const Vec3& firstCorner, const Vec3& secondCorner,
                                              const Document& document, bool crossing) {
    const auto nearby = document.query2D(firstCorner, secondCorner);
    return selectModelsInRect({firstCorner.x, firstCorner.y}, {secondCorner.x, secondCorner.y},
                              document, crossing,
                              [](const Vec3& point) { return Vec2{point.x, point.y}; }, &nearby);
}

std::vector<std::size_t> selectModelsInRect3D(const Vec2& firstCorner, const Vec2& secondCorner,
                                              const Document& document, const Camera& camera,
                                              int viewportWidth, int viewportHeight, bool crossing) {
    return selectModelsInRect(firstCorner, secondCorner, document, crossing, [&](const Vec3& point) {
        return camera.project(point, viewportWidth, viewportHeight);
    });
}

bool shouldEvaluateSnapping(bool selectingEntities, bool zoomPhase, bool cameraNavigating) noexcept {
    return !selectingEntities && !zoomPhase && !cameraNavigating;
}

} // namespace mm
