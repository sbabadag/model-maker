#pragma once

// PARAMETRIK ALIN LEVHASI BAGLANTISI (Phase C1) — saf veri modeli + yerlesim.
//
// Tekla "End plate connection" karsiligi (sadeleltirilmis, ilk dikey dilim):
//   - Main Part    = kolon (delinen, bulonlarin girdigi ana parca)
//   - Secondary    = kiris (ucuna alin levhasi eklenen parca)
//   - Alin levhasi = kiris ucuna DIK, kiris kesitini saran dikdortgen plaka
//     (baglantinin SAHIP OLDUGU alt nesne; baglanti silinince levha da silinir,
//      kullanici parcalari KORUNUR).
//   - Bulon grubu = levhayi kolona bulonlar (mevcut BoltGroup mekanizmasi;
//     delikler CutKind::Hole, montaj bolt_geometry).
//
// Bulonlar levhayi + kolonu deler (kirisi DEGIL — levha kirise kaynakli varsayilir,
// bulonlar levha->kolon baglantisi kurar). Bulon yerlesimi BoltGroup'un pattern
// mekanizmasina devredilir: origin = levhanin sol-alt kosesi (edgeDistance iceri),
// xDir = levha genislik yonu (u), axis = kiris yonu; countX*countY ve spacing
// degerleri levha boyutlarindan turetilir. Boylece delik uretimi (boltGroupHole-
// Modifiers) ve montaj (buildBoltGroupAssemblyShape) SIFIR degisiklikle calisir.
//
// windows.h yok; Linux'ta derlenir/test edilir. Gercek levha B-Rep'i ve bulon
// montaji OCC katmaninda uretilir (end_plate_geometry.hpp).

#include "model_maker/bolt.hpp"
#include "model_maker/entity_id.hpp"
#include "model_maker/geometry.hpp"
#include "model_maker/reference_geometry.hpp"

#include <string>
#include <vector>

namespace mm {

// Alin levhasi baglanti parametreleri (Tekla End plate properties karsiligi,
// sadeleltirilmis). Kaynak/rijitlestirici/haunch/norm kontrolleri SONRAKI
// fazlara birakildi.
struct EndPlateParams {
    double plateThickness{12.0};   // levha kalinligi (mm)
    double plateExtra{20.0};       // YATAY (genislik) kenar payi (her yan, mm)
    // DUSEY kenar paylari (Tekla 144 Picture tab: upper/lower edge distance).
    // <0 = plateExtra kullan (geriye uyumlu, eski dosyalarda alan yok). Ayrı
    // ust/alt pay levha yuksekligini secHeight+top+bottom yapar ve levha
    // merkezini kesit merkezinden (top-bottom)/2 kadar kaydirir.
    double topExtra{-1.0};         // UST kenar payi (mm, LEGACY: pozitif = disari buyut)
    double bottomExtra{-1.0};      // ALT kenar payi (mm, LEGACY: pozitif = disari buyut)
    // --- Tekla 144 Picture: upper/lower edge distance (Milestone 2) ---------
    // Tekla isaret sozlesmesi: POZITIF = levha kenarini ICERI ceker (levhayi
    // kucultur), NEGATIF = DISARI tasir (levhayi buyutur). KESIN olarak legacy
    // topExtra/bottomExtra'dan AYRIDIR (orada pozitif = buyut). < -1e8 (sentinel)
    // = tanimsiz -> legacy topExtra/bottomExtra/plateExtra kullanilir (eski
    // dosyalar kayipsiz acilir, ters isaret hatasi OLMAZ).
    double upperEdgeDistance{-1e9};  // levha ust kenari - kesit ust kenari (Tekla)
    double lowerEdgeDistance{-1e9};  // levha alt kenari - kesit alt kenari (Tekla)
    // Acik levha boyutlari (0 = otomatik). Otomatik boyut ile manual override
    // birbirini sessizce ezmez: override>0 ise otomatik boyut uretimi atlanir.
    double plateWidthOverride{0.0};
    double plateHeightOverride{0.0};
    double gap{0.0};               // kolon yuzeyi ile levha arasi bosluk (mm, Tekla 101: 2 mm)
    double boltDiameter{20.0};     // bulon capi (M20)
    double holeDiameter{22.0};     // delik capi (M20 -> 22)
    std::string boltGrade{"8.8"};  // bulon sinifi (etiket)
    int boltCountX{2};             // levha genislik yonunde bulon adedi (genelde 2)
    int boltCountY{2};             // levha yukseklik yonunde bulon adedi (2 veya 4)
    double boltEdge{30.0};         // levha kenarindan bulon merkezine (mm)
    // --- Tekla "Bolt group dimensions" (Bulonlar sekmesi) -------------------
    // Default'lar mevcut davranisi korur: yatay ortalanmis (Orta, offset 0,
    // aralik otomatik flans/2), dikey alt kenardan boltEdge (Alt, offset<0).
    int boltAlignH{1};             // yatay olcum: 0=Sol, 1=Orta, 2=Sag
    int boltAlignV{2};             // dikey olcum: 0=Ust, 1=Orta, 2=Alt
    double boltOffsetH{0.0};       // yatay konum olcusu (mm; Sol/Sag'da <0 = boltEdge)
    double boltOffsetV{-1.0};      // dikey konum olcusu (mm; Ust/Alt'ta <0 = boltEdge)
    double boltSpacingX{0.0};      // yatay aralik (0 = otomatik flans/2)
    double boltSpacingY{0.0};      // dikey aralik (0 = otomatik levha-2*boltEdge)
    // DEGISKEN bulon araliklari (Tekla "each space between bolts"): N bulon icin
    // N-1 ayri aralik. Bos ise sabit boltSpacingX/Y (veya otomatik) kullanilir.
    std::vector<double> boltSpacingXs;
    std::vector<double> boltSpacingYs;
    std::vector<int> deletedBolts; // silinen bulon numaralari (1-based)
    bool hasHead{true};
    bool hasNut{true};
    bool hasWasher{true};
    // --- Tekla "Bolt basic properties" + "Bolting direction" (bulon sekmesi) ---
    std::string boltStandard{};     // bulon standardi (katalog)
    bool threadInMat{false};        // disin bulonlu parcada olup olmadigi
    int siteWorkshop{0};            // 0=Site, 1=Workshop
    int boltCutLengthMode{0};       // 0=tek flans, 1=cift flans (grip derinligi)
    double boltLengthIncrease{0.0}; // bulon boyu artisi (mm)
    int boltDirection{1};           // 1=bas kolona (-eksen), 2=bas kirise (+eksen)
    std::string boltComment{};      // aciklama
    bool operator==(const EndPlateParams&) const = default;
};

// Yerlesim sonucu: levha merkezi + ortonormal taban + boyutlar + KURULU bulon
// grubu (id/displayEntityId bos; Document katmani bunlari atar).
struct EndPlatePlacement {
    Vec3 plateCenter{};
    Vec3 u{1.0, 0.0, 0.0};   // levha genislik yonu (yatay)
    Vec3 v{0.0, 1.0, 0.0};   // levha yukseklik yonu (dikey)
    Vec3 axis{0.0, 0.0, 1.0}; // kiris yonu (bulon ekseni)
    double plateWidth{0.0};
    double plateHeight{0.0};
    double secWidth{0.0};          // kiris kesit genisligi (bulon flans gauge'i)
    double plateThickness{0.0};
    double colFlangeThickness{0.0}; // kolon flans kalinligi (bulon kavrama hesabi)
    double boltGripLength{0.0};     // bolon saft kavrama uzunlugu (levha+gap+flans)
    BoltGroup bolts;          // origin/axis/xDir/count/spacing/params kurulu
};

// Kalici alin levhasi baglanti kaydi. Alt nesne sahipligi:
//   plateEntityId = baglantinin urettigi levha modeli (normal model).
//   bolts         = sahip olunan bulon grubu (bolts.id kalicidir; delikler
//                   owner=bolts.id, montaj displayEntityId).
// mainPart/secondaryPart kullanici parcalaridir — baglanti silinince KORUNUR.
struct EndPlateConnection {
    EntityId id{kInvalidEntityId};
    EntityId mainPart{kInvalidEntityId};      // kolon
    EntityId secondaryPart{kInvalidEntityId}; // kiris
    EntityId plateEntityId{kInvalidEntityId}; // levha (alt nesne)
    EndPlateParams params;
    BoltGroup bolts;                          // sahip olunan bulon grubu
    bool enabled{true};
    bool operator==(const EndPlateConnection&) const = default;
};

// Kiriş ekseninden ortonormal (u, v, axis) tabani uretir: axis = normalize(beamAxis),
// u = referenceNormal x axis (yatay genislik), v = axis x u (dikey yukseklik).
// referenceNormal ~ axis ise farkli bir ref denenir (dejenere degil). Donen
// vektorler birimdir.
struct EndPlateFrame {
    Vec3 axis{0.0, 0.0, 1.0};
    Vec3 u{1.0, 0.0, 0.0};
    Vec3 v{0.0, 1.0, 0.0};
};
EndPlateFrame endPlateFrame(const Vec3& beamAxis, const Vec3& referenceNormal) noexcept;

// Alin levhasi yerlesimini hesaplar. Girdiler: kirisin uc noktasi (beamEnd) ve
// kiris yonu (beamAxis), kiris kesit bbox'i (secWidth x secHeight, mm), parametreler.
// Cikti: levha merkezi/tabanı/boyutlari + KURULU bulon grubu (origin = levhanin
// sol-alt kosesi, countX/countY/spacing parametrelerden). countX/countY <= 0 ise 1,
// spacing degerleri (count>1) levha boyutlarindan turetilir.
EndPlatePlacement computeEndPlatePlacement(const Vec3& beamEnd, const Vec3& beamAxis,
                                           double secWidth, double secHeight,
                                           const EndPlateParams& params) noexcept;

// ALIN LEVHASI YERLESIMI — kolonun GERCEK temas yuzeyine gore (Phase R1).
// `face`: kolonun kirise bakan sinir duzlemi (normal, kolondan DISARI/kirise
// dogru bakar). Levhanin kolon tarafindaki yuzu `face.origin + gap * face.normal`
// uzerine oturur; levha kalinligi `face.normal` yonunde (kirise dogru) acilir.
// Boylece gap=0 iken levha yuzu kolon yuzuyle CAKISIR, kolonun icine girmez.
// (Egik kiris adaptasyonu sonraki dilimde.)
//
// SECTIONANCHOR DUZELTMESI: levha, kiris EKSENINE (anchor cizgisi) DEGIL,
// kirisin GERCEK kesit MERKEZINE oturur. `sectionHeightDir` (birim) kirisin
// kesit yukseklik yonu (profileSectionFrame(...).height), `anchorOffset` ise
// kesit merkezinin eksene gore bu yondeki ISARETLI kaymasi
// (profileSectionAnchorOffset: Top -> -h/2, Center -> 0, Bottom -> +h/2).
// Levha merkezi `face.origin + normal*(gap+t/2) + sectionHeightDir*anchorOffset`
// olur. Varsayilanlar (offset 0) eski "eksen uzerinde ortalama" davranisini
// korur — yalnizca anchor bilinçli cagiran (resolveEndPlatePlacement) kaydirir.
EndPlatePlacement computeEndPlatePlacementOnFace(
    const ReferencePlane& face, double gap, double secWidth, double secHeight,
    const EndPlateParams& params, const Vec3& sectionHeightDir = Vec3{0.0, 0.0, 1.0},
    double anchorOffset = 0.0, const Vec3& sectionWidthDir = Vec3{0.0, 0.0, 0.0}) noexcept;

} // namespace mm
