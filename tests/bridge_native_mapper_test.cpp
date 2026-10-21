// MODEL MAKER BRIDGE -> NATIVE component esleme testi (OCCT'siz ctest).
//
// Dogrular:
//   1) 146 V3 fixture tespit edilir -> typeId tekla_shear_plate_146.
//   2) Parametre cikarimi + provenance: kalinlik/genislik/yukseklik (observed),
//      bulon capi (observed), delik capi (inferred/standard), boltCount = dusey
//      sira (observed), kenar mesafesi (observed), aralik (observed), grade
//      (unresolved), malzeme (observed).
//   3) Desteklenmeyen ozellikler ACIKCA raporlanir: kaynak + 2 kolonlu bulon.
//   4) 144 fixture 146 degil -> detected=false.
//   5) Uye ekseni eksikse equivalence Unverified (uydurulmaz).
//   6) Cok kolonlu (countX>1) + uyeler mevcutsa NotEquivalent.
//   7) Tek kolonlu tutarli paket -> Equivalent (round-trip).
//   8) Parametrik degistirme: kalinlik 10->12, sira 3->4 native generator'a yansir.

#include "model_maker/bridge_native_mapper.hpp"
#include "model_maker/bridge_package.hpp"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace mm;

static int failures = 0;
static void check(bool ok, const char* label) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", label);
    if (!ok) ++failures;
}
static bool approx(double a, double b, double tol = 1e-6) { return std::fabs(a - b) < tol; }

// Mapped param'i ada gore bul (yoksa nullptr).
static const BridgeMappedParam* findMapped(const BridgeNativeMapping& m,
                                           const std::wstring& name) {
    for (const auto& p : m.params)
        if (p.name == name) return &p;
    return nullptr;
}
static double mappedDouble(const BridgeNativeMapping& m, const std::wstring& name) {
    const BridgeMappedParam* p = findMapped(m, name);
    if (!p) return 0.0;
    if (const double* d = std::get_if<double>(&p->value)) return *d;
    if (const int* i = std::get_if<int>(&p->value)) return static_cast<double>(*i);
    return 0.0;
}
static int mappedInt(const BridgeNativeMapping& m, const std::wstring& name) {
    const BridgeMappedParam* p = findMapped(m, name);
    if (!p) return 0;
    if (const int* i = std::get_if<int>(&p->value)) return *i;
    if (const double* d = std::get_if<double>(&p->value)) return static_cast<int>(*d);
    return 0;
}
static std::wstring mappedStr(const BridgeNativeMapping& m, const std::wstring& name) {
    const BridgeMappedParam* p = findMapped(m, name);
    if (!p) return L"";
    if (const std::wstring* s = std::get_if<std::wstring>(&p->value)) return *s;
    return L"";
}

static BridgePackage load(const char* sub) {
    const std::string dir = std::string(MM_BRIDGE_FIXTURE_DIR) + "/" + sub;
    BridgePackage pkg;
    std::vector<std::string> errors;
    const bool ok = readBridgePackage(dir, pkg, errors);
    check(ok, "paket okundu + dogrulandi");
    if (!ok)
        for (const auto& e : errors) std::printf("  err: %s\n", e.c_str());
    return pkg;
}

// Sentetik profil katalogu (makeInput ile tutarli: kolon 300x300, kiris 200x300).
static std::vector<SteelProfile> catalog() {
    SteelProfile col;
    col.name = "HEA400";
    col.height = 300.0;  // kesit yuksekligi
    col.width = 300.0;   // flans genisligi
    SteelProfile beam;
    beam.name = "HEA300";
    beam.height = 300.0;
    beam.width = 200.0;
    return {col, beam};
}

// Tutarli tek-kolonlu shear tab paketi (round-trip icin).
static BridgePackage makeSingleColumnPackage() {
    BridgePackage pkg;
    pkg.manifest.sourceComponentNumber = 146;
    pkg.manifest.sourceComponentName = "Shear plate simple";
    pkg.component.number = 146;
    pkg.component.name = "Shear plate simple";
    pkg.component.primaryMember.guid = "c1";
    pkg.component.primaryMember.profile = "HEA400";
    pkg.component.primaryMember.role = "COLUMN";
    pkg.component.primaryMember.startPoint = BridgeVec3{0, 0, 0};
    pkg.component.primaryMember.endPoint = BridgeVec3{0, 0, 3000};
    BridgeMemberRef beam;
    beam.guid = "b1";
    beam.profile = "HEA300";
    beam.role = "BEAM";
    beam.startPoint = BridgeVec3{0, 0, 3000};
    beam.endPoint = BridgeVec3{0, 4000, 3000};
    pkg.component.secondaryMembers.push_back(beam);

    BridgeObject plate;
    plate.kind = "ContourPlate";
    plate.sourceGUID = "p1";
    plate.profile = "PL10";
    plate.material = "S235JR";
    plate.bbox = BridgeBBox{{0, 0, 0}, {10, 170, 200}};  // kalinlik X, genislik Y, yukseklik Z
    pkg.objects.push_back(plate);

    BridgeObject bg;
    bg.kind = "BoltGroup";
    bg.sourceGUID = "bg1";
    bg.boltGroup = BridgeBoltGroupInfo{};
    bg.boltGroup->size = "M20";
    bg.boltGroup->axis = "x";
    bg.boltGroup->direction = BridgeVec3{1, 0, 0};
    // Tek kolon, 3 sira (Z=40,100,160; Y=85 sabit).
    for (double z : {40.0, 100.0, 160.0}) {
        BridgeBolt b;
        b.position = BridgeVec3{5, 85, z};
        b.nominalDiameter = 20;
        b.holeDiameterSource = "standard";
        bg.boltGroup->bolts.push_back(b);
    }
    pkg.objects.push_back(bg);

    // parametreler: native generator'un kullanacagi degerler.
    BridgeParameter t;
    t.name = "plateThickness"; t.type = "Length"; t.value = 10.0;
    pkg.parameters.push_back(t);
    BridgeParameter w;
    w.name = "plateWidth"; w.type = "Length"; w.value = 170.0;
    pkg.parameters.push_back(w);
    BridgeParameter h;
    h.name = "plateHeight"; h.type = "Length"; h.value = 200.0;
    pkg.parameters.push_back(h);
    return pkg;
}

int main() {
    // --- 1) 146 V3 fixture: tespit + cikarim + provenance ----------------------
    {
        const BridgePackage pkg = load("fixture-146-shear-plate-v3.mmbridge");
        const BridgeNativeMapping m = mapBridgeToNative(pkg, catalog());

        check(m.detected, "146 V3: tespit edildi");
        check(m.typeId == L"tekla_shear_plate_146", "146 V3: typeId tekla_shear_plate_146");

        check(approx(mappedDouble(m, L"plateThickness"), 10.0), "kalinlik 10 (observed)");
        check(approx(mappedDouble(m, L"plateWidth"), 170.0), "genislik 170 (observed)");
        check(approx(mappedDouble(m, L"plateHeight"), 200.0), "yukseklik 200 (observed)");

        check(approx(mappedDouble(m, L"boltDiameter"), 20.0), "bulon capi 20 (observed)");
        check(approx(mappedDouble(m, L"holeDiameter"), 22.0), "delik capi 22 (inferred/standard)");

        check(mappedInt(m, L"boltCount") == 3, "boltCount = 3 dusey sira (6 toplam -> 2x3)");
        check(approx(mappedDouble(m, L"boltEdge"), 40.0), "kenar mesafesi 40 (observed)");
        check(mappedStr(m, L"boltSpacing") == L"60", "aralik 60 (observed)");
        check(mappedStr(m, L"grade") == L"8.8", "grade 8.8 (unresolved)");

        const BridgeMappedParam* th = findMapped(m, L"plateThickness");
        check(th && th->provenance == BridgeParamProvenance::Observed,
              "kalinlik provenance = observed");
        const BridgeMappedParam* hole = findMapped(m, L"holeDiameter");
        check(hole && hole->provenance == BridgeParamProvenance::Inferred,
              "delik capi provenance = inferred (standard)");
        const BridgeMappedParam* grade = findMapped(m, L"grade");
        check(grade && grade->provenance == BridgeParamProvenance::Unresolved,
              "grade provenance = unresolved");

        check(m.plateMaterial == "S235JR" &&
                  m.materialProvenance == BridgeParamProvenance::Observed,
              "malzeme S235JR (observed)");

        // Desteklenmeyenler: kaynak + 2 kolonlu bulon.
        bool hasWeld = false, hasTwoCol = false;
        for (const auto& u : m.unsupportedFeatures) {
            if (u.find("kaynak") != std::string::npos) hasWeld = true;
            if (u.find("2 kolon") != std::string::npos) hasTwoCol = true;
        }
        check(hasWeld, "kaynak desteklenmiyor raporlandi");
        check(hasTwoCol, "2 kolonlu bulon desteklenmiyor raporlandi");

        // 2 kolonlu bulon -> NotEquivalent (uye baglamindan bagimsiz).
        check(m.equivalence.state == BridgeEquivalenceState::NotEquivalent,
              "2 kolonlu -> equivalence NotEquivalent");
        bool hasTwoColIssue = false;
        for (const auto& i : m.equivalence.issues)
            if (i.find("cok kolonlu") != std::string::npos) hasTwoColIssue = true;
        check(hasTwoColIssue, "NotEquivalent sebebi: cok kolonlu bulon grubu");
    }

    // --- 2) 144 fixture: 146 degil -------------------------------------------
    {
        const BridgePackage pkg = load("fixture-144-end-plate-v3.mmbridge");
        const BridgeNativeMapping m = mapBridgeToNative(pkg, catalog());
        check(!m.detected, "144: 146 olarak tespit EDILMEDI");
    }

    // --- 3) Parametrik degistirme A (10->12) ve B (3->4) ----------------------
    {
        ComponentInput in;
        in.main.id = EntityId{20};
        in.main.axisFrom = {0, 0, 0};
        in.main.axisTo = {0, 0, 3000};
        in.main.refNormal = {0, 0, 1};
        in.main.secWidth = 300.0;
        in.main.secHeight = 300.0;
        MemberGeometry beam;
        beam.id = EntityId{21};
        beam.axisFrom = {0, 0, 3000};
        beam.axisTo = {0, 4000, 3000};
        beam.refNormal = {0, 0, 1};
        beam.secWidth = 200.0;
        beam.secHeight = 300.0;
        in.secondary.push_back(beam);

        // A: kalinlik 10 -> 12.
        in.params = {{L"plateThickness", CompParamValue{10.0}},
                     {L"plateWidth", CompParamValue{170.0}},
                     {L"plateHeight", CompParamValue{200.0}},
                     {L"boltCount", CompParamValue{3}}};
        GenerationPlan a = generateShearPlateSimple146(in);
        check(a.ok && a.plates.size() >= 1, "parametrik A: 10mm uretildi");
        if (!a.plates.empty()) check(approx(a.plates[0].thickness, 10.0), "A: kalinlik 10");

        in.params[0] = {L"plateThickness", CompParamValue{12.0}};
        GenerationPlan a2 = generateShearPlateSimple146(in);
        check(a2.ok && !a2.plates.empty() && approx(a2.plates[0].thickness, 12.0),
              "A: kalinlik 10 -> 12 yansidi");

        // B: sira 3 -> 4.
        in.params = {{L"plateThickness", CompParamValue{10.0}},
                     {L"plateWidth", CompParamValue{170.0}},
                     {L"plateHeight", CompParamValue{200.0}},
                     {L"boltCount", CompParamValue{3}}};
        GenerationPlan b = generateShearPlateSimple146(in);
        check(b.ok && b.boltGroups.size() == 1 && b.boltGroups[0].countY == 3,
              "B: 3 sira");
        in.params[3] = {L"boltCount", CompParamValue{4}};
        GenerationPlan b2 = generateShearPlateSimple146(in);
        check(b2.ok && b2.boltGroups.size() == 1 && b2.boltGroups[0].countY == 4,
              "B: sira 3 -> 4 yansidi");
    }

    // --- 4) Tek kolonlu tutarli paket -> Equivalent (round-trip) ---------------
    {
        const BridgePackage pkg = makeSingleColumnPackage();
        const BridgeNativeMapping m = mapBridgeToNative(pkg, catalog());
        check(m.detected, "tek kolon: tespit edildi");
        check(mappedInt(m, L"boltCount") == 3, "tek kolon: boltCount 3");
        check(m.equivalence.state == BridgeEquivalenceState::Equivalent,
              "tek kolon: equivalence Equivalent");
        if (m.equivalence.state != BridgeEquivalenceState::Equivalent)
            for (const auto& i : m.equivalence.issues)
                std::printf("  issue: %s\n", i.c_str());
    }

    // --- 5) Tek kolonlu ama uye ekseni yok -> Unverified ----------------------
    {
        BridgePackage pkg = makeSingleColumnPackage();
        pkg.component.primaryMember.startPoint.reset();  // kolon ekseni kaldirildi
        pkg.component.primaryMember.endPoint.reset();
        const BridgeNativeMapping m = mapBridgeToNative(pkg, catalog());
        check(m.detected, "eksik uye: tespit edildi");
        check(m.equivalence.state == BridgeEquivalenceState::Unverified,
              "eksik uye ekseni -> equivalence Unverified (uydurulmadi)");
    }

    std::printf("FAILURES: %d\n", failures);
    return failures == 0 ? 0 : 1;
}
