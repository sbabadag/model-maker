// P-2/5 — MALZEME LISTESI / RAPOR: levha, bulon ve kaynak satirlari.
//
// Kontrat (docs/agent/P-1-STUDIO-PAKETLERI.md, P-2):
//   "Malzeme listesi / rapor: levha, bulon ve kaynak satirlari."
//
// Bu test GERCEK Document uzerinde raporu kurar ve denetler:
//   * Ayni gorunen levhalar TEK satirda toplanir (adet artar), farkli olcu ayri.
//   * Bulon montaji ve kaynak katisi GORSEL modelleri levha sayilmaz.
//   * Profilli uye ayri bolumde; boy bbox'tan gelir.
//   * Rapor Turkce ve toplam satiri tutarli.

#include "model_maker/bolt.hpp"
#include "model_maker/document.hpp"
#include "model_maker/report.hpp"
#include "model_maker/weld.hpp"

#include <cmath>
#include <cstdio>
#include <string>

using namespace mm;

static int failures = 0;
static void check(bool ok, const std::string& label) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", label.c_str());
    if (!ok) ++failures;
}

// Eksen hizali kutu (bbox'tan baska bir sey rapora girmez).
static WireframeModel box(double sx, double sy, double sz) {
    std::vector<Vec3> v;
    for (int i = 0; i < 8; ++i) {
        v.push_back(Vec3{(i & 1) ? sx : 0.0, (i & 2) ? sy : 0.0, (i & 4) ? sz : 0.0});
    }
    std::vector<Edge> e{{0, 1}, {1, 3}, {3, 2}, {2, 0}, {4, 5}, {5, 7},
                        {7, 6}, {6, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
    return WireframeModel(v, e);
}

int main() {
    Document doc;

    // --- UYE: HEB300, boy 3000 mm -------------------------------------------
    {
        WireframeModel m = box(300.0, 300.0, 3000.0);
        auto props = m.properties();
        props.profileName = "HEB300";
        props.material = "S235JR";
        m.setProperties(props);
        doc.addModel(std::move(m));
    }
    // --- LEVHALAR: ayni iki levha + farkli bir levha -------------------------
    for (int k = 0; k < 2; ++k) {
        WireframeModel m = box(300.0, 300.0, 15.0);
        auto props = m.properties();
        props.name = "PL-1";
        props.material = "S235JR";
        props.description = "Pos_No: EP1";
        m.setProperties(props);
        doc.addModel(std::move(m));
    }
    {
        WireframeModel m = box(500.0, 500.0, 30.0);
        auto props = m.properties();
        props.name = "BP-1";
        props.material = "S275JR";
        m.setProperties(props);
        doc.addModel(std::move(m));
    }
    // --- BULON GRUBU: M20 8.8, 2x2 = 4 bulon (gorsel modeli levha SAYILMAZ) --
    {
        BoltGroup bg;
        bg.origin = {0.0, 0.0, 0.0};
        bg.axis = {0.0, 0.0, 1.0};
        bg.xDir = {1.0, 0.0, 0.0};
        bg.countX = 2;
        bg.countY = 2;
        bg.spacingX = 100.0;
        bg.spacingY = 100.0;
        bg.params.nominalDiameter = 20.0;
        bg.params.holeDiameter = 22.0;
        bg.params.grade = "8.8";
        bg.gripLength = 45.0;
        doc.addBoltGroup(bg, box(30.0, 30.0, 60.0));
    }
    // --- KAYNAK: kose kaynagi a=5, cift tarafli (gorsel modeli SAYILMAZ) ----
    {
        Weld w;
        w.type = WeldType::Fillet;
        w.size = 5.0;
        w.doubleSided = true;
        doc.addWeld(w, box(10.0, 10.0, 10.0));
    }

    const MaterialList list = buildMaterialList(doc);
    check(list.memberCount == 1, "uye sayisi 1");
    check(list.plateCount == 3, "levha sayisi 3 (bulon/kaynak gorseli HARIC)");
    check(list.plates.size() == 2, "levha satiri 2 (ayni olcu tek satirda)");
    check(list.boltCount == 4, "bulon sayisi 4 (2x2 grid)");
    check(list.boltGroups.size() == 1, "bulon satiri 1 grup");
    check(list.weldCount == 1, "kaynak sayisi 1");
    if (list.plates.size() == 2) {
        int two = 0, one = 0;
        for (const auto& r : list.plates) {
            if (r.count == 2) ++two;
            else if (r.count == 1) ++one;
        }
        check(two == 1 && one == 1, "PL-1 adet 2, BP-1 adet 1");
    }

    const std::wstring text = materialListReport(doc);
    const std::wstring& t = text;
    const auto has = [&t](const wchar_t* s) { return t.find(s) != std::wstring::npos; };
    check(has(L"MALZEME LISTESI"), "rapor basligi");
    check(has(L"UYELER"), "UYELER bolumu");
    check(has(L"LEVHALAR"), "LEVHALAR bolumu");
    check(has(L"BULON GRUPLARI"), "BULON GRUPLARI bolumu");
    check(has(L"KAYNAKLAR"), "KAYNAKLAR bolumu");
    check(has(L"HEB300"), "uye profili raporda");
    check(has(L"L=3000 mm"), "uye boyu bbox'tan");
    check(has(L"300 x 300 x 15"), "levha olcusu (buyukten kucuge)");
    check(has(L"500 x 500 x 30"), "ikinci levha olcusu");
    check(has(L"Pos_No: EP1"), "levha pos numarasi");
    check(has(L"M20 8.8"), "bulon cap + sinif");
    check(has(L"2x2 = 4 bulon"), "bulon grid adedi");
    check(has(L"Kose kaynagi"), "kaynak turu");
    check(has(L"cift tarafli"), "kaynak tek/cift tarafli");
    check(has(L"TOPLAM:"), "toplam satiri");

    // Bos belge: bolumler "yok" demeli, cokme olmamali.
    {
        Document empty;
        const std::wstring et = materialListReport(empty);
        check(et.find(L"MALZEME LISTESI") != std::wstring::npos && et.find(L"yok") != std::wstring::npos,
              "bos belge: bolumler 'yok' ile cikar");
        const MaterialList el = buildMaterialList(empty);
        check(el.memberCount == 0 && el.plateCount == 0 && el.boltCount == 0 && el.weldCount == 0,
              "bos belge: sayilar 0");
    }

    if (failures) {
        std::printf("%d FAIL\n", failures);
        return 1;
    }
    std::printf("\n--- rapor ornegi ---\n");
    std::wprintf(L"%ls", text.c_str());
    std::printf("ALL PASS\n");
    return 0;
}
