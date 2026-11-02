// MM-5 adim 1 — ComponentGenerator TYPE-ERASED cagrilabilir testi (OCC GEREKTIRMEZ).
//
// Kural: paket yukleyici recipe'yi CAPTURE eden lambda kaydedebilmeli; ayni
// zamanda mevcut fonksiyon isaretcisi kayitlari (`registerType(t, generateEndPlate)`)
// AYNEN derlenip calismali. Bos uretec sessiz varsayilan URETMEZ; istisna
// firlatan uretec registry'de plana cevrilir (noexcept sinirinda terminate YOK).
#include "model_maker/component_engine.hpp"

#include <cstdio>
#include <stdexcept>
#include <string>

using namespace mm;

static int checks = 0, failures = 0;
static void check(bool ok, const char* what) {
    ++checks;
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
}

// Eski usul: serbest fonksiyon isaretcisi (yerlesik generatorlarin deseni).
static GenerationPlan fixedGenerator(const ComponentInput&) {
    GenerationPlan plan;
    plan.ok = true;
    plan.plates.push_back(PlateSpec{});
    plan.plateKeys.push_back("fixed_plate");
    return plan;
}

static ComponentType makeType(const wchar_t* id) {
    ComponentType t;
    t.typeId = id;
    t.displayName = id;
    return t;
}

int main() {
    ComponentInput input;
    input.main.axisFrom = {0, 0, 0};
    input.main.axisTo = {0, 0, 3000};

    // 1) Fonksiyon isaretcisi kaydi (geriye donuk uyum) — ortuk donusum.
    {
        ComponentRegistry reg;
        check(reg.registerType(makeType(L"legacy_ptr"), fixedGenerator), "1 fonksiyon isaretcisi kaydedilir");
        const ComponentGenerator* g = reg.generator(L"legacy_ptr");
        check(g != nullptr && !g->empty(), "1 generator() cagrilabilir uretec dondurur");
        const GenerationPlan p = reg.generate(L"legacy_ptr", input);
        check(p.ok && p.plates.size() == 1 && p.plateKeys.at(0) == "fixed_plate",
              "1 fonksiyon isaretcisi plani uretir");
    }

    // 2) Capture eden lambda (paket recipe ureteci deseni).
    {
        ComponentRegistry reg;
        ComponentType t = makeType(L"pkg_lambda");
        CompParamSpec spec;
        spec.name = L"plateThickness";
        spec.type = CompParamType::Length;
        spec.unit = L"mm";
        spec.defDouble = 12.0;
        t.params.push_back(spec);
        const std::string role = "pkg_plate";
        ComponentGenerator gen = ComponentGenerator(
            [role](const ComponentInput& in) {
                GenerationPlan plan;
                plan.ok = true;
                PlateSpec ps;
                ps.thickness = 20.0;
                plan.plates.push_back(ps);
                plan.plateKeys.push_back(role);
                (void)in;
                return plan;
            });
        check(reg.registerType(t, gen), "2 lambda ureteci kaydedilir");
        const GenerationPlan p = reg.generate(L"pkg_lambda", input);
        check(p.ok && p.plates.size() == 1 && p.plateKeys.at(0) == role,
              "2 lambda capture'i (role) plana yansir");
        check(p.plates.at(0).thickness == 20.0, "2 lambda govdesi calisir");
    }

    // 3) Bos uretec reddedilir (sessiz varsayilan yok).
    {
        ComponentRegistry reg;
        check(!reg.registerType(makeType(L"empty_gen"), ComponentGenerator{}), "3 bos uretec reddedilir");
        check(!reg.registerType(makeType(L"null_gen"), nullptr), "3 nullptr reddedilir");
        const GenerationPlan p = reg.generate(L"empty_gen", input);
        check(!p.ok && !p.error.empty(), "3 kayitsiz tip ok=false + Turkce hata");
    }

    // 4) Istisna firlatan uretec: noexcept sinirinda plana cevrilir.
    {
        ComponentRegistry reg;
        ComponentGenerator gen = ComponentGenerator(
            [](const ComponentInput&) -> GenerationPlan {
                throw std::runtime_error("paket recipe hatasi");
            });
        check(reg.registerType(makeType(L"throwing"), gen), "4 istisna firlatan uretec kaydedilir");
        const GenerationPlan p = reg.generate(L"throwing", input);
        check(!p.ok, "4 istisna ok=false plana cevrilir");
        check(p.error.find(L"istisna") != std::wstring::npos, "4 hata metni istisnayi bildirir");
    }

    // 5) Uretec kopyalanabilir: ayni lambda iki tipe kaydedilebilir.
    {
        ComponentRegistry reg;
        ComponentGenerator gen = ComponentGenerator([](const ComponentInput&) {
            GenerationPlan plan;
            plan.plates.push_back(PlateSpec{});
            return plan;
        });
        check(reg.registerType(makeType(L"copy_a"), gen), "5 kopya kaydi A");
        check(reg.registerType(makeType(L"copy_b"), gen), "5 kopya kaydi B");
        check(reg.generate(L"copy_a", input).plates.size() == 1 &&
              reg.generate(L"copy_b", input).plates.size() == 1,
              "5 iki tip de ayni uretecle calisir");
    }

    // 6) Cakisan typeId reddedilir (paket <-> yerlesik kurali burada da gecerli).
    {
        ComponentRegistry reg;
        check(reg.registerType(makeType(L"dup"), fixedGenerator), "6 ilk kayit basarili");
        check(!reg.registerType(makeType(L"dup"), fixedGenerator), "6 ayni typeId reddedilir");
    }

    std::printf("\n%d/%d checks gecti\n", checks - failures, checks);
    return failures == 0 ? 0 : 1;
}
