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
#include <cstdint>
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

// --- Surumlu butunluk zarfi (Phase 3.4D cokme onlemi) ----------------------
//
// serializeBRep, ham BinTools payload'unu surumlu bir zarfa sarar; boylece
// deserializeBRep, baytlari BinTools::Read'e VERMEDEN ONCE butunlugu dogrular.
// (OCCT 8.0.1 bozuk binary girdide — or. BinTools_Curve2dSet::Curve2d icinde —
// erisim ihlali verir; SHA-256 on-dogrulamasi bunu onler: butunluk bozuksa
// BinTools::Read hic cagrilmaz.)
//
// Zarf v1 yerlesimi (little-endian; sabit 52 bayt baslik):
//   offset  boyut  alan
//   0       8      magic  "MMBREP01"
//   8       4      zarf surumu (uint32 LE) == kBRepEnvelopeVersion
//   12      8      payload uzunlugu (uint64 LE)
//   20      32     payload SHA-256 (ham 32 bayt)
//   52      N      ham BinTools payload
//
// Eski (zarfSIZ) bloblarla uyum: magic ile baslamayan girdi "eski" sayilir ve
// BinTools baslik on-kontrolunden sonra dogrudan okunur (bkz. deserializeBRep).
inline constexpr std::size_t kBRepEnvelopeHeaderSize = 52;
inline constexpr std::size_t kBRepEnvelopeMagicSize = 8;
inline constexpr const char* kBRepEnvelopeMagic = "MMBREP01";  // 8 bayt + NUL
inline constexpr std::uint32_t kBRepEnvelopeVersion = 1;

// Ham BinTools payload'unu surumlu zarfa sarmalar (SHA-256 hesaplar). Test ve
// ic yeniden kullanim icin acik. Normalde false donmez.
bool wrapBRepEnvelope(const std::string& rawPayload, std::string& envelope);

// TopoDS_Shape -> zarfli binary blob. Basarisizlikta false doner ve `error`
// (null degilse) insanca-okunur bir neden alir. Null sekil veya akim hatasi
// reddedilir.
bool serializeBRep(const TopoDS_Shape& shape, std::string& blob,
                   std::string* error = nullptr);

// Binary blob -> TopoDS_Shape. Zarf varsa butunluk (magic/surum/uzunluk/SHA-256)
// dogrulanir; dogrulama basarisizsa BinTools::Read HIC cagrilmadan false doner.
// Zarf yoksa (eski dosya) BinTools baslik on-kontrolunden sonra dogrudan okunur.
// Basarisizlikta `shape` DOKUNULMAZ birakilir. Basarida `shape` null olmayan bir
// sekildir; BRepCheck dogrulamasi AYRI `validateBRep` ile yapilir.
bool deserializeBRep(const std::string& blob, TopoDS_Shape& shape,
                     std::string* error = nullptr);

// BRepCheck_Analyzer ile geometri gecerliligi. Null veya gecersiz sekilde false.
bool validateBRep(const TopoDS_Shape& shape);

} // namespace mm

#endif // MM_HAS_OCC
