// GENERIC COMPONENT CATALOG (C7) — çekirdek test.
//
// Katalog = ComponentRegistry + ComponentDialogRegistry kayıtlarından OKUNAN bir
// view'dir (ikinci bir kayıt deposu DEĞİL). Bu test doğrular:
//   1) Çalışma-zamanı kayıt: registerBuiltInComponents/registerBuiltInDialogs
//      çağrılınca katalog anında görür (derleme-zamanı değil).
//   2) Metadata: numara/kategori/özellik/coverage/pickCount registry'den taşır.
//   3) End Plate (catalogVisible=false) katalogda GÖRÜNMEZ — kendi zengin komut
//      yolu korunur.
//   4) Fin Plate + 146 görünür; ikisinin de generator + dialog kaydı var.
//   5) Generic üretim: registry.generate hem fin_plate hem 146 için geçerli plan
//      üretir (generic katalog komutunun beslediği zincir); bilinmeyen tip
//      ok=false (belge değişmez).
//   6) Katalog deterministik sıralanır (category, sonra number).
//   7) View canlıdır: dialog kaydı silinince hasDialog=false (önbellek yok).
// OCC'siz, Linux'ta koşar.

#include "model_maker/component_catalog.hpp"

#include <cstdio>
#include <string>

using namespace mm;

static int failures = 0;
static void check(bool ok, const char* label) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", label);
    if (!ok) ++failures;
}

static ComponentInput makeInput() {
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
    return in;
}

int main() {
    // --- 1) Çalışma-zamanı kayıt + katalog keşfi -----------------------------
    ComponentRegistry reg;
    ComponentDialogRegistry dialogs;
    check(reg.typeCount() == 0, "baslangicta registry bos");

    registerBuiltInComponents(reg);
    registerBuiltInDialogs(dialogs);
    check(reg.typeCount() == 4, "4 yerlesik tip kayitli (end plate + fin plate + 146 + 40)");

    const std::vector<CatalogEntry> catalog = buildComponentCatalog(reg, dialogs);
    check(catalog.size() == 3, "katalogda 3 gorunur giris (fin plate + 146 + 40)");

    // --- 2) End Plate gizli (kendi zengin komut yolu) ------------------------
    check(findCatalogEntry(catalog, L"beam_to_column_end_plate") == nullptr,
          "end plate katalogda YOK (catalogVisible=false)");
    const ComponentType* ep = reg.findType(L"beam_to_column_end_plate");
    check(ep != nullptr && !ep->catalog.catalogVisible, "end plate tipi var ama catalogVisible=false");

    // --- 3) 146 metadata'sı registry'den taşınır -----------------------------
    const CatalogEntry* e146 = findCatalogEntry(catalog, L"tekla_shear_plate_146");
    check(e146 != nullptr, "146 katalogda");
    if (e146) {
        check(e146->sourceComponentNumber == L"146", "146 numara = 146");
        check(e146->category == L"Shear connection", "146 kategori");
        check(e146->coverageStatus == L"PARTIALLY_IMPLEMENTED", "146 coverage PARTIALLY_IMPLEMENTED");
        check(e146->pickCount == 2, "146 pickCount 2");
        check(!e146->supportedFeatures.empty(), "146 desteklenen ozellikler dolu");
        check(e146->hasGenerator, "146 generator var");
        check(e146->hasDialog, "146 dialog var");
        check(!e146->dialogGroups.empty(), "146 dialog gruplari dolu");
    }

    // --- 3B) Haunch 40: native generator and editable dialog -----------------
    const CatalogEntry* h40 = findCatalogEntry(catalog, L"tekla_haunch_40");
    check(h40 != nullptr, "Haunch 40 katalogda");
    if (h40) {
        check(h40->sourceComponentNumber == L"40", "Haunch 40 kaynak numarasi");
        check(h40->pickCount == 2, "Haunch 40 kolon + kiris");
        check(h40->coverageStatus == L"PARTIALLY_IMPLEMENTED", "Haunch 40 coverage");
        check(h40->hasGenerator && h40->hasDialog, "Haunch 40 generator + dialog");
        check(!h40->dialogGroups.empty(), "Haunch 40 tabbed parameter groups");
    }

    // --- 4) Fin Plate de katalogda (ikinci tip; mekanizma tekrar kullanılabilir)
    const CatalogEntry* efp = findCatalogEntry(catalog, L"fin_plate");
    check(efp != nullptr, "fin plate katalogda");
    if (efp) {
        check(efp->hasGenerator, "fin plate generator var");
        check(efp->hasDialog, "fin plate dialog var");
        check(!efp->dialogGroups.empty(), "fin plate dialog gruplari dolu");
    }

    // --- 5) Deterministik sıralama: category artan; ayni kategoride number ---
    check(!catalog.empty() && catalog[0].typeId == L"tekla_haunch_40",
          "katalog kategoriye gore siralanmis (Moment / Haunch once)");

    // --- 6) Generic üretim zinciri (katalog komutunun beslediği yol) ---------
    {
        GenerationPlan fp = reg.generate(L"fin_plate", makeInput());
        GenerationPlan s146 = reg.generate(L"tekla_shear_plate_146", makeInput());
        GenerationPlan h40p = reg.generate(L"tekla_haunch_40", makeInput());
        check(h40p.ok && h40p.plates.size() == 4 && h40p.boltGroups.size() == 2,
              "generic Haunch 40 generation");
        check(fp.ok && s146.ok, "generic uretim: fin_plate ve 146 gecerli plan");
        check(fp.plates.size() == 1 && fp.boltGroups.size() == 1,
              "generic uretim: levha + bulon grubu");

        GenerationPlan bad = reg.generate(L"bilinmeyen_tip", makeInput());
        check(!bad.ok, "bilinmeyen tip -> ok=false (belge degismez)");
    }

    // --- 7) View canlıdır (önbellek yok) -------------------------------------
    {
        ComponentDialogRegistry emptyDialogs;  // dialog kaydi YOK
        const std::vector<CatalogEntry> c2 = buildComponentCatalog(reg, emptyDialogs);
        const CatalogEntry* f2 = findCatalogEntry(c2, L"fin_plate");
        check(f2 != nullptr && !f2->hasDialog && f2->dialogGroups.empty(),
              "dialog kaydi olmayinca hasDialog=false (canli view, onbellek yok)");
    }

    std::printf("FAILURES: %d\n", failures);
    return failures == 0 ? 0 : 1;
}
