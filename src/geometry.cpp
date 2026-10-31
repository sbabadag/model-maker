#include "model_maker/geometry.hpp"

#include <algorithm>
#include <cmath>
#include <map>
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

ProfileSectionFrame profileSectionFrame(const Vec3& axisDirection,
                                        const Vec3& referenceNormal) noexcept {
    ProfileSectionFrame frame;
    const auto axis = normalized(axisDirection);
    if (!axis) return frame; // sifir uzunluk: dunya cercevesi
    frame.axis = *axis;
    // Genislik ekseni = n x axis → baslik plakalari (normal = height) n
    // duzleminin PARALELI olur. n = dunya Z iken sonuc eskisiyle BIREBIR ayni
    // ({-ay, ax, 0}).
    const Vec3 worldUp{0.0, 0.0, 1.0};
    const Vec3 reference = normalized(referenceNormal).value_or(worldUp);
    // Kademeli dusme: (1) istenen duzlem, (2) n ∥ axis ise dunya Z kurali,
    // (3) o da dejenere ise (dusey uye) dunya X — eski davranis.
    auto width = normalized(cross3(reference, *axis));
    if (!width) width = normalized(cross3(worldUp, *axis));
    frame.width = width ? *width : Vec3{1.0, 0.0, 0.0};
    const auto height = normalized(cross3(frame.axis, frame.width));
    frame.height = height ? *height : Vec3{0.0, 1.0, 0.0}; // sag elli: Z ^ X = Y
    return frame;
}

Vec3 memberEndCenter(const WireframeModel& model, bool toEnd) noexcept {
    const auto& p = model.properties();
    const Vec3 from{p.axisFromX, p.axisFromY, p.axisFromZ};
    const Vec3 to{p.axisToX, p.axisToY, p.axisToZ};
    const Vec3 axisVec{to.x - from.x, to.y - from.y, to.z - from.z};
    const double len = std::sqrt(axisVec.x * axisVec.x + axisVec.y * axisVec.y +
                                 axisVec.z * axisVec.z);
    if (len <= 1e-9) return toEnd ? to : from;
    const Vec3 axis{axisVec.x / len, axisVec.y / len, axisVec.z / len};
    const double target = toEnd ? len : 0.0;
    double sx = 0.0, sy = 0.0, sz = 0.0;
    std::size_t count = 0;
    for (const Vec3& v : model.vertices()) {
        const double t = (v.x - from.x) * axis.x + (v.y - from.y) * axis.y +
                         (v.z - from.z) * axis.z;
        if (std::fabs(t - target) > 0.75) continue;   // uc duzleminden uzak
        sx += v.x; sy += v.y; sz += v.z; ++count;
    }
    if (count < 3) return toEnd ? to : from;
    return Vec3{sx / static_cast<double>(count), sy / static_cast<double>(count),
                sz / static_cast<double>(count)};
}

Vec3 workPlaneDeltaToWorld(const WorkPlane& plane, const Vec3& delta) noexcept {
    return Vec3{plane.u.x * delta.x + plane.v.x * delta.y + plane.normal.x * delta.z,
                plane.u.y * delta.x + plane.v.y * delta.y + plane.normal.y * delta.z,
                plane.u.z * delta.x + plane.v.z * delta.y + plane.normal.z * delta.z};
}

Vec3 worldDeltaToWorkPlane(const WorkPlane& plane, const Vec3& delta) noexcept {
    return Vec3{plane.u.x * delta.x + plane.u.y * delta.y + plane.u.z * delta.z,
                plane.v.x * delta.x + plane.v.y * delta.y + plane.v.z * delta.z,
                plane.normal.x * delta.x + plane.normal.y * delta.y + plane.normal.z * delta.z};
}

std::optional<WorkPlane> WorkPlane::fromThreePoints(Vec3 first, Vec3 second, Vec3 third) noexcept {
    const auto uAxis = normalized(second - first);
    if (!uAxis) return std::nullopt;
    const auto normalAxis = normalized(cross3(*uAxis, third - first));
    if (!normalAxis) return std::nullopt;
    const auto vAxis = normalized(cross3(*normalAxis, *uAxis));
    if (!vAxis) return std::nullopt;
    return WorkPlane{first, *uAxis, *vAxis, *normalAxis};
}

std::optional<WorkPlane> constructionPlaneFrame(const WireframeModel& model) noexcept {
    const auto& vertices = model.vertices();
    if (model.constructionKind() != ConstructionKind::Plane || vertices.size() != 4)
        return std::nullopt;
    for (const auto& v : vertices)
        if (!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z)) return std::nullopt;
    const auto uAxis = normalized(vertices[1] - vertices[0]);
    if (!uAxis) return std::nullopt;
    const auto vAxis = normalized(vertices[3] - vertices[0]);
    if (!vAxis) return std::nullopt;
    const auto normalAxis = normalized(cross3(*uAxis, *vAxis));
    if (!normalAxis) return std::nullopt;
    const Vec3 origin = (vertices[0] + vertices[1] + vertices[2] + vertices[3]) * 0.25;
    return WorkPlane{origin, *uAxis, *vAxis, *normalAxis};
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

// Noktayi is duzlemine IZ DUSUR: normal yonundeki bilesen atilir.
// Iki noktali gorunuste secilen noktalar icin kullanilir (bkz. application.cpp):
// gorunus duzleminin normali = bakis yonu oldugundan izdusum EKRANDA noktayi
// oynatmaz, ama duzlem disi geometriye snap olsa bile tasima/kopyalama duzlem
// ICINDE kalir.
Vec3 WorkPlane::projectPoint(Vec3 point) const noexcept {
    const auto n = normalized(normal);
    if (!n) return point; // bozuk normal: dokunma
    const double offset = dot3(point - origin, *n);
    return point - (*n) * offset;
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

WireframeModel WireframeModel::polyline(const std::vector<Vec3>& vertices) {
    if (vertices.size() < 2) return {vertices, {}};
    std::vector<Edge> edges;
    edges.reserve(vertices.size() - 1);
    for (std::size_t i = 0; i + 1 < vertices.size(); ++i)
        edges.push_back(Edge{i, i + 1});
    return {vertices, edges};
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

void WireframeModel::affineAbout(const Vec3& center, const std::array<double, 9>& m) noexcept {
    const auto apply = [&](const Vec3& v) {
        const Vec3 d = v - center;
        return Vec3{center.x + m[0] * d.x + m[1] * d.y + m[2] * d.z,
                    center.y + m[3] * d.x + m[4] * d.y + m[5] * d.z,
                    center.z + m[6] * d.x + m[7] * d.y + m[8] * d.z};
    };
    for (auto& vertex : vertices_) vertex = apply(vertex);
    if (insertionPoint_) *insertionPoint_ = apply(*insertionPoint_);
    if (analyticCenter_) *analyticCenter_ = apply(*analyticCenter_);
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

void WireframeModel::rotateAroundAxis(const Vec3& center, const Vec3& axis, double radians) noexcept {
    const double cosine = std::cos(radians);
    const double sine = std::sin(radians);
    const double axisLength = std::sqrt(axis.x * axis.x + axis.y * axis.y + axis.z * axis.z);
    if (axisLength <= 1e-12) return;
    const Vec3 n{axis.x / axisLength, axis.y / axisLength, axis.z / axisLength};
    const auto rotatePoint = [&](Vec3 point) {
        const Vec3 p = point - center;
        const double dotVal = p.x * n.x + p.y * n.y + p.z * n.z;
        const Vec3 cross = {p.y * n.z - p.z * n.y, p.z * n.x - p.x * n.z, p.x * n.y - p.y * n.x};
        point.x = center.x + p.x * cosine + cross.x * sine + n.x * dotVal * (1.0 - cosine);
        point.y = center.y + p.y * cosine + cross.y * sine + n.y * dotVal * (1.0 - cosine);
        point.z = center.z + p.z * cosine + cross.z * sine + n.z * dotVal * (1.0 - cosine);
        return point;
    };
    for (auto& vertex : vertices_) vertex = rotatePoint(vertex);
    if (insertionPoint_) *insertionPoint_ = rotatePoint(*insertionPoint_);
    if (analyticCenter_) *analyticCenter_ = rotatePoint(*analyticCenter_);
}

std::optional<MeshRayHit> raycastMesh(const WireframeModel& model, Vec3 rayOrigin,
                                      Vec3 rayDirection) noexcept {
    const std::vector<Vec3>& verts = model.vertices();
    const std::vector<Face>& faces = model.faces();
    if (verts.empty() || faces.empty()) return std::nullopt;
    // Isin yonunu birimle (giris birim olmayabilir).
    double rl = std::sqrt(rayDirection.x * rayDirection.x + rayDirection.y * rayDirection.y +
                          rayDirection.z * rayDirection.z);
    if (rl <= 1e-12) return std::nullopt;
    const Vec3 dir{rayDirection.x / rl, rayDirection.y / rl, rayDirection.z / rl};

    const double eps = 1e-9;
    std::optional<MeshRayHit> best;
    for (std::size_t faceIdx = 0; faceIdx < faces.size(); ++faceIdx) {
        const Face& face = faces[faceIdx];
        if (face.size() < 3) continue;
        // Fan ucleme: {v0, vi, vi+1} — poligon ve ucgen girdileri birlikte isler.
        const Vec3& v0 = verts[face[0]];
        for (std::size_t i = 1; i + 1 < face.size(); ++i) {
            const Vec3& v1 = verts[face[i]];
            const Vec3& v2 = verts[face[i + 1]];
            const Vec3 e1{v1.x - v0.x, v1.y - v0.y, v1.z - v0.z};
            const Vec3 e2{v2.x - v0.x, v2.y - v0.y, v2.z - v0.z};
            const Vec3 pvec{dir.y * e2.z - dir.z * e2.y, dir.z * e2.x - dir.x * e2.z,
                            dir.x * e2.y - dir.y * e2.x};
            const double det = e1.x * pvec.x + e1.y * pvec.y + e1.z * pvec.z;
            if (std::abs(det) < eps) continue;
            const double inv = 1.0 / det;
            const Vec3 tvec{rayOrigin.x - v0.x, rayOrigin.y - v0.y, rayOrigin.z - v0.z};
            const double u = (tvec.x * pvec.x + tvec.y * pvec.y + tvec.z * pvec.z) * inv;
            if (u < 0.0 || u > 1.0) continue;
            const Vec3 qvec{tvec.y * e1.z - tvec.z * e1.y, tvec.z * e1.x - tvec.x * e1.z,
                            tvec.x * e1.y - tvec.y * e1.x};
            const double v = (dir.x * qvec.x + dir.y * qvec.y + dir.z * qvec.z) * inv;
            if (v < 0.0 || u + v > 1.0) continue;
            const double t = (e2.x * qvec.x + e2.y * qvec.y + e2.z * qvec.z) * inv;
            if (t < 0.0) continue;
            if (best && t >= best->distance) continue;
            Vec3 n{e1.y * e2.z - e1.z * e2.y, e1.z * e2.x - e1.x * e2.z,
                   e1.x * e2.y - e1.y * e2.x};
            const double nl = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
            if (nl <= 1e-12) continue;
            n = Vec3{n.x / nl, n.y / nl, n.z / nl};
            MeshRayHit hit;
            hit.point = Vec3{rayOrigin.x + dir.x * t, rayOrigin.y + dir.y * t,
                             rayOrigin.z + dir.z * t};
            hit.normal = n;
            hit.distance = t;
            hit.faceIndex = faceIdx;
            best = hit;
        }
    }
    return best;
}

std::vector<std::size_t> planarFaceTriangleGroup(const WireframeModel& model,
                                                 std::size_t startTriangle) noexcept {
    const auto& faces = model.faces();
    const auto& verts = model.vertices();
    std::vector<std::size_t> result;
    if (startTriangle >= faces.size()) return result;
    if (faces[startTriangle].size() < 3) {
        result.push_back(startTriangle);
        return result;
    }
    // Ucgen normali (capraz carpim), birimlenmis.
    const auto triNormal = [&](std::size_t fi, double& nx, double& ny, double& nz) {
        const auto& f = faces[fi];
        if (f.size() < 3) return false;
        const Vec3& a = verts[f[0]];
        const Vec3& b = verts[f[1]];
        const Vec3& c = verts[f[2]];
        const double e1x = b.x - a.x, e1y = b.y - a.y, e1z = b.z - a.z;
        const double e2x = c.x - a.x, e2y = c.y - a.y, e2z = c.z - a.z;
        nx = e1y * e2z - e1z * e2y;
        ny = e1z * e2x - e1x * e2z;
        nz = e1x * e2y - e1y * e2x;
        const double l = std::sqrt(nx * nx + ny * ny + nz * nz);
        if (l <= 1e-12) return false;
        nx /= l; ny /= l; nz /= l;
        return true;
    };
    double rnx = 0.0, rny = 0.0, rnz = 0.0;
    if (!triNormal(startTriangle, rnx, rny, rnz)) {
        result.push_back(startTriangle);
        return result;
    }
    // Kenar (siralanmis iki kose) -> ucgen listesi komsuluk haritasi.
    std::map<std::pair<std::size_t, std::size_t>, std::vector<std::size_t>> edgeMap;
    for (std::size_t fi = 0; fi < faces.size(); ++fi) {
        const auto& f = faces[fi];
        if (f.size() < 3) continue;
        for (std::size_t k = 0; k < f.size(); ++k) {
            std::size_t a = f[k], b = f[(k + 1) % f.size()];
            if (a > b) std::swap(a, b);
            edgeMap[{a, b}].push_back(fi);
        }
    }
    // Es-duzlemsel baglantili yayilim.
    std::vector<bool> visited(faces.size(), false);
    std::vector<std::size_t> stack{startTriangle};
    visited[startTriangle] = true;
    while (!stack.empty()) {
        const std::size_t fi = stack.back();
        stack.pop_back();
        result.push_back(fi);
        const auto& f = faces[fi];
        for (std::size_t k = 0; k < f.size(); ++k) {
            std::size_t a = f[k], b = f[(k + 1) % f.size()];
            if (a > b) std::swap(a, b);
            const auto it = edgeMap.find({a, b});
            if (it == edgeMap.end()) continue;
            for (const std::size_t nb : it->second) {
                if (nb == fi || visited[nb]) continue;
                double nnx = 0.0, nny = 0.0, nnz = 0.0;
                if (!triNormal(nb, nnx, nny, nnz)) continue;
                const double dot = nnx * rnx + nny * rny + nnz * rnz;
                if (std::abs(dot) > 0.999) {
                    visited[nb] = true;
                    stack.push_back(nb);
                }
            }
        }
    }
    return result;
}

} // namespace mm
