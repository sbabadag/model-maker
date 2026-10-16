#pragma once
#ifdef MM_HAS_OCC
// BULON GORSEL GEOMETRİSİ (Phase B1, Slice 2) — gercek OCCT B-Rep.
//
// Bulon montaji (altigen bas + silindirik govde + altigen somun + iki pul)
// DELIK EKSENIYLE TAM ESMERKEZLI uretilir. Bu yalniz GORUNTULEME geometrisidir;
// gercek baglanti verisi BoltGroup'ta, gercek delik CutKind::Hole ile acilir.
// Helisel vida disi URETILMEZ (performans; basitlestirilmis geometri).
//
// Eksen kurali: bolt.axis birim yon = "bas tarafindan somun tarafina" yon.
// origin = bagli levha yigininin (grip) ortasi. Montaj bu origin'e gore
// simetrik uzanir: [-grip/2 - waster - head] ... [+grip/2 + waster + nut].
//
// windows.h yok; Linux'ta gercek OCCT ile derlenir/test edilir.

#include "model_maker/bolt.hpp"

#include <TopoDS_Shape.hxx>

namespace mm {

// Altigen prizma: merkez `center`, eksen `axis` (birim), kosegen (across-flats)
// `acrossFlats`, yukseklik `height`. Priizma eksen boyunca ortalanir
// (center ± height/2). Dejenere eksen/cap icin NULL sekil dondurur.
TopoDS_Shape boltHexPrism(const Vec3& center, const Vec3& axis,
                          double acrossFlats, double height);

// Ince disk (pul): merkez `center`, eksen `axis`, dis cap `outerDiameter`,
// kalinlik `thickness`. Eksen boyunca ortalanir.
TopoDS_Shape boltWasherDisc(const Vec3& center, const Vec3& axis,
                            double outerDiameter, double thickness);

// Silindirik govde: radius = nominalDiameter/2, eksen boyunca gripLength uzun,
// origin merkezli.
TopoDS_Shape boltShank(const Vec3& origin, const Vec3& axis,
                       double nominalDiameter, double gripLength);

// TAM bulon montaji (fuse edilmis tek kati). `gripLength` = bagli levha
// yigininin eksen boyunca toplam kalinligi (mm). Bilesenler (bas -> somun):
//   bas (altigen, k=0.7d) + pul (t=0.15d, D=2d) + govde (d cap, grip)
//   + pul + somun (altigen, m=0.8d). hasHead/hasNut/hasWasher bayraklari
//   ilgili bileseni atlar. Gecersiz eksen/cap ise NULL sekil dondurur.
TopoDS_Shape buildBoltAssemblyShape(const BoltGroup& bolt, double gripLength);

} // namespace mm
#endif // MM_HAS_OCC
