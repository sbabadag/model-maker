#include "model_maker/view_cube.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace mm {
namespace {

bool contains(const CubeRect& rect, int x, int y) noexcept {
    return x >= rect.left && x <= rect.right && y >= rect.top && y <= rect.bottom;
}

bool contains(const std::array<CubePoint, 4>& polygon, int x, int y) noexcept {
    bool inside = false;
    std::size_t previous = polygon.size() - 1;
    for (std::size_t current = 0; current < polygon.size(); ++current) {
        const auto& a = polygon[current];
        const auto& b = polygon[previous];
        const bool crosses = (a.y > y) != (b.y > y);
        if (crosses) {
            const double crossingX = static_cast<double>(b.x - a.x) * (y - a.y) /
                                     static_cast<double>(b.y - a.y) + a.x;
            if (x < crossingX) inside = !inside;
        }
        previous = current;
    }
    return inside;
}

CubePoint projected(const Camera& camera, Vec3 point, int centerX, int centerY, double scale) noexcept {
    const Vec3 view = camera.viewTransform(point);
    return {static_cast<int>(std::lround(centerX + view.x * scale)),
            static_cast<int>(std::lround(centerY - view.y * scale))};
}

// Kupsel normal uclusu -> faces[] indeksi (layout() tanimlariyla AYNI sira).
int faceIndexOfNormal(int a, int b, int c) noexcept {
    if (a < 0) return 0; // Left
    if (a > 0) return 1; // Right
    if (c < 0) return 2; // Bottom
    if (c > 0) return 3; // Top
    if (b < 0) return 4; // Back
    return 5;            // Front
}

// Noktanin dogru parcasina uzakliginin karesi (kenar tiklama testi).
double segmentDistanceSquared(CubePoint a, CubePoint b, int x, int y) noexcept {
    const double vx = static_cast<double>(b.x - a.x);
    const double vy = static_cast<double>(b.y - a.y);
    const double wx = static_cast<double>(x - a.x);
    const double wy = static_cast<double>(y - a.y);
    const double len2 = vx * vx + vy * vy;
    double t = 0.0;
    if (len2 > 1e-9) t = std::clamp((wx * vx + wy * vy) / len2, 0.0, 1.0);
    const double dx = wx - t * vx;
    const double dy = wy - t * vy;
    return dx * dx + dy * dy;
}

Vec3 normalizedOr(Vec3 v, Vec3 fallback) noexcept {
    const double length = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    if (length < 1e-12) return fallback;
    return {v.x / length, v.y / length, v.z / length};
}

// Kupsel hedefin (kenar/kose) dunya yonu. Face/Home icin de tanimli.
Vec3 cubeSpaceDirection(const ViewCubeTarget& target) noexcept {
    switch (target.kind) {
    case ViewCubeTargetKind::Face:
        switch (target.face) {
        case StandardView::Left: return {-1.0, 0.0, 0.0};
        case StandardView::Right: return {1.0, 0.0, 0.0};
        case StandardView::Bottom: return {0.0, 0.0, -1.0};
        case StandardView::Top: return {0.0, 0.0, 1.0};
        case StandardView::Back: return {0.0, -1.0, 0.0};
        case StandardView::Front: return {0.0, 1.0, 0.0};
        case StandardView::Isometric: break;
        }
        break;
    case ViewCubeTargetKind::Home:
        // ISO izleyici yonu: on-sag-ust kose.
        return normalizedOr({-1.0, -1.0, -1.0}, {0.0, 0.0, 1.0});
    default:
        break;
    }
    return normalizedOr({static_cast<double>(target.dx), static_cast<double>(target.dy),
                         static_cast<double>(target.dz)},
                        {0.0, 0.0, 1.0});
}

struct Quaternion {
    double w{1.0};
    double x{};
    double y{};
    double z{};
};

Quaternion quaternionFromRotation(const std::array<double, 9>& r) noexcept {
    Quaternion q{};
    const double trace = r[0] + r[4] + r[8];
    if (trace > 0.0) {
        const double s = std::sqrt(trace + 1.0) * 2.0;
        q.w = 0.25 * s;
        q.x = (r[7] - r[5]) / s;
        q.y = (r[2] - r[6]) / s;
        q.z = (r[3] - r[1]) / s;
    } else if (r[0] > r[4] && r[0] > r[8]) {
        const double s = std::sqrt(std::max(0.0, 1.0 + r[0] - r[4] - r[8])) * 2.0;
        q.w = (s > 1e-12) ? (r[7] - r[5]) / s : 0.0;
        q.x = 0.25 * s;
        q.y = (r[1] + r[3]) / (s > 1e-12 ? s : 1.0);
        q.z = (r[2] + r[6]) / (s > 1e-12 ? s : 1.0);
    } else if (r[4] > r[8]) {
        const double s = std::sqrt(std::max(0.0, 1.0 + r[4] - r[0] - r[8])) * 2.0;
        q.w = (s > 1e-12) ? (r[2] - r[6]) / s : 0.0;
        q.x = (r[1] + r[3]) / (s > 1e-12 ? s : 1.0);
        q.y = 0.25 * s;
        q.z = (r[5] + r[7]) / (s > 1e-12 ? s : 1.0);
    } else {
        const double s = std::sqrt(std::max(0.0, 1.0 + r[8] - r[0] - r[4])) * 2.0;
        q.w = (s > 1e-12) ? (r[3] - r[1]) / s : 0.0;
        q.x = (r[2] + r[6]) / (s > 1e-12 ? s : 1.0);
        q.y = (r[5] + r[7]) / (s > 1e-12 ? s : 1.0);
        q.z = 0.25 * s;
    }
    const double norm = std::sqrt(q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z);
    if (norm > 1e-12) {
        q.w /= norm;
        q.x /= norm;
        q.y /= norm;
        q.z /= norm;
    } else {
        q = Quaternion{};
    }
    return q;
}

std::array<double, 9> rotationFromQuaternion(const Quaternion& q) noexcept {
    const double xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
    const double xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
    const double wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;
    return {1.0 - 2.0 * (yy + zz), 2.0 * (xy - wz),       2.0 * (xz + wy),
            2.0 * (xy + wz),       1.0 - 2.0 * (xx + zz), 2.0 * (yz - wx),
            2.0 * (xz - wy),       2.0 * (yz + wx),       1.0 - 2.0 * (xx + yy)};
}

} // namespace

CubeRect ViewCube::hostBounds(int canvasWidth, int canvasHeight) noexcept {
    constexpr int width = 192;
    constexpr int height = 238;
    constexpr int margin = 12;
    canvasWidth = std::max(1, canvasWidth);
    canvasHeight = std::max(1, canvasHeight);
    const int hostWidth = std::min(width, canvasWidth);
    const int hostHeight = std::min(height, canvasHeight);
    const int left = std::max(0, canvasWidth - margin - hostWidth);
    const int top = canvasHeight >= height + margin ? margin : std::max(0, canvasHeight - hostHeight);
    return {left, top, left + hostWidth, top + hostHeight};
}

ViewCubeLayout ViewCube::layout(int viewportWidth, const Camera& camera) noexcept {
    ViewCubeLayout result;
    result.centerX = std::max(96, viewportWidth - 96);
    result.centerY = 119;
    result.bodyBounds = {result.centerX - 58, result.centerY - 58,
                         result.centerX + 58, result.centerY + 58};
    result.homeControl = {result.centerX + 19, 202, result.centerX + 65, 228};

    constexpr std::array<Vec3, 8> vertices{{
        {-1.0, -1.0, -1.0}, {1.0, -1.0, -1.0}, {1.0, 1.0, -1.0}, {-1.0, 1.0, -1.0},
        {-1.0, -1.0, 1.0}, {1.0, -1.0, 1.0}, {1.0, 1.0, 1.0}, {-1.0, 1.0, 1.0}
    }};
    for (std::size_t i = 0; i < vertices.size(); ++i)
        result.corners[i] = projected(camera, vertices[i], result.centerX, result.centerY, kViewCubeScalePx);

    struct FaceDefinition {
        StandardView view;
        std::array<std::size_t, 4> indices;
        Vec3 normal;
    };
    constexpr std::array<FaceDefinition, 6> definitions{{
        {StandardView::Left,   {0, 3, 7, 4}, {-1.0, 0.0, 0.0}},  // YZ plane, looking from +X
        {StandardView::Right,  {1, 5, 6, 2}, { 1.0, 0.0, 0.0}},  // YZ plane, looking from -X
        {StandardView::Bottom, {0, 4, 5, 1}, {0.0, 0.0, -1.0}},  // XY plane, looking from -Z
        // DUZELTME: yuz kose kumesi NORMALIYLE uyusmali. Onceden Top y=+1
        // dortgenini, Front z=+1 dortgenini kullaniyordu — normal yalnizca
        // GORUNURLUK icin kullanildigi icin hata gizli kaliyordu; UST
        // gorunusunde "ust yuz" EKRANDA BIR CIZGIYE cokuyordu (olcum: dor.
        // noktalarin y'si sabit) ve yuz merkezine tiklamak hicbir sey
        // secmiyordu.
        {StandardView::Top,    {4, 5, 6, 7}, {0.0, 0.0,  1.0}},  // XY plane, looking from +Z
        {StandardView::Back,   {0, 1, 2, 3}, {0.0, -1.0, 0.0}},  // XZ plane, looking from -Y
        {StandardView::Front,  {3, 2, 6, 7}, {0.0,  1.0, 0.0}}   // XZ plane, looking from +Y
    }};
    for (std::size_t faceIndex = 0; faceIndex < definitions.size(); ++faceIndex) {
        const auto& definition = definitions[faceIndex];
        auto& face = result.faces[faceIndex];
        face.view = definition.view;
        for (std::size_t i = 0; i < definition.indices.size(); ++i)
            face.points[i] = result.corners[definition.indices[i]];
        const Vec3 normal = camera.viewTransform(definition.normal);
        face.visible = normal.z > 1e-6;
        face.depth = normal.z;

        ViewCubeTarget target;
        target.kind = ViewCubeTargetKind::Face;
        target.face = definition.view;
        result.faceTargets[faceIndex] = target;
    }

    // --- 8 KOSE: kupsel kose = vertices[] sirasi; her koseye komsu 3 yuzden
    // en az biri gorunuyorsa kose "gorunur" (tiklanabilir).
    for (std::size_t i = 0; i < vertices.size(); ++i) {
        const int cx = static_cast<int>(vertices[i].x);
        const int cy = static_cast<int>(vertices[i].y);
        const int cz = static_cast<int>(vertices[i].z);
        ViewCubeTarget target;
        target.kind = ViewCubeTargetKind::Corner;
        target.dx = cx;
        target.dy = cy;
        target.dz = cz;
        result.cornerTargets[i] = target;
        const bool visible = result.faces[static_cast<std::size_t>(faceIndexOfNormal(cx, 0, 0))].visible ||
                             result.faces[static_cast<std::size_t>(faceIndexOfNormal(0, cy, 0))].visible ||
                             result.faces[static_cast<std::size_t>(faceIndexOfNormal(0, 0, cz))].visible;
        result.cornerVisible[i] = visible;
    }

    // --- 12 KENAR: kenar orta noktasi = yon uclusu (kupsel koordinat); iki
    // komsu yuzden en az biri gorunuyorsa kenar gorunur.
    for (std::size_t i = 0; i < kViewCubeEdgeDirections.size(); ++i) {
        const int dx = kViewCubeEdgeDirections[i][0];
        const int dy = kViewCubeEdgeDirections[i][1];
        const int dz = kViewCubeEdgeDirections[i][2];
        ViewCubeTarget target;
        target.kind = ViewCubeTargetKind::Edge;
        target.dx = dx;
        target.dy = dy;
        target.dz = dz;
        result.edgeTargets[i] = target;
        result.edgeMidpoints[i] = projected(camera, {static_cast<double>(dx), static_cast<double>(dy),
                                                     static_cast<double>(dz)},
                                            result.centerX, result.centerY, kViewCubeScalePx);
        // Kenar gorunur: iki sifirdan farkli bilesen -> iki komsu yuz; en az
        // biri gorunuyorsa kenar tiklanabilir.
        bool visible = false;
        if (dx != 0 && dy != 0) {
            visible = result.faces[static_cast<std::size_t>(faceIndexOfNormal(dx, 0, 0))].visible ||
                      result.faces[static_cast<std::size_t>(faceIndexOfNormal(0, dy, 0))].visible;
        } else if (dx != 0 && dz != 0) {
            visible = result.faces[static_cast<std::size_t>(faceIndexOfNormal(dx, 0, 0))].visible ||
                      result.faces[static_cast<std::size_t>(faceIndexOfNormal(0, 0, dz))].visible;
        } else if (dy != 0 && dz != 0) {
            visible = result.faces[static_cast<std::size_t>(faceIndexOfNormal(0, dy, 0))].visible ||
                      result.faces[static_cast<std::size_t>(faceIndexOfNormal(0, 0, dz))].visible;
        }
        result.edgeVisible[i] = visible;
    }

    result.axisOrigin = {result.centerX - 47, 214};
    const auto axisPoint = [&](Vec3 axis) {
        const Vec3 view = camera.viewTransform(axis);
        return CubePoint{static_cast<int>(std::lround(result.axisOrigin.x + view.x * 19.0)),
                         static_cast<int>(std::lround(result.axisOrigin.y - view.y * 19.0))};
    };
    result.xAxis = axisPoint({1.0, 0.0, 0.0});
    result.yAxis = axisPoint({0.0, 1.0, 0.0});
    result.zAxis = axisPoint({0.0, 0.0, 1.0});
    return result;
}

Vec3 viewCubeTargetDirection(const ViewCubeTarget& target) noexcept {
    return cubeSpaceDirection(target);
}

bool viewCubeTurntableAngles(const ViewCubeTarget& target, double& yaw, double& pitch) noexcept {
    const Vec3 direction = cubeSpaceDirection(target);
    const double length = std::sqrt(direction.x * direction.x + direction.y * direction.y +
                                    direction.z * direction.z);
    if (length < 1e-12) return false;
    const double nx = direction.x / length;
    const double ny = direction.y / length;
    const double nz = direction.z / length;
    pitch = std::asin(std::clamp(ny, -1.0, 1.0));
    const double cp = std::cos(pitch);
    if (cp < 1e-9) {
        // Kutup (Front/Back): yaw belirsizdir; 0 secilir — setView ile AYNI.
        yaw = 0.0;
    } else {
        yaw = std::atan2(-nx, nz);
    }
    return true;
}

std::array<double, 9> viewCubeTargetRotation(const ViewCubeTarget& target) noexcept {
    if (target.kind == ViewCubeTargetKind::Face) return Camera::standardViewRotation(target.face);
    if (target.kind == ViewCubeTargetKind::Home) return Camera::standardViewRotation(StandardView::Isometric);
    double yaw = 0.0;
    double pitch = 0.0;
    if (!viewCubeTurntableAngles(target, yaw, pitch)) return Camera::standardViewRotation(StandardView::Top);
    return Camera::turntableRotation(yaw, pitch);
}

std::array<double, 9> viewCubeSlerpRotation(const std::array<double, 9>& from,
                                            const std::array<double, 9>& to, double t) noexcept {
    if (t <= 0.0) return from;
    if (t >= 1.0) return to;
    Quaternion a = quaternionFromRotation(from);
    Quaternion b = quaternionFromRotation(to);
    double dot = a.w * b.w + a.x * b.x + a.y * b.y + a.z * b.z;
    // KISA YOL: ayni rotasyonun iki temsili (+q/-q) — negatifse isaret cevrilir.
    if (dot < 0.0) {
        b.w = -b.w;
        b.x = -b.x;
        b.y = -b.y;
        b.z = -b.z;
        dot = -dot;
    }
    Quaternion result{};
    if (dot > 0.9995) {
        result.w = a.w + (b.w - a.w) * t;
        result.x = a.x + (b.x - a.x) * t;
        result.y = a.y + (b.y - a.y) * t;
        result.z = a.z + (b.z - a.z) * t;
    } else {
        const double theta = std::acos(std::clamp(dot, -1.0, 1.0));
        const double sinTheta = std::sin(theta);
        const double wa = std::sin((1.0 - t) * theta) / sinTheta;
        const double wb = std::sin(t * theta) / sinTheta;
        result.w = a.w * wa + b.w * wb;
        result.x = a.x * wa + b.x * wb;
        result.y = a.y * wa + b.y * wb;
        result.z = a.z * wa + b.z * wb;
    }
    const double norm = std::sqrt(result.w * result.w + result.x * result.x +
                                  result.y * result.y + result.z * result.z);
    if (norm > 1e-12) {
        result.w /= norm;
        result.x /= norm;
        result.y /= norm;
        result.z /= norm;
    } else {
        return to;
    }
    return rotationFromQuaternion(result);
}

std::optional<ViewCubeTarget> ViewCube::hitTest(int x, int y, int viewportWidth,
                                                const Camera& camera) noexcept {
    const auto cube = layout(viewportWidth, camera);
    if (contains(cube.homeControl, x, y)) {
        ViewCubeTarget home;
        home.kind = ViewCubeTargetKind::Home;
        home.face = StandardView::Isometric;
        return home;
    }

    // 1) KOSE (daha belirgin; once denenir).
    const ViewCubeTarget* bestCorner = nullptr;
    long bestCornerDistance = 0;
    for (std::size_t i = 0; i < cube.cornerTargets.size(); ++i) {
        if (!cube.cornerVisible[i]) continue;
        const long dx = static_cast<long>(x) - cube.corners[i].x;
        const long dy = static_cast<long>(y) - cube.corners[i].y;
        const long distance = dx * dx + dy * dy;
        if (distance <= static_cast<long>(kViewCubeCornerRadiusPx) * kViewCubeCornerRadiusPx &&
            (!bestCorner || distance < bestCornerDistance)) {
            bestCorner = &cube.cornerTargets[i];
            bestCornerDistance = distance;
        }
    }
    if (bestCorner) return *bestCorner;

    // 2) KENAR: gorunur kenar dogru parcasina dik uzaklik.
    const ViewCubeTarget* bestEdge = nullptr;
    double bestEdgeDistance = 0.0;
    const double limit = static_cast<double>(kViewCubeEdgeHalfWidthPx) * kViewCubeEdgeHalfWidthPx;
    for (std::size_t i = 0; i < cube.edgeTargets.size(); ++i) {
        if (!cube.edgeVisible[i]) continue;
        // Kenarin iki ucu: sifir bilesen her iki yonde -1/+1 alinir.
        const auto& target = cube.edgeTargets[i];
        const Vec3 cornerA{static_cast<double>(target.dx == 0 ? -1 : target.dx),
                           static_cast<double>(target.dy == 0 ? -1 : target.dy),
                           static_cast<double>(target.dz == 0 ? -1 : target.dz)};
        const Vec3 cornerB{static_cast<double>(target.dx == 0 ? 1 : target.dx),
                           static_cast<double>(target.dy == 0 ? 1 : target.dy),
                           static_cast<double>(target.dz == 0 ? 1 : target.dz)};
        const CubePoint a = projected(camera, cornerA, cube.centerX, cube.centerY, kViewCubeScalePx);
        const CubePoint b = projected(camera, cornerB, cube.centerX, cube.centerY, kViewCubeScalePx);
        const double distance = segmentDistanceSquared(a, b, x, y);
        if (distance <= limit && (!bestEdge || distance < bestEdgeDistance)) {
            bestEdge = &cube.edgeTargets[i];
            bestEdgeDistance = distance;
        }
    }
    if (bestEdge) return *bestEdge;

    // 3) YUZ: en one yakin gorunur yuz.
    const ViewCubeFace* closest = nullptr;
    for (const auto& face : cube.faces) {
        if (face.visible && contains(face.points, x, y) && (!closest || face.depth > closest->depth))
            closest = &face;
    }
    if (closest) {
        for (const auto& target : cube.faceTargets)
            if (target.kind == ViewCubeTargetKind::Face && target.face == closest->view) return target;
    }
    return std::nullopt;
}

bool ViewCube::containsWidget(int x, int y, int viewportWidth) noexcept {
    const int center = std::max(96, viewportWidth - 96);
    const CubeRect bounds{center - 76, 48, center + 76, 238};
    return contains(bounds, x, y);
}

} // namespace mm
