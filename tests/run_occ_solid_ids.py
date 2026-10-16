#!/usr/bin/env python3
"""OCC SAHIPLIK REGRESYON HARNESS'I (kaynak-govde cikarimi + GERCEK OCCT).

NE YAPAR
--------
`src/application.cpp` icindeki GERCEK uretim govdelerini canli kaynaktan
marker ile cikarir, bir OCC test sinifi (HarnessApp) icine yerlestirir, GERCEK
OCCT ile derler ve `occShapes_` (EntityId -> TopoDS_Shape) yasam dongusunu
calistirarak dogrular.

Cikarilan govdeler (canli kaynaktan, birebir):
  * addStyledModel      (model ekleme SIRASI: pushUndoSnapshot + addModel)
  * addCube             (seklin YENI varligin kimligiyle anahtarlanmasi)
  * addCylinder
  * addBooleanFuse / addBooleanCommon / addBooleanCut
  * lastLiveSolids      (canli kati secimi)
  * clearOccSolidStore
  * pushUndoSnapshot    (tek satir; addStyledModel bagimliligi)

STUB'LAR (uretim degil — harness saglar)
----------------------------------------
  * EditMode / TransformCommand enum'lari: gercek tanim `renderer.hpp`'de ve o
    baslik kosulsuz `windows.h` icerdigi icin Linux'ta include EDILEMEZ. Ayni
    uye adlariyla minimal stub verilir; adlar degisirse derleme PATLAR.
  * cancelTransformCommand / cancelDrawing / updateStatus / publishStatus:
    UI yan etkileri; geometri/kimlik davranisina girmezler.
  * currentEntityProperties: stil varsayilanlari (renk/katman). Kimlik
    atamasi, ekleme sirasi veya Boolean operand secimi BURADA YAPILMAZ —
    onlar gercek Document ve gercek govdelerdedir.

SINIR (durustce)
----------------
Bu bir **hedefli kaynak-govde regresyon testidir**. Gercek `Application`
sinifinin TAMAMINI (Qt/Win32 GUI, olay dongusu, dosya diyaloglari) calistirmaz;
dolayisiyla TAM entegrasyon testi DEGILDIR. GUI'ye bagli cagri yollari (dosya
acma gibi) burada yalniz kaynak-sekil kontrolu ile dogrulanir.

Kullanim:
  run_occ_solid_ids.py <build_dir> [--cxx PATH] [--occt-include DIR]
                       [--occt-libdir DIR] [--no-shape-check]
"""
import os
import re
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
APP_CPP = os.path.join(ROOT, "src", "application.cpp")
DOC_CPP = os.path.join(ROOT, "src", "document.cpp")

# Cekirdek kaynaklar (CMakeLists `model_maker_core` + OCC modulu). Liste
# degisirse LINK HATASI verir — sessiz gecmez.
CORE_SOURCES = [
    "src/geometry.cpp", "src/grid.cpp", "src/camera.cpp", "src/view_cube.cpp",
    "src/ribbon_layout.cpp", "src/performance.cpp", "src/dxf.cpp",
    "src/document.cpp", "src/drafting.cpp", "src/profile_database.cpp",
    "src/occ_geometry.cpp", "src/cut_modifier.cpp", "src/bolt.cpp",
    # Document-sahipli opak magaza: no-op saglayici (core) + GERCEK OCCT store.
    "src/occ_store_null.cpp", "src/occ_solid_store.cpp",
    # MMW8 (Phase 3.4B/3.4C): OCCT'siz codec + SHA-256 + native B-Rep serializer.
    "src/mmw8_codec.cpp", "src/sha256.cpp", "src/occ_brep_serialize.cpp",
]

OCC_LIBS = ["TKernel", "TKMath", "TKG2d", "TKG3d", "TKGeomBase", "TKBRep",
            "TKGeomAlgo", "TKTopAlgo", "TKPrim", "TKBO", "TKBool",
            "TKShHealing", "TKMesh", "TKXSBase", "TKDESTEP"]

# Cikarilacak uretim govdeleri: (anahtar adi, imza marker'i)
TARGETS = [
    ("pushUndoSnapshot", "void Application::pushUndoSnapshot()"),
    ("addStyledModel", "void Application::addStyledModel(WireframeModel model)"),
    ("addCube", "void Application::addCube()"),
    ("addCylinder", "void Application::addCylinder()"),
    ("addBooleanFuse", "void Application::addBooleanFuse()"),
    ("addBooleanCommon", "void Application::addBooleanCommon()"),
    ("addBooleanCut", "void Application::addBooleanCut()"),
    ("lastLiveSolids",
     "std::vector<TopoDS_Shape> Application::lastLiveSolids(std::size_t count) const"),
    # BELGE-SAHIPLI MAGAZA KOPRUSU (opak OccStore): gercek govdeler cikarilir;
    # ID atamasi/siralama/operand secimi BURADA TAKLIT EDILMEZ — hepsi gercek
    # Document + gercek store uzerinde kosar.
    ("occSolidStore", "OccSolidStore* Application::occSolidStore() noexcept"),
    ("occSolidStoreConst", "const OccSolidStore* Application::occSolidStore() const noexcept"),
    ("putOccSolid", "void Application::putOccSolid(std::size_t index, const TopoDS_Shape& shape)"),
    ("occSolidCount", "std::size_t Application::occSolidCount() const noexcept"),
    ("findOccSolid", "const TopoDS_Shape* Application::findOccSolid(std::size_t index) const"),
]


class HarnessError(Exception):
    pass


def find_span(text, sig, name):
    """Imza marker'indan baslayip eslesen `}`'e kadar olan (start, end) aralik.

    Yorum ve string literalleri atlanir. Marker bulunamaz veya parantezler
    dengesizse ACIKCA hata verir (sessiz gecis YOK)."""
    i = text.find(sig)
    if i < 0:
        raise HarnessError(f"MARKER NOT FOUND ({name!r}): {sig!r}")
    if i > 0 and text[i - 1] != "\n":
        raise HarnessError(f"marker {name!r} satir basinda degil — kaynak degismis olabilir")
    j = text.find("{", i + len(sig))
    if j < 0:
        raise HarnessError(f"no opening brace for {name!r}")
    depth = 0
    k = j
    n = len(text)
    while k < n:
        c = text[k]
        if c == "/" and k + 1 < n and text[k + 1] == "/":
            nl = text.find("\n", k)
            k = n if nl < 0 else nl
            continue
        if c == "/" and k + 1 < n and text[k + 1] == "*":
            e = text.find("*/", k + 2)
            k = n if e < 0 else e + 2
            continue
        if c == '"':
            k += 1
            while k < n and text[k] != '"':
                k += 2 if text[k] == "\\" else 1
            k += 1
            continue
        if c == "'":
            k += 1
            while k < n and text[k] != "'":
                k += 2 if text[k] == "\\" else 1
            k += 1
            continue
        if c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                return i, k + 1
        k += 1
    raise HarnessError(f"unbalanced braces for {name!r}")


def extract_bodies(text):
    bodies = {}
    for name, sig in TARGETS:
        i, k = find_span(text, sig, name)
        bodies[name] = text[i:k]
    return bodies


def assert_source_shape(bodies, text):
    """Cikarilan govdelerin BEKLENEN (duzeltilmis) bicimde oldugunu kilitler.
    Biri geri donerse test ACIKCA basarisiz olur — sessizce gecmez."""
    errs = []

    def need(name, needle):
        if needle not in bodies[name]:
            errs.append(f"{name}: beklenen ifade yok -> {needle!r}")

    def forbid(name, needle):
        if needle in bodies[name]:
            errs.append(f"{name}: YASAKLI (eski hatali) ifade var -> {needle!r}")

    # KARARLI KIMLIK: sekil, varlik INDEKSININ KIMLIGI altina yazilir (opak
    # store koprusu). Indeks-tabanli ham harita YASAK.
    for name in ("addCube", "addCylinder"):
        need(name, "putOccSolid(document_.models().size() - 1, shape);")
    for name in ("addBooleanFuse", "addBooleanCommon", "addBooleanCut"):
        need(name, "putOccSolid(document_.models().size() - 1, result);")
    # Eski off-by-one: ekleme ONCESI `emplace(models().size()-1, ...)` YASAK.
    for name in ("addCube", "addCylinder", "addBooleanFuse",
                 "addBooleanCommon", "addBooleanCut"):
        forbid(name, "emplace(")
    # Ekleme SIRASI: addStyledModel cagrisi store yazimindan ONCE olmali.
    for name in ("addCube", "addCylinder"):
        a = bodies[name].find("addStyledModel(")
        b = bodies[name].find("putOccSolid(")
        if a < 0 or b < 0 or a > b:
            errs.append(f"{name}: addStyledModel store yazimindan SONRA (off-by-one riski)")
    # MAGAZA KOPRUSU: Application'in AYRI haritasi YOK; tek kaynak Document.
    need("occSolidStore", "mm::asSolidStore(&document_.occStore());")
    # Phase 3.2B: putOccSolid ON-GORUNTU alir ve geometri deltasini KAYDEDER
    # (surum degisikligi sessizce undo disinda kalmaz).
    need("putOccSolid", "const EntityId id = document_.modelId(index);")
    need("putOccSolid", "const VersionId before = store->activeVersion(id);")
    need("putOccSolid", "const VersionId after = store->set(id, shape);")
    need("putOccSolid", "document_.recordGeometryDelta(id, before, after);")
    forbid("putOccSolid", "store->set(document_.modelId(index), shape)")
    need("occSolidCount", "store->size()")
    need("findOccSolid", "store->find(document_.modelId(index))")
    # lastLiveSolids yalniz CANLI varliklari dondurmeli (filtre Document'tan).
    need("lastLiveSolids", "document_.indexOfEntity(id).has_value()")
    need("lastLiveSolids", "store->lastLive(")
    # addStyledModel gercek model ekleme yolunu kullanmali.
    need("addStyledModel", "document_.addModel(std::move(model));")
    # BELGE YASAM DONGUSU: B-Rep temizligi ARTIK DOCUMENT'TA. Application'da
    # AYRI temizleyici KALMAMALI (iki yerde elle esleme riski).
    if "clearOccSolidStore" in text:
        errs.append("application.cpp hala clearOccSolidStore iceriyor "
                    "(magaza artik Document-sahipli)")
    for fn_sig, name, needle in (
            ("void Application::newDocument()", "newDocument", "document_.clear()"),
            ("void Application::openDocument()", "openDocument", "document_.load(")):
        try:
            i, k = find_span(text, fn_sig, name)
        except HarnessError as exc:
            errs.append(str(exc))
            continue
        if needle not in text[i:k]:
            errs.append(f"{name}: belge yasam dongusu cagrisi YOK ({needle!r}) "
                        "(magaza temizligi Document'a devredildi)")
    # Phase 3.2B: profil atamanin 1:1 (existingSolid) dali artik undo kaydi ACAR
    # (yoksa B-Rep + tel kafes degisikligi undo disinda kalirdi).
    idx = text.find("if (existingSolid) {")
    if idx < 0:
        errs.append("assignProfileToSelection: existingSolid dali bulunamadi")
    else:
        seg = text[idx:idx + 700]
        if "pushUndoSnapshot();" not in seg:
            errs.append("assignProfileToSelection: existingSolid dali undo kaydi "
                        "ACMIYOR (pushUndoSnapshot yok)")
    if errs:
        raise HarnessError("kaynak-sekil kontrolu BASARISIZ:\n  - " + "\n  - ".join(errs))


def assert_document_shape(doc_text):
    """document.cpp (Phase 3.2B) sozlesmesini kilitler: geometri deltasi kaydi
    (sessiz dusme YOK) + undo/redo icin IKI FAZLI atomik dogrulama."""
    errs = []

    def need(needle, what):
        if needle not in doc_text:
            errs.append(f"document.cpp: {what} YOK -> {needle!r}")

    # delta kaydi acik kayit yoksa KENDI kaydini acar (sessiz dusme YOK)
    need("if (undoStack_.empty()) {",
         "recordGeometryDelta kayit acmiyor (sessiz dusme riski)")
    need("void Document::recordGeometryDelta(EntityId entity, VersionId before, VersionId after)",
         "recordGeometryDelta tanimi yok")
    # ayNI varlik: ILK before, SON after
    need("rec.geometryAfter[i].version = after;",
         "ayni varlikta ILK-before/SON-after kurali yok")
    # iki fazli atomik dogrulama
    need("findVersionExists(ref.entity, ref.version)", "undo/redo on-dogrulamasi yok")
    need("occStore_->restoreActive(ref.entity, ref.version)", "geometri geri yukleme yok")
    # 'recordHasEffect' geometri deltasini da sayar (akis gorunurlugu)
    need("!rec.geometryBefore.empty()", "geometri deltasi undo gorunurlugune dahil degil")
    # Phase 3.2C: SURUM BUDAMA — canli referans kumesi hesaplanir ve magazaya
    # `keep` yuklemi olarak iletilir; tetik noktalari (pushSnapshot / redo /
    # clearHistory) budamayi cagirmali.
    need("void Document::pruneDeadVersions()", "pruneDeadVersions tanimi yok")
    need("occStore_->prune(", "magaza budama cagrisi yok (occStore_->prune)")
    need("for (const EntityId e : modelIds_)", "canli varliklarin etkin surumu korunmuyor")
    need("op.beforeIds", "kimlikle referanslanan varliklarin korunmasi yok (silme undo'su)")
    need("rec.geometryBefore", "undo/redo geometryBefore korunmuyor")
    if doc_text.count("pruneDeadVersions();") < 2:
        errs.append("document.cpp: budama tetik noktalari eksik (pushSnapshot/redo/clearHistory)")
    if errs:
        raise HarnessError("document.cpp sekil kontrolu BASARISIZ:\n  - " + "\n  - ".join(errs))


CPP_PREAMBLE = r"""
// !!! OTOMATIK URETILDI — tests/run_occ_solid_ids.py tarafindan.
// Uretim govdeleri src/application.cpp'den BIREBIR cikarilmistir.
#include "model_maker/document.hpp"
#include "model_maker/geometry.hpp"
#include "model_maker/occ_geometry.hpp"
#include "model_maker/entity_id.hpp"
#include "model_maker/occ_store.hpp"
#include "model_maker/occ_solid_store.hpp"

#include <TopoDS_Shape.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <GProp_GProps.hxx>
#include <Bnd_Box.hxx>
#include <BRepBndLib.hxx>
#include <Standard_Version.hxx>

#include <map>
#include <set>
#include <limits>
#include <vector>
#include <string>
#include <cstdio>
#include <cwchar>
#include <cmath>
#include <iterator>

using namespace mm;

// --- Uretim enum STUB'lari ------------------------------------------------
// Gercek tanim renderer.hpp'de; o baslik kosulsuz windows.h icerdigi icin
// Linux'ta include EDILEMEZ. Uye adlari BIREBIR olmali; degisirse derleme
// patlar (istenen davranis).
enum class EditMode { Draw2D, View3D };
enum class TransformCommand {
    None, Delete, Trim, Extend, Fillet, Rotate, Rotate3D, Offset,
    Move, Copy, Mirror, LinearArray, PolarArray
};

static int g_checks = 0;
static int g_failures = 0;
static void check(bool ok, const char* what) {
    ++g_checks;
    std::printf("  %s %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++g_failures;
}
static double volumeOf(const TopoDS_Shape& s) {
    if (s.IsNull()) return 0.0;
    GProp_GProps props;
    BRepGProp::VolumeProperties(s, props);
    return props.Mass();
}
static bool validShape(const TopoDS_Shape& s) {
    if (s.IsNull()) return false;
    BRepCheck_Analyzer analyzer(s, true);
    return analyzer.IsValid();
}
// GUVENLI MAGAZA ERISIMI: GERCEK URETIM MAGAZASI (OccSolidStore) uzerinden.
// `find` kullanilir; eksik kayit 0/false olarak raporlanir (null sekil yok).
static bool storeHas(const OccSolidStore& s, EntityId id) {
    return s.find(id) != nullptr;
}
static double storeVolume(const OccSolidStore& s, EntityId id) {
    const TopoDS_Shape* p = s.find(id);
    return p ? volumeOf(*p) : 0.0;
}
static bool storeValid(const OccSolidStore& s, EntityId id) {
    const TopoDS_Shape* p = s.find(id);
    return p != nullptr && validShape(*p);
}
// Seklin eksen hizali sinir kutusu (tam silindir / tum yukseklik kontrolu icin).
struct Box3 { double xmin, ymin, zmin, xmax, ymax, zmax; };
static bool storeBounds(const OccSolidStore& s, EntityId id, Box3& out) {
    const TopoDS_Shape* p = s.find(id);
    if (p == nullptr || p->IsNull()) return false;
    Bnd_Box box;
    BRepBndLib::Add(*p, box);
    if (box.IsVoid()) return false;
    box.Get(out.xmin, out.ymin, out.zmin, out.xmax, out.ymax, out.zmax);
    return true;
}
// Belirli bir SEKIL nesnesinin sinir kutusu (surum karsilastirmasi icin).
static bool shapeBoundsOf(const TopoDS_Shape& s, Box3& out) {
    if (s.IsNull()) return false;
    Bnd_Box box;
    BRepBndLib::Add(s, box);
    if (box.IsVoid()) return false;
    box.Get(out.xmin, out.ymin, out.zmin, out.xmax, out.ymax, out.zmax);
    return true;
}
// BELIRLI SURUMUN hacmi (findVersion ile — etkin surumden BAGIMSIZ).
static double versionVolume(const OccSolidStore& s, EntityId id, VersionId v) {
    const TopoDS_Shape* p = s.findVersion(id, v);
    return p ? volumeOf(*p) : 0.0;
}

// URETIM GEOMETRISI (olculmus — kod bu sekilleri uretir):
//   addCube     : BRepPrimAPI_MakeBox(2.6, 2.6, 2.6)          -> vol 17.576
//   addCylinder : BRepPrimAPI_MakeCylinder(1.3, 3.0)
//                 OCCT 2-arg overload = (R, H): r=1.3, h=3.0 -> TAM SILINDIR,
//                 vol = pi*r^2*h = 15.9279. (Onceki kod 3-arg (R,H,Angle) cagirip
//                 3.0 radyanlik DILIM uretiyordu -> vol 3.2955; duzeltildi.)
constexpr double kCubeVol = 17.576;
constexpr double kCylVol = 15.9279;
constexpr double kCylRadius = 1.3;
constexpr double kCylHeight = 3.0;
constexpr double kVolTol = 0.01;

// Harness App: GERCEK Document (B-Rep magazasi Document-SAHIPLI) + uretim
// govdeleri. AYRI bir occShapes_ haritasi YOKTUR — tek dogruluk kaynagi
// Document::occStore()'dur; testler bu GERCEK magazayi okur.
struct HarnessApp {
    Document document_;
    // OCC-acik Application'in yaptigi gibi GERCEK OCCT magazasini enjekte et
    // (constructor injection; uretim fabrikasi makeOccSolidStore kullanilir).
    HarnessApp() { document_.setOccStore(makeOccSolidStore()); }
    EditMode mode_{EditMode::View3D};
    TransformCommand transformCommand_{TransformCommand::None};
    bool drawingActive_{false};
    std::wstring solidStatusMessage_;
    std::wstring lastStatus_;
    int cancelDrawingCalls_{0};

    // --- STUB'lar (uretim degil) ---
    void cancelTransformCommand() {}
    void cancelDrawing() { ++cancelDrawingCalls_; }
    void updateStatus() {}
    void publishStatus(const std::wstring& text) { lastStatus_ = text; }
    EntityProperties currentEntityProperties() const { return EntityProperties{}; }

    // --- Windows OCC-kopru uyeleri (stub) ---
    // addCube govdesinde `#ifdef _WIN32` ile korunan bir kopru dali vardir.
    // MM_HAS_OCC dali bu govdede ONCE `return` ettigi icin kopru yolu CALISMA
    // ANINDA ULASILAMAZ; bu uyeler yalnizca MinGW'de (_WIN32 tanimli) o blogun
    // DERLENEBILMESI icin vardir. Gercek kopru yukleyicisi DEGILDIR.
    // Tipler uretim application.hpp:515-517 ile AYNI olmali; degisirse derleme
    // PATLAR (sozlesme kilidi). Iki isaretci de nullptr: dal (varsayimsal olarak)
    // calisirsa analitik yola duser ve test ACIKCA duser -- sessiz gecmez.
    void ensureOccBridge() {}
    int (*occBridgeSolidBox_)(double, double, double, float**, int*, unsigned int**, int*) = nullptr;
    void (*occBridgeFree_)(void*) = nullptr;

    // --- cikarilan URETIM govdeleri (bildirimler) ---
    void pushUndoSnapshot();
    void addStyledModel(WireframeModel model);
    void addCube();
    void addCylinder();
    void addBooleanFuse();
    void addBooleanCommon();
    void addBooleanCut();
    std::vector<TopoDS_Shape> lastLiveSolids(std::size_t count) const;
    OccSolidStore* occSolidStore() noexcept;
    const OccSolidStore* occSolidStore() const noexcept;
    void putOccSolid(std::size_t index, const TopoDS_Shape& shape);
    std::size_t occSolidCount() const noexcept;
    const TopoDS_Shape* findOccSolid(std::size_t index) const;
};
"""


def build_cpp(bodies):
    parts = [CPP_PREAMBLE, "\n// ==== CIKARILAN URETIM GOVDELERI (verbatim) ====\n"]
    for name, _ in TARGETS:
        body = bodies[name].replace("Application::", "HarnessApp::")
        parts.append(body + "\n")
    parts.append(MAIN_CPP)
    return "".join(parts)


MAIN_CPP = r"""
int main() {
    // SATIR TAMPONLAMASI: harness bir mutasyon altinda cokse bile (or. eksik
    // operandla lastLiveSolids), o ana kadarki PASS/FAIL satirlari gorunsun.
    // Aksi halde cikti tamponu kaybolur ve negatif kontrol "sessiz" gorunur.
    std::setvbuf(stdout, nullptr, _IOLBF, 0);
    std::printf("OCCT version: %s\n", OCC_VERSION_COMPLETE);

    // --- Test 1: kutu/silindir DOGRU EntityId ile gercek sekle baglanir ---
    {
        std::printf("Test 1: addCube/addCylinder keyed by correct EntityId\n");
        HarnessApp app;
        app.addCube();
        check(app.document_.models().size() == 1, "cube appended (1 model)");
        const EntityId id0 = app.document_.modelId(0);
        check(id0 != kInvalidEntityId, "cube id valid");
        check(storeHas((*app.occSolidStore()), id0), "store has cube id");
        check(storeValid((*app.occSolidStore()), id0), "cube shape valid (BRepCheck)");
        check(std::abs(storeVolume((*app.occSolidStore()), id0) - kCubeVol) < kVolTol,
              "cube shape is a 2.6 box (vol 17.576)");

        app.addCylinder();
        check(app.document_.models().size() == 2, "cylinder appended (2 models)");
        const EntityId id1 = app.document_.modelId(1);
        check(storeHas((*app.occSolidStore()), id1), "store has cylinder id");
        check(storeValid((*app.occSolidStore()), id1), "cylinder shape valid (BRepCheck)");
        check(std::abs(storeVolume((*app.occSolidStore()), id1) - kCylVol) < kVolTol,
              "cylinder = full cylinder pi*r^2*h (vol 15.9279, not a 172-deg slice)");
        // TAM TUR KILIDI: bbox her iki yonde tam capi kapsamali (dilim kapsamaz)
        // ve Z 0..3.0 olmali (yukseklik 3.0; eski hata h=1.3 veriyordu).
        Box3 cb{};
        check(storeBounds((*app.occSolidStore()), id1, cb), "cylinder bbox available");
        check(std::abs((cb.xmax - cb.xmin) - 2.0 * kCylRadius) < 0.02,
              "cylinder bbox X spans full diameter (2*1.3)");
        check(std::abs((cb.ymax - cb.ymin) - 2.0 * kCylRadius) < 0.02,
              "cylinder bbox Y spans full diameter (full turn, not a slice)");
        check(std::abs(cb.zmin - 0.0) < 0.02 && std::abs(cb.zmax - kCylHeight) < 0.02,
              "cylinder Z range is 0..3.0 (height 3.0, not 1.3)");
        // OFF-BY-ONE KILIDI: kutu kimligi hala KUTUYU tutmali (silindiri degil).
        check(std::abs(storeVolume((*app.occSolidStore()), id0) - kCubeVol) < kVolTol,
              "cube id still holds the box (no off-by-one overwrite)");
    }

    // --- Test 2: boolean son iki CANLI katiyi secer, sonuc YENI id'ye gider ---
    {
        std::printf("Test 2: boolean selects last two live solids; result gets new id\n");
        HarnessApp app;
        app.addCube();
        app.addCylinder();
        const EntityId a = app.document_.modelId(0);
        const EntityId b = app.document_.modelId(1);
        const auto operands = app.lastLiveSolids(2);
        check(operands.size() == 2, "lastLiveSolids returns 2");
        // ON KOSUL KORUMASI: store bozuksa (or. off-by-one mutasyonu) operands
        // 2'den az olabilir; operands[1]'e korumasiz erismek UB/segfault verir
        // ve testin GERCEK nedenini gizler. Once sayi dogrulanir, sonra icerik.
        if (operands.size() < 2) {
            check(false, "operands[0] = newest = cylinder (SKIPPED: <2 live solids)");
            check(false, "operands[1] = second newest = cube (SKIPPED: <2 live solids)");
        } else {
            check(std::abs(volumeOf(operands[0]) - kCylVol) < kVolTol,
                  "operands[0] = newest = cylinder");
            check(std::abs(volumeOf(operands[1]) - kCubeVol) < kVolTol,
                  "operands[1] = second newest = cube");
        }

        app.addBooleanFuse();
        check(app.document_.models().size() == 3, "fuse appends result model");
        const EntityId r = app.document_.modelId(2);
        check(r != a && r != b, "result id is NEW (not an operand id)");
        check(storeHas((*app.occSolidStore()), r), "store has result id");
        check(storeValid((*app.occSolidStore()), r), "fuse result valid (BRepCheck)");
        const double vr = storeVolume((*app.occSolidStore()), r);
        check(vr >= kCubeVol - kVolTol && vr <= kCubeVol + kCylVol + kVolTol,
              "fuse volume within [max, sum]");
        // Test 6 ile ortusen ama burada da acik: operandlar KORUNUR.
        check(storeHas((*app.occSolidStore()), a) && storeHas((*app.occSolidStore()), b),
              "fuse operands preserved in store");
    }

    // --- Test 3: ekleme/silme diger ID<->sekil eslesmelerini BOZMAZ ---
    {
        std::printf("Test 3: insert/delete keeps other id-shape mappings\n");
        HarnessApp app;
        app.addCube();       // id a
        app.addCylinder();   // id b
        app.addCube();       // id c
        const EntityId a = app.document_.modelId(0);
        const EntityId b = app.document_.modelId(1);
        const EntityId c = app.document_.modelId(2);
        check(a != b && b != c && a != c, "ids distinct");
        const double volA = storeVolume((*app.occSolidStore()), a);
        const double volC = storeVolume((*app.occSolidStore()), c);

        app.pushUndoSnapshot();          // silme komutunun gercek sozlesmesi
        app.document_.deleteModels({1}); // ortadaki (silindir) silinir
        check(app.document_.models().size() == 2, "one model removed");
        check(app.document_.modelId(0) == a, "id a now at index 0");
        check(app.document_.modelId(1) == c, "id c now at index 1");
        check(std::abs(storeVolume((*app.occSolidStore()), a) - volA) < 1e-9,
              "shape a UNCHANGED after unrelated delete");
        check(std::abs(storeVolume((*app.occSolidStore()), c) - volC) < 1e-9,
              "shape c UNCHANGED after unrelated delete");
        check(!app.document_.indexOfEntity(b).has_value(),
              "deleted id no longer resolves");
    }

    // --- Test 4: silme + undo/redo GERCEK yasam dongusu ---
    {
        std::printf("Test 4: delete + undo/redo real lifecycle\n");
        HarnessApp app;
        app.addCube();
        app.addCylinder();
        const EntityId a = app.document_.modelId(0);
        const double volBefore = storeVolume((*app.occSolidStore()), a);

        app.pushUndoSnapshot();
        app.document_.deleteModels({0});
        check(app.document_.models().size() == 1, "cube deleted");
        check(!app.document_.indexOfEntity(a).has_value(), "deleted id not resolvable");
        check(storeHas((*app.occSolidStore()), a),
              "store KEEPS deleted shape entry (documented policy: not pruned)");

        app.document_.undo();
        check(app.document_.models().size() == 2, "undo restores the model");
        check(app.document_.indexOfEntity(a).has_value(),
              "SAME id resolves again after undo");
        check(app.document_.modelId(app.document_.indexOfEntity(a).value()) == a,
              "id preserved through undo");
        check(std::abs(storeVolume((*app.occSolidStore()), a) - volBefore) < 1e-9,
              "shape UNCHANGED by undo (store holds it; undo does NOT rebuild B-Rep)");

        app.document_.redo();
        check(!app.document_.indexOfEntity(a).has_value(), "redo deletes again");
        check(storeHas((*app.occSolidStore()), a),
              "shape still in store after redo (undo/redo never touch store)");
    }

    // --- Test 5: clearOccSolidStore + document clear ---
    {
        // BELGE-SAHIPLI MAGAZA: temizligi ARTIK Document yapar. Application'da
        // AYRI bir clearOccSolidStore() YOKTUR (iki yerde elle esleme riski
        // yapisal olarak ortadan kalkti).
        std::printf("Test 5: Document::clear() empties the document-owned store\n");
        HarnessApp app;
        app.addCube();
        app.addCylinder();
        const EntityId a = app.document_.modelId(0);
        check(!(*app.occSolidStore()).empty(), "store non-empty before clear");
        app.document_.clear();
        check((*app.occSolidStore()).empty(), "store EMPTY after Document::clear()");
        check(!storeHas((*app.occSolidStore()), a), "old shape no longer reachable");
        check(app.document_.models().empty(), "document empty after clear");
        check(!app.document_.indexOfEntity(a).has_value(),
              "old id not resolvable after document clear");
    }

    // --- Test 6: fuse/common/cut operandlari TUKETMEZ ---
    {
        std::printf("Test 6: fuse/common/cut keep operands in store\n");
        struct OpCase { const char* name; void (HarnessApp::*run)(); double lo; double hi; };
        const OpCase cases[] = {
            {"fuse",   &HarnessApp::addBooleanFuse,   kCubeVol,               kCubeVol + kCylVol},
            {"common", &HarnessApp::addBooleanCommon, 0.0,                    kCylVol},
            {"cut",    &HarnessApp::addBooleanCut,    kCubeVol - kCylVol,     kCubeVol},
        };
        for (const auto& c : cases) {
            HarnessApp app;
            app.addCube();
            app.addCylinder();
            const EntityId a = app.document_.modelId(0);
            const EntityId b = app.document_.modelId(1);
            (app.*(c.run))();
            std::printf("  -- %s --\n", c.name);
            check(app.document_.models().size() == 3,
                  (std::string(c.name) + ": result appended as third model").c_str());
            const EntityId r = app.document_.modelId(2);
            check(r != a && r != b,
                  (std::string(c.name) + ": result id is new").c_str());
            check(storeHas((*app.occSolidStore()), r) && storeValid((*app.occSolidStore()), r),
                  (std::string(c.name) + ": result present + valid").c_str());
            const double vr = storeVolume((*app.occSolidStore()), r);
            check(vr >= c.lo - kVolTol && vr <= c.hi + kVolTol,
                  (std::string(c.name) + ": result volume in range").c_str());
            std::printf("     (%s volume = %.4f)\n", c.name, vr);
            // Sonucun GERCEKTEN hesaplandigini kanitla (bos sonuc yeterli degil).
            if (std::string(c.name) == "common")
                check(vr > 1e-6, "common: result NON-EMPTY (operands really intersected)");
            if (std::string(c.name) == "cut")
                check(vr < kCubeVol - 1e-6, "cut: result SMALLER than cube (material removed)");
            check(storeHas((*app.occSolidStore()), a),
                  (std::string(c.name) + ": operand cube STILL in store").c_str());
            check(storeHas((*app.occSolidStore()), b),
                  (std::string(c.name) + ": operand cylinder STILL in store").c_str());
        }
    }

    // --- Test 7: ortak yardimcilar (solidCylinderSolid/Volume) TAM silindir ---
    // Onceki hata: MakeCylinder(radius, radius, height) -> (R, H, Angle) dilim.
    {
        std::printf("Test 7: solidCylinderSolid/Volume produce full cylinders\n");
        const double expect = 3.14159265358979323846 * 1.5 * 1.5 * 5.0; // 35.3429
        const double vol = mm::solidCylinderVolume(1.5, 5.0);
        check(std::abs(vol - expect) / expect < 1e-3,
              "solidCylinderVolume(1.5,5.0) = pi*r^2*h (35.3429, full cylinder)");
        const auto solid = mm::solidCylinderSolid(1.5, 5.0, 0.2, 32);
        double zmin = 0.0, zmax = 0.0;
        bool first = true;
        for (const auto& v : solid.vertices()) {
            if (first) { zmin = zmax = v.z; first = false; }
            if (v.z < zmin) zmin = v.z;
            if (v.z > zmax) zmax = v.z;
        }
        check(!first && std::abs((zmax - zmin) - 5.0) < 0.05,
              "solidCylinderSolid bbox height = 5.0 (full cylinder, not a slice)");
    }

    // --- Test 8: SURUMLU GEOMETRI (Phase 3.2A) ---
    // Ayni EntityId altinda geometri degisince ESKI SURUM KORUNUR ve belirli bir
    // surum etkinlestirilebilir. Kimligi geri yuklemek YETMEZ — ayni kimlik
    // altinda yeni geometri uretilebilir; bu yuzden SURUM gerekir.
    // GERCEK URETIM MAGAZASI (OccSolidStore) kullanilir; sahte store YOK.
    {
        std::printf("Test 8: versioned geometry per EntityId (immutable snapshots)\n");
        HarnessApp app;
        app.addCube();  // 2.6 kutu -> v1
        const EntityId id = app.document_.modelId(0);
        OccSolidStore& store = *app.occSolidStore();

        const VersionId v1 = store.activeVersion(id);
        check(v1 != kInvalidVersionId, "v1 active after addCube");
        check(store.versionCount(id) == 1, "one version after first write");
        Box3 b1{};
        const TopoDS_Shape* p1a = store.find(id);
        check(p1a != nullptr && shapeBoundsOf(*p1a, b1), "v1 bbox available");
        const double v1span = b1.xmax - b1.xmin;

        // AYNI kimlige YENI, FARKLI geometri yaz -> yeni surum olusmali.
        const TopoDS_Shape box2 = BRepPrimAPI_MakeBox(1.0, 1.0, 1.0).Shape();
        Box3 b2{};
        check(shapeBoundsOf(box2, b2) && std::abs((b2.xmax - b2.xmin) - 1.0) < 1e-6,
              "v2 source geometry genuinely different (1.0 box)");
        const VersionId v2 = store.set(id, box2);
        check(v2 != kInvalidVersionId && v2 != v1, "v2 is new + distinct from v1");
        check(store.versionCount(id) == 2, "TWO versions preserved after 2nd write");
        check(store.activeVersion(id) == v2, "v2 becomes active");
        check(std::abs(storeVolume(store, id) - 1.0) < 1e-6,
              "find() = active = small box (vol 1.0), not the old cube");

        // ESKI SURUM DEGISMEDI (hacim + bbox) — Phase 3.2A'nin ANA iddiasi.
        check(versionVolume(store, id, v1) > 0.0, "v1 still retrievable by VersionId");
        check(std::abs(versionVolume(store, id, v1) - kCubeVol) < kVolTol,
              "v1 volume UNCHANGED after new version (17.576)");
        const TopoDS_Shape* p1b = store.findVersion(id, v1);
        Box3 b1b{};
        check(p1b != nullptr && shapeBoundsOf(*p1b, b1b), "v1 bbox still available");
        check(std::abs((b1b.xmax - b1b.xmin) - v1span) < 1e-9,
              "v1 bbox UNCHANGED after new version (exact)");
        check(std::abs((b1b.xmax - b1b.xmin) - 2.6) < 0.02,
              "v1 bbox is still the 2.6 cube");

        // SURUM ETKINLESTIRME: eski surumu geri getir (undo'nun ileride
        // dayanacagi mekanizma).
        check(store.activate(id, v1), "activate(v1) succeeds");
        check(store.activeVersion(id) == v1, "v1 active again");
        check(std::abs(storeVolume(store, id) - kCubeVol) < kVolTol,
              "find() back to v1 cube after activate");
        check(store.activate(id, v2), "activate(v2) succeeds (round trip)");

        // GECERSIZ SURUM GUVENLI REDDEDILIR (durum degismez).
        const VersionId now = store.activeVersion(id);
        check(!store.activate(id, kInvalidVersionId), "activate(0) rejected");
        check(!store.activate(id, 999999), "activate(unknown version) rejected");
        check(store.activeVersion(id) == now, "active UNCHANGED after rejected activate");
        check(store.findVersion(id, 999999) == nullptr, "findVersion(unknown) = nullptr");
        check(store.findVersion(id, kInvalidVersionId) == nullptr, "findVersion(0) = nullptr");

        // SURUM KIMLIKLERI: benzersiz + sabit (olusturma sirasi korunur).
        const std::vector<VersionId> vids = store.versionIds(id);
        check(vids.size() == 2 && vids[0] == v1 && vids[1] == v2,
              "versionIds stable + creation-ordered (v1,v2)");

        // CAPRAZ IZOLASYON: bir varligin surumleri digerine KARISMAZ.
        app.addCylinder();
        const EntityId id2 = app.document_.modelId(app.document_.models().size() - 1);
        check(id2 != id, "second entity id distinct");
        check(store.findVersion(id2, v1) == nullptr, "v1 does NOT belong to other entity");
        check(store.findVersion(id2, v2) == nullptr, "v2 does NOT belong to other entity");
        bool crossed = false;
        for (VersionId x : store.versionIds(id2))
            if (x == v1 || x == v2) crossed = true;
        check(!crossed, "no version-id crossing between entities");
        check(store.activeVersion(id2) != v1 && store.activeVersion(id2) != v2,
              "other entity has its own active version");
        // Surum etkinlestirme DIGER varligi ETKILEMEZ.
        check(store.activate(id, v1) && store.activeVersion(id) == v1 &&
              store.activeVersion(id2) != v1, "activate isolated per entity");
    }

    // --- Test 9: SURUM KIMLIGI URETIMI — TEKRAR YOK (Phase 3.2A) ---
    // Her `set` BENZERSIZ ve KESIN ARTAN bir VersionId uretir; hicbir id iki
    // kez uretilmez. Eski surumler, yeni surumler eklendikce KORUNUR. Bu,
    // "asla tekrar kullanilmaz" sozlesmesinin gozlemlenebilir regresyonudur.
    {
        std::printf("Test 9: version-id allocation is unique + strictly increasing\n");
        HarnessApp app;
        app.addCube();
        const EntityId id = app.document_.modelId(0);
        OccSolidStore& store = *app.occSolidStore();

        constexpr int kSets = 12;
        const std::size_t versionsBefore = store.versionCount(id);
        std::vector<VersionId> seen;
        bool strictlyIncreasing = true, allValid = true;
        for (int i = 0; i < kSets; ++i) {
            const TopoDS_Shape s = BRepPrimAPI_MakeBox(1.0 + 0.1 * i, 1.0, 1.0).Shape();
            const VersionId v = store.set(id, s);
            if (v == kInvalidVersionId) allValid = false;
            if (!seen.empty() && !(v > seen.back())) strictlyIncreasing = false;
            seen.push_back(v);
        }
        check(allValid, "every set returned a valid (non-zero) VersionId");
        check(strictlyIncreasing, "VersionIds strictly increasing (no reuse)");
        std::set<VersionId> uniq(seen.begin(), seen.end());
        check(uniq.size() == static_cast<std::size_t>(kSets),
              "all VersionIds distinct (no duplicate allocation)");
        check(store.versionCount(id) == versionsBefore + static_cast<std::size_t>(kSets),
              "all versions preserved (old ones not overwritten)");

        // ESKI surumler HALA erisilebilir ve icerikleri dogru (ilk yazilan 1.0).
        check(std::abs(versionVolume(store, id, seen.front()) - 1.0) < 1e-6,
              "oldest version still retrievable + correct volume (1.0)");
        check(std::abs(versionVolume(store, id, seen.back()) - (1.0 + 0.1 * (kSets - 1))) < 1e-6,
              "newest version is the last written box (vol 2.1)");

        // CAKISMA YOK: baska varligin kimlikleri bu varligin kumesiyle kesisMES.
        app.addCylinder();
        const EntityId id2 = app.document_.modelId(app.document_.models().size() - 1);
        bool overlap = false;
        for (VersionId x : store.versionIds(id2))
            if (uniq.count(x) != 0) overlap = true;
        check(!overlap, "second entity shares NO version ids (no reuse across entities)");
    }

    // --- Test 10: SURUM SAYACI TUKENME SINIRI (pure, OCCT'siz) ---
    // advanceVersionIdCounter kMax'tan sonra 0'a (TUKENDI) gecer; AYNI id'yi
    // TEKRAR uretmez. (Kelepceleme yanlis geometri dondururdu: ayni slot'ta
    // eslesen ILK surum kazanir.)
    {
        std::printf("Test 10: version-id counter exhaustion boundary\n");
        constexpr VersionId kMax = std::numeric_limits<VersionId>::max();
        check(kInvalidVersionId == 0, "kInvalidVersionId = 0");
        check(advanceVersionIdCounter(1) == 2, "counter advance(1) -> 2");
        check(advanceVersionIdCounter(kMax - 1) == kMax, "counter advance(max-1) -> max");
        check(advanceVersionIdCounter(kMax) == kInvalidVersionId,
              "counter advance(max) -> 0 (EXHAUSTED, does not reuse max)");
        check(advanceVersionIdCounter(kMax) != kMax,
              "counter advance(max) does NOT stay at max");
        check(advanceVersionIdCounter(kInvalidVersionId) == kInvalidVersionId,
              "counter exhausted stays 0");
    }

    // =====================================================================
    // Phase 3.2B — TRANSACTIONAL GEOMETRY UNDO/REDO (gercek Document+store)
    // =====================================================================

    // --- Test 11: geometri degisikligi (replace) -> undo -> redo ---
    // Ayni EntityId altinda YENI B-Rep; undo ESKI SURUMU (hacim/bbox) geri
    // getirmeli. Yalnizca kimligi geri yuklemek YETMEZ.
    {
        std::printf("Test 11: replace -> undo -> redo restores B-Rep version\n");
        HarnessApp app;
        app.addCube();
        const EntityId id0 = app.document_.modelId(0);
        OccSolidStore& st = *app.occSolidStore();
        const double volOld = storeVolume(st, id0);
        Box3 bOld{};
        check(storeBounds(st, id0, bOld), "old bbox available");

        const TopoDS_Shape small = BRepPrimAPI_MakeBox(1.0, 1.0, 1.0).Shape();
        app.pushUndoSnapshot();
        app.document_.replaceModel(0, {mm::shapeToWireframeWithFaces(small, 0.15)});
        app.putOccSolid(0, small);
        check(std::abs(storeVolume(st, id0) - 1.0) < 1e-6, "after op: new geometry (vol 1.0)");

        check(app.document_.undo(), "undo returns true");
        check(std::abs(storeVolume(st, id0) - volOld) < kVolTol,
              "undo restores OLD B-Rep geometry (vol 17.576)");
        Box3 bBack{};
        check(storeBounds(st, id0, bBack) &&
              std::abs((bBack.xmax - bBack.xmin) - (bOld.xmax - bOld.xmin)) < 1e-6,
              "undo restores OLD bbox (not just volume)");

        check(app.document_.redo(), "redo returns true");
        check(std::abs(storeVolume(st, id0) - 1.0) < 1e-6,
              "redo restores NEW B-Rep geometry (vol 1.0)");
    }

    // --- Test 12: ayni komutta AYNI EntityId'ye binden fazla yazma ---
    // Ilk ONCESI + son SONRASI saklanir; TEK undo ARA surumu ATLAMALI.
    {
        std::printf("Test 12: two writes to same EntityId -> single undo skips intermediate\n");
        HarnessApp app;
        app.addCube();
        const EntityId id = app.document_.modelId(0);
        OccSolidStore& st = *app.occSolidStore();
        const TopoDS_Shape s1 = BRepPrimAPI_MakeBox(1.0, 1.0, 1.0).Shape();
        const TopoDS_Shape s2 = BRepPrimAPI_MakeBox(2.0, 1.0, 1.0).Shape();
        app.pushUndoSnapshot();
        app.putOccSolid(0, s1);
        app.putOccSolid(0, s2);
        check(std::abs(storeVolume(st, id) - 2.0) < 1e-6, "after two writes: last geometry (vol 2.0)");
        check(app.document_.undo(), "undo returns true");
        check(std::abs(storeVolume(st, id) - kCubeVol) < kVolTol,
              "SINGLE undo returns to ORIGINAL (17.576), not intermediate (1.0)");
    }

    // --- Test 13: TEK komutta BIRDEN FAZLA EntityId -> atomik geri alma ---
    {
        std::printf("Test 13: multiple EntityIds in one command -> atomic undo\n");
        HarnessApp app;
        app.addCube();
        app.addCylinder();
        const EntityId a = app.document_.modelId(0);
        const EntityId b = app.document_.modelId(1);
        OccSolidStore& st = *app.occSolidStore();
        const double va = storeVolume(st, a), vb = storeVolume(st, b);

        const TopoDS_Shape na = BRepPrimAPI_MakeBox(1.0, 1.0, 1.0).Shape();
        const TopoDS_Shape nb = BRepPrimAPI_MakeBox(2.0, 2.0, 2.0).Shape();
        app.pushUndoSnapshot();
        app.putOccSolid(0, na);
        app.putOccSolid(1, nb);
        check(std::abs(storeVolume(st, a) - 1.0) < 1e-6 && std::abs(storeVolume(st, b) - 8.0) < 1e-6,
              "both entities changed in one command");

        check(app.document_.undo(), "undo returns true");
        check(std::abs(storeVolume(st, a) - va) < kVolTol && std::abs(storeVolume(st, b) - vb) < kVolTol,
              "BOTH restored atomically (no partial undo)");
    }

    // --- Test 14: yeni kati olustur -> undo -> redo ---
    {
        std::printf("Test 14: create solid -> undo -> redo\n");
        HarnessApp app;
        app.addCube();
        const EntityId id0 = app.document_.modelId(0);
        OccSolidStore& st = *app.occSolidStore();
        check(storeHas(st, id0), "created: geometry present");

        check(app.document_.undo(), "undo returns true");
        check(!app.document_.indexOfEntity(id0).has_value(), "entity removed by undo");
        check(st.find(id0) == nullptr, "no ACTIVE geometry after create-undo (version 'none')");
        check(st.activeVersion(id0) == kInvalidVersionId, "active version = none");

        check(app.document_.redo(), "redo returns true");
        check(app.document_.indexOfEntity(id0).has_value(), "entity re-added");
        check(storeHas(st, id0) && std::abs(storeVolume(st, id0) - kCubeVol) < kVolTol,
              "redo restores geometry (vol 17.576)");
    }

    // --- Test 15: boolean -> undo -> redo; operandlar korunur ---
    {
        std::printf("Test 15: boolean -> undo -> redo; operands preserved\n");
        HarnessApp app;
        app.addCube();
        app.addCylinder();
        const EntityId a = app.document_.modelId(0);
        const EntityId b = app.document_.modelId(1);
        app.addBooleanFuse();
        const EntityId r = app.document_.modelId(2);
        OccSolidStore& st = *app.occSolidStore();
        const double vr = storeVolume(st, r);

        check(app.document_.undo(), "undo returns true");
        check(!app.document_.indexOfEntity(r).has_value(), "fuse result removed by undo");
        check(storeHas(st, a) && storeHas(st, b), "operands preserved through undo");
        check(st.find(r) == nullptr, "result geometry 'none' after undo");

        check(app.document_.redo(), "redo returns true");
        check(app.document_.indexOfEntity(r).has_value(), "result re-added by redo");
        check(std::abs(storeVolume(st, r) - vr) < kVolTol, "redo restores fuse geometry");
    }

    // --- Test 16: sil -> undo -> redo (geometri undo icin KORUNUR) ---
    {
        std::printf("Test 16: delete -> undo -> redo; geometry preserved\n");
        HarnessApp app;
        app.addCube();
        const EntityId id0 = app.document_.modelId(0);
        OccSolidStore& st = *app.occSolidStore();
        const double v = storeVolume(st, id0);

        app.pushUndoSnapshot();
        app.document_.deleteModels({0});
        check(app.document_.models().empty(), "entity deleted");
        check(storeHas(st, id0), "deleted entity's geometry PRESERVED in store (undo policy)");

        check(app.document_.undo(), "undo returns true");
        check(app.document_.indexOfEntity(id0).has_value() &&
              app.document_.modelId(0) == id0, "SAME EntityId restored");
        check(std::abs(storeVolume(st, id0) - v) < kVolTol, "geometry intact after undo");

        check(app.document_.redo(), "redo returns true");
        check(app.document_.models().empty(), "redo deletes again");
        check(storeHas(st, id0), "geometry still preserved after redo (no pruning in 3.2B)");
    }

    // --- Test 17: EKSIK surum -> islem REDDEDILIR, tum durum degismez ---
    {
        std::printf("Test 17: missing version -> undo rejected, state unchanged\n");
        HarnessApp app;
        app.addCube();
        const EntityId id0 = app.document_.modelId(0);
        OccSolidStore& st = *app.occSolidStore();
        const std::size_t nModels = app.document_.models().size();
        const double vBefore = storeVolume(st, id0);
        const std::size_t rBefore = app.document_.models().size();  // redo sayimi icin

        app.pushUndoSnapshot();
        // BOZUK delta: ONCESI var olmayan bir surume isaret ediyor.
        app.document_.recordGeometryDelta(id0, 999999u, kInvalidVersionId);
        check(app.document_.canUndo(), "record is on the stack");

        check(!app.document_.undo(), "undo REJECTED (missing version)");
        check(app.document_.models().size() == nModels, "document UNCHANGED after rejected undo");
        check(std::abs(storeVolume(st, id0) - vBefore) < 1e-9, "store UNCHANGED after rejected undo");
        check(app.document_.canUndo(), "rejected record NOT consumed from stack");
        (void)rBefore;
    }

    // --- Test 18: YALNIZ geometri deltasi olan kayit undo edilebilir ---
    {
        std::printf("Test 18: geometry-delta-only record is undoable\n");
        HarnessApp app;
        app.addCube();
        const EntityId id = app.document_.modelId(0);
        OccSolidStore& st = *app.occSolidStore();
        const double v0 = storeVolume(st, id);

        app.pushUndoSnapshot();
        const VersionId prev = st.activeVersion(id);
        const VersionId nv = st.set(id, BRepPrimAPI_MakeBox(1.0, 1.0, 1.0).Shape());
        app.document_.recordGeometryDelta(id, prev, nv);
        // ops BOS, layers ayni — yalniz geometri deltasi var.
        check(app.document_.canUndo(), "canUndo() sees geometry-only record");
        check(app.document_.undo(), "undo returns true");
        check(std::abs(storeVolume(st, id) - v0) < kVolTol, "geometry restored from delta-only record");
    }

    // --- Test 19: SNAPSHOT'SIZ geometri yazimi SESSIZCE DUSMEZ ---
    {
        std::printf("Test 19: geometry write with no open snapshot auto-opens a record\n");
        HarnessApp app;
        app.addCube();
        app.document_.clearHistory();
        check(!app.document_.hasOpenUndoRecord(), "no open record after clearHistory()");
        const EntityId id = app.document_.modelId(0);
        OccSolidStore& st = *app.occSolidStore();
        const VersionId prev = st.activeVersion(id);
        const VersionId nv = st.set(id, BRepPrimAPI_MakeBox(1.0, 1.0, 1.0).Shape());
        app.document_.recordGeometryDelta(id, prev, nv);  // kayit YOK -> KENDI acar
        check(app.document_.hasOpenUndoRecord(), "recordGeometryDelta OPENED its own record");
        check(app.document_.canUndo(), "delta is undoable (not silently dropped)");
        check(app.document_.undo(), "undo returns true");
        check(std::abs(storeVolume(st, id) - kCubeVol) < kVolTol,
              "geometry restored via auto-opened record");
    }

    // --- Test 20: undo sonrasi YENI islem redo gecmisini temizler + budar ---
    // Phase 3.2C: yeni islem redo gecmisini TEMIZLER ve terk edilen redo
    // surumleri BUDANIR (eski 3.2B'de korunuyordu; artik budama aktif).
    {
        std::printf("Test 20: new op after undo clears redo history + prunes abandoned versions\n");
        HarnessApp app;
        app.addCube();
        const EntityId id = app.document_.modelId(0);
        OccSolidStore& st = *app.occSolidStore();
        const VersionId v1 = st.activeVersion(id);

        app.pushUndoSnapshot();
        app.putOccSolid(0, BRepPrimAPI_MakeBox(1.0, 1.0, 1.0).Shape());
        const VersionId v2 = st.activeVersion(id);
        check(v2 != v1, "v2 written (distinct version)");
        check(app.document_.undo(), "undo returns true");
        check(app.document_.canRedo(), "canRedo true after undo");
        check(st.activeVersion(id) == v1, "v1 active after undo");

        app.pushUndoSnapshot();  // redo temizlenir; terk edilen v2 BUDANIR
        app.putOccSolid(0, BRepPrimAPI_MakeBox(2.0, 2.0, 2.0).Shape());
        check(!app.document_.canRedo(), "new op cleared redo history");
        check(std::abs(storeVolume(st, id) - 8.0) < 1e-6, "new geometry active");
        check(st.findVersion(id, v2) == nullptr,
              "abandoned redo version (v2) pruned after new op");
        check(st.findVersion(id, v1) != nullptr,
              "v1 preserved (undo still reaches original geometry)");
    }

    // =====================================================================
    // Phase 3.2C — SURUM BUDAMA (gercek Document + gercek OccSolidStore)
    // =====================================================================

    // --- Test 21: sil -> undo -> redo, budama AKTIFKEN geometri korunur ---
    // Silme store'a dokunmaz; silinen varligin etkin surumu, Delete op'unun
    // kimlik referansi (beforeIds) ile budamadan KORUNUR.
    {
        std::printf("Test 21: delete -> undo -> redo survives pruning (identity-ref)\n");
        HarnessApp app;
        app.addCube();
        const EntityId id0 = app.document_.modelId(0);
        OccSolidStore& st = *app.occSolidStore();
        const double v = storeVolume(st, id0);

        app.pushUndoSnapshot();
        app.document_.deleteModels({0});
        check(app.document_.models().empty(), "entity deleted");
        // Budama tetikle: bir sonraki komutun snapshot'i (100 sinirine gerek
        // yok — her pushSnapshot budar).
        app.pushUndoSnapshot();
        check(storeHas(st, id0),
              "deleted entity geometry survives prune (Delete op identity-ref)");
        check(std::abs(storeVolume(st, id0) - v) < kVolTol,
              "deleted geometry volume intact after prune");

        check(app.document_.undo(), "undo restores deleted entity after prune");
        check(app.document_.indexOfEntity(id0).has_value(), "entity back after undo");
        check(std::abs(storeVolume(st, id0) - v) < kVolTol, "geometry intact after undo");

        check(app.document_.redo(), "redo deletes again");
        check(app.document_.models().empty(), "redo removed entity");
        check(storeHas(st, id0), "geometry still preserved (Delete op still undoable)");
    }

    // --- Test 22: 150+ ardisik degisiklik -> surum sayisi SINIRLI kalir ---
    // Budama olmasaydi 150 surum birikir; budama ile ~100 kayit sinirina
    // bagli SINIRLI bir sayida kalir.
    {
        std::printf("Test 22: 150+ consecutive changes -> version count bounded\n");
        HarnessApp app;
        app.addCube();
        const EntityId id = app.document_.modelId(0);
        OccSolidStore& st = *app.occSolidStore();

        for (int i = 0; i < 150; ++i) {
            app.pushUndoSnapshot();
            app.putOccSolid(0, BRepPrimAPI_MakeBox(1.0 + 0.01 * i, 1.0, 1.0).Shape());
        }
        // 1 ilk surum + 150 yeni surum yazildi (151); ama undo 100 kayitla
        // sinirli ve her pushSnapshot budar -> saklanan surum sayisi cok daha az.
        const std::size_t total = st.totalVersionCount();
        std::printf("     (total versions after 150 changes = %zu)\n", total);
        check(total < 120, "version count bounded (<120) after 150 changes");
        check(total > 1, "at least the active version remains");
        check(app.document_.canUndo(), "undo still available after 150 changes");
    }

    // --- Test 23: coklu EntityId transaction -> GEREKLI tum surumler korunur ---
    {
        std::printf("Test 23: multi-entity transaction keeps all needed versions\n");
        HarnessApp app;
        app.addCube();
        app.addCylinder();
        const EntityId a = app.document_.modelId(0);
        const EntityId b = app.document_.modelId(1);
        OccSolidStore& st = *app.occSolidStore();
        const VersionId va0 = st.activeVersion(a);
        const VersionId vb0 = st.activeVersion(b);

        app.pushUndoSnapshot();
        app.putOccSolid(0, BRepPrimAPI_MakeBox(1.0, 1.0, 1.0).Shape());
        app.putOccSolid(1, BRepPrimAPI_MakeBox(2.0, 2.0, 2.0).Shape());
        // Budama tetikle (kayit artik "tamamlandi").
        app.pushUndoSnapshot();

        check(st.findVersion(a, va0) != nullptr, "a's before version preserved after prune");
        check(st.findVersion(b, vb0) != nullptr, "b's before version preserved after prune");

        check(app.document_.undo(), "multi-entity undo works after prune");
        check(st.activeVersion(a) == va0 && st.activeVersion(b) == vb0,
              "BOTH entities restored atomically after prune");
    }

    // --- Test 24: budanan surum guvenli reddedilir; gecerli surum calisir ---
    {
        std::printf("Test 24: pruned version safely rejected; valid version still works\n");
        HarnessApp app;
        app.addCube();
        const EntityId e = app.document_.modelId(0);
        OccSolidStore& st = *app.occSolidStore();
        const VersionId v1 = st.activeVersion(e);

        app.pushUndoSnapshot();
        app.putOccSolid(0, BRepPrimAPI_MakeBox(1.0, 1.0, 1.0).Shape());
        const VersionId v2 = st.activeVersion(e);
        check(v2 != v1, "v2 written");

        check(app.document_.undo(), "undo to v1");
        app.pushUndoSnapshot();  // redo temizlenir -> v2 budanir

        check(st.findVersion(e, v2) == nullptr, "pruned version rejected (findVersion)");
        check(!st.findVersionExists(e, v2), "pruned version rejected (findVersionExists)");
        check(!st.activate(e, v2), "pruned version rejected (activate)");
        check(!st.restoreActive(e, v2), "pruned version rejected (restoreActive)");
        check(st.activeVersion(e) == v1, "active UNCHANGED after rejected operations");
        check(st.findVersion(e, v1) != nullptr, "valid active version still works");
    }

    // --- Test 25: clearHistory sonrasi eski geometriye ERISILEMEZ ---
    {
        std::printf("Test 25: after clearHistory, superseded geometry unreachable\n");
        HarnessApp app;
        app.addCube();
        const EntityId e = app.document_.modelId(0);
        OccSolidStore& st = *app.occSolidStore();
        const VersionId v1 = st.activeVersion(e);

        app.pushUndoSnapshot();
        app.putOccSolid(0, BRepPrimAPI_MakeBox(1.0, 1.0, 1.0).Shape());
        check(st.versionCount(e) == 2, "two versions before clearHistory");

        app.document_.clearHistory();  // budama: yalniz etkin surum kalir
        check(st.findVersion(e, v1) == nullptr,
              "superseded geometry unreachable after clearHistory");
        check(st.versionCount(e) == 1, "only active version remains after clearHistory");
        check(storeHas(st, e), "active geometry still reachable");
    }

    // --- Test 26: rollback icin gereken surumler kaybolmaz ---
    // Uc ardisik degisiklik; budama sonrasi uc surum de (undo zinciri icin)
    // korunur ve geri alinabilir.
    {
        std::printf("Test 26: rollback chain survives pruning\n");
        HarnessApp app;
        app.addCube();
        const EntityId e = app.document_.modelId(0);
        OccSolidStore& st = *app.occSolidStore();
        const VersionId v1 = st.activeVersion(e);

        app.pushUndoSnapshot();
        app.putOccSolid(0, BRepPrimAPI_MakeBox(1.0, 1.0, 1.0).Shape());
        const VersionId v2 = st.activeVersion(e);
        app.pushUndoSnapshot();
        app.putOccSolid(0, BRepPrimAPI_MakeBox(2.0, 1.0, 1.0).Shape());
        const VersionId v3 = st.activeVersion(e);

        app.pushUndoSnapshot();  // budama tetikle
        check(st.findVersion(e, v1) != nullptr, "rollback version v1 preserved");
        check(st.findVersion(e, v2) != nullptr, "rollback version v2 preserved");
        check(st.findVersion(e, v3) != nullptr, "rollback version v3 preserved");

        check(app.document_.undo() && st.activeVersion(e) == v2, "rollback to v2 works");
        check(app.document_.undo() && st.activeVersion(e) == v1, "rollback to v1 works");
    }

    std::printf("\nchecks=%d failures=%d\n", g_checks, g_failures);
    if (g_failures == 0) std::printf("OCC SOLID-ID HARNESS: OK\n");
    else std::printf("OCC SOLID-ID HARNESS: FAILED\n");
    return g_failures == 0 ? 0 : 1;
}
"""


def detect_occt_include():
    for cand in ("/usr/include/opencascade", "/usr/local/include/opencascade"):
        if os.path.exists(os.path.join(cand, "BRepPrimAPI_MakeBox.hxx")):
            return cand
    import glob
    for cand in glob.glob("/usr/include/opencascade*"):
        if os.path.exists(os.path.join(cand, "BRepPrimAPI_MakeBox.hxx")):
            return cand
    return None


def detect_occt_libdir():
    import glob
    for pat in ("/usr/lib/x86_64-linux-gnu/libTKernel.so*",
                "/usr/lib/aarch64-linux-gnu/libTKernel.so*",
                "/usr/lib64/libTKernel.so*", "/usr/lib/libTKernel.so*"):
        hit = sorted(glob.glob(pat))
        if hit:
            return os.path.dirname(hit[0])
    return None


def parse_args(argv):
    if not argv:
        raise HarnessError("usage: run_occ_solid_ids.py <build_dir> [--cxx ...] ...")
    build_dir = argv[0]
    opts = {"cxx": None, "occt_include": None, "occt_libdir": None,
            "shape_check": True}
    i = 1
    while i < len(argv):
        a = argv[i]
        if a in ("--cxx", "--occt-include", "--occt-libdir"):
            if i + 1 >= len(argv):
                raise HarnessError(f"{a} needs a value")
            opts[a[2:].replace("-", "_")] = argv[i + 1]
            i += 2
        elif a == "--no-shape-check":
            # Negatif kontrol kosucusu icin: kaynak-sekil kapisi kapatilir ki
            # MUTASYONUN RUNTIME davranisi sinansin (sekil kapisi mutasyonu
            # erken yakalayip gercek testi calistirmasini engellemesin).
            opts["shape_check"] = False
            i += 1
        else:
            raise HarnessError(f"unknown argument: {a}")
    return build_dir, opts


def main(argv):
    try:
        build_dir, opts = parse_args(argv)
        if not os.path.isfile(APP_CPP):
            raise HarnessError(f"production source not found: {APP_CPP}")

        with open(APP_CPP, "r", encoding="utf-8") as fh:
            text = fh.read()

        bodies = extract_bodies(text)
        with open(DOC_CPP, "r", encoding="utf-8") as fh:
            doc_text = fh.read()
        if opts["shape_check"]:
            assert_source_shape(bodies, text)
            assert_document_shape(doc_text)
            print("kaynak-govde cikarimi OK (%d govde; sekil kontrolleri gecti)" % len(bodies))
        else:
            print("kaynak-govde cikarimi OK (%d govde; sekil kapisi KAPALI)" % len(bodies))

        cxx = opts["cxx"] or shutil.which("g++") or shutil.which("c++")
        if not cxx:
            raise HarnessError("C++ derleyici bulunamadi (--cxx ile verin)")
        inc = opts["occt_include"] or detect_occt_include()
        libdir = opts["occt_libdir"] or detect_occt_libdir()
        if not inc:
            raise HarnessError("OCCT baslik dizini bulunamadi (--occt-include ile verin)")
        if not libdir:
            raise HarnessError("OCCT kutuphane dizini bulunamadi (--occt-libdir ile verin)")

        os.makedirs(build_dir, exist_ok=True)
        gen = os.path.join(build_dir, "occ_solid_ids_harness.cpp")
        exe = os.path.join(build_dir, "occ_solid_ids_harness")
        if os.name == "nt":
            exe += ".exe"
        with open(gen, "w", encoding="utf-8") as fh:
            fh.write(build_cpp(bodies))

        cmd = [cxx, "-std=c++20", "-O1", "-DMM_HAS_OCC", "-D_USE_MATH_DEFINES",
               "-I" + os.path.join(ROOT, "include"), "-I" + inc, gen]
        cmd += [os.path.join(ROOT, s) for s in CORE_SOURCES]
        cmd += ["-o", exe, "-L" + libdir]
        cmd += ["-Wl,--start-group"] + ["-l" + lib for lib in OCC_LIBS] + ["-Wl,--end-group"]
        if os.name == "nt":
            cmd += ["-lkernel32", "-luser32", "-lgdi32", "-ladvapi32",
                    "-lshell32", "-lole32", "-loleaut32", "-luuid", "-lws2_32"]
        print("derliyor: %s" % cxx)
        proc = subprocess.run(cmd, capture_output=True, text=True)
        if proc.returncode != 0:
            sys.stdout.write(proc.stdout)
            sys.stderr.write(proc.stderr)
            raise HarnessError("derleme BASARISIZ")

        print("calistiriliyor: %s" % exe)
        run = subprocess.run([exe], capture_output=True, text=True)
        sys.stdout.write(run.stdout)
        sys.stderr.write(run.stderr)
        if run.returncode != 0:
            raise HarnessError(f"harness testleri BASARISIZ (exit={run.returncode})")
        print("HARNESS OK")
        return 0
    except HarnessError as exc:
        print("HARNESS HATASI: %s" % exc, file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
