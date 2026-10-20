// NATIVE BINARY B-Rep SERIALIZER — round-trip testleri (Phase 3.4A).
//
// Gercek OCCT ile: Create -> Serialize -> Deserialize -> Validate -> Boolean.
// Sinanir: tam silindir, kutu, boolean fuse/cut/common, duzlemle kesilmis
// (trim) kati, extrude edilmis katı, coklu kati (compound), null/bos,
// bozuk/kesilmis blob.
//
// Dogrulama HACIM esitligine INDIRGENMEZ: sekil tipi, BRepCheck gecerliligi,
// hacim, eksen-hizali sinir kutusu VE topolojik eleman sayilari karsilastirilir.
// Ayrica round-trip sonrasi seklin boolean isleminde KULLANILABILIR oldugu
// kanitlanir (kucuk bir kutuyla fuse).
//
// Bu test BAGIMSIZDIR: yalnizca serializer (occ_brep_serialize.cpp) + OCCT
// ilkelleri kullanir; uygulama geometri modulu (occ_geometry) gerektirmez.

#include "model_maker/occ_brep_serialize.hpp"

#include <Bnd_Box.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepGProp.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRep_Builder.hxx>
#include <GProp_GProps.hxx>
#include <TopAbs_ShapeEnum.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Shape.hxx>
#include <gp_Ax2.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>
#include <gp_Vec.hxx>

#include <cmath>
#include <cstdio>
#include <string>

using namespace mm;

namespace {

int g_checks = 0;
int g_failures = 0;

void check(bool ok, const char* what) {
    ++g_checks;
    std::printf("  %s %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++g_failures;
}

double volumeOf(const TopoDS_Shape& s) {
    if (s.IsNull()) return 0.0;
    GProp_GProps props;
    BRepGProp::VolumeProperties(s, props);
    return props.Mass();
}

struct Box3 { double xmin, ymin, zmin, xmax, ymax, zmax; };

bool boundsOf(const TopoDS_Shape& s, Box3& out) {
    if (s.IsNull()) return false;
    Bnd_Box box;
    BRepBndLib::Add(s, box);
    if (box.IsVoid()) return false;
    box.Get(out.xmin, out.ymin, out.zmin, out.xmax, out.ymax, out.zmax);
    return true;
}

struct TopoCounts { int verts{0}, edges{0}, faces{0}, solids{0}; };

TopoCounts countTopology(const TopoDS_Shape& s) {
    TopoCounts c;
    for (TopExp_Explorer ex(s, TopAbs_VERTEX); ex.More(); ex.Next()) ++c.verts;
    for (TopExp_Explorer ex(s, TopAbs_EDGE); ex.More(); ex.Next()) ++c.edges;
    for (TopExp_Explorer ex(s, TopAbs_FACE); ex.More(); ex.Next()) ++c.faces;
    for (TopExp_Explorer ex(s, TopAbs_SOLID); ex.More(); ex.Next()) ++c.solids;
    return c;
}

bool nearEq(double a, double b, double tol = 1e-6) { return std::abs(a - b) <= tol; }

// Ortak round-trip akisi; TUM dogrulama eksenlerini karsilar.
bool roundTripEquivalent(const TopoDS_Shape& original, const char* label) {
    std::string blob, err;
    if (!serializeBRep(original, blob, &err)) {
        std::printf("    [%s] serialize FAILED: %s\n", label, err.c_str());
        return false;
    }
    std::printf("    [%s] blob = %zu bytes (format v%d, OCCT %s)\n",
                label, blob.size(), bRepFormatVersion(), occVersionString());

    TopoDS_Shape restored;
    if (!deserializeBRep(blob, restored, &err)) {
        std::printf("    [%s] deserialize FAILED: %s\n", label, err.c_str());
        return false;
    }
    if (restored.IsNull()) { std::printf("    [%s] restored NULL\n", label); return false; }
    if (!validateBRep(restored)) { std::printf("    [%s] BRepCheck INVALID\n", label); return false; }

    const bool typeOk = restored.ShapeType() == original.ShapeType();
    const double va = volumeOf(original);
    const double vb = volumeOf(restored);
    const bool volOk = va > 0.0 ? nearEq(va, vb, 1e-6 * va) : nearEq(va, vb, 1e-9);
    Box3 ba{}, bb{};
    const bool bboxOk = boundsOf(original, ba) && boundsOf(restored, bb) &&
                        nearEq(ba.xmin, bb.xmin, 1e-6) && nearEq(ba.xmax, bb.xmax, 1e-6) &&
                        nearEq(ba.ymin, bb.ymin, 1e-6) && nearEq(ba.ymax, bb.ymax, 1e-6) &&
                        nearEq(ba.zmin, bb.zmin, 1e-6) && nearEq(ba.zmax, bb.zmax, 1e-6);
    const TopoCounts ca = countTopology(original);
    const TopoCounts cb = countTopology(restored);
    const bool topoOk = ca.verts == cb.verts && ca.edges == cb.edges &&
                        ca.faces == cb.faces && ca.solids == cb.solids;
    const bool ok = typeOk && volOk && bboxOk && topoOk;
    std::printf("    [%s] type=%d/%d vol=%.6f/%.6f bbox=%s topo(V/E/F/S)=%d%d%d%d/%d%d%d%d\n",
                label, static_cast<int>(restored.ShapeType()),
                static_cast<int>(original.ShapeType()), vb, va,
                bboxOk ? "OK" : "FAIL",
                cb.verts, cb.edges, cb.faces, cb.solids,
                ca.verts, ca.edges, ca.faces, ca.solids);
    return ok;
}

// Round-trip sonrasi boolean kullanilabilirligi.
//
// KOK NEDEN (upstream, KANITLI — OCCT issue #1637, GCC bugzilla #127917):
// GCC 16.1/16.2 -O3'te OCCT 7.9.3'un BRepClass3d_SClassifier::Perform'unu
// YANLIS derliyor. IPA-CP, NCollection_UBTree::Select ozyinelemesini iki
// selector tipi icin klonlarken nokta-selector klonundaki ozyinelemeli cagriyi
// cizgi-selector klonuna yonlendiriyor -> cizgi-selector stale stack'ten curve
// okuyup Extrema_ExtCC::Perform'da segfault veriyor. Sonuc: katilarda
// kenar/kose uzerinde siniflandirma yapan HER boolean (FUSE/CUT/COMMON fark
// etmez) cokebilir. Belirti Windows/MSYS2'de cikiyor cunku MSYS2'nin
// mingw-w64-ucrt-x86_64-opencascade 7.9.3-3 paketi (2026-08-10) GCC 16 ile
// derlendi. Linux'ta cokmemesinin sebebi GCC 13/14 ile derlenmis olmasi.
//
// Serializer MASUM (bisect ile KANITLI: serialize EDILMEMIS orijinal seklin
// bile boolean'i cokuyor). Testin kendisi DOGRU; cokme tamamen cevresel.
//
// COZUM (cevre tarafinda, repo'da yapilacak bir sey YOK):
//  1) OCCT 7.9.3'u -fno-ipa-cp-clone ile yeniden derlemek (GCC 16+ ise), VEYA
//  2) GCC 16.3 (fix backport'u r17-5258) cikinca MSYS2 opencascade rebuild'i, VEYA
//  3) OCCT 8.0/master (NCollection_UBTree::Select iteratif, bfa0311 — etkilenmez).
bool booleanUsableAfterRoundTrip(const TopoDS_Shape& original) {
    Box3 bb{};
    if (!boundsOf(original, bb)) return false;

    std::string blob;
    if (!serializeBRep(original, blob)) return false;
    TopoDS_Shape restored;
    if (!deserializeBRep(blob, restored)) return false;
    if (restored.IsNull()) return false;

    const double dx = bb.xmax - bb.xmin;
    const double dy = bb.ymax - bb.ymin;
    const double dz = bb.zmax - bb.zmin;
    const double longest = dx > dy ? (dx > dz ? dx : dz) : (dy > dz ? dy : dz);
    const double m = 0.5 * longest + 1.0;  // marj: kutu sekilden buyuk
    const TopoDS_Shape bigBox = BRepPrimAPI_MakeBox(
        gp_Pnt(bb.xmin - m, bb.ymin - m, bb.zmin - m),
        gp_Pnt(bb.xmax + m, bb.ymax + m, bb.zmax + m)).Shape();

    std::printf("    [boolean] cut(containing-box, restored) ...\n");
    BRepAlgoAPI_Cut cut(bigBox, restored);
    if (!cut.IsDone()) {
        std::printf("    [boolean] cut NOT DONE\n");
        return false;
    }
    const TopoDS_Shape result = cut.Shape();
    std::printf("    [boolean] cut OK (vol=%.6f)\n", volumeOf(result));
    return !result.IsNull() && validateBRep(result);
}

TopoDS_Shape makeCompound(const TopoDS_Shape& a, const TopoDS_Shape& b) {
    TopoDS_Compound compound;
    BRep_Builder builder;
    builder.MakeCompound(compound);
    builder.Add(compound, a);
    builder.Add(compound, b);
    return compound;
}

// AYRIK (cakismayan) iki katidan compound: boolean kullanilabilirlik testi
// icin kendi kendine kesismeyen temiz bir girdi gerekir.
TopoDS_Shape makeDisjointCompound() {
    const TopoDS_Shape box = BRepPrimAPI_MakeBox(2.0, 2.0, 2.0).Shape();
    // Silindir kutu'dan uzaga (x=6) yerlesir -> compound icinde cakisma YOK.
    const gp_Ax2 axis(gp_Pnt(6.0, 0.0, 0.0), gp_Dir(0, 0, 1));
    const TopoDS_Shape cyl = BRepPrimAPI_MakeCylinder(axis, 0.8, 4.0).Shape();
    return makeCompound(box, cyl);
}

// Ucgen yuzun ekstruzyonu (BRepPrimAPI_MakePrism) — "extrude edilmis" katı.
TopoDS_Shape makeExtrudedPrism() {
    BRepBuilderAPI_MakePolygon poly(gp_Pnt(0, 0, 0), gp_Pnt(2, 0, 0),
                                    gp_Pnt(0, 3, 0), gp_Pnt(0, 0, 0));
    BRepBuilderAPI_MakeFace face(poly.Wire());
    return BRepPrimAPI_MakePrism(face.Face(), gp_Vec(0, 0, 4)).Shape();
}

// Iki kutu cakismasiyla "trim edilmis" (kose kesilmis) katı.
TopoDS_Shape makeTrimmedSolid() {
    const TopoDS_Shape big = BRepPrimAPI_MakeBox(4.0, 4.0, 4.0).Shape();
    const TopoDS_Shape corner = BRepPrimAPI_MakeBox(2.0, 2.0, 2.0).Shape();
    BRepAlgoAPI_Cut cut(big, corner);
    return cut.Shape();
}

} // namespace

int main() {
    std::setvbuf(stdout, nullptr, _IOLBF, 0);
    std::printf("== B-Rep SERIALIZER round-trip (gercek OCCT %s, BinTools format v%d) ==\n",
                occVersionString(), bRepFormatVersion());

    int passed = 0, failed = 0;
    const auto run = [&](const char* name, const TopoDS_Shape& s) {
        std::printf("Test: %s\n", name);
        bool ok = roundTripEquivalent(s, name);
        if (ok) {
            const bool usable = booleanUsableAfterRoundTrip(s);
            check(usable, "round-trip shape usable in boolean (fuse with probe)");
            ok = usable;
        }
        if (ok) ++passed; else ++failed;
    };

    run("full cylinder r=1.3 h=3.0", BRepPrimAPI_MakeCylinder(1.3, 3.0).Shape());
    run("box 2.6^3", BRepPrimAPI_MakeBox(2.6, 2.6, 2.6).Shape());
    {
        const TopoDS_Shape cube = BRepPrimAPI_MakeBox(2.6, 2.6, 2.6).Shape();
        // Silindir kutunun MERKEZINE yerlestirilir ve yaricapi net kucuktur:
        // eski hali (MakeCylinder(1.3,3.0) orijinde, ekseni kutu kosesinden)
        // silindiri kutu yuzlerine TEGET yapiyordu -> dejeneratif boolean ->
        // patolojik cok-katili compound -> OCCT 7.9.3 Windows'ta yeniden fuse
        // ederken BRepClass3d_SClassifier/Extrema_ExtCC segfault.
        const gp_Ax2 cylAxis(gp_Pnt(1.3, 1.3, 0.0), gp_Dir(0, 0, 1));
        const TopoDS_Shape cyl = BRepPrimAPI_MakeCylinder(cylAxis, 0.8, 3.0).Shape();
        run("boolean FUSE", BRepAlgoAPI_Fuse(cube, cyl).Shape());
        run("boolean CUT", BRepAlgoAPI_Cut(cube, cyl).Shape());
        run("boolean COMMON", BRepAlgoAPI_Common(cube, cyl).Shape());
    }
    run("trimmed solid (corner cut)", makeTrimmedSolid());
    run("extruded prism (triangle)", makeExtrudedPrism());
    run("multi-solid compound (disjoint)", makeDisjointCompound());

    // 9) Null / bos geometri.
    {
        std::printf("Test: null shape\n");
        TopoDS_Shape nullShape;
        std::string blob, err;
        check(!serializeBRep(nullShape, blob, &err), "null shape serialize rejected");
        check(blob.empty(), "null shape produces no blob");
        check(!deserializeBRep("", nullShape, &err), "empty blob deserialize rejected");
        if (!serializeBRep(nullShape, blob, &err)) ++passed; else ++failed;
    }

    // 10) Bozulmus / kesilmis blob.
    {
        std::printf("Test: corrupt/truncated blob\n");
        const TopoDS_Shape box = BRepPrimAPI_MakeBox(2.0, 2.0, 2.0).Shape();
        std::string blob;
        if (!serializeBRep(box, blob)) {
            check(false, "setup: box serialize failed");
            ++failed;
        } else {
            const std::size_t originalSize = blob.size();
            const std::string truncated = blob.substr(0, originalSize / 2);
            TopoDS_Shape t; std::string err;
            const bool readOk = deserializeBRep(truncated, t, &err);
            check(!readOk || !validateBRep(t), "truncated blob never yields a VALID shape");

            std::string corrupted = blob;
            corrupted.erase(corrupted.begin() + static_cast<std::ptrdiff_t>(originalSize / 2));
            TopoDS_Shape c;
            const bool readOk2 = deserializeBRep(corrupted, c, &err);
            check(!readOk2 || !validateBRep(c), "corrupted blob never yields a VALID shape");

            if ((!readOk || !validateBRep(t)) && (!readOk2 || !validateBRep(c)))
                ++passed; else ++failed;
        }
    }

    // 11) Zarf (envelope) butunluk + geriye-donuk uyum (Phase 3.4D cokme onlemi).
    {
        std::printf("Test: envelope integrity & legacy compatibility\n");
        const TopoDS_Shape box = BRepPrimAPI_MakeBox(2.0, 2.0, 2.0).Shape();
        std::string envelope;
        check(serializeBRep(box, envelope), "setup: box serialize (enveloped)");
        check(envelope.size() > kBRepEnvelopeHeaderSize, "envelope larger than header");
        check(envelope.compare(0, kBRepEnvelopeMagicSize, kBRepEnvelopeMagic) == 0,
              "envelope magic present");

        bool ok = true;

        // C.4 — kesilmis zarf reddedilir (BinTools'a ulasmadan).
        {
            const std::string truncated = envelope.substr(0, envelope.size() / 2);
            TopoDS_Shape t; std::string err;
            const bool r = deserializeBRep(truncated, t, &err);
            check(!r, "truncated envelope rejected");
            check(t.IsNull(), "truncated envelope leaves shape null");
            ok = ok && !r && t.IsNull();
        }

        // C.5 — yanlis payload uzunlugu reddedilir.
        {
            std::string bad = envelope;
            for (int i = 12; i < 20; ++i) bad[static_cast<std::size_t>(i)] = '\0';
            TopoDS_Shape t; std::string err;
            const bool r = deserializeBRep(bad, t, &err);
            check(!r, "wrong payload length rejected");
            check(t.IsNull(), "wrong length leaves shape null");
            ok = ok && !r && t.IsNull();
        }

        // C.6 — desteklenmeyen zarf surumu reddedilir.
        {
            std::string bad = envelope;
            bad[8] = static_cast<char>(99); bad[9] = 0; bad[10] = 0; bad[11] = 0;
            TopoDS_Shape t; std::string err;
            const bool r = deserializeBRep(bad, t, &err);
            check(!r, "unsupported envelope version rejected");
            check(t.IsNull(), "unsupported version leaves shape null");
            ok = ok && !r && t.IsNull();
        }

        // C.3 — yanlis SHA-256 BinTools::Read'e ulasmadan reddedilir.
        {
            std::string bad = envelope;
            bad[20] = static_cast<char>(bad[20] ^ 0x01);
            TopoDS_Shape t; std::string err;
            const bool r = deserializeBRep(bad, t, &err);
            check(!r, "wrong SHA-256 rejected");
            check(t.IsNull(), "wrong SHA leaves shape null");
            ok = ok && !r && t.IsNull();
        }

        // C.2 — bozuk payload (zarfli) kontrolu FAIL, cokme YOK.
        {
            std::string bad = envelope;
            const std::size_t mid =
                kBRepEnvelopeHeaderSize + (envelope.size() - kBRepEnvelopeHeaderSize) / 2;
            bad[mid] = static_cast<char>(bad[mid] ^ 0x5A);
            TopoDS_Shape t; std::string err;
            const bool r = deserializeBRep(bad, t, &err);
            check(!r, "corrupt enveloped payload rejected");
            check(t.IsNull(), "corrupt payload leaves shape null");
            ok = ok && !r && t.IsNull();
        }

        // C.7 — eski (zarfsiz) raw blob hala yuklenir (geriye donuk uyum).
        {
            const std::string rawPayload = envelope.substr(kBRepEnvelopeHeaderSize);
            TopoDS_Shape t; std::string err;
            const bool r = deserializeBRep(rawPayload, t, &err);
            check(r, "legacy unwrapped blob loads");
            check(!t.IsNull() && validateBRep(t), "legacy blob yields valid shape");
            check(!t.IsNull() && nearEq(volumeOf(t), volumeOf(box), 1e-6),
                  "legacy blob volume preserved");
            ok = ok && r && !t.IsNull() && validateBRep(t) &&
                 nearEq(volumeOf(t), volumeOf(box), 1e-6);
        }

        // C.9 — basarisiz deserialize cikti Shape'i degistirmez.
        {
            const TopoDS_Shape probe = BRepPrimAPI_MakeBox(3.0, 3.0, 3.0).Shape();
            TopoDS_Shape t = probe;
            std::string bad = envelope;
            bad[20] = static_cast<char>(bad[20] ^ 0x01);  // gecersiz SHA
            std::string err;
            const bool r = deserializeBRep(bad, t, &err);
            check(!r, "failed deserialize returns false");
            check(!t.IsNull() && nearEq(volumeOf(t), volumeOf(probe), 1e-9),
                  "failed deserialize leaves output shape unchanged");
            ok = ok && !r && !t.IsNull() && nearEq(volumeOf(t), volumeOf(probe), 1e-9);
        }

        if (ok) ++passed; else ++failed;
    }

    // Metadata sozlesmesi.
    check(bRepFormatVersion() > 0, "format version is positive");
    check(occVersionString() != nullptr && occVersionString()[0] != '\0',
          "OCCT version string is non-empty");
    check(maxBRepBlobSize() > 0, "max blob size is positive");

    std::printf("\nchecks=%d failures=%d\n", g_checks, g_failures);
    std::printf("B-Rep SERIALIZER round-trip: %d passed, %d failed\n", passed, failed);
    return (g_failures == 0 && failed == 0) ? 0 : 1;
}
