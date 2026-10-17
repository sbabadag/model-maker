// COMPONENT SERIALIZE (Phase C2b) — Document/MMW8 entegrasyonu.
//
// ComponentInstanceRecord'un KALICI serializasyonu: MMW8 'I' blogu (TypeId +
// SchemaVersion + tipli parametreler + yerlesim + referanslar + sahip olunan alt
// nesneler). Kilitlenen davranislar:
//   * Codec round-trip: tum veri alanlari BIREBIR korunur (dirty/orphan haric —
//     bunlar yuklemede yeniden turetilir).
//   * Document save/load: bilinen typeId -> orphan=false + param migration
//     (eksik -> varsayilan, bilinmeyen -> KORUNUR) + dirty=true.
//   * Bilinmeyen typeId -> orphan=true (COKME YOK), parametreler kaybolmaz.
//   * Undo/redo: ComponentAdd/Remove ayni kimligi geri getirir.
// OCC'siz, Linux'ta kosar.

#include "model_maker/component_engine.hpp"
#include "model_maker/document.hpp"
#include "model_maker/mmw8_codec.hpp"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

using namespace mm;

static int g_failures = 0;
static void check(bool ok, const char* label) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", label);
    if (!ok) ++g_failures;
}

static bool nearEq(double a, double b, double tol = 1e-9) { return std::abs(a - b) < tol; }

static std::filesystem::path tempPath(const char* name) {
    return std::filesystem::temp_directory_path() / name;
}

// Veri alanlarini karsilastirir (dirty/orphan yuklemede yeniden turetilir).
static bool sameComponent(const ComponentInstanceRecord& a, const ComponentInstanceRecord& b) {
    if (a.id != b.id || a.typeId != b.typeId || a.schemaVersion != b.schemaVersion ||
        a.mainPart != b.mainPart || a.enabled != b.enabled)
        return false;
    if (a.secondaryParts != b.secondaryParts) return false;
    if (a.ownedPlateIds != b.ownedPlateIds) return false;
    if (a.ownedBoltGroupIds != b.ownedBoltGroupIds) return false;
    if (a.params.size() != b.params.size()) return false;
    for (std::size_t i = 0; i < a.params.size(); ++i) {
        if (a.params[i].name != b.params[i].name) return false;
        if (a.params[i].value != b.params[i].value) return false;
    }
    const auto v3 = [](const Vec3& x, const Vec3& y) {
        return nearEq(x.x, y.x) && nearEq(x.y, y.y) && nearEq(x.z, y.z);
    };
    return v3(a.origin, b.origin) && v3(a.u, b.u) && v3(a.v, b.v) && v3(a.axis, b.axis);
}

// Zengin bir kayit kurar: tum param turleri (double/int/bool/wstring/EntityRef),
// LCS, sahip olunan levha + bulon gruplari.
static ComponentInstanceRecord buildRichRecord(EntityId id) {
    ComponentInstanceRecord r;
    r.id = id;
    r.typeId = L"beam_to_column_end_plate";
    r.schemaVersion = 1;
    r.mainPart = EntityId{10};
    r.secondaryParts = {EntityId{11}, EntityId{12}};
    r.params = {
        {L"plateThickness", CompParamValue{15.0}},
        {L"boltCountX", CompParamValue{3}},
        {L"hasHead", CompParamValue{true}},
        {L"grade", CompParamValue{std::wstring{L"10.9"}}},
        {L"beamRef", CompParamValue{EntityId{11}}},
    };
    r.origin = {100.0, 200.0, 300.0};
    r.u = {1.0, 0.0, 0.0};
    r.v = {0.0, 1.0, 0.0};
    r.axis = {0.0, 0.0, 1.0};
    r.ownedPlateIds = {EntityId{50}};
    r.ownedBoltGroupIds = {EntityId{51}, EntityId{52}};
    r.enabled = true;
    r.dirty = true;
    return r;
}

// 1) Codec seviyesi: MMW8 'I' blogu tum veri alanlarini BIREBIR korur.
static void testCodecRoundTrip() {
    std::printf("Test 1: MMW8 codec component round-trip\n");
    Mmw8Document doc;
    doc.nextEntityId = 100;
    doc.metadata.binToolsFormatVersion = 0;
    doc.componentInstances.push_back(buildRichRecord(EntityId{60}));

    std::ostringstream out;
    writeMmw8(out, doc);
    const std::string bytes = out.str();

    Mmw8Document parsed;
    std::istringstream in(bytes);
    readMmw8(in, parsed);

    check(parsed.componentInstances.size() == 1, "1 component okundu");
    if (parsed.componentInstances.size() == 1) {
        const auto& got = parsed.componentInstances[0];
        const auto& exp = doc.componentInstances[0];
        check(sameComponent(got, exp), "tum veri alanlari BIREBIR korunur");
        check(got.dirty, "okunan kayit kirli (recompute bekliyor)");
        check(!got.orphan, "codec seviyesinde orphan turetilmez");
        bool gradeOk = false, refOk = false;
        for (const auto& p : got.params) {
            if (p.name == L"grade")
                if (const auto* s = std::get_if<std::wstring>(&p.value)) gradeOk = (*s == L"10.9");
            if (p.name == L"beamRef")
                if (const auto* e = std::get_if<EntityId>(&p.value)) refOk = (*e == EntityId{11});
        }
        check(gradeOk, "wstring param (grade=10.9) korunur");
        check(refOk, "EntityRef param (beamRef=11) korunur");
    }
}

// 2) Document save/load: bilinen tip -> orphan=false + migration + dirty.
static void testDocumentRoundTrip() {
    std::printf("Test 2: Document save/load (bilinen tip, migration)\n");
    const auto path = tempPath("mmw8_component_roundtrip.mmw");
    Document doc;
    const std::size_t idx = doc.addComponentInstance(buildRichRecord(kInvalidEntityId));
    const EntityId recId = doc.componentInstances()[idx].id;
    check(recId != kInvalidEntityId, "id atandi");
    doc.save(path);

    Document loaded;
    loaded.load(path);
    check(loaded.componentInstances().size() == 1, "1 component yuklendi");
    if (loaded.componentInstances().size() == 1) {
        const auto& got = loaded.componentInstances()[0];
        check(got.id == recId, "id korunur");
        check(got.typeId == L"beam_to_column_end_plate", "typeId korunur");
        check(!got.orphan, "bilinen tip orphan degil");
        check(got.dirty, "yuklenen kayit kirli");
        check(got.enabled, "enabled korunur");
        check(got.schemaVersion == 1, "schemaVersion korunur");
        bool gradeOk = false;
        int countY = -1;
        bool hasBeamRef = false;
        for (const auto& p : got.params) {
            if (p.name == L"grade")
                if (const auto* s = std::get_if<std::wstring>(&p.value)) gradeOk = (*s == L"10.9");
            if (p.name == L"boltCountY")
                if (const auto* i = std::get_if<int>(&p.value)) countY = *i;
            if (p.name == L"beamRef") hasBeamRef = true;
        }
        check(gradeOk, "param grade=10.9 korunur");
        check(countY == 4, "eksik param boltCountY -> varsayilan 4 (migration)");
        check(hasBeamRef, "bilinmeyen param beamRef KORUNUR");
    }
    std::error_code ec;
    std::filesystem::remove(path, ec);
}

// 3) Bilinmeyen typeId -> orphan (COKME YOK), parametreler kaybolmaz.
static void testUnknownTypeOrphan() {
    std::printf("Test 3: bilinmeyen typeId -> orphan\n");
    const auto path = tempPath("mmw8_component_orphan.mmw");
    ComponentInstanceRecord rec = buildRichRecord(kInvalidEntityId);
    rec.typeId = L"gelecegin_componenti";
    rec.params = {{L"x", CompParamValue{1.5}}, {L"y", CompParamValue{EntityId{7}}}};
    Document doc;
    doc.addComponentInstance(rec);
    doc.save(path);

    Document loaded;
    loaded.load(path);  // COKMEMELI
    check(loaded.componentInstances().size() == 1, "1 orphan component yuklendi");
    if (loaded.componentInstances().size() == 1) {
        const auto& got = loaded.componentInstances()[0];
        check(got.orphan, "bilinmeyen tip orphan olarak isaretlenir");
        check(got.typeId == L"gelecegin_componenti", "typeId korunur (bilgi kaybi yok)");
        check(got.params.size() == 2, "parametreler KORUNUR (hizalanmaz)");
    }
    std::error_code ec;
    std::filesystem::remove(path, ec);
}

// 4) Undo/redo: ComponentAdd/Remove ayni kimligi geri getirir.
static void testUndoRedo() {
    std::printf("Test 4: component instance undo/redo\n");
    Document doc;
    doc.pushSnapshot();
    const std::size_t idx = doc.addComponentInstance(buildRichRecord(kInvalidEntityId));
    const EntityId rid = doc.componentInstances()[idx].id;
    check(doc.componentInstances().size() == 1, "eklendi");
    check(doc.undo(), "undo basarili");
    check(doc.componentInstances().empty(), "undo sonrasi bos");
    check(doc.redo(), "redo basarili");
    check(doc.componentInstances().size() == 1 && doc.componentInstances()[0].id == rid,
          "redo ayni kimligi geri getirir");
}

int main() {
    // Document::load orphan tespiti icin yerlesik katalog gerekli.
    registerBuiltInComponents(ComponentRegistry::instance());
    testCodecRoundTrip();
    testDocumentRoundTrip();
    testUnknownTypeOrphan();
    testUndoRedo();
    std::printf("FAILURES: %d\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
