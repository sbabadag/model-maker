// KOPYA + B-REP: "kopyalanan componentlarda parametre degisikligi birlesimi
// degistirmiyor" bug'inin MEKANIZMA testi (gercek OCCT).
//
// Teshis: Document::copyModels yalniz WIREFRAME kopyalar; OCC magazasinda kopya
// EntityId icin kati YOKTUR. ComponentMaterializer::recompute kaynak (weld)
// adiminda store_->find(part) null donunce "Weld parcasi B-Rep yok" deyip TUM
// degisikligi atomik geri alir -> kullanici parametreyi uygular ama birlesim
// ayni kalir.
//
// Bu test iki seyi kanitlar:
//   1) Kopya parcanin magazada B-Rep'i YOKTUR (kok neden),
//   2) Kopya, KALICI profil ozelliklerinden yeniden uretilebilir ve dogru
//      konumda kati verir (Application'daki ensureCutSolid duzeltmesinin yolu).
//
// Linux'ta OCC-acik yapilandirmada kosar (build-occ-hermes).
#include "model_maker/document.hpp"
#include "model_maker/occ_geometry.hpp"
#include "model_maker/occ_solid_store.hpp"
#include "model_maker/profile_database.hpp"

#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>

#include <cmath>
#include <cstdio>

using namespace mm;

static int failures = 0;
static void check(bool ok, const char* what) {
    std::printf("%s  %s\n", ok ? "GECTI" : "KALDI", what);
    if (!ok) ++failures;
}

static SteelProfile makeProfile(const char* name, double h, double b, double tw, double tf) {
    SteelProfile p;
    p.name = name;
    p.height = h;
    p.width = b;
    p.plateThickness = tw;
    p.flangeThickness = tf;
    return p;
}

static void bbox(const TopoDS_Shape& s, double out[6]) {
    Bnd_Box box;
    BRepBndLib::Add(s, box);
    box.Get(out[0], out[1], out[2], out[3], out[4], out[5]);
}

int main() {
    const SteelProfile ipe200 = makeProfile("IPE200", 200.0, 100.0, 5.6, 8.5);

    Document doc;
    doc.setOccStore(makeOccSolidStore());
    auto* store = asSolidStore(&doc.occStore());
    if (!store) {
        std::printf("KALDI  OCC magazasi enjekte edilemedi\n");
        return 1;
    }

    // Profilli kiris modeli: kalici ozellikler (profil + eksen + bolum normali).
    doc.pushSnapshot();
    {
        WireframeModel beamModel = WireframeModel::line({0, 0, 0}, {0, 0, 3000});
        EntityProperties props = beamModel.properties();
        props.profileName = "IPE200";
        props.axisFromX = 0.0; props.axisFromY = 0.0; props.axisFromZ = 0.0;
        props.axisToX = 0.0; props.axisToY = 0.0; props.axisToZ = 3000.0;
        props.sectionNormalX = 0.0; props.sectionNormalY = 1.0; props.sectionNormalZ = 0.0;
        beamModel.setProperties(std::move(props));
        doc.addModel(std::move(beamModel));
    }
    const EntityId beam = doc.modelIds().back();

    // Application'in yaptigi gibi taban katisi uret (ensureCutSolid).
    const TopoDS_Shape original =
        extrudeProfileSolid(ipe200, {0, 0, 0}, {0, 0, 3000}, 0.0, {0, 1, 0});
    check(!original.IsNull(), "A: orijinal kiris katisi uretildi");
    store->set(beam, original);
    check(store->activeVersion(beam) != kInvalidVersionId, "A: orijinalin B-Rep'i magazada");

    // KOPYA (2000 mm oteye) — Application::copySelectionBy ile ayni yol.
    doc.pushSnapshot();
    doc.copyModels({*doc.indexOfEntity(beam)}, Vec3{2000, 0, 0});
    const EntityId beamCopy = doc.modelIds().back();
    check(beamCopy != beam, "B: kopya yeni kimlik aldi");

    // 1) KOK NEDEN: kopyanin B-Rep'i yok.
    check(store->activeVersion(beamCopy) == kInvalidVersionId,
          "B: KOPYA icin magazada B-Rep YOK (kok neden)");
    check(store->find(beamCopy) == nullptr,
          "B: materializer store_->find(kopya) null -> kaynak adimi rollback eder");

    // 2) DUZELTME YOLU: kalici ozelliklerden yeniden uret (ensureCutSolid mantigi).
    const std::size_t copyIdx = *doc.indexOfEntity(beamCopy);
    const EntityProperties copyProps = doc.models()[copyIdx].properties();
    check(std::fabs(copyProps.axisFromX - 2000.0) < 1e-9,
          "C: kopyanin ekseni otelemeyle tasinmis (kalici ozellik)");
    const TopoDS_Shape regenerated =
        extrudeProfileSolid(ipe200,
                            {copyProps.axisFromX, copyProps.axisFromY, copyProps.axisFromZ},
                            {copyProps.axisToX, copyProps.axisToY, copyProps.axisToZ},
                            copyProps.profileRotation,
                            {copyProps.sectionNormalX, copyProps.sectionNormalY,
                             copyProps.sectionNormalZ});
    check(!regenerated.IsNull(), "C: kopya katisi yeniden uretilebildi");
    store->set(beamCopy, regenerated);
    check(store->activeVersion(beamCopy) != kInvalidVersionId,
          "C: yeniden uretimden sonra kopyanin B-Rep'i VAR");

    double ob[6]{}, cb[6]{};
    bbox(original, ob);
    bbox(regenerated, cb);
    check(std::fabs((cb[0] - ob[0]) - 2000.0) < 1e-6 && std::fabs((cb[3] - ob[3]) - 2000.0) < 1e-6,
          "C: yeniden uretilen kati +2000 mm otede (dogru konum)");

    std::printf(failures == 0 ? "TUM KONTROLLER GECTI\n" : "%d KONTROL KALDI\n", failures);
    return failures == 0 ? 0 : 1;
}
