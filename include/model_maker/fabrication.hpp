#pragma once
// SEMANTIK IMALAT OZELLIKLERI (P2A) — cizimden BAGIMSIZ, Document'tan turetilen tipli imalat verisi.
//
// KURAL (kalici): imalat deligi ASLA "guzel gorunen 2B daire / bulon basi / rastgele bbox"tan
// cikarilmaz. Tek kaynak: Document'taki gercek Hole CutModifier (bulon grubuna ait), levha
// cercevesi icin kalici ComponentInstanceRecord::children[].plate (PlateSpec) ve uye ekseni
// (EntityProperties axis). Kaynagi olmayan her sey `Unresolved` olarak RAPORLANIR, uydurulmaz.
//
// KATMANLAR:
//   fab::extractFabrication()  -> FabricationInventory (bu dosya; geometri verisi)
//   drawing_builder            -> olcu/callout URETIMI (inventory'den; geometri != annotation)
//   drawing_layout             -> yerlesim (annotation'lari yalniz kagit mm'de konumlandirir)
//
// KOORDINAT SOZLESMELERI:
//   * Levha-yerel (u,v): origin = PlateSpec.center (levha ORTASI), u = genislik yonu, v = yukseklik
//     yonu, n = kalinlik normali. Delik (u,v) = (P - center) . u/v.   [mm]
//   * Uye-yerel (x',y',z'): origin = uye ekseni BASLANGICI (axisFrom), x' = eksen yonu (from->to),
//     z' = dunya +Z'nin x'e dik bileseni (dusey ise dunya +X), y' = z' x x'. Bu bir KONVANSIYONdur
//     (kesit rolunden degil); profilRotation != 0 ise y'/z' ofsetleri belirsiz -> Unresolved.
//   * Ofsetler uye BASLANGICINDAN (axisFrom) olculur — uye ucundan degil.
//
// OCCT'siz, Qt'siz; Linux ctest'te kosar.

#include "model_maker/document.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace mm::fab {

enum class FeatureKind { MemberDatum, Plate, HoleGroup, Hole };

// Kararli imalat ozelligi kimligi: yeniden uretimde, kaydet/ac sonrasi AYNI model icin AYNI anahtar.
//   datum:<memberId>   plate:<plateId>   group:<plateId>.<boltGroupId>.<dia*10>
//   hole:<plateId>.<boltGroupId>.<row>.<col>      (row: ustten 1.., col: soldan 1.. — levha cercevesinde)
std::string featureKey(FeatureKind kind, EntityId owner, EntityId group = kInvalidEntityId,
                       long long a = 0, long long b = 0);

enum class HoleShape { Round, Slot };

struct MemberDatum {
    bool valid{false};
    std::string id;             // "datum:<id>"
    EntityId member{kInvalidEntityId};
    Vec3 start{};
    Vec3 end{};
    Vec3 x{1, 0, 0};            // eksen yonu
    Vec3 y{0, 1, 0};
    Vec3 z{0, 0, 1};
    double length{0.0};
    bool rollResolved{true};    // profileRotation == 0 -> true
    std::string orientationBasis;   // y'/z' nasil turetildi (kullaniciya aciklama)
    // Dunya noktasi -> uye-yerel (baslangictan).
    Vec3 toLocal(const Vec3& p) const noexcept;
};

struct PlateFeature {
    std::string id;             // "plate:<id>"
    EntityId plate{kInvalidEntityId};
    EntityId component{kInvalidEntityId};   // sahip ComponentInstanceRecord
    std::string roleKey;
    Vec3 origin{};              // levha ortasi (dunya)
    Vec3 u{1, 0, 0}, v{0, 1, 0}, n{0, 0, 1};
    double width{0.0};          // u boyu
    double height{0.0};         // v boyu
    double thickness{0.0};
    std::vector<Vec2> outline;  // levha-yerel (u,v), CCW; pah (chamfer) dahil
    bool outlineExact{true};    // yay pahlar (tip 2/3) duz pah olarak yaklastirildiysa false
    bool tapered{false};        // pah/konik kesim var
    std::string material;
    // Levha zarfinin uye-yerel sinirlari (baslangictan, outline gercek koselerinden).
    Vec3 localMin{}, localMax{}, localCentre{};
    // Levha-yerel zarf (centre'a gore).
    double envMinU{0}, envMaxU{0}, envMinV{0}, envMaxV{0};
    std::uint64_t documentRevision{0};

    // Uye-yerel k. eksen (0=x',1=y',2=z') boyunca uye BASLANGICINDAN levhaya ofset (isaretli).
    //   levha tamamen + tarafta -> yakin yuz (localMin); tamamen - tarafta -> yakin yuz (localMax);
    //   baslangic duzlemini kesiyorsa -> levha MERKEZI (straddle=true); baslangica degiyorsa -> 0.
    double offsetRef(int k, bool* straddle = nullptr) const noexcept;
};

struct HoleFeature {
    std::string id;             // "hole:<plate>.<group>.<row>.<col>"
    EntityId plate{kInvalidEntityId};
    EntityId boltGroup{kInvalidEntityId};
    std::size_t modifierIndex{0};   // Document::cutModifiers() indeksi (gercek kaynak)
    int row{0};                 // ustten, 1 tabanli (levha v azalan)
    int col{0};                 // soldan, 1 tabanli (levha u artan)
    double u{0.0}, v{0.0};      // levha-yerel merkez
    Vec3 world{};
    HoleShape shape{HoleShape::Round};
    double diameter{0.0};
    double slotAlong{0.0}, slotAcross{0.0};
};

struct HoleGroupFeature {
    std::string id;             // "group:..."
    EntityId plate{kInvalidEntityId};
    EntityId boltGroup{kInvalidEntityId};
    double diameter{0.0};
    HoleShape shape{HoleShape::Round};
    int rows{0}, cols{0};
    bool regular{false};        // tam dikdortgen ag (rows*cols == delik sayisi)
    std::vector<double> colU;   // artan
    std::vector<double> rowV;   // AZALAN (ust sira ilk)
    std::vector<double> pitchU; // ardisik merkez farklari (cols-1)
    std::vector<double> pitchV; // ardisik (rows-1)
    // NET kenar mesafeleri (levha zarf kenari -> en dis deligin DIS kenari).
    double edgeMinU{0}, edgeMaxU{0}, edgeBottom{0}, edgeTop{0};
    std::vector<std::size_t> holes;   // FabricationInventory::holes indeksleri
    int count() const noexcept { return static_cast<int>(holes.size()); }
};

struct Unresolved {
    std::string code;           // sabit makine kodu (testler/JSON bunu kullanir)
    std::string detail;         // insan metni (UTF-8)
    EntityId entity{kInvalidEntityId};
    bool operator==(const Unresolved&) const = default;
};

struct FabricationInventory {
    std::uint64_t documentRevision{0};
    EntityId mainPart{kInvalidEntityId};
    MemberDatum datum;
    std::vector<PlateFeature> plates;
    std::vector<HoleFeature> holes;
    std::vector<HoleGroupFeature> groups;
    std::vector<Unresolved> unresolved;

    const PlateFeature* plateOf(EntityId plate) const noexcept;
    bool hasUnresolved() const noexcept { return !unresolved.empty(); }
    bool hasCode(const std::string& code) const noexcept;
};

// parts: cizilen parcalar; mainPart: uye ekseni verecek ana parca.
FabricationInventory extractFabrication(const Document& doc, const std::vector<EntityId>& parts,
                                        EntityId mainPart);

// Parca imzasina katilan delik/kesme parmak izi: 0 = delik ve kesme yok (imza DEGISMEZ).
// Levha cercevesi varsa levha-yerel (u,v,cap,sekil); yoksa donusumden bagimsiz yedek (cap + merkez uzakligi).
std::uint64_t drillingFingerprint(const Document& doc, EntityId part);

} // namespace mm::fab
