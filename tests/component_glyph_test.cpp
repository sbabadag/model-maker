// COMPONENT SEMBOLU (GLYPH) — portatif cekirdek testi (OCC gerekmez).
//
// collectComponentGlyphs TEK KAYNAKTIR: renderer bu listeyi cizer, application
// ayni listeyle hit-test yapar. Bu test iki uretim yolunu (End Plate + universal
// parametrik component/Fin Plate) ve tekillesme/atlama kurallarini kilitler.
//
// Derleme (Linux, OCC'siz):
//   g++ -std=gnu++20 -O1 -I include tests/component_glyph_test.cpp \
//       build/libmodel_maker_core.a -o component_glyph_test && ./component_glyph_test

#include "model_maker/component_glyph.hpp"
#include "model_maker/component_engine.hpp"
#include "model_maker/document.hpp"

#include <cmath>
#include <cstdio>

using namespace mm;

static int failures = 0;
static void check(bool ok, const char* label) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", label);
    if (!ok) ++failures;
}

static bool nearD(double a, double b, double tol) { return std::abs(a - b) < tol; }

// Verilen kose kutusunu temsil eden basit cizgi modeli (bounds = kutu).
static WireframeModel lineBox(const Vec3& lo, const Vec3& hi) {
    return WireframeModel::line(lo, hi);
}

int main() {
    Document document;

    // --- 1) ALIN LEVHASI (End Plate) --------------------------------------
    // Levha modeli (0,0,0)-(10,20,30) -> merkez (5,10,15).
    EndPlateConnection conn;
    const std::size_t connIndex = document.addEndPlateConnection(
        conn, lineBox({0, 0, 0}, {10, 20, 30}), lineBox({0, 0, 0}, {1, 1, 1}));
    const EndPlateConnection& storedConn = document.endPlateConnections()[connIndex];
    check(storedConn.plateEntityId != kInvalidEntityId,
          "end plate levha EntityId atandi");

    // --- 2) UNIVERSAL COMPONENT (Fin Plate benzeri) -------------------------
    // Sahip olunan levha modeli (100,0,0)-(140,20,30) -> merkez (120,10,15).
    document.addModel(lineBox({100, 0, 0}, {140, 20, 30}));
    const EntityId finPlateId = document.modelId(document.models().size() - 1);

    // Bulon grubu (display modeli (200,0,0)-(200,0,0) -> merkez (200,0,0)).
    BoltGroup bolt;
    const std::size_t bgIndex = document.addBoltGroup(bolt, lineBox({200, 0, 0}, {200, 0, 0}));
    const EntityId boltGroupId = document.boltGroups()[bgIndex].id;

    ComponentInstanceRecord fin;
    fin.typeId = L"fin_plate";
    fin.mainPart = document.modelId(0);  // kolon (bu testte onemsiz)
    fin.ownedPlateIds = {finPlateId};
    fin.ownedBoltGroupIds = {boltGroupId};
    document.addComponentInstance(fin);
    const ComponentInstanceRecord& storedFin = document.componentInstances().back();
    check(storedFin.id != kInvalidEntityId, "fin plate component EntityId atandi");

    // --- 3) TOPLAMA -------------------------------------------------------
    {
        const auto glyphs = collectComponentGlyphs(document);
        check(glyphs.size() == 2, "iki sembol: End Plate + Fin Plate");

        const ComponentGlyph* ep = nullptr;
        const ComponentGlyph* fp = nullptr;
        for (const auto& g : glyphs) {
            if (g.entityId == storedConn.plateEntityId) ep = &g;
            if (g.entityId == finPlateId) fp = &g;
        }
        check(ep != nullptr && fp != nullptr, "her iki component icin sembol var");

        if (ep) {
            check(nearD(ep->anchor.x, 5.0, 1e-6) && nearD(ep->anchor.y, 10.0, 1e-6) &&
                      nearD(ep->anchor.z, 15.0, 1e-6),
                  "end plate sembolu levha merkezinde");
            check(ep->entityId == storedConn.plateEntityId, "end plate sembolu levha EntityId'sine bagli");
            check(ep->componentId == storedConn.id, "end plate sembolu baglanti kimligini tasir");
            check(!ep->universal, "end plate sembolu universal degil");
        }
        if (fp) {
            check(nearD(fp->anchor.x, 120.0, 1e-6) && nearD(fp->anchor.y, 10.0, 1e-6) &&
                      nearD(fp->anchor.z, 15.0, 1e-6),
                  "fin plate sembolu levha merkezinde");
            check(fp->entityId == finPlateId, "fin plate sembolu sahip olunan levhaya bagli");
            check(fp->componentId == storedFin.id, "fin plate sembolu parent component kimligini tasir");
            check(fp->universal, "fin plate sembolu universal");
        }
    }

    // --- 4) TEKILLESME: ayni levha iki yoldan gelirse tek sembol ------------
    {
        ComponentInstanceRecord duplicate;
        duplicate.typeId = L"fin_plate";
        duplicate.ownedPlateIds = {storedConn.plateEntityId};  // End Plate levhasi
        document.addComponentInstance(duplicate);
        const auto glyphs = collectComponentGlyphs(document);
        int countForEndPlatePlate = 0;
        for (const auto& g : glyphs)
            if (g.entityId == storedConn.plateEntityId) ++countForEndPlatePlate;
        check(countForEndPlatePlate == 1, "ayni levha icin tek sembol (cift sembol yok)");
        // duplicate bir levha DAHA eklemez (entityId zaten goruldu).
        check(glyphs.size() == 2, "kopya kayit yeni sembol uretmez");
        document.removeComponentInstance(document.componentInstances().size() - 1);
    }

    // --- 5) DEVRE DISI / ORPHAN atlanir ------------------------------------
    {
        ComponentInstanceRecord disabled;
        disabled.typeId = L"fin_plate";
        disabled.enabled = false;
        document.addModel(lineBox({300, 0, 0}, {310, 20, 30}));
        disabled.ownedPlateIds = {document.modelId(document.models().size() - 1)};
        document.addComponentInstance(disabled);

        ComponentInstanceRecord orphan;
        orphan.typeId = L"bilinmeyen_tip";
        orphan.orphan = true;
        document.addModel(lineBox({400, 0, 0}, {410, 20, 30}));
        orphan.ownedPlateIds = {document.modelId(document.models().size() - 1)};
        document.addComponentInstance(orphan);

        const auto glyphs = collectComponentGlyphs(document);
        check(glyphs.size() == 2, "devre disi ve orphan component sembol uretmez");
    }

    // --- 6) GECERSIZ EntityId atlanir --------------------------------------
    {
        ComponentInstanceRecord invalid;
        invalid.typeId = L"fin_plate";
        invalid.ownedPlateIds = {kInvalidEntityId};
        document.addComponentInstance(invalid);
        const auto glyphs = collectComponentGlyphs(document);
        check(glyphs.size() == 2, "gecersiz EntityId sembol uretmez");
    }

    std::printf("%s\n", failures == 0 ? "ALL PASS" : "SOME FAILED");
    return failures == 0 ? 0 : 1;
}
