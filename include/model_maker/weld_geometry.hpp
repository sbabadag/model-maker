#pragma once
#ifdef MM_HAS_OCC

// GERCEK 3B KAYNAK KATISI (Phase W3): iki B-Rep parca arasindaki FILLET kaynagini
// OpenCASCADE ile uretir. Bu modul YALNIZCA OCC-acik derleme birimlerinde ortaya
// cikar (weld.hpp veri modeli OCCT'siz kalir).
//
// KAPSAM (dürüst): yalniz dogrulanmis DOGRUSAL fillet kaynaklari katı olarak
// uretilir. Egrisel/karmasik birlesimler, belirsiz coklu kenarlar ve butt kaynagi
// (agiz/nufuziyet bilinmedigi icin) katı URETILMEZ — geometrySupported=false kalir.
//
// Kaynak olcusu (size) BACAK (leg) olarak yorumlanir (esit bacakli kose kaynagi);
// throat kullanilmak isteniyorsa cagiran leg = throat*sqrt(2) donusumu yapmalidir.

#include "model_maker/geometry.hpp"
#include "model_maker/weld.hpp"

#include <TopoDS_Shape.hxx>

#include <string>
#include <vector>

namespace mm {

// Bir kaynak guzergahi (junction): iki parcanin dogrusal temas kenari.
struct WeldJunction {
    bool valid{false};
    Vec3 start{};        // kaynak baslangic noktasi
    Vec3 end{};          // kaynak bitis noktasi
    Vec3 direction{1, 0, 0};  // birim
    double length{0.0};
    Vec3 mainNormal{};   // ana parcanin temas yuzu normali (birim; disa yonlu)
    Vec3 secNormal{};    // ikincil parcanin temas yuzu normali (birim; disa yonlu)
    std::string reason;  // gecersizse nedeni
};

// Dogrusal temas kenarlarini bulur: `from` seklinin, `onto` seklinin YUZEYINE
// yatan (tol icinde) DUZ kenarlari. Uzunluga gore azalan sirada doner. Her
// sonucun baslangic/bitis + yonu tutarlidir (start->end = direction*length).
std::vector<WeldJunction> findWeldJunctions(const TopoDS_Shape& from,
                                            const TopoDS_Shape& onto,
                                            double tol);

// En uygun tek guzergahi secer (en uzun temas kenari). Normal'leri de doldurur.
// Birden fazla PARALEL kenar varsa yalniz en uzununu doner (cift tarafli icin
// cagiran findWeldJunctions'in tum listesini kullanir).
WeldJunction findBestWeldJunction(const TopoDS_Shape& main,
                                  const TopoDS_Shape& secondary,
                                  double tol);

// Esit bacakli FILET kaynagi katisini uretir: `start`->`end` dogrusu boyunca,
// mainNormal/secNormal'in tanimladigi kosede bacak=leg olan dik ucgen kesit
// prism (sweep) edilir. `aboveSide=true` kosede ust taraf (iki normalin acisortay
// tarafi); false ise karsit (asagi). Gecersiz geometride Null shape doner.
// Hacim (90° kose icin) = 0.5 * leg^2 * length.
TopoDS_Shape buildFilletWeldSolid(const Vec3& start, const Vec3& end,
                                  const Vec3& mainNormal, const Vec3& secNormal,
                                  double leg, bool aboveSide);

// Bir weld kaydi icin katilari uretir (main/sec parca B-Rep'lerinden). Basarili
// olursa solids (1 = tek tarafli, 2 = cift tarafli) + geometrySupported mantigi
// icin `outUnsupportedReason` bos kalir. Basarisizlikta bos vektor + neden.
std::vector<TopoDS_Shape> buildWeldSolids(const Weld& w,
                                          const TopoDS_Shape& mainShape,
                                          const TopoDS_Shape& secondaryShape,
                                          double leg,
                                          std::string& outUnsupportedReason);

} // namespace mm

#endif // MM_HAS_OCC
