#pragma once
// NATIVE BINARY B-Rep SERIALIZER (Phase 3.4A).
//
// TopoDS_Shape <-> binary blob donusumu. OCCT'nin IKILI (binary) B-Rep
// formatini kullanir: `BinTools::Write` / `BinTools::Read`. (Dikkat:
// `BRepTools::Write` ASCII/TEXT uretir, IKILI DEGILDIR — bu yuzden burada
// BinTools kullanilir.)
//
// Bu baslik YALNIZCA OCC-acik derleme birimleri tarafindan include edilir ve
// OCCT basliklari icerir. `document.hpp` bu basligi GORMEZ ve ICERMEZ —
// Document OCC'siz kalir (sozlesme korunur). MMW8 entegrasyonu (ileri faz)
// bu serilestirmeyi OCC'siz bir kanca uzerinden Document'a ulastiracaktir.
//
// FORMAT SOZLESMESI (acikca tanimli):
//   * Serilestirici: BinTools (OCCT ikili B-Rep), `BinTools_FormatVersion_CURRENT`
//     (su an VERSION_4; BinTools_FormatVersion.hxx'ten). Bu surum blob'un ICINE
//     gomulur ve okurken taninir.
//   * Triangulation: theWithTriangles = TRUE  (yuz tesselasyonu blob'a yazilir;
//     yuklemede yeniden meshing gerektirmez, hizli gorsellestirme).
//   * Normals:       theWithNormals   = FALSE (analitik geometri normalleri
//     yeniden hesaplar; yalniz triangulation-only geometride gerekir).
//   * OCCT surumu:   blob'un ICINE gomulmez; ayri metadata olarak `occVersionString()`
//     ile disari verilir (MMW8 dosya basligi bunu tasir — bkz. Phase 3.4B+).
//
// Sinirlar: blob boyutu `maxBRepBlobSize()` ile ustten sinirli (kaynak
// tuketimi korumasi). Gecersiz/bos geometri acikca reddedilir.

#ifdef MM_HAS_OCC

#include <TopoDS_Shape.hxx>

#include <cstddef>
#include <string>

namespace mm {

// Bu build'in OCCT surum dizgisi (or. "7.9.2"). Blob'a gomulmez; MMW8 dosya
// basliginda dis metadata olarak tasinir (capraz-surum teshis icin).
const char* occVersionString() noexcept;

// Bu serilestiricinin yazdigi BinTools binary format surumu
// (BinTools_FormatVersion_CURRENT). Blob icine gomulur.
int bRepFormatVersion() noexcept;

// Kabul/uretim icin ust blob boyut siniri (bayt). Kaynak tuketimi korumasi.
std::size_t maxBRepBlobSize() noexcept;

// TopoDS_Shape -> binary blob. Basarisizlikta false doner ve `error` (null
// degilse) insanca-okunur bir neden alir. Null sekil veya akim hatasi reddedilir.
bool serializeBRep(const TopoDS_Shape& shape, std::string& blob,
                   std::string* error = nullptr);

// Binary blob -> TopoDS_Shape. Basarisizlikta (bos/bozuk/asiri-buyuk blob,
// akim hatasi, OCCT istisnasi) false doner ve `shape` DOKUNULMAZ birakilir.
// Basarida `shape` null olmayan bir sekildir; BRepCheck dogrulamasi AYRI
// `validateBRep` ile yapilir (hafif okuma ile tam dogrulama ayri tutulur).
bool deserializeBRep(const std::string& blob, TopoDS_Shape& shape,
                     std::string* error = nullptr);

// BRepCheck_Analyzer ile geometri gecerliligi. Null veya gecersiz sekilde false.
bool validateBRep(const TopoDS_Shape& shape);

} // namespace mm

#endif // MM_HAS_OCC
