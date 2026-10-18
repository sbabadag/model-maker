#pragma once

// PARAMETRIK BULON (Phase B1) — saf veri modeli + yardimcilar.
//
// Tekla "bolt group" karsiligi; Single Bolt ilk dilimde bir grup = tek bulon.
// Bulon GRUBU kalici bir kayit olarak tutulur (kendi EntityId'si) ve bagli
// parcalara EntityId ile referans verir. Delikler, mevcut CutModifier zinciri
// yeniden kullanilarak CutKind::Hole turuyle uretilir — ayri bir "gorsel siyah
// daire" YOK, gercek B-Rep silindir kesimi vardir. Bulonun GORSEL geometrisi
// (bas + govde + somun + pul) ayri bir display varligidir (displayEntityId);
// baglanti verisi (hangi parcalar, eksen, cap) burada tasinir.
//
// windows.h yok; Linux'ta derlenir/test edilir. Gercek B-Rep deligi OCC
// katmaninda acilir (occ_geometry.cpp, CutKind::Hole).

#include "model_maker/cut_modifier.hpp"
#include "model_maker/entity_id.hpp"
#include "model_maker/geometry.hpp"

#include <string>
#include <vector>

namespace mm {

// Bulon parametreleri (Tekla Bolt properties karsiligi; sadeleltirilmis).
struct BoltParams {
    double nominalDiameter{20.0};   // M20 -> 20 mm (govde/oks capi)
    double holeDiameter{22.0};      // delik capi (M20 -> 22 mm normal bosluk)
    std::string grade{"8.8"};       // bulon sinifi (etiket/display)
    bool hasHead{true};             // bas
    bool hasNut{true};              // somun
    bool hasWasher{true};           // pul
    bool operator==(const BoltParams&) const = default;
};

// Bulon elemanlarinin standart boyutlari (nominal captan turetilir).
// ISO 4014/4017 (altigen bas), ISO 4032 (altigen somun), ISO 7089 (pul)
// yaklasik degerleri. YALNIZ goruntuleme geometrisi icindir; gercek delik
// yalnizca holeDiameter ile acilir (hesaplamada kullanilmaz).
struct BoltDimensions {
    double headAcrossFlats{};       // bas kosegen (s = 1.5*d)
    double headHeight{};            // bas yuksekligi (k = 0.7*d)
    double nutAcrossFlats{};        // somun kosegen (s = 1.5*d)
    double nutHeight{};             // somun yuksekligi (m = 0.8*d)
    double washerOuterDiameter{};   // pul dis cap (D = 2*d)
    double washerThickness{};       // pul kalinligi (t = 0.15*d)
};
BoltDimensions boltDimensions(double nominalDiameter) noexcept;

// Kalici bulon grubu (Single Bolt: bir bulon).
struct BoltGroup {
    EntityId id{kInvalidEntityId};              // bulon grubunun kendi kimligi
    std::vector<EntityId> parts;                // bagli parcalar (ilk = main)
    Vec3 origin{};                              // bulon merkezi (mm)
    Vec3 axis{0.0, 0.0, 1.0};                   // bulon ekseni (birim)
    Vec3 xDir{1.0, 0.0, 0.0};                   // grup x ekseni (eksene dik)
    BoltParams params;
    EntityId displayEntityId{kInvalidEntityId}; // gorsel montaj varligi
    // BOLT GROUP PATTERN (rectangular array): Tekla bolt group karsiligi.
    // countX*countY adet bulon; xDir boyunca spacingX, yDir (=axis x xDir)
    // boyunca spacingY aralikla dizilir. Single Bolt = countX=countY=1,
    // spacing=0. Gecersiz (<=0) adetler hesaplamada 1'e cekilir.
    int countX{1};
    int countY{1};
    double spacingX{0.0};
    double spacingY{0.0};
    // Silinen bulonlarin 1-based numaralari (Tekla "bolts to delete" listesi).
    // Numaralama boltGroupBoltOrigins ile ayni: j*satir + i (j=0 en alt satir,
    // i soldan saga). Silinen bulonlar delik/display uretiminde atlanir.
    std::vector<int> deletedBolts;
    bool enabled{true};
    bool operator==(const BoltGroup&) const = default;
};

// Bir parca icin Hole CutModifier uretir: eksen = normalize(axis), merkez =
// origin, cap = holeDiameter. Hedef parcanin EntityId'si KORUNUR (kimlik
// degismez; yalniz geometri surumu ilerler). Gecersiz cap/eksen yine bir
// modifier dondurur ama OCC katmani base'e dokunmaz (guvenli no-op): cap <= 0
// veya eksen ~0 ise islem atlanir.
CutModifier makeHoleCutModifier(EntityId part, const Vec3& origin,
                                const Vec3& axis, double holeDiameter) noexcept;

// Bulon grubunun TUM bagli parcalari icin delik modifier listesi (her bulon
// pozisyonu x her parca icin bir delik). Pattern (countX/countY) dikkate
// alinir; bulon tasininca/parametre degisince bu liste yeniden uretilir
// (yeniden hesap). kInvalidEntityId parcalar atlanir.
std::vector<CutModifier> boltGroupHoleModifiers(const BoltGroup& bolt) noexcept;

// Bulon grubunu, bagli parcalarin birlesik sinir kutusu merkezine YEREL x
// ve/veya y ekseninde ortalayan yeni origin'i dondurur. centerX/centerY hangi
// yonde ortalama yapilacagini secer (ikisi de true = tam ortalama). Grup
// merkezi = origin + 0.5*(countX-1)*spacingX*u + 0.5*(countY-1)*spacingY*v
// (u = yerel x, v = yerel y). boundsCenter dunya koordinatinda parca kutusu
// merkezi (levha icin = yuzey ayak izi merkezi). Eksen/xDir dejenereyse
// guvenli varsayilan cerceve kullanilir.
Vec3 boltGroupCenteredOrigin(const BoltGroup& bolt, const Vec3& boundsCenter,
                             bool centerX, bool centerY) noexcept;

// Bulon grubundaki TUM bulon merkezlerini uretir (rectangular pattern):
// origin + i*spacingX*xDir + j*spacingY*yDir, i in [0,countX), j in [0,countY).
// yDir = normalize(axis) x xDir (eksene dik). countX/countY <= 0 ise 1 alinir.
std::vector<Vec3> boltGroupBoltOrigins(const BoltGroup& bolt) noexcept;

// Bulon ekseni gecerli mi (birimlestirilebilir, sifir degil).
bool isBoltAxisValid(const Vec3& axis) noexcept;

// --- STANDART BOYUT KATALOGU ------------------------------------------------
// Desteklenen nominal caplar (M12..M30). Degerler mm.
const std::vector<double>& standardBoltDiameters() noexcept;

// ISO normal bosluklu delik capi (mm) — nominal captan turetilir.
// M12->14, M16->18, M20->22, M24->26, M27->30, M30->33.
// Katalogda olmayan cap icin nominal + 2 mm (guvenli varsayilan).
double standardBoltHoleDiameter(double nominalDiameter) noexcept;

// Kalite bilgisi gecerli mi (8.8 / 10.9). Bos etiket de gecerli sayilir
// (sinif belirtilmemis). Duyarliliksiz degil; tam eslesme aranir.
bool isBoltGradeValid(const std::string& grade) noexcept;

} // namespace mm
