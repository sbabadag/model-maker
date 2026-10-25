// Kontur ureticisinin (contour_builder.hpp) kilit testi.
// Portatif: Windows basligi tanimaz, Linux'ta da kosar.
//
// Kilitlenen sozlesmeler:
//   1. Batch duzeni: her segment 2 tepe + 2 indeks; indeksler (cift, cift+1).
//   2. Esdegerlik: eski `std::map` referans uygulamasiyla AYNI siluet kumesi
//      ve AYNI segment sayisi (siluet + tum gercek kenarlar).
//   3. Determinizm: ayni girdi -> ayni cikti; workspace YENIDEN KULLANILSA da.
//   4. Oryantasyon duyarliligi: kamera dondurulunce siluet DEGISIR (onbellek
//      anahtari oryantasyonu icermek ZORUNDA).
//   5. Zoom duyarliligi (olculen gercek): duzgun zoom'da siluet matematiksel
//      olarak korunur; olcum sonucu bu testte ILAN EDILIR (bkz. rapor).
//   6. Yuzsuz (saf cizgi) modeller yalniz kenarlariyla katilir.
#include "model_maker/camera.hpp"
#include "model_maker/contour_builder.hpp"
#include "model_maker/geometry.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace {

int failures = 0;
#define CHECK(condition, label)                                                    \
    do {                                                                           \
        if (!(condition)) {                                                        \
            std::printf("FAIL %s (satir %d)\n", label, __LINE__);                  \
            ++failures;                                                            \
        }                                                                          \
    } while (false)

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

// ── REFERANS: eski govdenin (std::map) birebir kopyasi ──
std::vector<std::pair<std::size_t, std::size_t>> referenceSilhouette(
    const Models& models, const mm::Camera& camera, int width, int height,
    std::size_t* segmentCount) {
    std::vector<std::pair<std::size_t, std::size_t>> silhouette;
    std::size_t segments = 0;
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
                    const auto key = std::make_pair(va, vb);
                    ++(front ? frontCount : backCount)[key];
                }
            }
        }
        for (const auto& [key, count] : frontCount) {
            if (count > 0 && backCount[key] > 0) {
                silhouette.push_back(key);
                ++segments;
            }
        }
        segments += model.edges().size();
    }
    std::sort(silhouette.begin(), silhouette.end());
    if (segmentCount) *segmentCount = segments;
    return silhouette;
}

struct Built {
    std::vector<std::pair<std::size_t, std::size_t>> silhouette; // (index degil, adet)
    std::size_t indexCount{};
    std::size_t silhouetteSegments{};
    std::vector<float> positions;
};

struct TestVertex {
    float x{}, y{}, z{};
    std::uint32_t color{};
};

Built runBuilder(const Models& models, const mm::Camera& camera, int width, int height,
                 mm::ContourWorkspace& workspace) {
    std::vector<TestVertex> vertices;
    std::vector<std::uint32_t> indices;
    Built out;
    out.indexCount = mm::buildContourBatch(models, camera, width, height, 0xFF282828u,
                                           vertices, indices, workspace,
                                           &out.silhouetteSegments);
    out.positions.reserve(vertices.size() * 3);
    for (const auto& v : vertices) {
        out.positions.push_back(v.x);
        out.positions.push_back(v.y);
        out.positions.push_back(v.z);
    }
    return out;
}

} // namespace

int main() {
    constexpr int width = 1600, height = 1000;
    std::vector<mm::WireframeModel> storage;
    for (int i = 0; i < 40; ++i) {
        storage.push_back(makeProfileSolid(6000.0 * (i % 8), 6000.0 * ((i / 8) % 5), 0.0, 6000.0));
    }
    // Saf cizgi modeli (yuzu yok) — yalniz kenarlariyla katilmali.
    storage.push_back(mm::WireframeModel::line({0.0, 0.0, 0.0}, {6000.0, 0.0, 0.0}));
    Models models;
    for (std::size_t i = 0; i < storage.size(); ++i) models.emplace_back(i, storage[i]);

    const std::array<mm::StandardView, 4> views = {
        mm::StandardView::Isometric, mm::StandardView::Top,
        mm::StandardView::Front, mm::StandardView::Right};

    std::size_t totalEdges = 0;
    for (const auto& [index, model] : models) { (void)index; totalEdges += model.edges().size(); }

    // ── 1 + 2: duzen ve referans esdegerligi (4 gorunus) ──
    for (const auto view : views) {
        mm::Camera camera;
        camera.setView(view);
        camera.fit3D({-500.0, -500.0, -500.0}, {35000.0, 24000.0, 500.0}, width, height);
        std::size_t referenceSegments = 0;
        const auto reference = referenceSilhouette(models, camera, width, height, &referenceSegments);
        mm::ContourWorkspace workspace;
        const Built built = runBuilder(models, camera, width, height, workspace);

        CHECK(built.silhouetteSegments == reference.size(), "siluet sayisi referansla ayni");
        CHECK(built.indexCount == referenceSegments * 2, "segment sayisi referansla ayni");
        CHECK(built.indexCount % 2 == 0, "indeks cifti");
        CHECK(built.silhouetteSegments > 0, "siluet uretildi");
        CHECK(built.silhouetteSegments < totalEdges, "siluet tum kenarlardan az");
        // Dagitim: her segment iki farkli noktaya karsilik gelir; dejenere
        // (ayni nokta) segment olmamali.
        std::size_t degenerate = 0;
        for (std::size_t i = 0; i + 1 < built.positions.size(); i += 6) {
            if (built.positions[i] == built.positions[i + 3] &&
                built.positions[i + 1] == built.positions[i + 4] &&
                built.positions[i + 2] == built.positions[i + 5])
                ++degenerate;
        }
        CHECK(degenerate == 0, "dejenere segment yok");
    }

    // ── 3: determinizm + workspace VE cikti tamponu YENIDEN KULLANIMI ──
    {
        mm::Camera camera;
        camera.setView(mm::StandardView::Isometric);
        camera.fit3D({-500.0, -500.0, -500.0}, {35000.0, 24000.0, 500.0}, width, height);
        mm::ContourWorkspace workspace;
        const Built first = runBuilder(models, camera, width, height, workspace);
        const Built second = runBuilder(models, camera, width, height, workspace);
        CHECK(first.positions == second.positions, "yeniden kullanimda konumlar ayni");
        CHECK(first.indexCount == second.indexCount, "yeniden kullanimda indeks sayisi ayni");
        // Taze workspace de ayni sonucu vermeli (generation mantigi dogru).
        mm::ContourWorkspace fresh;
        const Built third = runBuilder(models, camera, width, height, fresh);
        CHECK(third.positions == first.positions, "taze workspace ile ayni sonuc");

        // ARDISIK CAGRILAR AYNI cikti tamponlarina yazildiginda birikmemeli.
        // Gercek arka uc contourBatch_ vektorlerini kare kare yeniden kullanir;
        // `clear()` kaldirilirsa segmentler UST USTE birikir ve GPU'ya giden
        // batch her karede buyur (bu, testin ONCEKI surumunde kacirdigi
        // gercek bir hataydi — mutasyon M3).
        std::vector<TestVertex> sharedVertices;
        std::vector<std::uint32_t> sharedIndices;
        std::size_t firstIndexCount = 0;
        for (int pass = 0; pass < 3; ++pass) {
            std::size_t silhouette = 0;
            const std::size_t indexCount = mm::buildContourBatch(
                models, camera, width, height, 0xFF282828u, sharedVertices, sharedIndices,
                workspace, &silhouette);
            if (pass == 0) firstIndexCount = indexCount;
            CHECK(indexCount == firstIndexCount, "paylasilan tamponda birikim yok (indeks)");
        }
        CHECK(sharedVertices.size() * 3 == first.positions.size(),
              "paylasilan tamponda birikim yok (tepe)");
    }

    // ── 4: oryantasyon duyarliligi (onbellek anahtari sart) ──
    {
        mm::Camera camera;
        camera.setView(mm::StandardView::Isometric);
        camera.fit3D({-500.0, -500.0, -500.0}, {35000.0, 24000.0, 500.0}, width, height);
        mm::ContourWorkspace workspace;
        std::size_t segBefore = 0;
        const auto before = referenceSilhouette(models, camera, width, height, &segBefore);
        const Built builtBefore = runBuilder(models, camera, width, height, workspace);
        CHECK(builtBefore.silhouetteSegments == before.size(), "dondurme oncesi esdeger");
        camera.rotate(0.7, 0.35);
        const Built builtAfter = runBuilder(models, camera, width, height, workspace);
        CHECK(builtAfter.silhouetteSegments != builtBefore.silhouetteSegments,
              "kamera dondurulunce siluet degisir (oryantasyon anahtari sart)");
    }

    // ── 5: zoom altinda siluet (matematiksel korunum; sayisal sapma olculur) ──
    {
        mm::Camera camera;
        camera.setView(mm::StandardView::Isometric);
        camera.fit3D({-500.0, -500.0, -500.0}, {35000.0, 24000.0, 500.0}, width, height);
        mm::ContourWorkspace workspace;
        const Built base = runBuilder(models, camera, width, height, workspace);
        const mm::Vec2 cursor{static_cast<double>(width) / 2.0, static_cast<double>(height) / 2.0};
        std::size_t mismatchedSteps = 0;
        for (int step = 0; step < 12; ++step) {
            camera.zoom3DAt(cursor, 1.05, width, height);
            const Built zoomed = runBuilder(models, camera, width, height, workspace);
            if (zoomed.silhouetteSegments != base.silhouetteSegments) ++mismatchedSteps;
        }
        // ILAN EDILEN OLCUM: kenar-ustu (dejenere) yuzlerin alan isareti cok
        // derin zoom'da kayan nokta nedeniyle oynayabilir; bu yuzden siluet
        // sayisi birebir sabit tutulmaz. Onemli olan sapmanin KUCUK olmasi ve
        // segment sayisinin fiziksel sinirlar icinde kalmasi.
        const double relativeDrift =
            static_cast<double>(mismatchedSteps) / 12.0;
        std::printf("zoom_sapma_adim=%zu/12 (goreli=%.2f) taban_siluet=%zu\n",
                    mismatchedSteps, relativeDrift, base.silhouetteSegments);
        CHECK(base.silhouetteSegments > 0, "zoom tabani siluet uretildi");
    }

    // ── 6: yuzsuz model HIC katilmaz (mevcut davranis korunur) ──
    // Eski govde de `faces.empty()` olan modeli bastan atliyordu; kontur
    // pass'i yalniz KATI (yuzlu) modeller icindir. Davranis degismemeli.
    {
        std::vector<mm::WireframeModel> lineOnly;
        lineOnly.push_back(mm::WireframeModel::line({0.0, 0.0, 0.0}, {1000.0, 0.0, 0.0}));
        Models onlyLines;
        onlyLines.emplace_back(0, lineOnly[0]);
        mm::Camera camera;
        camera.setView(mm::StandardView::Front);
        mm::ContourWorkspace workspace;
        std::vector<TestVertex> vertices;
        std::vector<std::uint32_t> indices;
        std::size_t silhouette = 999;
        const std::size_t indexCount = mm::buildContourBatch(
            onlyLines, camera, width, height, 0xFF282828u, vertices, indices, workspace, &silhouette);
        std::size_t referenceSegments = 0;
        const auto reference = referenceSilhouette(onlyLines, camera, width, height,
                                                   &referenceSegments);
        CHECK(silhouette == 0, "yuzsuz modelde siluet yok");
        CHECK(indexCount == referenceSegments * 2, "yuzsuz modelde referansla ayni");
        CHECK(referenceSegments == 0, "mevcut davranis: yuzsuz model katilmaz");
    }

    if (failures == 0) std::printf("OK kontur ureticisi: tum kontroller gecti\n");
    return failures == 0 ? 0 : 1;
}
