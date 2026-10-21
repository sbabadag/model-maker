// KAYNAK KATISI MMW8 ROUND-TRIP (Phase W3): weld + display model + OCC kati
// kayit/ac sonrasi korunur (displayEntityId, boyut, cift-tarafli, kati hacmi,
// shop assembly). Gercek OCCT + Document save/load.
#include "model_maker/document.hpp"
#include "model_maker/occ_solid_store.hpp"
#include "model_maker/occ_geometry.hpp"
#include "model_maker/weld_geometry.hpp"
#include "model_maker/weld.hpp"

#include <BRepGProp.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <GProp_GProps.hxx>
#include <TopoDS_Shape.hxx>

#include <cmath>
#include <cstdio>
#include <filesystem>

using namespace mm;

static int failures = 0;
static void check(bool ok, const char* label) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", label);
    if (!ok) ++failures;
}
static double vol(const TopoDS_Shape& s) {
    if (s.IsNull()) return -1.0;
    GProp_GProps p;
    BRepGProp::VolumeProperties(s, p);
    return p.Mass();
}

int main() {
    // Kolon + levha (T-joint) + fillet kaynagi, document + store uzerinden.
    Document doc;
    doc.setOccStore(makeOccSolidStore());
    OccSolidStore* store = asSolidStore(&doc.occStore());
    if (!store) { std::printf("FAIL store\n"); return 1; }

    TopoDS_Shape colSolid = BRepPrimAPI_MakeBox(gp_Pnt(-100, -100, 0), 100, 200, 100).Solid();
    TopoDS_Shape plateSolid = BRepPrimAPI_MakeBox(gp_Pnt(0, -5, 40), 50, 10, 20).Solid();
    doc.addModel(shapeToWireframeWithFaces(colSolid, 0.15));
    const EntityId colId = doc.modelId(doc.models().size() - 1);
    doc.addModel(shapeToWireframeWithFaces(plateSolid, 0.15));
    const EntityId plateId = doc.modelId(doc.models().size() - 1);
    store->set(colId, colSolid);
    store->set(plateId, plateSolid);

    Weld w;
    w.mainPart = colId;
    w.secondaryPart = plateId;
    w.type = WeldType::Fillet;
    w.size = 5.0;
    w.doubleSided = false;
    w.weldClass = WeldClass::Shop;
    std::string reason;
    auto solids = buildWeldSolids(w, colSolid, plateSolid, 5.0, reason);
    check(solids.size() == 1, "kaynak kati uretildi");
    if (solids.empty()) return 1;
    w.geometrySupported = true;
    doc.addWeld(std::move(w), shapeToWireframeWithFaces(solids[0], 0.15));
    const Weld& created = doc.welds().back();
    check(created.displayEntityId != kInvalidEntityId, "displayEntityId atandi");
    store->set(created.displayEntityId, solids[0]);
    check(doc.assemblies().size() == 1, "shop weld tek assembly kurdu");

    // Kaydet -> temiz belgeye yukle.
    const auto path = std::filesystem::temp_directory_path() / "mmw8_weld_roundtrip.mmw8";
    doc.save(path);

    Document d2;
    d2.setOccStore(makeOccSolidStore());
    d2.load(path);
    OccSolidStore* store2 = asSolidStore(&d2.occStore());
    check(d2.welds().size() == 1, "yukleme: 1 kaynak");
    check(d2.assemblies().size() == 1, "yukleme: shop assembly korundu");
    if (d2.welds().size() != 1) return 1;
    const Weld& w2 = d2.welds()[0];
    check(w2.size == 5.0, "yukleme: boyut korundu");
    check(w2.geometrySupported, "yukleme: geometrySupported korundu");
    check(w2.displayEntityId != kInvalidEntityId, "yukleme: displayEntityId korundu");
    const TopoDS_Shape* s2 = store2 ? store2->find(w2.displayEntityId) : nullptr;
    check(s2 != nullptr, "yukleme: kaynak kati geri geldi");
    if (s2) {
        std::printf("  kaynak kati hacmi yukleme sonrasi = %.3f (beklenen 250.0)\n", vol(*s2));
        check(std::fabs(vol(*s2) - 250.0) < 1.0, "yukleme: kaynak kati hacmi 250 mm^3");
    }

    std::filesystem::remove(path);
    std::printf("%s\n", failures == 0 ? "ALL PASS" : "SOME FAILED");
    return failures == 0 ? 0 : 1;
}
