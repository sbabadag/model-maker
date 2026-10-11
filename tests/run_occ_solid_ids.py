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

# Cekirdek kaynaklar (CMakeLists `model_maker_core` + OCC modulu). Liste
# degisirse LINK HATASI verir — sessiz gecmez.
CORE_SOURCES = [
    "src/geometry.cpp", "src/grid.cpp", "src/camera.cpp", "src/view_cube.cpp",
    "src/ribbon_layout.cpp", "src/performance.cpp", "src/dxf.cpp",
    "src/document.cpp", "src/drafting.cpp", "src/profile_database.cpp",
    "src/occ_geometry.cpp",
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
    ("clearOccSolidStore", "void Application::clearOccSolidStore() noexcept"),
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

    fixed_key = "occShapes_[document_.modelId(document_.models().size() - 1)]"
    # Kutu/silindir/boolean sonucu YENI varligin kimligiyle anahtarlanmali.
    for name in ("addCube", "addCylinder"):
        need(name, fixed_key + " = shape;")
    for name in ("addBooleanFuse", "addBooleanCommon", "addBooleanCut"):
        need(name, fixed_key + " = result;")
    # Eski off-by-one: ekleme ONCESI `emplace(models().size()-1, ...)` YASAK.
    for name in ("addCube", "addCylinder", "addBooleanFuse",
                 "addBooleanCommon", "addBooleanCut"):
        forbid(name, "emplace(")
    # Ekleme SIRASI: addStyledModel cagrisi anahtarlamadan ONCE olmali.
    for name in ("addCube", "addCylinder"):
        a = bodies[name].find("addStyledModel(")
        b = bodies[name].find(fixed_key)
        if a < 0 or b < 0 or a > b:
            errs.append(f"{name}: addStyledModel anahtarlamadan SONRA (off-by-one riski)")
    # lastLiveSolids yalniz CANLI varliklari dondurmeli.
    need("lastLiveSolids", "document_.indexOfEntity(it->first).has_value()")
    # addStyledModel gercek model ekleme yolunu kullanmali.
    need("addStyledModel", "document_.addModel(std::move(model));")
    # Temizleme gercekten magazayi bosaltmali.
    need("clearOccSolidStore", "occShapes_.clear();")
    # BELGE YASAM DONGUSU (item 5'in baglantisi): yeni belge / dosya acma
    # yollari BRep tablosunu SIFIRLAMALI. GUI (dosya diyalogu) burada
    # calistirilamadigi icin KAYNAK-SEKIL olarak kilitlenir.
    calls = text.count("clearOccSolidStore();")
    if calls < 3:
        errs.append(f"clearOccSolidStore(): {calls} cagri yeri (beklenen >= 3)")
    for fn_sig, name in (("void Application::newDocument()", "newDocument"),
                         ("void Application::openDocument()", "openDocument")):
        try:
            i, k = find_span(text, fn_sig, name)
        except HarnessError as exc:
            errs.append(str(exc))
            continue
        if "clearOccSolidStore();" not in text[i:k]:
            errs.append(f"{name}: belge temizleme yolunda clearOccSolidStore() YOK")
    if errs:
        raise HarnessError("kaynak-sekil kontrolu BASARISIZ:\n  - " + "\n  - ".join(errs))


CPP_PREAMBLE = r"""
// !!! OTOMATIK URETILDI — tests/run_occ_solid_ids.py tarafindan.
// Uretim govdeleri src/application.cpp'den BIREBIR cikarilmistir.
#include "model_maker/document.hpp"
#include "model_maker/geometry.hpp"
#include "model_maker/occ_geometry.hpp"
#include "model_maker/entity_id.hpp"

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
// GUVENLI MAGAZA ERISIMI: std::map::operator[] VAR OLMAYAN anahtar icin null
// sekil EKLER (magazayi kirletir ve null sekil uzerinde hacim hesabi coker).
// Bu erisimciler `find` kullanir ve eksik kaydi 0/false olarak raporlar.
static bool storeHas(const std::map<EntityId, TopoDS_Shape>& m, EntityId id) {
    return m.find(id) != m.end();
}
static double storeVolume(const std::map<EntityId, TopoDS_Shape>& m, EntityId id) {
    const auto it = m.find(id);
    return it == m.end() ? 0.0 : volumeOf(it->second);
}
static bool storeValid(const std::map<EntityId, TopoDS_Shape>& m, EntityId id) {
    const auto it = m.find(id);
    return it != m.end() && validShape(it->second);
}
// Seklin eksen hizali sinir kutusu (tam silindir / tum yukseklik kontrolu icin).
struct Box3 { double xmin, ymin, zmin, xmax, ymax, zmax; };
static bool storeBounds(const std::map<EntityId, TopoDS_Shape>& m, EntityId id,
                        Box3& out) {
    const auto it = m.find(id);
    if (it == m.end() || it->second.IsNull()) return false;
    Bnd_Box box;
    BRepBndLib::Add(it->second, box);
    if (box.IsVoid()) return false;
    box.Get(out.xmin, out.ymin, out.zmin, out.xmax, out.ymax, out.zmax);
    return true;
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

// Harness App: gercek Document + gercek `occShapes_` + uretim govdeleri.
struct HarnessApp {
    Document document_;
    std::map<EntityId, TopoDS_Shape> occShapes_;
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
    void clearOccSolidStore() noexcept;
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
        check(app.occShapes_.count(id0) == 1, "store has cube id");
        check(storeValid(app.occShapes_, id0), "cube shape valid (BRepCheck)");
        check(std::abs(storeVolume(app.occShapes_, id0) - kCubeVol) < kVolTol,
              "cube shape is a 2.6 box (vol 17.576)");

        app.addCylinder();
        check(app.document_.models().size() == 2, "cylinder appended (2 models)");
        const EntityId id1 = app.document_.modelId(1);
        check(app.occShapes_.count(id1) == 1, "store has cylinder id");
        check(storeValid(app.occShapes_, id1), "cylinder shape valid (BRepCheck)");
        check(std::abs(storeVolume(app.occShapes_, id1) - kCylVol) < kVolTol,
              "cylinder = full cylinder pi*r^2*h (vol 15.9279, not a 172-deg slice)");
        // TAM TUR KILIDI: bbox her iki yonde tam capi kapsamali (dilim kapsamaz)
        // ve Z 0..3.0 olmali (yukseklik 3.0; eski hata h=1.3 veriyordu).
        Box3 cb{};
        check(storeBounds(app.occShapes_, id1, cb), "cylinder bbox available");
        check(std::abs((cb.xmax - cb.xmin) - 2.0 * kCylRadius) < 0.02,
              "cylinder bbox X spans full diameter (2*1.3)");
        check(std::abs((cb.ymax - cb.ymin) - 2.0 * kCylRadius) < 0.02,
              "cylinder bbox Y spans full diameter (full turn, not a slice)");
        check(std::abs(cb.zmin - 0.0) < 0.02 && std::abs(cb.zmax - kCylHeight) < 0.02,
              "cylinder Z range is 0..3.0 (height 3.0, not 1.3)");
        // OFF-BY-ONE KILIDI: kutu kimligi hala KUTUYU tutmali (silindiri degil).
        check(std::abs(storeVolume(app.occShapes_, id0) - kCubeVol) < kVolTol,
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
        check(app.occShapes_.count(r) == 1, "store has result id");
        check(storeValid(app.occShapes_, r), "fuse result valid (BRepCheck)");
        const double vr = storeVolume(app.occShapes_, r);
        check(vr >= kCubeVol - kVolTol && vr <= kCubeVol + kCylVol + kVolTol,
              "fuse volume within [max, sum]");
        // Test 6 ile ortusen ama burada da acik: operandlar KORUNUR.
        check(app.occShapes_.count(a) == 1 && app.occShapes_.count(b) == 1,
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
        const double volA = storeVolume(app.occShapes_, a);
        const double volC = storeVolume(app.occShapes_, c);

        app.pushUndoSnapshot();          // silme komutunun gercek sozlesmesi
        app.document_.deleteModels({1}); // ortadaki (silindir) silinir
        check(app.document_.models().size() == 2, "one model removed");
        check(app.document_.modelId(0) == a, "id a now at index 0");
        check(app.document_.modelId(1) == c, "id c now at index 1");
        check(std::abs(storeVolume(app.occShapes_, a) - volA) < 1e-9,
              "shape a UNCHANGED after unrelated delete");
        check(std::abs(storeVolume(app.occShapes_, c) - volC) < 1e-9,
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
        const double volBefore = storeVolume(app.occShapes_, a);

        app.pushUndoSnapshot();
        app.document_.deleteModels({0});
        check(app.document_.models().size() == 1, "cube deleted");
        check(!app.document_.indexOfEntity(a).has_value(), "deleted id not resolvable");
        check(app.occShapes_.count(a) == 1,
              "store KEEPS deleted shape entry (documented policy: not pruned)");

        app.document_.undo();
        check(app.document_.models().size() == 2, "undo restores the model");
        check(app.document_.indexOfEntity(a).has_value(),
              "SAME id resolves again after undo");
        check(app.document_.modelId(app.document_.indexOfEntity(a).value()) == a,
              "id preserved through undo");
        check(std::abs(storeVolume(app.occShapes_, a) - volBefore) < 1e-9,
              "shape UNCHANGED by undo (store holds it; undo does NOT rebuild B-Rep)");

        app.document_.redo();
        check(!app.document_.indexOfEntity(a).has_value(), "redo deletes again");
        check(app.occShapes_.count(a) == 1,
              "shape still in store after redo (undo/redo never touch store)");
    }

    // --- Test 5: clearOccSolidStore + document clear ---
    {
        std::printf("Test 5: clearOccSolidStore empties store; document clear drops ids\n");
        HarnessApp app;
        app.addCube();
        app.addCylinder();
        const EntityId a = app.document_.modelId(0);
        check(!app.occShapes_.empty(), "store non-empty before clear");
        app.clearOccSolidStore();
        check(app.occShapes_.empty(), "store EMPTY after clearOccSolidStore");
        check(app.occShapes_.count(a) == 0, "old shape no longer reachable");
        app.document_.clear();
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
            check(storeHas(app.occShapes_, r) && storeValid(app.occShapes_, r),
                  (std::string(c.name) + ": result present + valid").c_str());
            const double vr = storeVolume(app.occShapes_, r);
            check(vr >= c.lo - kVolTol && vr <= c.hi + kVolTol,
                  (std::string(c.name) + ": result volume in range").c_str());
            std::printf("     (%s volume = %.4f)\n", c.name, vr);
            // Sonucun GERCEKTEN hesaplandigini kanitla (bos sonuc yeterli degil).
            if (std::string(c.name) == "common")
                check(vr > 1e-6, "common: result NON-EMPTY (operands really intersected)");
            if (std::string(c.name) == "cut")
                check(vr < kCubeVol - 1e-6, "cut: result SMALLER than cube (material removed)");
            check(app.occShapes_.count(a) == 1,
                  (std::string(c.name) + ": operand cube STILL in store").c_str());
            check(app.occShapes_.count(b) == 1,
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
        if opts["shape_check"]:
            assert_source_shape(bodies, text)
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
        cmd += ["-l" + lib for lib in OCC_LIBS]
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
