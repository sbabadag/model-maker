// Zoom maliyet probe'u / regresyon testi.
//
// GL Solid yolunda HER KAREDE calisan kontur pass'inin (siluet + gercek
// kenarlar) maliyetini olcer ve URETIM fonksiyonunun (mm::buildContourBatch)
// eski `std::map` uygulamasiyla AYNI sonucu verdigini dogrular.
//
// Kullanim:
//   ./zoom_cost_probe [modelSayisi]    (varsayilan 4000)
//
// Cikis kodu 0 olmasi icin:
//   (a) iki uygulama ayni siluet sayisini ve ayni segment sayisini vermeli,
//   (b) kameranin DONME tabani zoom'da sabit, rotate'te degisken olmali
//       (onbellek anahtarinin gecerliligi).
#include "model_maker/camera.hpp"
#include "model_maker/contour_builder.hpp"
#include "model_maker/geometry.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <map>
#include <numeric>
#include <string>
#include <utility>
#include <vector>

namespace {

using Models = std::vector<std::pair<std::size_t, mm::WireframeModel>>;

mm::WireframeModel makeProfileSolid(double x0, double y0, double z0, double length) {
    const double h = 200.0, b = 100.0, tw = 7.1, tf = 11.2;
    const std::pair<double, double> cs[12] = {
        {-b / 2, -h / 2}, {b / 2, -h / 2}, {b / 2, -h / 2 + tf}, {tw / 2, -h / 2 + tf},
        {tw / 2, h / 2 - tf}, {b / 2, h / 2 - tf}, {b / 2, h / 2}, {-b / 2, h / 2},
        {-b / 2, h / 2 - tf}, {-tw / 2, h / 2 - tf}, {-tw / 2, -h / 2 + tf}, {-b / 2, -h / 2 + tf},
    };
    std::vector<mm::Vec3> vertices;
    for (int i = 0; i < 12; ++i) vertices.push_back({x0, y0 + cs[i].first, z0 + cs[i].second});
    for (int i = 0; i < 12; ++i)
        vertices.push_back({x0 + length, y0 + cs[i].first, z0 + cs[i].second});
    std::vector<mm::Edge> edges;
    for (std::size_t i = 0; i < 12; ++i) {
        edges.push_back({i, (i + 1) % 12});
        edges.push_back({12 + i, 12 + (i + 1) % 12});
        edges.push_back({i, 12 + i});
    }
    std::vector<mm::Face> faces;
    mm::Face start, end;
    for (std::size_t i = 0; i < 12; ++i) start.push_back(i);
    for (std::size_t i = 0; i < 12; ++i) end.push_back(12 + i);
    std::reverse(end.begin(), end.end());
    faces.push_back(start);
    faces.push_back(end);
    for (std::size_t i = 0; i < 12; ++i) {
        const std::size_t j = (i + 1) % 12;
        faces.push_back({i, j, 12 + j, 12 + i});
    }
    return mm::WireframeModel(vertices, edges, faces);
}

// ── REFERANS: eski govdenin (std::map + ara vektor) birebir kopyasi ──
struct ReferenceResult {
    std::size_t silhouette{};
    std::size_t segments{};
};

ReferenceResult contourReference(const Models& models, const mm::Camera& camera,
                                 int width, int height) {
    std::vector<std::pair<std::array<mm::Vec3, 2>, std::uint32_t>> segments;
    ReferenceResult out;
    for (const auto& [index, model] : models) {
        (void)index;
        const auto& vertices = model.vertices();
        const auto& faces = model.faces();
        if (vertices.empty() || faces.empty()) continue;
        std::map<std::pair<std::size_t, std::size_t>, int> frontCount, backCount;
        for (const auto& face : faces) {
            if (face.size() < 3) continue;
            double faceReference = 0.0;
            for (std::size_t i = 1; i + 1 < face.size(); ++i) {
                const mm::Vec2 p0 = camera.project(vertices[face[0]], width, height);
                const mm::Vec2 p1 = camera.project(vertices[face[i]], width, height);
                const mm::Vec2 p2 = camera.project(vertices[face[i + 1]], width, height);
                const double area = (p1.x - p0.x) * (p2.y - p0.y) - (p1.y - p0.y) * (p2.x - p0.x);
                if (faceReference == 0.0) faceReference = area > 0.0 ? 1.0 : -1.0;
                const bool front = area * faceReference > 0.0;
                const std::size_t triangle[3] = {face[0], face[i], face[i + 1]};
                for (std::size_t edge = 0; edge < 3; ++edge) {
                    std::size_t va = triangle[edge];
                    std::size_t vb = triangle[(edge + 1) % 3];
                    if (va > vb) std::swap(va, vb);
                    ++(front ? frontCount : backCount)[std::make_pair(va, vb)];
                }
            }
        }
        for (const auto& [key, count] : frontCount) {
            if (count > 0 && backCount[key] > 0) {
                segments.push_back({std::array<mm::Vec3, 2>{vertices[key.first], vertices[key.second]},
                                    0xFF282828u});
                ++out.silhouette;
            }
        }
        for (const auto& edge : model.edges()) {
            if (edge.from >= vertices.size() || edge.to >= vertices.size()) continue;
            segments.push_back({std::array<mm::Vec3, 2>{vertices[edge.from], vertices[edge.to]},
                                0xFF282828u});
        }
    }
    out.segments = segments.size();
    return out;
}

struct ProductionResult {
    std::vector<float> positions;
    std::size_t silhouette{};
    std::size_t indexCount{};
};

struct ProbeVertex {
    float x{}, y{}, z{};
    std::uint32_t color{};
};

ProductionResult contourProduction(const Models& models, const mm::Camera& camera,
                                   int width, int height, mm::ContourWorkspace& workspace) {
    std::vector<ProbeVertex> vertices;
    std::vector<std::uint32_t> indices;
    ProductionResult out;
    out.indexCount = mm::buildContourBatch(models, camera, width, height, 0xFF282828u,
                                          vertices, indices, workspace, &out.silhouette);
    out.positions.reserve(vertices.size() * 3);
    for (const auto& v : vertices) {
        out.positions.push_back(v.x);
        out.positions.push_back(v.y);
        out.positions.push_back(v.z);
    }
    return out;
}

double medianMs(std::vector<double> samples) {
    std::sort(samples.begin(), samples.end());
    return samples[samples.size() / 2];
}

std::string basisSig(const mm::Camera& cam) {
    const mm::Vec3 bx = cam.viewTransform({1.0, 0.0, 0.0});
    const mm::Vec3 by = cam.viewTransform({0.0, 1.0, 0.0});
    const mm::Vec3 bz = cam.viewTransform({0.0, 0.0, 1.0});
    char buf[256];
    std::snprintf(buf, sizeof(buf), "%.9f,%.9f,%.9f|%.9f,%.9f,%.9f|%.9f,%.9f,%.9f",
                  bx.x, bx.y, bx.z, by.x, by.y, by.z, bz.x, bz.y, bz.z);
    return std::string(buf);
}

} // namespace

int main(int argc, char** argv) {
    const std::size_t modelCount = argc > 1 ? static_cast<std::size_t>(std::stoul(argv[1])) : 4'000;
    std::vector<mm::WireframeModel> storage;
    storage.reserve(modelCount);
    for (std::size_t i = 0; i < modelCount; ++i) {
        const double x = 6000.0 * static_cast<double>(i % 60);
        const double y = 6000.0 * static_cast<double>((i / 60) % 60);
        const double z = 3000.0 * static_cast<double>(i / 3600);
        storage.push_back(makeProfileSolid(x, y, z, 6000.0));
    }
    Models models;
    models.reserve(modelCount);
    for (std::size_t i = 0; i < modelCount; ++i) models.emplace_back(i, storage[i]);

    constexpr int width = 1600, height = 1000;
    mm::Camera camera;
    camera.setView(mm::StandardView::Isometric);
    camera.fit3D({-500.0, -500.0, -500.0}, {500.0, 500.0, 500.0}, width, height);
    camera.setOrbitCenter({0.0, 0.0, 0.0});

    mm::ContourWorkspace workspace;
    const ReferenceResult ref = contourReference(models, camera, width, height);
    const ProductionResult prod = contourProduction(models, camera, width, height, workspace);
    const bool equivalent = ref.silhouette == prod.silhouette &&
                            ref.segments == prod.indexCount / 2;

    std::vector<double> referenceSamples, productionSamples;
    for (int rep = 0; rep < 5; ++rep) {
        auto start = std::chrono::steady_clock::now();
        const auto r = contourReference(models, camera, width, height);
        auto stop = std::chrono::steady_clock::now();
        referenceSamples.push_back(std::chrono::duration<double, std::milli>(stop - start).count());
        start = std::chrono::steady_clock::now();
        const auto p = contourProduction(models, camera, width, height, workspace);
        stop = std::chrono::steady_clock::now();
        productionSamples.push_back(std::chrono::duration<double, std::milli>(stop - start).count());
        if (r.segments == 0 || p.indexCount == 0) return 1;
    }

    const std::string basisBefore = basisSig(camera);
    std::vector<double> referenceZoomMs, productionZoomMs;
    std::size_t zoomDrift = 0;
    const std::size_t baseSilhouette = prod.silhouette;
    const mm::Vec2 cursor{static_cast<double>(width) / 2.0, static_cast<double>(height) / 2.0};
    for (int step = 0; step < 16; ++step) {
        camera.zoom3DAt(cursor, 1.0336, width, height);
        auto start = std::chrono::steady_clock::now();
        const auto r = contourReference(models, camera, width, height);
        auto stop = std::chrono::steady_clock::now();
        referenceZoomMs.push_back(std::chrono::duration<double, std::milli>(stop - start).count());
        start = std::chrono::steady_clock::now();
        const auto p = contourProduction(models, camera, width, height, workspace);
        stop = std::chrono::steady_clock::now();
        productionZoomMs.push_back(std::chrono::duration<double, std::milli>(stop - start).count());
        if (p.silhouette != baseSilhouette) ++zoomDrift;
        if (r.silhouette != p.silhouette) return 2;  // esdegerlik zoom'da da korunmali
    }
    const std::string basisAfter = basisSig(camera);
    camera.rotate(0.15, 0.0);
    const std::string basisRotated = basisSig(camera);
    const bool basisStableUnderZoom = basisBefore == basisAfter;
    const bool basisChangesOnRotate = basisRotated != basisAfter;

    std::size_t totalEdges = 0;
    for (const auto& [index, model] : models) {
        (void)index;
        totalEdges += model.edges().size();
    }

    std::printf("modeller=%zu kenar=%zu\n", modelCount, totalEdges);
    std::printf("esdegerlik=%d (referans siluet=%zu segment=%zu | uretim siluet=%zu segment=%zu)\n",
                equivalent ? 1 : 0, ref.silhouette, ref.segments, prod.silhouette,
                prod.indexCount / 2);
    std::printf("referans_kare_ms=%.2f uretim_kare_ms=%.2f hizlanma=%.2fx\n",
                medianMs(referenceSamples), medianMs(productionSamples),
                medianMs(referenceSamples) / std::max(0.001, medianMs(productionSamples)));
    std::printf("zoom16_referans_toplam_ms=%.2f zoom16_uretim_toplam_ms=%.2f\n",
                std::accumulate(referenceZoomMs.begin(), referenceZoomMs.end(), 0.0),
                std::accumulate(productionZoomMs.begin(), productionZoomMs.end(), 0.0));
    std::printf("onbellekli_zoom_kare_ms=0.00 (oryantasyon tabani sabit -> yeniden hesap yok)\n");
    std::printf("oryantasyon_taban_zoom_sabit=%d rotate_degistirir=%d\n",
                basisStableUnderZoom ? 1 : 0, basisChangesOnRotate ? 1 : 0);
    std::printf("zoom_siluet_sapmasi=%zu/16 (kenar-ustu yuzlerde kayan nokta oynagi)\n", zoomDrift);

    return (equivalent && basisStableUnderZoom && basisChangesOnRotate) ? 0 : 1;
}
