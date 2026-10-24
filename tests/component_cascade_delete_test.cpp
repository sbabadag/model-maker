// COMPONENT KASKAD SILME: ana ya da ikincil parca silinirse TUM component
// (kayit + levhalar + bulon gruplari + component'a ait kesmeler) silinir;
// ilgisiz parca/component'a dokunulmaz; undo hepsini geri getirir.
#include "model_maker/component_engine.hpp"
#include "model_maker/document.hpp"

#include <cstdio>

using namespace mm;

static int failures = 0;
static void check(bool ok, const char* label) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", label);
    if (!ok) ++failures;
}

struct Scene {
    Document doc;
    EntityId col{}, beam{}, other{};
    EntityId plate{}, boltDisp{}, compId{};
};

static Scene build() {
    Scene s;
    s.doc.addModel(WireframeModel::line({0, 0, 0}, {0, 0, 3000}));      s.col = s.doc.modelId(0);
    s.doc.addModel(WireframeModel::line({0, 0, 3000}, {4000, 0, 3000})); s.beam = s.doc.modelId(1);
    s.doc.addModel(WireframeModel::line({9000, 0, 0}, {9000, 0, 3000})); s.other = s.doc.modelId(2);
    s.doc.addModel(WireframeModel::line({100, 0, 3000}, {140, 20, 3030}));
    s.plate = s.doc.modelId(3);
    const std::size_t bi = s.doc.addBoltGroup(BoltGroup{}, WireframeModel::line({200, 0, 0}, {200, 0, 0}));
    s.boltDisp = s.doc.boltGroups()[bi].displayEntityId;
    ComponentInstanceRecord rec;
    rec.typeId = L"tekla_shear_plate_146";
    rec.mainPart = s.col;
    rec.secondaryParts = {s.beam};
    rec.ownedPlateIds = {s.plate};
    rec.ownedBoltGroupIds = {s.doc.boltGroups()[bi].id};
    s.doc.addComponentInstance(rec);
    s.compId = s.doc.componentInstances().back().id;
    return s;
}

static void deleteById(Scene& s, EntityId id) {
    const auto idx = s.doc.indexOfEntity(id);
    if (!idx) return;
    s.doc.deleteModels({*idx});
}

int main() {
    {
        Scene s = build();
        check(s.doc.componentInstances().size() == 1 && s.doc.boltGroups().size() == 1, "kurulum");
        deleteById(s, s.beam);  // IKINCIL silinir
        check(s.doc.componentInstances().empty(), "ikincil silinince component kaydi silindi");
        check(s.doc.boltGroups().empty(), "bulon grubu silindi");
        check(!s.doc.indexOfEntity(s.plate), "levha silindi");
        check(!s.doc.indexOfEntity(s.boltDisp), "bulon display silindi");
        check(s.doc.indexOfEntity(s.col).has_value() && s.doc.indexOfEntity(s.other).has_value(),
              "ana kolon ve ilgisiz parca duruyor");
        check(s.doc.models().size() == 2, "geriye yalniz kolon + ilgisiz parca");
    }
    {
        Scene s = build();
        deleteById(s, s.col);   // ANA silinir
        check(s.doc.componentInstances().empty() && s.doc.boltGroups().empty() &&
                  !s.doc.indexOfEntity(s.plate) && !s.doc.indexOfEntity(s.boltDisp),
              "ana silinince tum component silindi");
        check(s.doc.indexOfEntity(s.beam).has_value(), "kiris duruyor");
    }
    {
        Scene s = build();
        deleteById(s, s.other);  // ilgisiz parca
        check(s.doc.componentInstances().size() == 1 && s.doc.boltGroups().size() == 1 &&
                  s.doc.indexOfEntity(s.plate).has_value(),
              "ilgisiz parca silinince component KORUNUR");
    }
    {
        Scene s = build();
        s.doc.pushSnapshot();  // undo kaydini ac (uygulamada her komut oncesi yapilir)
        deleteById(s, s.beam);
        s.doc.undo();  // TEK undo adimi hepsini geri getirmeli
        std::printf("  undo sonrasi: comp=%zu bolt=%zu plate=%d beam=%d models=%zu\n",
                    s.doc.componentInstances().size(), s.doc.boltGroups().size(),
                    (int)s.doc.indexOfEntity(s.plate).has_value(), (int)s.doc.indexOfEntity(s.beam).has_value(),
                    s.doc.models().size());
        check(s.doc.componentInstances().size() == 1 && s.doc.boltGroups().size() == 1 &&
                  s.doc.indexOfEntity(s.plate).has_value() && s.doc.indexOfEntity(s.beam).has_value(),
              "undo component + levha + bulon + kirisi geri getirir");
    }
    std::printf("%s (%d failure)\n", failures == 0 ? "ALL PASS" : "SOME FAILED", failures);
    return failures == 0 ? 0 : 1;
}
