# B-Rep serilestirme + MMW8 (Phase 3.4A)

## BinTools vs BRepTools (KRITIK ayrim)

- `BRepTools::Write/Read` -> **ASCII/TEXT** B-Rep (insan-okunur, daha buyuk).
- `BinTools::Write/Read`  -> **IKILI (binary)** B-Rep. Kalicilik icin BUNU kullan.
- Imzalar (OCCT 7.9):
    `BinTools::Write(shape, Standard_OStream&, withTriangles, withNormals, BinTools_FormatVersion, Message_ProgressRange)`
    `BinTools::Read(TopoDS_Shape&, Standard_IStream&, Message_ProgressRange)`
- `Standard_OStream = std::ostream`, `Standard_IStream = std::istream` -> `std::ostringstream`/`std::istringstream` kullan.
- `BinTools_FormatVersion_CURRENT` = VERSION_4 (BinTools_FormatVersion.hxx). Blob'a gomulur, okurken taninir.
- BinTools sembolleri `libTKBRep.so`'da (TKernel DEGIL).

## Standard_Failure tuzağı (KANITLI crash)

`Standard_Failure : public Standard_Transient` — **`std::exception`'dan TUREMEZ.**
`catch (const std::exception&)` OCCT istisnalarini YAKALAMAZ; yakalanmayan
`Standard_Failure` `std::terminate` -> SIGABRT (exit -6). BinTools::Read bozuk
blob'da Standard_Failure firlatir. Serializer'da `catch (const Standard_Failure& ex)`
ayri blok olarak `catch (const std::exception&)` + `catch (...)` ile birlikte olmali;
mesaj `ex.GetMessageString()` (Standard_CString = const char*).

## Serializer API (occ_brep_serialize.hpp/.cpp, MM_HAS_OCC guard'lı)

- `serializeBRep(shape, blob, error)` -> bool (null sekil/akim hatasi reddeder).
- `deserializeBRep(blob, shape, error)` -> bool (bos/boyut-siniri/bozuk/istisna reddeder; basarisizlikta `shape` DOKUNULMAZ).
- `validateBRep(shape)` -> bool (BRepCheck_Analyzer).
- `occVersionString()` -> OCC_VERSION_COMPLETE (or. "7.9.2") — blob'a gomulmez, MMW8 basliginda dis metadata.
- `bRepFormatVersion()` -> BinTools_FormatVersion_CURRENT (4).
- `maxBRepBlobSize()` -> 64 MiB (kaynak tuketimi korumasi).
- Format secimi ACIKCA: withTriangles=TRUE (hizli goruntuleme), withNormals=FALSE.
- `document.hpp` bu basligi ICERMEZ — OCC'siz kalir (sozlesme). MMW8 entegrasyonu (3.4B+) OCC'siz bir kanca uzerinden yapilacak.

## Paralel boolean motoru — bagimsiz test ikililerinde guard SART (KANITLI)

OCCT'nin paralel boolean motoru (`BOPTools_Parallel::Perform`) is parcacigi yarisinda
nondeterministik segfault uretir (Extrema_ExtCC / yuz siniflandirma); belirti YALNIZ
Windows'ta, gdb DISINDA gorulur (commit 7ad0c13: End Plate bulon deligi kesimi).
Kok cozum `BOPAlgo_Options::SetParallelMode(Standard_False)` (idempotent, global) —
ama bu guard YALNIZ `makeOccSolidStore()` icinde oldugu icin, magaza YARATMAYAN
bagimsiz OCC test ikilileri (or. occ_brep_serialize_test, occ_smoke, occ_cut_probe)
boolean Fuse/Cut/Common'i paralel motorda kosar ve ayni yarisa aciktir. Boyle bir test
main() basinda guard'i AYRI cağirmali. Linux'ta (OCCT 7.9.x, TBB'siz) yaris belirtisi
COGUNLUKLA gorulmez -> Linux testinin yesil olmasi Windows duzeltmesini KANITLAMAZ;
Windows'ta art arda 20 kosu + tam ctest ile dogrula.

## Round-trip testi (tests/occ_brep_serialize_test.cpp + run_occ_brep_serialize.py)

- Bagimsiz: yalniz serializer + OCCT ilkelleri (occ_geometry GEREKMEZ).
- Dogrulama HACIM'e indirgenmez: tip (ShapeType) + hacim (rel tol) + bbox + topolojik sayi (V/E/F/S) + BRepCheck + boolean kullanilabilirlik.
- Boolean kullanilabilirlik testi icin compound AYRIK katilar olmali (cakisan katilar BRepAlgoAPI_Fuse'u bozar).
- ctest kaydi: `elseif(UNIX AND NOT CMAKE_CROSSCOMPILING)` dalinda (MM_SYSTEM_OCC_INCLUDE) — model_maker_occ library OCC-kapali build'de kurulmaz.
