// KULLANICI COMPONENT KATALOGU — kalici/tasinabilir derived sablon testi (OCC'siz).
//
// Dogrular:
//   1) deriveComponentTemplate: family semasi kopyalanir, override'lar varsayilana
//      islenir, benzersiz typeId uretilir.
//   2) Ayni ad + ayni family -> AYNI typeId (idempotent); farkli ad -> farkli id;
//      cakisma -> ensureUnique _2/_3 ekler.
//   3) JSON round-trip: save -> load alanlari birebir korur (specs dahil).
//   4) registerInto: derived type registry'ye + generator + dialog kaydedilir;
//      family kayitli degilse sablon ATLANIR ve uyari verilir (missing fallback).
//   5) mappedParamsToCompParams: Unresolved skip / aktarim.
//   6) userCatalogEntryToComponentType: catalogVisible=true + meta aktarilir.

#include "model_maker/user_catalog.hpp"
#include "model_maker/bridge_native_mapper.hpp"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

using namespace mm;

static int failures = 0;
static void check(bool ok, const char* label) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", label);
    if (!ok) ++failures;
}

static std::filesystem::path tempJson() {
    static int n = 0;
    ++n;
    return std::filesystem::temp_directory_path() /
           ("mm_user_catalog_test_" + std::to_string(n) + ".json");
}

int main() {
    // --- 1) derive: defaults + typeId --------------------------------------
    const ComponentType src = shearPlate146ComponentType();
    const std::vector<CompParam> overrides = {
        CompParam{L"plateThickness", CompParamValue{12.0}},
        CompParam{L"plateWidth", CompParamValue{170.0}},
        CompParam{L"plateHeight", CompParamValue{200.0}},
        CompParam{L"boltCount", CompParamValue{3}},
        CompParam{L"boltCountX", CompParamValue{2}},
        CompParam{L"boltDiameter", CompParamValue{20.0}},
        CompParam{L"grade", CompParamValue{std::wstring(L"8.8")}},
    };
    UserCatalogEntry e1 = deriveComponentTemplate(src, L"Shear Plate 146 - Özel", overrides);
    check(!e1.typeId.empty(), "derive: typeId dolu");
    check(e1.typeId.rfind(L"user_", 0) == 0, "derive: typeId 'user_' prefixli");
    check(e1.sourceTypeId == L"tekla_shear_plate_146", "derive: sourceTypeId = family");
    check(e1.specs.size() == src.params.size(), "derive: sema boyutu korunur");

    // override'lar varsayilana islendi mi?
    const CompParamSpec* th = findParamSpec(e1.specs, L"plateThickness");
    check(th && std::fabs(th->defDouble - 12.0) < 1e-9, "derive: plateThickness default 12");
    const CompParamSpec* cntX = findParamSpec(e1.specs, L"boltCountX");
    check(cntX && cntX->defInt == 2, "derive: boltCountX default 2");
    // override EDILMEYEN parametre family varsayilanini korur
    const CompParamSpec* hole = findParamSpec(e1.specs, L"holeDiameter");
    check(hole && std::fabs(hole->defDouble - 22.0) < 1e-9, "derive: holeDiameter family default 22");

    // --- 2) idempotency + benzersizlik -------------------------------------
    UserCatalogEntry e2 = deriveComponentTemplate(src, L"Shear Plate 146 - Özel", overrides);
    check(e2.typeId == e1.typeId, "idempotent: ayni ad + family -> ayni typeId");
    UserCatalogEntry e3 = deriveComponentTemplate(src, L"Başka Levha", overrides);
    check(e3.typeId != e1.typeId, "benzersiz: farkli ad -> farkli typeId");

    // ensureUnique: cakisma cozumu (farkli ad, ayni typeId zorla)
    UserCatalog cat;
    cat.addOrUpdate(e1);
    UserCatalogEntry clash = e3;
    clash.typeId = e1.typeId;  // farkli ad, cakisan id
    cat.ensureUnique(clash);
    check(clash.typeId != e1.typeId && clash.typeId.rfind(e1.typeId + L"_", 0) == 0,
          "ensureUnique: cakisma _2/_3 ekler");

    // --- 3) JSON round-trip -------------------------------------------------
    cat.addOrUpdate(e1);
    cat.addOrUpdate(e3);
    const std::filesystem::path p = tempJson();
    std::string err;
    check(cat.save(p, err), "save: ok");
    UserCatalog cat2;
    check(cat2.load(p, err), "load: ok");
    check(cat2.size() == 2, "load: 2 sablon");
    const UserCatalogEntry* r1 = cat2.find(e1.typeId);
    check(r1 != nullptr, "load: e1 bulunur");
    check(r1 && r1->displayName == L"Shear Plate 146 - Özel", "load: displayName korunur");
    check(r1 && r1->specs.size() == src.params.size(), "load: specs korunur");
    const CompParamSpec* rth = r1 ? findParamSpec(r1->specs, L"plateThickness") : nullptr;
    check(rth && std::fabs(rth->defDouble - 12.0) < 1e-9, "load: plateThickness 12 round-trip");
    const CompParamSpec* ren = r1 ? findParamSpec(r1->specs, L"shearTabPosition") : nullptr;
    check(ren && ren->choices.size() == 3, "load: enum choices round-trip");

    // bozuk JSON -> ok=false, error dolu
    {
        const std::filesystem::path bad = tempJson();
        std::FILE* f = std::fopen(bad.string().c_str(), "wb");
        if (f) { std::fputs("{ not json", f); std::fclose(f); }
        UserCatalog catBad;
        std::string e2err;
        check(!catBad.load(bad, e2err) && !e2err.empty(), "load: bozuk JSON -> ok=false + error");
    }

    // --- 4) registerInto (family mevcut) ------------------------------------
    ComponentRegistry reg;
    ComponentDialogRegistry dialogs;
    registerBuiltInComponents(reg);
    registerBuiltInDialogs(dialogs);
    UserCatalog cat3;
    cat3.addOrUpdate(e1);  // family "tekla_shear_plate_146" kayitli
    const auto warn3 = cat3.registerInto(reg, dialogs);
    check(warn3.empty(), "registerInto: family mevcut -> uyari yok");
    check(reg.findType(e1.typeId) != nullptr, "registerInto: derived type registry'de");
    const ComponentGenerator* g = reg.generator(e1.typeId);
    check(g != nullptr, "registerInto: derived generator miras alindi");
    check(dialogs.find(e1.typeId) != nullptr, "registerInto: derived dialog kayitli");
    // Sablon eski surumde kaydedilmis olabilir (kayitli specs'te gri Tekla
    // alanlari yok): derived dialog bunlari AILEDEN almali, yoksa Haunch/Notch
    // sekmeleri bos kalip gizlenir. Motor tipine ise gri alan GIRMEZ.
    if (const ComponentDialogDefinition* dd = dialogs.find(e1.typeId)) {
        std::size_t doc = 0;
        for (const auto& s : dd->specs)
            if (s.documentedOnly) ++doc;
        check(doc >= 60, "registerInto: derived dialog gri Tekla alanlarini aileden alir");
        bool haunchHasParams = false;
        for (const auto& grp : dd->schema.groups)
            if (grp.title == L"Haunch" && !grp.paramNames.empty() &&
                findParamSpec(dd->specs, grp.paramNames.front()) != nullptr)
                haunchHasParams = true;
        check(haunchHasParams, "registerInto: derived Haunch sekmesi bos degil");
    }
    if (const ComponentType* dt = reg.findType(e1.typeId)) {
        bool anyDoc = false;
        for (const auto& s : dt->params)
            if (s.documentedOnly) anyDoc = true;
        check(!anyDoc, "registerInto: gri alan motor tipine girmez");
    }

    // derived generator GERCEKTEN plan uretir (family generatorunu cagirir).
    // Basit ortogonal kolon + kiris girdisi.
    if (g) {
        ComponentInput in;
        in.main.id = 1;
        in.main.axisFrom = Vec3{0, 0, 0};
        in.main.axisTo = Vec3{0, 0, 3000};
        in.main.refNormal = Vec3{0, 0, 1};
        in.main.secWidth = 300;
        in.main.secHeight = 390;
        in.secondary.push_back(MemberGeometry{});
        in.secondary[0].id = 2;
        in.secondary[0].axisFrom = Vec3{0, -1000, 1500};
        in.secondary[0].axisTo = Vec3{0, 0, 1500};
        in.secondary[0].refNormal = Vec3{0, 0, 1};
        in.secondary[0].secWidth = 300;
        in.secondary[0].secHeight = 290;
        const GenerationPlan plan = (*g)(in);
        check(plan.ok, "derived generator: ortogonal kolon+kiris plan uretir");
        check(plan.ok && !plan.plates.empty(), "derived generator: levha uretilir");
        check(plan.ok && !plan.boltGroups.empty(), "derived generator: bulon grubu uretilir");
    }

    // --- 5) registerInto (family KAYITLI DEGIL) -----------------------------
    UserCatalog cat4;
    UserCatalogEntry orphanTemplate = e1;
    orphanTemplate.typeId = L"user_missing_family";
    orphanTemplate.sourceTypeId = L"nonexistent_family";
    orphanTemplate.displayName = L"Kayip Aile";
    cat4.addOrUpdate(orphanTemplate);
    ComponentRegistry reg2;
    ComponentDialogRegistry dialogs2;
    registerBuiltInComponents(reg2);  // family yok
    const auto warn4 = cat4.registerInto(reg2, dialogs2);
    check(warn4.size() == 1, "registerInto: eksik family -> 1 uyari");
    check(reg2.findType(L"user_missing_family") == nullptr, "registerInto: eksik family sablonu kaydedilmez");

    // --- 6) mappedParamsToCompParams ----------------------------------------
    BridgeNativeMapping map;
    map.params.push_back(BridgeMappedParam{L"plateThickness", CompParamValue{10.0},
                                           BridgeParamProvenance::Observed, "x"});
    map.params.push_back(BridgeMappedParam{L"beamGap", CompParamValue{20.0},
                                           BridgeParamProvenance::Unresolved, "yok"});
    const auto all = mappedParamsToCompParams(map, false);
    check(all.size() == 2, "mappedParams: hepsi aktarilir (skip=false)");
    const auto resolved = mappedParamsToCompParams(map, true);
    check(resolved.size() == 1 && resolved[0].name == L"plateThickness",
          "mappedParams: Unresolved atlanir (skip=true)");

    // --- 7) userCatalogEntryToComponentType --------------------------------
    const ComponentType t = userCatalogEntryToComponentType(e1);
    check(t.typeId == e1.typeId && t.displayName == e1.displayName, "toComponentType: kimlik");
    check(t.catalog.catalogVisible == true, "toComponentType: catalogVisible=true");
    check(t.catalog.pickCount == 2, "toComponentType: pickCount korunur");

    // isReferenceOnly sablonu (unsupported geometri) -> referans etiketi korunur
    UserCatalogEntry ref = deriveComponentTemplate(src, L"Referans Şablon", {},
                                                   true, L"stiffener desteklenmiyor");
    check(ref.isReferenceOnly && ref.referenceNote == L"stiffener desteklenmiyor",
          "derive: reference-only etiketi");

    std::printf("---\n%s: %d failure\n", failures == 0 ? "OK" : "FAIL", failures);
    return failures == 0 ? 0 : 1;
}
