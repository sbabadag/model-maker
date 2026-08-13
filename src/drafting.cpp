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
        if (!document.modelIsEditable(modelIndex)) continue;
        totalVertices += document.models()[modelIndex].vertices().size();
        totalEdges += document.models()[modelIndex].edges().size();
    }
    constexpr std::size_t candidateBudget = 20'000;
    const std::size_t vertexStride = std::max<std::size_t>(1, (totalVertices + candidateBudget - 1) /
                                                               candidateBudget);
    const std::size_t edgeStride = std::max<std::size_t>(1, (totalEdges + candidateBudget - 1) /
                                                             candidateBudget);

    for (const auto modelIndex : *candidateIndices) {
        if (!document.modelIsEditable(modelIndex)) continue;
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

SnapResult choose(std::vector<Candidate> candidates, const Vec3& raw,
                  const SnapTypeMask* enabledTypes = nullptr) {
    if (enabledTypes) {
        std::erase_if(candidates, [&](const Candidate& candidate) {
            const auto index = static_cast<std::size_t>(candidate.type);
            return index >= enabledTypes->size() || !(*enabledTypes)[index];
        });
    }
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
                            std::optional<Vec3> referencePoint,
                            const SnapTypeMask* enabledTypes) {
    if (objectSnapEnabled) {
        const auto nearby = document.query2D({cursor.x - objectTolerance, cursor.y - objectTolerance, cursor.z},
                                             {cursor.x + objectTolerance, cursor.y + objectTolerance, cursor.z});
        auto result = choose(objectCandidates(cursor, document, objectTolerance,
            [&](const Vec3& point) { return distance2D(cursor, point); }, referencePoint, &nearby),
            cursor, enabledTypes);
        if (result.type == SnapType::None && nearby.size() != document.models().size()) {
            const double trackingRange = std::max(1.0, objectTolerance * 64.0);
            const auto tracked = document.query2D({cursor.x - trackingRange, cursor.y - trackingRange, cursor.z},
                                                  {cursor.x + trackingRange, cursor.y + trackingRange, cursor.z});
            result = choose(objectCandidates(cursor, document, objectTolerance,
                [&](const Vec3& point) { return distance2D(cursor, point); }, referencePoint, &tracked),
                cursor, enabledTypes);
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
                              std::optional<Vec3> referencePoint,
                              const SnapTypeMask* enabledTypes) {
    return snap3D(screenCursor, document, camera, viewportWidth, viewportHeight,
                  objectTolerancePixels, gridSpacing,
                  WorkPlane{{0.0, 0.0, workPlaneZ}, {1.0, 0.0, 0.0},
                            {0.0, 1.0, 0.0}, {0.0, 0.0, 1.0}},
                  objectSnapEnabled, gridSnapEnabled, referencePoint, enabledTypes);
}

SnapResult SnapEngine::snap3D(const Vec2& screenCursor, const Document& document,
                              const Camera& camera, int viewportWidth, int viewportHeight,
                              double objectTolerancePixels, double gridSpacing, const WorkPlane& workPlane,
                              bool objectSnapEnabled, bool gridSnapEnabled,
                              std::optional<Vec3> referencePoint,
                              const SnapTypeMask* enabledTypes) {
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
        constexpr std::size_t apparentIntersectionEdgeLimit = 512;
        // Only iterate nearby models (spatial-index filtered), not the entire document
        for (const auto index : nearby) {
            if (index >= document.models().size()) continue;
            const auto& model = document.models()[index];
            for (const auto& edge : model.edges()) {
                if (edges.size() >= apparentIntersectionEdgeLimit) break;
                const Vec3& a = model.vertices()[edge.from];
                const Vec3& b = model.vertices()[edge.to];
                edges.push_back({a, b, camera.project(a, viewportWidth, viewportHeight),
                                       camera.project(b, viewportWidth, viewportHeight)});
            }
            if (edges.size() >= apparentIntersectionEdgeLimit) break;
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
        auto result = choose(std::move(candidates), *raw, enabledTypes);
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
    const bool relative = !text.empty() && text.front() == L'@';
    if (relative) {
        if (!origin) return std::nullopt;
        text.remove_prefix(1);
        if (text.empty()) return std::nullopt;
    }
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
            const Vec3 point{*x, *y, *z};
            return relative ? std::optional<Vec3>{*origin + point} : std::optional<Vec3>{point};
        }
        const Vec3 point{*x, *y, 0.0};
        return relative ? std::optional<Vec3>{*origin + point} : std::optional<Vec3>{point};
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

EntityProperties resolveEntityStyle(
    const EntityStyleSelection& selection,
    const std::unordered_map<std::string, EntityProperties>& layers) {
    EntityProperties properties;
    properties.layer = selection.layer.empty() ? "0" : selection.layer;
    const auto layer = layers.find(properties.layer);
    if (layer != layers.end()) {
        properties.effectiveColor = layer->second.effectiveColor;
        properties.effectiveLineType = layer->second.effectiveLineType;
        properties.effectiveLineWeight = layer->second.effectiveLineWeight;
    }
    if (selection.trueColor) {
        properties.trueColor = selection.trueColor;
        properties.effectiveColor = *selection.trueColor;
    }
    properties.lineType = selection.lineType.empty() ? "BYLAYER" : selection.lineType;
    if (properties.lineType != "BYLAYER") properties.effectiveLineType = properties.lineType;
    return properties;
}

const wchar_t* toolLabel(DrawTool tool) noexcept {
    switch (tool) {
    case DrawTool::Line: return L"LINE";
    case DrawTool::Polyline: return L"POLYLINE";
    case DrawTool::Rectangle: return L"RECTANGLE";
    case DrawTool::Circle: return L"CIRCLE";
    case DrawTool::Face3D: return L"3DFACE";
    }
    return L"";
}

Vec3 constrainOrtho(const Vec3& anchor, const Vec3& cursor) noexcept {
    OrthoAxis axis = OrthoAxis::None;
    return constrainOrtho(anchor, cursor, axis);
}

Vec3 constrainOrtho(const Vec3& anchor, const Vec3& cursor, OrthoAxis& chosenAxis) noexcept {
    const double dx = std::abs(cursor.x - anchor.x);
    const double dy = std::abs(cursor.y - anchor.y);
    chosenAxis = (dx >= dy) ? OrthoAxis::X : OrthoAxis::Y;
    return dx >= dy
        ? Vec3{cursor.x, anchor.y, cursor.z}
        : Vec3{anchor.x, cursor.y, cursor.z};
}

SnapResult applyOrtho(const Vec3& anchor, SnapResult candidate, bool preserveObjectSnaps) noexcept {
    if (!preserveObjectSnaps || candidate.type == SnapType::None || candidate.type == SnapType::Grid) {
        const Vec3 original = candidate.point;
        OrthoAxis axis = OrthoAxis::None;
        candidate.point = constrainOrtho(anchor, candidate.point, axis);
        candidate.orthoAxis = axis;
        if (!preserveObjectSnaps && candidate.point != original) candidate.type = SnapType::None;
    }
    return candidate;
}

Vec3 constrainOrtho3D(const Vec3& anchor, const Vec2& screenCursor, const Camera& camera,
                      int viewportWidth, int viewportHeight) noexcept {
    OrthoAxis axis = OrthoAxis::None;
    return constrainOrtho3D(anchor, screenCursor, camera, viewportWidth, viewportHeight, axis);
}

Vec3 constrainOrtho3D(const Vec3& anchor, const Vec2& screenCursor, const Camera& camera,
                      int viewportWidth, int viewportHeight, OrthoAxis& chosenAxis) noexcept {
    const Vec2 origin = camera.project(anchor, viewportWidth, viewportHeight);
    const Vec2 cursorDelta{screenCursor.x - origin.x, screenCursor.y - origin.y};
    const std::array<Vec3, 3> axes{{{1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {0.0, 0.0, 1.0}}};

    Vec3 bestPoint = anchor;
    double bestResidual = std::numeric_limits<double>::infinity();
    std::size_t bestIndex = 0;
    for (std::size_t index = 0; index < axes.size(); ++index) {
        const auto& axis = axes[index];
        const Vec2 projected = camera.project(anchor + axis, viewportWidth, viewportHeight);
        const Vec2 axisScreen{projected.x - origin.x, projected.y - origin.y};
        const double lengthSquared = axisScreen.x * axisScreen.x + axisScreen.y * axisScreen.y;
        if (lengthSquared <= epsilon) continue;

        const double worldDistance = (cursorDelta.x * axisScreen.x + cursorDelta.y * axisScreen.y) /
                                     lengthSquared;
        const double residualX = cursorDelta.x - worldDistance * axisScreen.x;
        const double residualY = cursorDelta.y - worldDistance * axisScreen.y;
        const double residual = residualX * residualX + residualY * residualY;
        if (residual < bestResidual) {
            bestResidual = residual;
            bestPoint = anchor + axis * worldDistance;
            bestIndex = index;
        }
    }
    chosenAxis = static_cast<OrthoAxis>(bestIndex + 1);
    return bestPoint;
}

Vec3 constrainOrtho3D(const Vec3& anchor, const Vec2& screenCursor, const Camera& camera,
                      int viewportWidth, int viewportHeight, const WorkPlane& workPlane,
                      bool includePlaneNormal) noexcept {
    OrthoAxis axis = OrthoAxis::None;
    return constrainOrtho3D(anchor, screenCursor, camera, viewportWidth, viewportHeight,
                            workPlane, includePlaneNormal, axis);
}

Vec3 constrainOrtho3D(const Vec3& anchor, const Vec2& screenCursor, const Camera& camera,
                      int viewportWidth, int viewportHeight, const WorkPlane& workPlane,
                      bool includePlaneNormal, OrthoAxis& chosenAxis) noexcept {
    const Vec2 origin = camera.project(anchor, viewportWidth, viewportHeight);
    const Vec2 cursorDelta{screenCursor.x - origin.x, screenCursor.y - origin.y};
    const std::array<Vec3, 3> axes{{workPlane.u, workPlane.v, workPlane.normal}};

    Vec3 bestPoint = anchor;
    double bestResidual = std::numeric_limits<double>::infinity();
    std::size_t bestIndex = 0;
    const std::size_t axisCount = includePlaneNormal ? axes.size() : axes.size() - 1;
    for (std::size_t index = 0; index < axisCount; ++index) {
        const auto& axis = axes[index];
        const Vec2 projected = camera.project(anchor + axis, viewportWidth, viewportHeight);
        const Vec2 axisScreen{projected.x - origin.x, projected.y - origin.y};
        const double lengthSquared = axisScreen.x * axisScreen.x + axisScreen.y * axisScreen.y;
        if (lengthSquared <= epsilon) continue;

        const double planeDistance = (cursorDelta.x * axisScreen.x + cursorDelta.y * axisScreen.y) /
                                     lengthSquared;
        const double residualX = cursorDelta.x - planeDistance * axisScreen.x;
        const double residualY = cursorDelta.y - planeDistance * axisScreen.y;
        const double residual = residualX * residualX + residualY * residualY;
        if (residual < bestResidual) {
            bestResidual = residual;
            bestPoint = anchor + axis * planeDistance;
            bestIndex = index;
        }
    }
    chosenAxis = static_cast<OrthoAxis>(bestIndex + 1);
    return bestPoint;
}

SnapResult applyOrtho3D(const Vec3& anchor, const Vec2& screenCursor, SnapResult candidate,
                        const Camera& camera, int viewportWidth, int viewportHeight,
                        bool preserveObjectSnaps) noexcept {
    if (!preserveObjectSnaps || candidate.type == SnapType::None || candidate.type == SnapType::Grid) {
        const Vec3 original = candidate.point;
        OrthoAxis axis = OrthoAxis::None;
        candidate.point = constrainOrtho3D(anchor, screenCursor, camera, viewportWidth,
                                           viewportHeight, axis);
        candidate.orthoAxis = axis;
        if (!preserveObjectSnaps && candidate.point != original) candidate.type = SnapType::None;
    }
    return candidate;
}

SnapResult applyOrtho3D(const Vec3& anchor, const Vec2& screenCursor, SnapResult candidate,
                        const Camera& camera, int viewportWidth, int viewportHeight,
                        const WorkPlane& workPlane, bool includePlaneNormal,
                        bool preserveObjectSnaps) noexcept {
    if (!preserveObjectSnaps || candidate.type == SnapType::None || candidate.type == SnapType::Grid) {
        const Vec3 original = candidate.point;
        OrthoAxis axis = OrthoAxis::None;
        candidate.point = constrainOrtho3D(anchor, screenCursor, camera, viewportWidth,
                                           viewportHeight, workPlane, includePlaneNormal, axis);
        candidate.orthoAxis = axis;
        if (!preserveObjectSnaps && candidate.point != original) candidate.type = SnapType::None;
    }
    return candidate;
}

SnapResult applyPolarTracking(const Vec3& anchor, SnapResult candidate, const WorkPlane& plane,
                              double incrementDegrees, double apertureDegrees,
                              bool preserveObjectSnaps) noexcept {
    if (preserveObjectSnaps && candidate.type != SnapType::None && candidate.type != SnapType::Grid)
        return candidate;
    if (!std::isfinite(incrementDegrees) || incrementDegrees <= 0.0 ||
        !std::isfinite(apertureDegrees) || apertureDegrees < 0.0)
        return candidate;

    const Vec3 delta = candidate.point - anchor;
    const double alongU = delta.x * plane.u.x + delta.y * plane.u.y + delta.z * plane.u.z;
    const double alongV = delta.x * plane.v.x + delta.y * plane.v.y + delta.z * plane.v.z;
    const double radius = std::hypot(alongU, alongV);
    if (radius <= epsilon) return candidate;

    const double increment = incrementDegrees * std::numbers::pi / 180.0;
    const double angle = std::atan2(alongV, alongU);
    const double lockedAngle = std::round(angle / increment) * increment;
    const double difference = std::abs(std::remainder(angle - lockedAngle, 2.0 * std::numbers::pi));
    if (difference > apertureDegrees * std::numbers::pi / 180.0) return candidate;

    const Vec3 original = candidate.point;
    candidate.point = anchor + plane.u * (radius * std::cos(lockedAngle)) +
                      plane.v * (radius * std::sin(lockedAngle));
    if (!preserveObjectSnaps && candidate.point != original) candidate.type = SnapType::None;
    return candidate;
}

SnapResult applyPolarTracking(const Vec3& anchor, SnapResult candidate,
                              double incrementDegrees, double apertureDegrees,
                              bool preserveObjectSnaps) noexcept {
    return applyPolarTracking(anchor, candidate,
                              WorkPlane{{}, {1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {0.0, 0.0, 1.0}},
                              incrementDegrees, apertureDegrees, preserveObjectSnaps);
}

TemporaryTrackingResult resolveTemporaryPointTracking(
    SnapResult candidate, const std::vector<Vec3>& acquiredPoints, const WorkPlane& plane,
    double tolerance) noexcept {
    TemporaryTrackingResult tracking;
    tracking.result = candidate;
    const bool explicitObjectSnap = candidate.type != SnapType::None && candidate.type != SnapType::Grid;
    double bestDistance = std::max(0.0, tolerance);
    const auto normalDistance = [&](const Vec3& point) {
        const Vec3 relative = point - plane.origin;
        return relative.x * plane.normal.x + relative.y * plane.normal.y + relative.z * plane.normal.z;
    };
    const Vec2 cursorLocal = plane.toPlane(candidate.point);
    const double cursorNormal = normalDistance(candidate.point);
    for (std::size_t first = 0; first < acquiredPoints.size(); ++first) {
        for (std::size_t second = first + 1; second < acquiredPoints.size(); ++second) {
            tracking.guides.push_back({acquiredPoints[first], acquiredPoints[second]});
            const Vec3 midpoint = (acquiredPoints[first] + acquiredPoints[second]) * 0.5;
            const Vec2 midpointLocal = plane.toPlane(midpoint);
            if (!explicitObjectSnap) {
                const double distance = std::hypot(cursorLocal.x - midpointLocal.x,
                                                   cursorLocal.y - midpointLocal.y);
                if (distance <= bestDistance) {
                    bestDistance = distance;
                    tracking.result = {midpoint, SnapType::Midpoint, distance};
                    tracking.locked = true;
                }
            }

            const Vec2 firstLocal = plane.toPlane(acquiredPoints[first]);
            const Vec2 secondLocal = plane.toPlane(acquiredPoints[second]);
            const double firstNormal = normalDistance(acquiredPoints[first]);
            const double secondNormal = normalDistance(acquiredPoints[second]);
            if (std::abs(firstNormal - secondNormal) > std::max(tolerance, epsilon)) continue;
            const double normal = (firstNormal + secondNormal) * 0.5;
            const std::array<Vec3, 2> corners{
                plane.fromPlane({firstLocal.x, secondLocal.y}) + plane.normal * normal,
                plane.fromPlane({secondLocal.x, firstLocal.y}) + plane.normal * normal};
            for (const auto& corner : corners) {
                if (std::find(acquiredPoints.begin(), acquiredPoints.end(), corner) != acquiredPoints.end())
                    continue;
                if (std::find(tracking.derivedPoints.begin(), tracking.derivedPoints.end(), corner) ==
                    tracking.derivedPoints.end())
                    tracking.derivedPoints.push_back(corner);
                if (explicitObjectSnap) continue;
                const Vec2 cornerLocal = plane.toPlane(corner);
                const double distance = std::hypot(
                    std::hypot(cursorLocal.x - cornerLocal.x, cursorLocal.y - cornerLocal.y),
                    cursorNormal - normal);
                if (distance <= bestDistance) {
                    bestDistance = distance;
                    tracking.result = {corner, SnapType::Intersection, distance};
                    tracking.locked = true;
                }
            }
        }
    }
    if (!explicitObjectSnap && !tracking.locked) {
        std::optional<TrackingGuide> activeAxisGuide;
        for (const auto& acquired : acquiredPoints) {
            const Vec2 acquiredLocal = plane.toPlane(acquired);
            const double acquiredNormal = normalDistance(acquired);
            const std::array<Vec3, 2> projected{
                plane.fromPlane({cursorLocal.x, acquiredLocal.y}) + plane.normal * acquiredNormal,
                plane.fromPlane({acquiredLocal.x, cursorLocal.y}) + plane.normal * acquiredNormal};
            for (const auto& point : projected) {
                const Vec2 local = plane.toPlane(point);
                const double distance = std::hypot(
                    std::hypot(cursorLocal.x - local.x, cursorLocal.y - local.y),
                    cursorNormal - acquiredNormal);
                if (distance < bestDistance - epsilon) {
                    bestDistance = distance;
                    tracking.result = {point, SnapType::Extension, distance};
                    tracking.locked = true;
                    activeAxisGuide = TrackingGuide{acquired, point};
                }
            }
        }
        if (activeAxisGuide) tracking.guides.push_back(*activeAxisGuide);
    }
    return tracking;
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
        if (!document.modelIsEditable(index)) continue;
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
        if (!document.modelIsEditable(index)) continue;
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

template <typename Project>
std::optional<Vec3> crossingSelectionPickPoint(const WireframeModel& model,
                                               const Vec2& firstCorner, const Vec2& secondCorner,
                                               Project project) {
    if (model.vertices().size() != 2 || model.edges().size() != 1 ||
        model.edges().front() != Edge{0, 1}) return std::nullopt;
    const SelectionBounds bounds = selectionBounds(firstCorner, secondCorner);
    const Vec3& start = model.vertices()[0];
    const Vec3& end = model.vertices()[1];
    const Vec2 a = project(start);
    const Vec2 b = project(end);
    const double dx = b.x - a.x;
    const double dy = b.y - a.y;
    double lower = 0.0;
    double upper = 1.0;
    const auto clip = [&](double p, double q) {
        if (std::abs(p) <= epsilon) return q >= -epsilon;
        const double ratio = q / p;
        if (p < 0.0) lower = std::max(lower, ratio);
        else upper = std::min(upper, ratio);
        return lower <= upper + epsilon;
    };
    if (!clip(-dx, a.x - bounds.left) || !clip(dx, bounds.right - a.x) ||
        !clip(-dy, a.y - bounds.top) || !clip(dy, bounds.bottom - a.y)) return std::nullopt;
    const double parameter = std::clamp((lower + upper) * 0.5, 0.0, 1.0);
    return start + (end - start) * parameter;
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
    const auto nearby = projectedCandidates(cursor, document, camera, viewportWidth,
                                            viewportHeight, tolerancePixels);
    return hitTestModel(cursor, document, tolerancePixels, [&](const Vec3& point) {
        return camera.project(point, viewportWidth, viewportHeight);
    }, &nearby);
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
    // Query spatial index: project selection corners to world space for bounds
    const auto proj = [&](double x, double y) {
        return camera.unproject2D({x, y}, viewportWidth, viewportHeight);
    };
    const Vec3 c1 = proj(firstCorner.x, firstCorner.y);
    const Vec3 c2 = proj(secondCorner.x, secondCorner.y);
    // Build a deep-enough world bounds from the projected corners
    const double zPad = 10000.0;
    const Vec3 lo{std::min(c1.x, c2.x), std::min(c1.y, c2.y), std::min(c1.z, c2.z) - zPad};
    const Vec3 hi{std::max(c1.x, c2.x), std::max(c1.y, c2.y), std::max(c1.z, c2.z) + zPad};
    const auto nearby = document.queryBounds([&](const Bounds3& bounds) {
        return bounds.maximum.x >= lo.x && bounds.minimum.x <= hi.x &&
               bounds.maximum.y >= lo.y && bounds.minimum.y <= hi.y &&
               bounds.maximum.z >= lo.z && bounds.minimum.z <= hi.z;
    });
    return selectModelsInRect(firstCorner, secondCorner, document, crossing, [&](const Vec3& point) {
        return camera.project(point, viewportWidth, viewportHeight);
    }, &nearby);
}

std::optional<Vec3> crossingSelectionPickPoint2D(const WireframeModel& model,
                                                 const Vec3& firstCorner, const Vec3& secondCorner) {
    return crossingSelectionPickPoint(model, {firstCorner.x, firstCorner.y},
                                      {secondCorner.x, secondCorner.y},
                                      [](const Vec3& point) { return Vec2{point.x, point.y}; });
}

std::optional<Vec3> crossingSelectionPickPoint3D(const WireframeModel& model,
                                                 const Vec2& firstCorner, const Vec2& secondCorner,
                                                 const Camera& camera, int viewportWidth,
                                                 int viewportHeight) {
    return crossingSelectionPickPoint(model, firstCorner, secondCorner, [&](const Vec3& point) {
        return camera.project(point, viewportWidth, viewportHeight);
    });
}

std::optional<WireframeModel> offsetModel2D(const WireframeModel& source, double distance,
                                            const Vec3& sidePoint) {
    if (!std::isfinite(distance) || distance <= epsilon) return std::nullopt;
    if (source.analyticCenter() && source.analyticRadius()) {
        const Vec3 center = *source.analyticCenter();
        const double radius = *source.analyticRadius();
        const bool outside = distance2D(sidePoint, center) >= radius;
        const double offsetRadius = outside ? radius + distance : radius - distance;
        if (offsetRadius <= epsilon) return std::nullopt;
        WireframeModel result = WireframeModel::circle(center, offsetRadius,
            std::max<std::size_t>(3, source.vertices().size()));
        result.setProperties(source.properties());
        return result;
    }
    if (source.vertices().size() != 2 ||
        source.edges().size() != 1 || source.edges().front() != Edge{0, 1}) return std::nullopt;
    const Vec3& from = source.vertices()[0];
    const Vec3& to = source.vertices()[1];
    const double dx = to.x - from.x;
    const double dy = to.y - from.y;
    const double length = std::hypot(dx, dy);
    if (length <= epsilon) return std::nullopt;
    const double side = dx * (sidePoint.y - from.y) - dy * (sidePoint.x - from.x);
    if (std::abs(side) <= epsilon) return std::nullopt;
    const double sign = side > 0.0 ? 1.0 : -1.0;
    const Vec3 displacement{-dy * sign * distance / length,
                             dx * sign * distance / length, 0.0};
    WireframeModel result = WireframeModel::line(from + displacement, to + displacement);
    result.setProperties(source.properties());
    return result;
}

std::optional<WireframeModel> mirrorModel2D(const WireframeModel& source, const Vec3& axisStart,
                                            const Vec3& axisEnd) {
    const double axisX = axisEnd.x - axisStart.x;
    const double axisY = axisEnd.y - axisStart.y;
    const double lengthSquared = axisX * axisX + axisY * axisY;
    if (lengthSquared <= epsilon * epsilon) return std::nullopt;
    const auto reflect = [&](const Vec3& point) {
        const double projection = ((point.x - axisStart.x) * axisX +
                                   (point.y - axisStart.y) * axisY) / lengthSquared;
        const double closestX = axisStart.x + projection * axisX;
        const double closestY = axisStart.y + projection * axisY;
        return Vec3{2.0 * closestX - point.x, 2.0 * closestY - point.y, point.z};
    };

    WireframeModel result;
    if (source.isPointEntity() && !source.vertices().empty()) {
        result = WireframeModel::point(reflect(source.vertices().front()));
    } else if (source.isFace3D() && source.vertices().size() == 4) {
        result = WireframeModel::face3D({reflect(source.vertices()[0]), reflect(source.vertices()[1]),
                                         reflect(source.vertices()[2]), reflect(source.vertices()[3])});
    } else if (source.analyticCenter() && source.analyticRadius()) {
        result = WireframeModel::circle(reflect(*source.analyticCenter()), *source.analyticRadius(),
                                        std::max<std::size_t>(3, source.vertices().size()));
    } else {
        std::vector<Vec3> vertices;
        vertices.reserve(source.vertices().size());
        for (const auto& vertex : source.vertices()) vertices.push_back(reflect(vertex));
        result = WireframeModel(std::move(vertices), source.edges(), source.faces());
    }
    result.setProperties(source.properties());
    return result;
}

std::vector<WireframeModel> linearArray2D(const WireframeModel& source, std::size_t itemCount,
                                          const Vec3& spacing) {
    std::vector<WireframeModel> copies;
    if (itemCount < 2 || (std::abs(spacing.x) <= epsilon && std::abs(spacing.y) <= epsilon &&
                          std::abs(spacing.z) <= epsilon)) return copies;
    copies.reserve(itemCount - 1);
    for (std::size_t index = 1; index < itemCount; ++index) {
        copies.push_back(source);
        copies.back().translate(spacing * static_cast<double>(index));
    }
    return copies;
}

std::vector<WireframeModel> polarArray2D(const WireframeModel& source, std::size_t itemCount,
                                         const Vec3& center) {
    std::vector<WireframeModel> copies;
    if (itemCount < 2) return copies;
    copies.reserve(itemCount - 1);
    const double step = 2.0 * std::numbers::pi / static_cast<double>(itemCount);
    for (std::size_t index = 1; index < itemCount; ++index) {
        copies.push_back(source);
        copies.back().rotateAroundZ(center, step * static_cast<double>(index));
    }
    return copies;
}

std::optional<std::vector<WireframeModel>> trimLine2D(
    const WireframeModel& source, const std::vector<WireframeModel>& boundaries,
    const Vec3& pickPoint) {
    if (source.vertices().size() != 2 || source.edges().size() != 1 ||
        source.edges().front() != Edge{0, 1}) return std::nullopt;
    const Vec3& start = source.vertices()[0];
    const Vec3& end = source.vertices()[1];
    const Vec3 direction = end - start;
    const double lengthSquared = dot2D(direction, direction);
    if (lengthSquared <= epsilon * epsilon) return std::nullopt;

    std::vector<double> cuts;
    for (const auto& boundary : boundaries) {
        for (const auto& edge : boundary.edges()) {
            if (edge.from >= boundary.vertices().size() || edge.to >= boundary.vertices().size()) continue;
            if (const auto intersection = segmentIntersection(start, end,
                    boundary.vertices()[edge.from], boundary.vertices()[edge.to])) {
                const double parameter = dot2D(*intersection - start, direction) / lengthSquared;
                if (parameter > epsilon && parameter < 1.0 - epsilon)
                    cuts.push_back(parameter);
            }
        }
    }
    if (cuts.empty()) return std::nullopt;
    std::sort(cuts.begin(), cuts.end());
    cuts.erase(std::unique(cuts.begin(), cuts.end(), [](double a, double b) {
        return std::abs(a - b) <= epsilon;
    }), cuts.end());
    const double picked = dot2D(pickPoint - start, direction) / lengthSquared;
    const auto makeSegment = [&](double from, double to) {
        auto segment = WireframeModel::line(start + direction * from, start + direction * to);
        segment.setProperties(source.properties());
        return segment;
    };

    std::vector<WireframeModel> result;
    const auto upper = std::upper_bound(cuts.begin(), cuts.end(), picked);
    if (upper == cuts.begin()) {
        result.push_back(makeSegment(cuts.front(), 1.0));
    } else if (upper == cuts.end()) {
        result.push_back(makeSegment(0.0, cuts.back()));
    } else {
        result.push_back(makeSegment(0.0, *std::prev(upper)));
        result.push_back(makeSegment(*upper, 1.0));
    }
    return result;
}

std::optional<WireframeModel> extendLine2D(
    const WireframeModel& source, const std::vector<WireframeModel>& boundaries,
    const Vec3& pickPoint) {
    if (source.vertices().size() != 2 || source.edges().size() != 1 ||
        source.edges().front() != Edge{0, 1}) return std::nullopt;
    const Vec3& start = source.vertices()[0];
    const Vec3& end = source.vertices()[1];
    const Vec3 direction = end - start;
    const double lengthSquared = dot2D(direction, direction);
    if (lengthSquared <= epsilon * epsilon) return std::nullopt;
    const double picked = dot2D(pickPoint - start, direction) / lengthSquared;
    const bool extendStart = picked <= 0.5;
    std::optional<double> best;

    for (const auto& boundary : boundaries) {
        for (const auto& edge : boundary.edges()) {
            if (edge.from >= boundary.vertices().size() || edge.to >= boundary.vertices().size()) continue;
            const Vec3& first = boundary.vertices()[edge.from];
            const Vec3& second = boundary.vertices()[edge.to];
            const double sx = second.x - first.x;
            const double sy = second.y - first.y;
            const double denominator = direction.x * sy - direction.y * sx;
            if (std::abs(denominator) <= epsilon) continue;
            const double dx = first.x - start.x;
            const double dy = first.y - start.y;
            const double targetParameter = (dx * sy - dy * sx) / denominator;
            const double boundaryParameter = (dx * direction.y - dy * direction.x) / denominator;
            if (boundaryParameter < -epsilon || boundaryParameter > 1.0 + epsilon) continue;
            const double targetZ = start.z + targetParameter * direction.z;
            const double boundaryZ = first.z + boundaryParameter * (second.z - first.z);
            if (std::abs(targetZ - boundaryZ) > 1e-7) continue;
            if (extendStart && targetParameter < -epsilon &&
                (!best || targetParameter > *best)) best = targetParameter;
            if (!extendStart && targetParameter > 1.0 + epsilon &&
                (!best || targetParameter < *best)) best = targetParameter;
        }
    }
    if (!best) return std::nullopt;
    auto result = extendStart
        ? WireframeModel::line(start + direction * *best, end)
        : WireframeModel::line(start, start + direction * *best);
    result.setProperties(source.properties());
    return result;
}

namespace {
Vec3 toPlaneCoordinates(const Vec3& point, const WorkPlane& plane) noexcept {
    const Vec3 delta = point - plane.origin;
    const Vec2 planar = plane.toPlane(point);
    return {planar.x, planar.y,
            delta.x * plane.normal.x + delta.y * plane.normal.y + delta.z * plane.normal.z};
}

Vec3 fromPlaneCoordinates(const Vec3& point, const WorkPlane& plane) noexcept {
    return plane.origin + plane.u * point.x + plane.v * point.y + plane.normal * point.z;
}

WireframeModel modelToPlaneCoordinates(const WireframeModel& source, const WorkPlane& plane) {
    WireframeModel result;
    if (source.isPointEntity() && !source.vertices().empty()) {
        result = WireframeModel::point(toPlaneCoordinates(source.vertices().front(), plane));
    } else if (source.isFace3D() && source.vertices().size() == 4) {
        result = WireframeModel::face3D({toPlaneCoordinates(source.vertices()[0], plane),
                                         toPlaneCoordinates(source.vertices()[1], plane),
                                         toPlaneCoordinates(source.vertices()[2], plane),
                                         toPlaneCoordinates(source.vertices()[3], plane)});
    } else if (source.analyticCenter() && source.analyticRadius()) {
        result = WireframeModel::circle(toPlaneCoordinates(*source.analyticCenter(), plane),
                                        *source.analyticRadius(),
                                        std::max<std::size_t>(3, source.vertices().size()));
    } else {
        std::vector<Vec3> vertices;
        vertices.reserve(source.vertices().size());
        for (const auto& vertex : source.vertices())
            vertices.push_back(toPlaneCoordinates(vertex, plane));
        result = WireframeModel(std::move(vertices), source.edges(), source.faces());
    }
    result.setProperties(source.properties());
    return result;
}

WireframeModel modelFromPlaneCoordinates(const WireframeModel& source, const WorkPlane& plane) {
    WireframeModel result;
    if (source.isPointEntity() && !source.vertices().empty()) {
        result = WireframeModel::point(fromPlaneCoordinates(source.vertices().front(), plane));
    } else if (source.isFace3D() && source.vertices().size() == 4) {
        result = WireframeModel::face3D({fromPlaneCoordinates(source.vertices()[0], plane),
                                         fromPlaneCoordinates(source.vertices()[1], plane),
                                         fromPlaneCoordinates(source.vertices()[2], plane),
                                         fromPlaneCoordinates(source.vertices()[3], plane)});
    } else if (source.analyticCenter() && source.analyticRadius()) {
        const Vec3 localCenter = *source.analyticCenter();
        WorkPlane shiftedPlane = plane;
        shiftedPlane.origin = shiftedPlane.origin + shiftedPlane.normal * localCenter.z;
        result = WireframeModel::circleOnPlane(shiftedPlane, {localCenter.x, localCenter.y},
                                               *source.analyticRadius(),
                                               std::max<std::size_t>(3, source.vertices().size()));
    } else {
        std::vector<Vec3> vertices;
        vertices.reserve(source.vertices().size());
        for (const auto& vertex : source.vertices())
            vertices.push_back(fromPlaneCoordinates(vertex, plane));
        result = WireframeModel(std::move(vertices), source.edges(), source.faces());
    }
    result.setProperties(source.properties());
    return result;
}

std::vector<WireframeModel> modelsToPlaneCoordinates(const std::vector<WireframeModel>& source,
                                                     const WorkPlane& plane) {
    std::vector<WireframeModel> result;
    result.reserve(source.size());
    for (const auto& model : source) result.push_back(modelToPlaneCoordinates(model, plane));
    return result;
}
} // namespace

bool projectedBoundsIntersectsViewport(const Bounds3& bounds, const Camera& camera,
                                       int viewportWidth, int viewportHeight,
                                       double marginPixels) {
    if (viewportWidth <= 0 || viewportHeight <= 0) return false;
    const std::array<Vec3, 8> corners{{
        {bounds.minimum.x, bounds.minimum.y, bounds.minimum.z},
        {bounds.maximum.x, bounds.minimum.y, bounds.minimum.z},
        {bounds.minimum.x, bounds.maximum.y, bounds.minimum.z},
        {bounds.maximum.x, bounds.maximum.y, bounds.minimum.z},
        {bounds.minimum.x, bounds.minimum.y, bounds.maximum.z},
        {bounds.maximum.x, bounds.minimum.y, bounds.maximum.z},
        {bounds.minimum.x, bounds.maximum.y, bounds.maximum.z},
        {bounds.maximum.x, bounds.maximum.y, bounds.maximum.z},
    }};
    double minX = std::numeric_limits<double>::infinity();
    double minY = std::numeric_limits<double>::infinity();
    double maxX = -std::numeric_limits<double>::infinity();
    double maxY = -std::numeric_limits<double>::infinity();
    for (const auto& corner : corners) {
        const Vec2 projected = camera.project(corner, viewportWidth, viewportHeight);
        minX = std::min(minX, projected.x);
        minY = std::min(minY, projected.y);
        maxX = std::max(maxX, projected.x);
        maxY = std::max(maxY, projected.y);
    }
    return maxX >= -marginPixels && maxY >= -marginPixels &&
           minX <= static_cast<double>(viewportWidth) + marginPixels &&
           minY <= static_cast<double>(viewportHeight) + marginPixels;
}

std::optional<WireframeModel> offsetModelOnPlane(const WireframeModel& source, double distance,
                                                 const Vec3& sidePoint, const WorkPlane& plane) {
    const auto local = offsetModel2D(modelToPlaneCoordinates(source, plane), distance,
                                     toPlaneCoordinates(sidePoint, plane));
    if (!local) return std::nullopt;
    return modelFromPlaneCoordinates(*local, plane);
}

std::optional<WireframeModel> mirrorModelOnPlane(const WireframeModel& source,
                                                 const Vec3& axisStart, const Vec3& axisEnd,
                                                 const WorkPlane& plane) {
    const auto local = mirrorModel2D(modelToPlaneCoordinates(source, plane),
                                     toPlaneCoordinates(axisStart, plane),
                                     toPlaneCoordinates(axisEnd, plane));
    if (!local) return std::nullopt;
    return modelFromPlaneCoordinates(*local, plane);
}

std::optional<WireframeModel> rotateModel2D(const WireframeModel& source,
                                            const Vec3& center, double angleDeg) {
    const double radians = angleDeg * std::numbers::pi / 180.0;
    const double cosA = std::cos(radians);
    const double sinA = std::sin(radians);
    const auto rotate = [&](const Vec3& point) {
        const double dx = point.x - center.x;
        const double dy = point.y - center.y;
        return Vec3{center.x + cosA * dx - sinA * dy,
                    center.y + sinA * dx + cosA * dy, point.z};
    };

    WireframeModel result;
    if (source.isPointEntity() && !source.vertices().empty()) {
        result = WireframeModel::point(rotate(source.vertices().front()));
    } else if (source.isFace3D() && source.vertices().size() == 4) {
        result = WireframeModel::face3D({rotate(source.vertices()[0]), rotate(source.vertices()[1]),
                                         rotate(source.vertices()[2]), rotate(source.vertices()[3])});
    } else if (source.analyticCenter() && source.analyticRadius()) {
        result = WireframeModel::circle(rotate(*source.analyticCenter()), *source.analyticRadius(),
                                        std::max<std::size_t>(3, source.vertices().size()));
    } else {
        std::vector<Vec3> vertices;
        vertices.reserve(source.vertices().size());
        for (const auto& vertex : source.vertices()) vertices.push_back(rotate(vertex));
        result = WireframeModel(std::move(vertices), source.edges(), source.faces());
    }
    result.setProperties(source.properties());
    return result;
}

std::optional<WireframeModel> rotateModelOnPlane(const WireframeModel& source,
                                                 const Vec3& center, double angleDeg,
                                                 const WorkPlane& plane) {
    const auto local = rotateModel2D(modelToPlaneCoordinates(source, plane),
                                     toPlaneCoordinates(center, plane), angleDeg);
    if (!local) return std::nullopt;
    return modelFromPlaneCoordinates(*local, plane);
}

std::optional<WireframeModel> rotateModelAroundAxis(const WireframeModel& source,
                                                    const Vec3& center, const Vec3& axis,
                                                    double angleDeg) {
    if (axis.x * axis.x + axis.y * axis.y + axis.z * axis.z <= epsilon) return std::nullopt;
    WireframeModel copy = source;
    copy.rotateAroundAxis(center, axis, angleDeg * std::numbers::pi / 180.0);
    return copy;
}

std::vector<WireframeModel> polarArrayOnPlane(const WireframeModel& source, std::size_t itemCount,
                                              const Vec3& center, const WorkPlane& plane) {
    auto localCopies = polarArray2D(modelToPlaneCoordinates(source, plane), itemCount,
                                    toPlaneCoordinates(center, plane));
    std::vector<WireframeModel> result;
    result.reserve(localCopies.size());
    for (const auto& copy : localCopies) result.push_back(modelFromPlaneCoordinates(copy, plane));
    return result;
}

std::optional<std::vector<WireframeModel>> trimLineOnPlane(
    const WireframeModel& source, const std::vector<WireframeModel>& boundaries,
    const Vec3& pickPoint, const WorkPlane& plane) {
    const auto local = trimLine2D(modelToPlaneCoordinates(source, plane),
                                  modelsToPlaneCoordinates(boundaries, plane),
                                  toPlaneCoordinates(pickPoint, plane));
    if (!local) return std::nullopt;
    std::vector<WireframeModel> result;
    result.reserve(local->size());
    for (const auto& segment : *local) result.push_back(modelFromPlaneCoordinates(segment, plane));
    return result;
}

std::optional<WireframeModel> extendLineOnPlane(
    const WireframeModel& source, const std::vector<WireframeModel>& boundaries,
    const Vec3& pickPoint, const WorkPlane& plane) {
    const auto local = extendLine2D(modelToPlaneCoordinates(source, plane),
                                    modelsToPlaneCoordinates(boundaries, plane),
                                    toPlaneCoordinates(pickPoint, plane));
    if (!local) return std::nullopt;
    return modelFromPlaneCoordinates(*local, plane);
}

std::optional<FilletResult> filletLinesOnPlane(
    const WireframeModel& first, const Vec3& firstPick,
    const WireframeModel& second, const Vec3& secondPick,
    double radius, const WorkPlane& plane) {
    if (radius <= epsilon || first.vertices().size() != 2 || first.edges().size() != 1 ||
        second.vertices().size() != 2 || second.edges().size() != 1)
        return std::nullopt;

    const auto firstLocal = modelToPlaneCoordinates(first, plane);
    const auto secondLocal = modelToPlaneCoordinates(second, plane);
    const Vec3 a = firstLocal.vertices()[0];
    const Vec3 b = firstLocal.vertices()[1];
    const Vec3 c = secondLocal.vertices()[0];
    const Vec3 d = secondLocal.vertices()[1];
    if (std::max({std::abs(a.z), std::abs(b.z), std::abs(c.z), std::abs(d.z)}) > 1e-6)
        return std::nullopt;

    const Vec2 r{b.x - a.x, b.y - a.y};
    const Vec2 s{d.x - c.x, d.y - c.y};
    const auto cross = [](const Vec2& lhs, const Vec2& rhs) {
        return lhs.x * rhs.y - lhs.y * rhs.x;
    };
    const double denominator = cross(r, s);
    const double firstLength = std::hypot(r.x, r.y);
    const double secondLength = std::hypot(s.x, s.y);
    if (std::abs(denominator) <= epsilon || firstLength <= epsilon || secondLength <= epsilon)
        return std::nullopt;
    const Vec2 ca{c.x - a.x, c.y - a.y};
    const double firstParameter = cross(ca, s) / denominator;
    const Vec2 intersection{a.x + r.x * firstParameter, a.y + r.y * firstParameter};

    const auto pickedRay = [&](const Vec2& lineDirection, double lineLength, const Vec3& pick) {
        Vec2 direction{lineDirection.x / lineLength, lineDirection.y / lineLength};
        const Vec2 localPick{plane.toPlane(pick).x, plane.toPlane(pick).y};
        if ((localPick.x - intersection.x) * direction.x +
            (localPick.y - intersection.y) * direction.y < 0.0) {
            direction.x = -direction.x;
            direction.y = -direction.y;
        }
        return direction;
    };
    const Vec2 firstDirection = pickedRay(r, firstLength, firstPick);
    const Vec2 secondDirection = pickedRay(s, secondLength, secondPick);
    const double cosine = std::clamp(firstDirection.x * secondDirection.x +
                                     firstDirection.y * secondDirection.y, -1.0, 1.0);
    const double angle = std::acos(cosine);
    if (angle <= 1e-6 || std::abs(std::numbers::pi - angle) <= 1e-6) return std::nullopt;
    const double tangentDistance = radius / std::tan(angle * 0.5);
    const double centerDistance = radius / std::sin(angle * 0.5);
    const Vec2 bisectorRaw{firstDirection.x + secondDirection.x,
                           firstDirection.y + secondDirection.y};
    const double bisectorLength = std::hypot(bisectorRaw.x, bisectorRaw.y);
    if (bisectorLength <= epsilon || !std::isfinite(tangentDistance)) return std::nullopt;
    const Vec2 tangentFirst{intersection.x + firstDirection.x * tangentDistance,
                            intersection.y + firstDirection.y * tangentDistance};
    const Vec2 tangentSecond{intersection.x + secondDirection.x * tangentDistance,
                             intersection.y + secondDirection.y * tangentDistance};
    const Vec2 center{intersection.x + bisectorRaw.x / bisectorLength * centerDistance,
                      intersection.y + bisectorRaw.y / bisectorLength * centerDistance};

    const auto farEndpoint = [&](const WireframeModel& line, const Vec2& direction) {
        const Vec3& p0 = line.vertices()[0];
        const Vec3& p1 = line.vertices()[1];
        const double t0 = (p0.x - intersection.x) * direction.x +
                          (p0.y - intersection.y) * direction.y;
        const double t1 = (p1.x - intersection.x) * direction.x +
                          (p1.y - intersection.y) * direction.y;
        return t0 >= t1 ? p0 : p1;
    };
    auto firstLine = WireframeModel::line(
        fromPlaneCoordinates(farEndpoint(firstLocal, firstDirection), plane),
        plane.fromPlane(tangentFirst));
    auto secondLine = WireframeModel::line(
        fromPlaneCoordinates(farEndpoint(secondLocal, secondDirection), plane),
        plane.fromPlane(tangentSecond));
    firstLine.setProperties(first.properties());
    secondLine.setProperties(second.properties());

    double startAngle = std::atan2(tangentFirst.y - center.y, tangentFirst.x - center.x);
    double endAngle = std::atan2(tangentSecond.y - center.y, tangentSecond.x - center.x);
    double sweep = endAngle - startAngle;
    while (sweep > std::numbers::pi) sweep -= 2.0 * std::numbers::pi;
    while (sweep < -std::numbers::pi) sweep += 2.0 * std::numbers::pi;
    const std::size_t segments = std::max<std::size_t>(8,
        static_cast<std::size_t>(std::ceil(std::abs(sweep) / (std::numbers::pi / 32.0))));
    std::vector<Vec3> arcVertices;
    std::vector<Edge> arcEdges;
    arcVertices.reserve(segments + 1);
    arcEdges.reserve(segments);
    for (std::size_t index = 0; index <= segments; ++index) {
        const double fraction = static_cast<double>(index) / static_cast<double>(segments);
        const double current = startAngle + sweep * fraction;
        arcVertices.push_back(plane.fromPlane({center.x + radius * std::cos(current),
                                              center.y + radius * std::sin(current)}));
        if (index) arcEdges.push_back({index - 1, index});
    }
    WireframeModel arc(std::move(arcVertices), std::move(arcEdges));
    arc.setProperties(first.properties());
    return FilletResult{std::move(firstLine), std::move(secondLine), std::move(arc)};
}

bool shouldEvaluateSnapping(bool selectingEntities, bool zoomPhase, bool cameraNavigating) noexcept {
    return !selectingEntities && !zoomPhase && !cameraNavigating;
}

SnapMarkerSymbol snapMarkerSymbol(SnapType type) noexcept {
    switch (type) {
    case SnapType::None: return SnapMarkerSymbol::None;
    case SnapType::Grid: return SnapMarkerSymbol::GridCross;
    case SnapType::Endpoint: return SnapMarkerSymbol::Square;
    case SnapType::Midpoint: return SnapMarkerSymbol::Triangle;
    case SnapType::Center: return SnapMarkerSymbol::Circle;
    case SnapType::GeometricCenter: return SnapMarkerSymbol::CircleCross;
    case SnapType::Node: return SnapMarkerSymbol::CrossedCircle;
    case SnapType::Quadrant: return SnapMarkerSymbol::Diamond;
    case SnapType::Intersection: return SnapMarkerSymbol::Cross;
    case SnapType::ApparentIntersection: return SnapMarkerSymbol::BoxedCross;
    case SnapType::Extension: return SnapMarkerSymbol::ExtensionLine;
    case SnapType::Insertion: return SnapMarkerSymbol::LinkedSquares;
    case SnapType::Perpendicular: return SnapMarkerSymbol::RightAngle;
    case SnapType::Tangent: return SnapMarkerSymbol::TangentCircle;
    case SnapType::Nearest: return SnapMarkerSymbol::Hourglass;
    case SnapType::Parallel: return SnapMarkerSymbol::ParallelLines;
    }
    return SnapMarkerSymbol::None;
}

} // namespace mm
