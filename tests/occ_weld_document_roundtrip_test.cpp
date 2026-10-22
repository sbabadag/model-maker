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
    std::printf("  kaynak kati adedi = %zu (tam cevre: her temas kenari)\n", solids.size());
    check(!solids.empty(), "kaynak kati uretildi");
    if (solids.empty()) {
        std::printf("  neden='%s'\n", reason.c_str());
        return 1;
    }
    // TAM CEVRE KAYNAK: 4 temas kenari -> 4 fillet; display kati bunlarin
    // birlesimi (materializer'daki buildWeldGeometry ile ayni yol).
    TopoDS_Shape fused = solids[0];
    for (std::size_t i = 1; i < solids.size(); ++i)
        fused = booleanFuseShape(fused, solids[i]);
    check(!fused.IsNull(), "tam cevre kati birlestirildi");
    w.geometrySupported = true;
    doc.addWeld(std::move(w), shapeToWireframeWithFaces(fused, 0.15));
    const Weld& created = doc.welds().back();
    check(created.displayEntityId != kInvalidEntityId, "displayEntityId atandi");
    store->set(created.displayEntityId, fused);
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
        std::printf("  kaynak kati hacmi yukleme sonrasi = %.3f (beklenen 750.0, tam cevre)\n",
                    vol(*s2));
        check(std::fabs(vol(*s2) - 750.0) < 1.0, "yukleme: kaynak kati hacmi 750 mm^3 (tam cevre)");
    }

    std::filesystem::remove(path);
    std::printf("%s\n", failures == 0 ? "ALL PASS" : "SOME FAILED");
    return failures == 0 ? 0 : 1;
}
