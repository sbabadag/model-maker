#pragma once

// Solid/HiddenLine gorunumunde HER KAREDE uretilen KONTUR cizgileri
// (siluet + gercek kenarlar) icin PORTATIF uretici.
//
// Neden ayri bir birim: eski kod bunu dogrudan GL arka ucunun icinde
// yapiyordu ve iki pahali aliskanlik vardi:
//   1. Model basina iki `std::map<pair<size_t,size_t>,int>` kuruluyordu
//      (tahsis/yikim seli) — 18k model / 252k yuz icin saniyede yuz binlerce
//      dugum.
//   2. ~1M segmentlik ara `std::vector` her karede sifirdan doldurulup
//      ikinci bir dongude GPU batch'ine KOPYALANIYORDU (~56 MB/kare).
// Bu birim ikisini de kaldirir: duz acik-adresli hash (model basina
// GENERATION artisi, temizleme yok) + segmentleri DOGRUDAN cagiranin
// batch'ine yazar.
//
// ONBELLEK ANAHTARI ICIN ONEMLI OZELLIK (olculdu): front/back siniflandirmasi
// yalnizca kameranin DONME kismina baglidir. Izdusumun 2B ucgen alaninin
// ISARETI, duzgun olcekli (zoom) + otelemeli (pan) donusumlerde KORUNUR.
// Bu yuzden silhouette kumesi zoom/pan boyunca yeniden hesaplanmak zorunda
// degildir; `Camera::viewTransform` tabani degismedikce sonuc aynidir.

#include "model_maker/camera.hpp"
#include "model_maker/geometry.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace mm {

// ── Yeniden kullanilan, kararli durumda tahsis YAPMAYAN hash kabi ──
// Model basina iki ayri map yerine TEK tablo: bir kenar icin front ve back
// sayaclari ayni girdide tutulur (bakim yarisi azalir). `beginModel` yalnizca
// generation'i artirir; bayat girdiler okunmaz, temizleme maliyeti yoktur.
class ContourWorkspace {
public:
    // maxEdgesPerModel: model basina ayri kenar ust siniri (slot kapasitesi).
    void reset(std::size_t maxEdgesPerModel) {
        std::size_t capacity = 64;
        const std::size_t need = maxEdgesPerModel * 2 + 16;
        while (capacity < need) capacity <<= 1;
        if (slots_.size() != capacity) {
            slots_.assign(capacity, Slot{});
            generation_ = 1;
        }
        mask_ = capacity - 1;
    }

    void beginModel() {
        ++generation_;
        if (generation_ == 0) { // sarma: bayatlari yok et
            for (auto& slot : slots_) slot.generation = 0;
            generation_ = 1;
        }
    }

    void addCount(std::uint64_t key, bool front) {
        std::size_t index = hash(key) & mask_;
        while (true) {
            Slot& slot = slots_[index];
            if (slot.generation != generation_) {
                slot.key = key;
                slot.generation = generation_;
                slot.front = 0;
                slot.back = 0;
                if (front) slot.front = 1; else slot.back = 1;
                return;
            }
            if (slot.key == key) {
                if (front) ++slot.front; else ++slot.back;
                return;
            }
            index = (index + 1) & mask_;
        }
    }

    // Aktif model icin front>0 VE back>0 olan kenarlari dolasir (siluet).
    template <typename Function>
    void forEachSilhouette(Function&& function) const {
        for (const Slot& slot : slots_) {
            if (slot.generation != generation_) continue;
            if (slot.front > 0 && slot.back > 0) {
                function(static_cast<std::size_t>(slot.key >> 32),
                         static_cast<std::size_t>(slot.key & 0xFFFFFFFFull));
            }
        }
    }

private:
    struct Slot {
        std::uint64_t key{};
        std::int32_t front{};
        std::int32_t back{};
        std::uint32_t generation{};
    };
    static std::uint64_t hash(std::uint64_t key) noexcept {
        key ^= key >> 33;
        key *= 0xff51afd7ed558ccdull;
        key ^= key >> 33;
        return key;
    }
    std::vector<Slot> slots_;
    std::uint32_t generation_{1};
    std::size_t mask_{};
};

// ── Kontur batch'i (GL_LINES) ──
// Vertex duzeni GpuLineBatch ile AYNIDIR; boylece arka uc ek kopya yapmadan
// kendi batch'ini doldurabilir. `Vertex` sablonu sayesinde bu birim Windows
// basligi tanimaz (portatif, Linux'ta test edilebilir).
template <typename Vertex>
struct ContourBatchView {
    std::vector<Vertex>* vertices{};
    std::vector<std::uint32_t>* indices{};
    std::size_t silhouetteSegments{};
};

// Model listesindeki her katı icin siluet kenarlar + TUM gercek kenarlar
// uretilir ve dogrudan `vertices`/`indices` icine yazilir. Donus: index sayisi.
// Not: `faces` bos olan modeller (saf cizgi/nokta) atlanir — eskisi gibi.
template <typename Vertex>
std::size_t buildContourBatch(
    const std::vector<std::pair<std::size_t, WireframeModel>>& models,
    const Camera& camera, int width, int height, std::uint32_t color,
    std::vector<Vertex>& vertices, std::vector<std::uint32_t>& indices,
    ContourWorkspace& workspace, std::size_t* silhouetteSegmentsOut = nullptr) {
    vertices.clear();
    indices.clear();
    std::size_t totalEdges = 0;
    std::size_t maxEdgesPerModel = 0;
    for (const auto& [index, model] : models) {
        (void)index;
        totalEdges += model.edges().size();
        maxEdgesPerModel = std::max(maxEdgesPerModel, model.edges().size());
    }
    workspace.reset(maxEdgesPerModel);
    // Siluet tahmini ~ gercek kenarin yarisi; kapasite bir kez buyur, sonra
    // kare kare yeniden tahsis olmaz.
    vertices.reserve(totalEdges * 3);
    indices.reserve(totalEdges * 3);

    const auto emit = [&](const Vec3& a, const Vec3& b) {
        const std::uint32_t base = static_cast<std::uint32_t>(vertices.size());
        Vertex va{};
        va.x = static_cast<float>(a.x);
        va.y = static_cast<float>(a.y);
        va.z = static_cast<float>(a.z);
        va.color = color;
        Vertex vb = va;
        vb.x = static_cast<float>(b.x);
        vb.y = static_cast<float>(b.y);
        vb.z = static_cast<float>(b.z);
        vertices.push_back(va);
        vertices.push_back(vb);
        indices.push_back(base);
        indices.push_back(base + 1);
    };

    std::size_t silhouetteSegments = 0;
    for (const auto& [index, model] : models) {
        (void)index;
        const auto& modelVertices = model.vertices();
        const auto& faces = model.faces();
        if (modelVertices.empty() || faces.empty()) continue;
        workspace.beginModel();
        for (const auto& face : faces) {
            if (face.size() < 3) continue;
            // Yuz basina referans yon: OCC tesselasyonu ayni yuz icinde
            // tutarli sargi garanti etmez; ilk ucgenin 2B yonlenmesi o yuzun
            // referansidir, digerleri ona gore normalize edilir.
            double faceReference = 0.0;
            for (std::size_t i = 1; i + 1 < face.size(); ++i) {
                const Vec2 p0 = camera.project(modelVertices[face[0]], width, height);
                const Vec2 p1 = camera.project(modelVertices[face[i]], width, height);
                const Vec2 p2 = camera.project(modelVertices[face[i + 1]], width, height);
                const double area = (p1.x - p0.x) * (p2.y - p0.y) -
                                    (p1.y - p0.y) * (p2.x - p0.x);
                if (faceReference == 0.0) faceReference = area > 0.0 ? 1.0 : -1.0;
                const bool front = area * faceReference > 0.0;
                const std::size_t triangle[3] = {face[0], face[i], face[i + 1]};
                for (std::size_t edge = 0; edge < 3; ++edge) {
                    std::size_t va = triangle[edge];
                    std::size_t vb = triangle[(edge + 1) % 3];
                    if (va > vb) std::swap(va, vb);
                    workspace.addCount((static_cast<std::uint64_t>(va) << 32) |
                                       static_cast<std::uint64_t>(vb), front);
                }
            }
        }
        workspace.forEachSilhouette([&](std::size_t va, std::size_t vb) {
            if (va >= modelVertices.size() || vb >= modelVertices.size()) return;
            emit(modelVertices[va], modelVertices[vb]);
            ++silhouetteSegments;
        });
        // GERCEK KENARLAR: kirisma filtreli tel kafes — derinlik testi arka
        // kenarlari yuz dolgusunun altinda gizler.
        for (const auto& edge : model.edges()) {
            if (edge.from >= modelVertices.size() || edge.to >= modelVertices.size()) continue;
            emit(modelVertices[edge.from], modelVertices[edge.to]);
        }
    }
    if (silhouetteSegmentsOut) *silhouetteSegmentsOut = silhouetteSegments;
    return indices.size();
}

} // namespace mm
