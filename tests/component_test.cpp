// COMPONENT FOUNDATION (P4) — cekirdek test.
//
// Main/Secondary Part, parametrik Component tanimi, instance + parametreler,
// bagimlilik ve yeniden hesaplama altyapisi. OCC'siz, Linux'ta kosar; saf veri
// modeli + bagimlilik grafi (gercek geometri yeniden uretimi OCC katmaninda).

#include "model_maker/component.hpp"

#include <cmath>
#include <cstdio>

using namespace mm;

static int failures = 0;
static void check(bool ok, const char* label) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", label);
    if (!ok) ++failures;
}

static bool near3(const Vec3& a, const Vec3& b, double tol = 1e-9) {
    return std::abs(a.x - b.x) < tol && std::abs(a.y - b.y) < tol && std::abs(a.z - b.z) < tol;
}

int main() {
    ComponentModel model;

    // 1) Tanim ekleme + tekillik.
    ComponentDefinition brace;
    brace.name = L"Çapraz Bağlantı";
    brace.parameters = {L"uzunluk", L"yukseklik"};
    check(model.addDefinition(brace), "tanim eklendi");
    check(!model.addDefinition(brace), "ayni isim reddedilir");
    check(model.findDefinition(L"Çapraz Bağlantı") != nullptr, "findDefinition bulur");
    check(model.findDefinition(L"Yok") == nullptr, "findDefinition yok -> nullptr");

    // 2) Instance ekleme (Main + Secondary).
    ComponentMember main;
    main.role = PartRole::Main;
    main.partId = EntityId{100};
    ComponentMember sec;
    sec.role = PartRole::Secondary;
    sec.partId = EntityId{101};
    const std::size_t i0 = model.addInstance(L"Çapraz Bağlantı",
        {{L"uzunluk", 1200.0}, {L"yukseklik", 800.0}}, {0, 0, 0}, {main, sec});
    check(model.instanceCount() == 1, "instance eklendi");
    check(!model.instance(i0).orphan, "tanim var -> orphan false");
    check(model.instance(i0).values.size() == 2, "2 parametre hizalandi");

    // 3) Main part tespiti.
    {
        const auto mp = model.mainPartIndex(i0);
        check(mp.has_value() && *mp == 0, "main part indeksi 0");
    }

    // 4) Parametre degistirme kirletir.
    check(model.instance(i0).dirty == false, "baslangicta temiz (addInstance hizaladi)");
    check(model.setInstanceParameter(i0, L"uzunluk", 1500.0), "parametre guncellendi");
    check(model.instance(i0).dirty == true, "parametre degisince kirli");
    check(model.instance(i0).values.size() == 2 &&
              model.instance(i0).values[0].value == 1500.0, "deger guncellendi");
    check(!model.setInstanceParameter(i0, L"olmayan", 1.0), "semada olmayan parametre reddedilir");

    // 5) Yerlesim degistirme kirletir.
    model.recomputeDirty(); // temizle
    check(model.setInstanceOrigin(i0, {10, 20, 30}), "origin guncellendi");
    check(model.instance(i0).dirty == true, "origin degisince kirli");
    check(near3(model.instance(i0).origin, {10, 20, 30}), "origin degeri dogru");

    // 6) Tanim semasi degisince bagli instance'lar kirlenir + recompute hizalar.
    model.recomputeDirty();
    check(model.dirtyInstances().empty(), "recompute sonrasi temiz");
    check(model.setDefinitionParameters(L"Çapraz Bağlantı", {L"uzunluk", L"yukseklik", L"et"}),
          "sema genisletildi");
    check(model.dirtyInstances().size() == 1, "semasi degisen tanimin instance'i kirli");
    const std::size_t recomputed = model.recomputeDirty();
    check(recomputed == 1, "recomputeDirty 1 instance isledi");
    check(model.instance(i0).values.size() == 3, "yeni parametre eklendi (varsayilan 0)");
    check(model.instance(i0).values[2].name == L"et" && model.instance(i0).values[2].value == 0.0,
          "eksik parametre varsayilanla dolduruldu");

    // 7) Bagimlilik kopmasi: tanim silinince instance orphan olur.
    check(model.removeDefinition(L"Çapraz Bağlantı"), "tanim silindi");
    check(model.dirtyInstances().size() == 1, "silinen tanimin instance'i kirli");
    model.recomputeDirty();
    check(model.instance(i0).orphan == true, "recompute orphan isaretler");
    check(model.instance(i0).dirty == false, "orphan isaretlenince temiz");

    // 8) Coklu instance: bagimsiz yeniden hesaplama.
    {
        ComponentModel m2;
        ComponentDefinition d;
        d.name = L"A"; d.parameters = {L"p"};
        m2.addDefinition(d);
        m2.addInstance(L"A", {{L"p", 1.0}}, {0, 0, 0}, {});
        m2.addInstance(L"A", {{L"p", 2.0}}, {0, 0, 0}, {});
        check(m2.instanceCount() == 2, "2 instance");
        m2.setInstanceParameter(0, L"p", 9.0);
        check(m2.dirtyInstances().size() == 1 && m2.dirtyInstances()[0] == 0,
              "yalniz degistirilen instance kirli");
        m2.recomputeDirty();
        check(m2.dirtyInstances().empty(), "hepsi temiz");
    }

    // 9) Parca bagimlilik grafi.
    {
        ComponentModel m3;
        ComponentDefinition d;
        d.name = L"B"; d.parameters = {L"p"};
        m3.addDefinition(d);
        ComponentMember m; m.role = PartRole::Main; m.partId = EntityId{200};
        ComponentMember s; s.role = PartRole::Secondary; s.partId = EntityId{201};
        const std::size_t i = m3.addInstance(L"B", {{L"p", 1.0}}, {0, 0, 0}, {m, s});
        const auto parts = m3.partsOfInstance(i);
        check(parts.size() == 2 && parts[0] == 200 && parts[1] == 201,
              "partsOfInstance: main+secondary");
        const auto users = m3.instancesUsingPart(EntityId{201});
        check(users.size() == 1 && users[0] == i, "instancesUsingPart: 1 instance");
        check(m3.instancesUsingPart(EntityId{999}).empty(), "kullanilmayan parca -> bos");
    }

    // 10) End Plate komponenti: tanim + CutModifier uretimi.
    {
        const ComponentDefinition def = makeEndPlateDefinition();
        check(def.name == L"End Plate", "End Plate tanim adi");
        check(def.parameters.size() == 3 && def.parameters[0] == L"plateThickness" &&
                  def.parameters[1] == L"plateWidth" && def.parameters[2] == L"plateHeight",
              "End Plate 3 parametre");

        ComponentInstance inst;
        inst.definition = L"End Plate";
        inst.values = {{L"plateThickness", 20.0}, {L"plateWidth", 300.0}, {L"plateHeight", 350.0}};
        ComponentMember main;
        main.role = PartRole::Main;
        main.partId = EntityId{200};
        inst.members = {main};

        const auto* mp = endPlateMainPart(inst);
        check(mp != nullptr && mp->partId == 200, "endPlateMainPart bulur");

        const Vec3 from{0, 0, 0};
        const Vec3 to{1000, 0, 0};
        const CutModifier fit = endPlateFittingCutModifier(inst, from, to);
        check(fit.kind == CutKind::Fitting, "End Plate fitting turu");
        check(fit.target == 200, "End Plate fitting hedefi = ana part");
        check(near3(fit.planeNormal, {1, 0, 0}), "eksen normali +X");
        check(near3(fit.planeOrigin, {1000, 0, 0}), "duzlem ana part ucunda (axisTo)");

        // Dejenere eksen (from==to): normal +X varsayilir.
        const CutModifier deg = endPlateFittingCutModifier(inst, {5, 5, 5}, {5, 5, 5});
        check(near3(deg.planeNormal, {1, 0, 0}), "dejenere eksen -> normal +X");
    }

    std::printf("%s\n", failures == 0 ? "ALL PASS" : "SOME FAILED");
    return failures == 0 ? 0 : 1;
}
