#pragma once

// CONSTRAINT SYSTEM (Phase R2a) — Tekla "Bind to Plane" + "Distance Variable"
// karsiligi ortak parametrik kisit motoru. End Plate'e ozel DEGIL; Base Plate,
// Fin Plate, Gusset ve Splice ayni cozucuyu kullanir.
//
// COZUCU OCCT'DEN BAGIMSIZDIR: gercek B-Rep yuz cozumu ayri adapter uzerinden
// gelir (FaceReference.resolved doldurulur). Burada yalnizca DUZLEM KISITLARI
// n·x = d icin deterministik, rank-tabanli konum cozumu yapilir. Genel amacli
// nonlinear solver YOK.
//
// Durumlar (ConstraintState):
//   Free             : konumu kisitlayan denklem yok
//   UnderConstrained : gecerli kismi cozum, kalan serbestlik var
//   Solved           : bagimsiz kisitlarla tam belirlendi
//   Conflict         : tutarsiz kisitlar
//   Unresolved       : referans geometri bulunamiyor
//   Cyclic           : bagimlilik grafiginde gecersiz dongu
//
// KISIT SAYISINA GORE DEGIL, LINEER BAGIMSIZLIK + TUTARLILIGA GORE cozulur:
// 4+ duzlem otomatik CONFLICT degildir; paralel + celiskili iki duzlem CONFLICT
// olabilir. Rank 0/1/2/3 icin serbestlik derecesi dogru raporlanir.
//
// Cozucu sonucu (PositionSolveResult) PERSISTENT parametrelerden AYRIDIR:
// solve() her cagrida deterministik olarak YENIDEN turetir.

#include "model_maker/reference_geometry.hpp"

#include <string>
#include <unordered_map>
#include <vector>

namespace mm {

// Component icinde KARARLI yerel anahtar (string). Indeks DEGIL — kayma/
// silme/eklemeden etkilenmez.
struct HandleKey {
    std::string key;
    bool operator==(const HandleKey& other) const noexcept = default;
};
struct HandleKeyHash {
    std::size_t operator()(const HandleKey& h) const noexcept {
        return std::hash<std::string>{}(h.key);
    }
};

enum class HandleKind { Point, Axis, Plane };

enum class ConstraintState { Free, UnderConstrained, Solved, Conflict, Unresolved, Cyclic };

// KONUM kisiti (PositionConstraint): handle'i bir hedef duzleme signedOffset ile
// baglar. Duzlem n·x = d; handle n·x = d + signedOffset uzerinde tutulur.
// ORIENTATION kisiti AYRIDIR (levha yuzunun kolon yuzune paralelligi); sonraki
// dilimde ayri tip olarak eklenir — model bu ayrima hazir.
struct DistanceConstraint {
    HandleKey handle;      // baglanan handle
    FaceReference target;  // hedef duzlem referansi (adapter cozer)
    double signedOffset{0.0};
    bool enabled{true};
};

// OLCUM degiskeni: iki handle arasi mesafe. Salt okunur; GEOMETRIYI ZORLAMAZ
// (cozucu sonrasi, cozumlenmis konumlardan doldurulur).
struct ReferenceDistance {
    HandleKey from;
    HandleKey to;
    double value{0.0};
};

// Cozucu ciktisi (bir handle icin). Persistent DEGIL — solve() turetir.
struct PositionSolveResult {
    ConstraintState state{ConstraintState::Free};
    Vec3 position{};   // son gecerli konuma en yakin cozum (kismi kisitli ise)
    int dof{3};        // kalan serbestlik derecesi = 3 - rank
    Vec3 freeAxis[3];  // serbest yonler (ilk `dof` tanesi gecerli)
};

class ConstraintSystem {
public:
    ConstraintSystem() = default;

    // Handle kaydi (kararli anahtar). initialPosition = son GECERLI konum;
    // kismi kisitli cozumde en yakin nokta bu konuma tercih edilir.
    void addHandle(HandleKey key, HandleKind kind, Vec3 initialPosition);

    // Kisit ekle (hedef duzlemi FaceReference ile; adapterin cozmesi gerekir).
    void addDistanceConstraint(const DistanceConstraint& constraint);

    // Olcum degiskeni ekle (cozumu zorlamaz).
    void addReferenceDistance(const ReferenceDistance& distance);

    // Bagimlilik: `dependent` handle'in referans cozumu `dependsOn` handle'ina
    // baglidir. Dongu → Cyclic.
    void addDependency(HandleKey dependent, HandleKey dependsOn);

    // Tum handle'lari cozer (dolulari: results_ + distances_).
    // true = dongu YOK; false = en az bir dongu var (dongudeki handle'lar
    // Cyclic isaretlenir, digerleri normal cozulur).
    bool solve();

    // Cozum sonucu (son solve() sonrasi). Kayitli degilse Free doner.
    PositionSolveResult position(const HandleKey& key) const;

    // Olcum degiskenlerinin guncel degeri (solve() sonrasi dolu).
    const std::vector<ReferenceDistance>& referenceDistances() const noexcept {
        return distances_;
    }

    std::size_t handleCount() const noexcept { return handles_.size(); }

private:
    struct HandleRecord {
        HandleKey key;
        HandleKind kind;
        Vec3 lastPosition;
    };
    std::vector<HandleRecord> handles_;
    std::unordered_map<HandleKey, std::size_t, HandleKeyHash> handleIndex_;
    std::vector<DistanceConstraint> constraints_;
    std::vector<ReferenceDistance> distances_;
    std::unordered_map<HandleKey, std::vector<HandleKey>, HandleKeyHash> dependencies_;
    std::unordered_map<HandleKey, PositionSolveResult, HandleKeyHash> results_;
    std::unordered_map<HandleKey, Vec3, HandleKeyHash> resolvedPositions_;
};

} // namespace mm
