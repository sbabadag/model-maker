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
static bool v3eq(const Vec3& a, const Vec3& b) {
    return nearEq(a.x, b.x) && nearEq(a.y, b.y) && nearEq(a.z, b.z);
}
static bool frameEq(const MemberLocalFrame& a, const MemberLocalFrame& b) {
    return v3eq(a.origin, b.origin) && v3eq(a.u, b.u) && v3eq(a.v, b.v) && v3eq(a.w, b.w);
}
static bool placementEq(const LocalPlacement& a, const LocalPlacement& b) {
    return v3eq(a.origin, b.origin) && v3eq(a.u, b.u) && v3eq(a.v, b.v) && v3eq(a.w, b.w);
}
static bool faceRefEq(const FaceReference& a, const FaceReference& b) {
    if (a.source != b.source || a.role != b.role || a.faceRole != b.faceRole ||
        a.selector != b.selector)
        return false;
    if (a.localFrame.has_value() != b.localFrame.has_value()) return false;
    if (a.localFrame && !frameEq(*a.localFrame, *b.localFrame)) return false;
    return true;
}

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
    if (!v3(a.origin, b.origin) || !v3(a.u, b.u) || !v3(a.v, b.v) || !v3(a.axis, b.axis))
        return false;
    // --- Slice 2d-3: parametrik ref/kisit/cerceve durumu ---
    if (!frameEq(a.frame.frame, b.frame.frame)) return false;
    if (a.frame.children.size() != b.frame.children.size()) return false;
    for (std::size_t i = 0; i < a.frame.children.size(); ++i)
        if (a.frame.children[i].id != b.frame.children[i].id ||
            !placementEq(a.frame.children[i].local, b.frame.children[i].local))
            return false;
    if (a.faceRefs.size() != b.faceRefs.size()) return false;
    for (std::size_t i = 0; i < a.faceRefs.size(); ++i)
        if (!faceRefEq(a.faceRefs[i], b.faceRefs[i])) return false;
    if (a.constraints.size() != b.constraints.size()) return false;
    for (std::size_t i = 0; i < a.constraints.size(); ++i) {
        if (a.constraints[i].handle.key != b.constraints[i].handle.key) return false;
        if (!faceRefEq(a.constraints[i].target, b.constraints[i].target)) return false;
        if (!nearEq(a.constraints[i].signedOffset, b.constraints[i].signedOffset)) return false;
        if (a.constraints[i].enabled != b.constraints[i].enabled) return false;
    }
    if (a.handles.size() != b.handles.size()) return false;
    for (std::size_t i = 0; i < a.handles.size(); ++i) {
        if (a.handles[i].key.key != b.handles[i].key.key) return false;
        if (a.handles[i].kind != b.handles[i].kind) return false;
        if (!v3eq(a.handles[i].initialPosition, b.handles[i].initialPosition)) return false;
    }
    if (a.dependencies.size() != b.dependencies.size()) return false;
    for (std::size_t i = 0; i < a.dependencies.size(); ++i) {
        if (a.dependencies[i].dependent.key != b.dependencies[i].dependent.key) return false;
        if (a.dependencies[i].dependsOn.key != b.dependencies[i].dependsOn.key) return false;
    }
    return true;
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
    // --- Slice 2d-3: parametrik ref/kisit/cerceve durumu ---
    MemberLocalFrame mf;
    mf.origin = {1.0, 2.0, 3.0};
    mf.u = {1.0, 0.0, 0.0};
    mf.v = {0.0, 1.0, 0.0};
    mf.w = {0.0, 0.0, 1.0};
    r.frame.frame = mf;
    MagneticChild child;
    child.id = "plate";
    child.local = LocalPlacement{{4.0, 5.0, 6.0}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    r.frame.children = {child};

    FaceReference fr;
    fr.source = EntityId{10};
    fr.role = ComponentInputRole::Main;
    fr.faceRole = SemanticFaceRole::FlangeOuterPositive;
    fr.localFrame = MemberLocalFrame{{0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    fr.selector = "top_flange";
    r.faceRefs = {fr};

    DistanceConstraint dc;
    dc.handle = HandleKey{"plate_face"};
    dc.target = fr;
    dc.signedOffset = 6.0;
    dc.enabled = true;
    r.constraints = {dc};

    r.handles = {ConstraintHandleDef{HandleKey{"plate_face"}, HandleKind::Plane, Vec3{0, 0, 0}}};
    r.dependencies = {ConstraintDependency{HandleKey{"plate_face"}, HandleKey{"member_end"}}};
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

static std::string serializeDoc(const Mmw8Document& d) {
    std::ostringstream os;
    writeMmw8(os, d);
    return os.str();
}
static bool readThrows(const std::string& bytes) {
    try {
        std::istringstream is(bytes);
        Mmw8Document d;
        readMmw8(is, d);
        return false;
    } catch (const std::exception&) {
        return true;
    }
}

// 5) SectionAnchor kaliciligi ('A' blogu): non-Top anchor korunur, Top default
//    (blok yazilmaz), tekrar kayit kimlikleri degistirmez.
static void testSectionAnchorRoundTrip() {
    std::printf("Test 5: SectionAnchor kaliciligi (A blogu)\n");
    const auto path = tempPath("mmw8_section_anchor.mmw");
    Document doc;
    {
        WireframeModel m = WireframeModel::line({0, 0, 0}, {100, 0, 0});
        EntityProperties p = m.properties();
        p.sectionAnchor = SectionAnchor::Center;
        m.setProperties(std::move(p));
        doc.addModel(std::move(m));
    }
    doc.addModel(WireframeModel::line({0, 0, 0}, {100, 0, 0}));  // Top default
    const EntityId centerId = doc.modelId(0);
    const EntityId topId = doc.modelId(1);
    doc.save(path);

    Document loaded;
    loaded.load(path);
    check(loaded.models().size() == 2, "2 model yuklendi");
    check(loaded.models()[0].properties().sectionAnchor == SectionAnchor::Center,
          "Center anchor korunur");
    check(loaded.models()[1].properties().sectionAnchor == SectionAnchor::Top,
          "Top anchor (varsayilan) korunur");

    // Ayni belge tekrar kaydedilince kimlikler DEGISMEMELI.
    const auto path2 = tempPath("mmw8_section_anchor2.mmw");
    loaded.save(path2);
    Document loaded2;
    loaded2.load(path2);
    check(loaded2.modelId(0) == centerId && loaded2.modelId(1) == topId,
          "tekrar kayit kimlikleri degistirmez");
    check(loaded2.models()[0].properties().sectionAnchor == SectionAnchor::Center,
          "ikinci kayit sonrasi Center korunur");
    std::error_code ec;
    std::filesystem::remove(path, ec);
    std::filesystem::remove(path2, ec);
}

// 6) Eski dosya (J/D/M/A yok): yeni alanlar BOS/varsayilan, bloklar YAZILMAZ.
static void testLegacyComponentDefaults() {
    std::printf("Test 6: eski dosya (J/D/M/A yok) -> varsayilanlar\n");
    ComponentInstanceRecord legacy;
    legacy.id = EntityId{42};
    legacy.typeId = L"beam_to_column_end_plate";
    legacy.schemaVersion = 1;
    legacy.mainPart = EntityId{10};
    legacy.params = {{L"plateThickness", CompParamValue{15.0}}};
    legacy.enabled = true;
    // Default-olmayan LCS + frame == frameFromLcs (tutarlı legacy durum):
    // 'M' blogu YAZILMAZ (frame turetilene esit) ve okumada frame LCS'ten turetir.
    legacy.origin = {10.0, 20.0, 30.0};
    legacy.u = {1.0, 0.0, 0.0};
    legacy.v = {0.0, 1.0, 0.0};
    legacy.axis = {0.0, 0.0, 1.0};
    legacy.frame.frame.origin = {10.0, 20.0, 30.0};
    legacy.frame.frame.u = {1.0, 0.0, 0.0};
    legacy.frame.frame.v = {0.0, 1.0, 0.0};
    legacy.frame.frame.w = {0.0, 0.0, 1.0};
    // faceRefs/constraints/handles/dependencies default (bos).

    Mmw8Document doc;
    doc.nextEntityId = 100;
    doc.metadata.binToolsFormatVersion = 0;
    doc.componentInstances.push_back(legacy);

    const std::string bytes = serializeDoc(doc);
    check(bytes.find("\nJ ") == std::string::npos, "J blogu yazilmadi (bos refs)");
    check(bytes.find("\nD ") == std::string::npos, "D blogu yazilmadi (bos kisit)");
    check(bytes.find("\nM ") == std::string::npos, "M blogu yazilmadi (bos cerceve)");
    check(bytes.find("\nA ") == std::string::npos, "A blogu yazilmadi (Top anchor)");

    Mmw8Document parsed;
    std::istringstream in(bytes);
    readMmw8(in, parsed);
    check(parsed.componentInstances.size() == 1, "1 legacy component");
    if (parsed.componentInstances.size() == 1) {
        const auto& r = parsed.componentInstances[0];
        check(r.faceRefs.empty() && r.constraints.empty() && r.handles.empty() &&
                  r.dependencies.empty() && r.frame.children.empty(),
              "legacy parametrik alanlar BOS");
        // Legacy migration: frame origin/u/v/axis'tan TURETILIR.
        check(v3eq(r.frame.frame.origin, legacy.origin), "legacy frame.origin LCS'ten turetildi");
        check(v3eq(r.frame.frame.w, legacy.axis), "legacy frame.w = component axis");
    }
}

// 7) Cocuksuz ama anlamli cerceve KAYBOLMAZ (reviewer fix): frame LCS'ten
//    farkliyken 'M' blogu yazilir ve round-trip cerceveyi korur.
static void testFrameWithoutChildren() {
    std::printf("Test 7: cocuksuz cerceve korunur\n");
    ComponentInstanceRecord r;
    r.id = EntityId{70};
    r.typeId = L"beam_to_column_end_plate";
    r.schemaVersion = 1;
    r.mainPart = EntityId{10};
    r.origin = {0, 0, 0};
    r.u = {1, 0, 0};
    r.v = {0, 1, 0};
    r.axis = {0, 0, 1};
    r.frame.frame.origin = {5.0, 6.0, 7.0};  // LCS'ten FARKLI (anlamli cerceve)
    r.frame.frame.u = {1, 0, 0};
    r.frame.frame.v = {0, 1, 0};
    r.frame.frame.w = {0, 0, 1};
    // children BOS.

    Mmw8Document doc;
    doc.nextEntityId = 100;
    doc.metadata.binToolsFormatVersion = 0;
    doc.componentInstances.push_back(r);

    const std::string bytes = serializeDoc(doc);
    check(bytes.find("\nM ") != std::string::npos, "M blogu yazildi (cocuksuz ama anlamli cerceve)");

    Mmw8Document parsed;
    std::istringstream in(bytes);
    readMmw8(in, parsed);
    check(parsed.componentInstances.size() == 1, "1 component");
    if (parsed.componentInstances.size() == 1) {
        const auto& got = parsed.componentInstances[0];
        check(got.frame.children.empty(), "cocuk yok");
        check(v3eq(got.frame.frame.origin, {5.0, 6.0, 7.0}),
              "cocuksuz anlamli cerceve korundu");
    }
}

// 8) Parametrik blok redleri: gecersiz enum, yinelenen blok, yinelenen handle.
static void testParamRejections() {
    std::printf("Test 8: parametrik blok redleri\n");
    Mmw8Document doc;
    doc.nextEntityId = 100;
    doc.metadata.binToolsFormatVersion = 0;
    WireframeModel m = WireframeModel::line({0, 0, 0}, {1, 0, 0});
    doc.models.push_back(m);
    doc.entityIds.push_back(60);
    doc.componentInstances.push_back(buildRichRecord(EntityId{60}));
    doc.sectionAnchors.push_back(Mmw8SectionAnchor{EntityId{60}, SectionAnchor::Center});

    const std::string valid = serializeDoc(doc);
    check(!readThrows(valid), "gecerli parametrik doc okunur");

    // Gecersiz anchor degeri.
    {
        Mmw8Document bad = doc;
        bad.sectionAnchors[0].anchor = static_cast<SectionAnchor>(9);
        check(readThrows(serializeDoc(bad)), "gecersiz anchor degeri reddedilir");
    }
    // Gecersiz faceRef role.
    {
        Mmw8Document bad = doc;
        bad.componentInstances[0].faceRefs[0].role = static_cast<ComponentInputRole>(9);
        check(readThrows(serializeDoc(bad)), "gecersiz faceRef role reddedilir");
    }
    // Gecersiz faceRef faceRole.
    {
        Mmw8Document bad = doc;
        bad.componentInstances[0].faceRefs[0].faceRole = static_cast<SemanticFaceRole>(9);
        check(readThrows(serializeDoc(bad)), "gecersiz faceRef faceRole reddedilir");
    }
    // Yinelenen handle anahtari.
    {
        Mmw8Document bad = doc;
        bad.componentInstances[0].handles.push_back(
            ConstraintHandleDef{HandleKey{"plate_face"}, HandleKind::Point, Vec3{1, 2, 3}});
        check(readThrows(serializeDoc(bad)), "yinelenen handle anahtari reddedilir");
    }
    // Tekrarlanan J blogu.
    {
        std::string s = valid;
        s += "J 1\n0\n";
        check(readThrows(s), "tekrarlanan J blogu reddedilir");
    }
}

int main() {
    // Document::load orphan tespiti icin yerlesik katalog gerekli.
    registerBuiltInComponents(ComponentRegistry::instance());
    testCodecRoundTrip();
    testDocumentRoundTrip();
    testUnknownTypeOrphan();
    testUndoRedo();
    testSectionAnchorRoundTrip();
    testLegacyComponentDefaults();
    testFrameWithoutChildren();
    testParamRejections();
    std::printf("FAILURES: %d\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
