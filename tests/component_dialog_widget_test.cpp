// UNIVERSAL COMPONENT DIALOG — Qt offscreen testi (Linux, Qt6 varken).
//
// Widget'ın gerçek davranışını başsız (offscreen) doğrular: end plate şeması
// kurulur, düzenlenmemiş değerler round-trip'te korunur (özellikle -1e9
// "tanımsız" sentinel'leri — spinbox kelepçelememeli), bilinmeyen (ekstra)
// parametreler korunur, fin plate şeması kurulur ve şema doğrulaması geçer.
// Windows'ta bu test kurulmaz (model-maker exe'si görsel olarak aynı widget'ı
// kullanır); Linux'ta Qt6 yoksa CMake bu testi hiç eklemez.

#include "model_maker/component_dialog_widget.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QTabWidget>

#include <cstdio>

static int failures = 0;
#define CHECK(cond, msg)                                          \
    do {                                                          \
        if (!(cond)) {                                            \
            ++failures;                                           \
            std::printf("FAIL: %s\n", msg);                       \
        }                                                         \
    } while (0)

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    // 1) End plate şeması kurulur + round-trip sentinel'leri korur.
    const mm::ComponentDialogDefinition ep = mm::endPlateDialogDefinition();
    CHECK(ep.schema.typeId == L"beam_to_column_end_plate", "end plate typeId");
    CHECK(ep.specs.size() == 27, "end plate 27 alan");
    CHECK(ep.schema.groups.size() == 2, "end plate 2 grup");

    mm::EndPlateParams p0;
    const auto current = mm::endPlateToParams(p0);
    mm::ComponentDialogWidget w(ep, current);
    w.resize(900, 640);
    w.show();

    const auto out = w.resultParams();
    const mm::EndPlateParams p1 = mm::endPlateFromParams(out);
    CHECK(p1 == p0, "end plate round-trip korunmali");
    // Sentinel'ler açıkça: spinbox clamp'i -1e9'u ezmemeli.
    CHECK(p1.upperEdgeDistance == -1e9, "upperEdgeDistance sentinel -1e9 korunmali");
    CHECK(p1.lowerEdgeDistance == -1e9, "lowerEdgeDistance sentinel -1e9 korunmali");
    CHECK(p1.topExtra == -1.0 && p1.bottomExtra == -1.0,
          "top/bottomExtra legacy sentineli korunmali");
    CHECK(p1.boltOffsetV == -1.0, "boltOffsetV sentineli korunmali");

    // 2) Bilinmeyen (ekstra) parametre korunur.
    std::vector<mm::CompParam> extra = current;
    extra.push_back(mm::CompParam{L"future_field", 42.0});
    mm::ComponentDialogWidget w2(ep, extra);
    const auto out2 = w2.resultParams();
    bool preserved = false;
    for (const auto& p : out2)
        if (p.name == L"future_field") preserved = true;
    CHECK(preserved, "bilinmeyen parametre korunmali");

    // 3) Fin plate şeması kurulur (engine specs'ten türer).
    const mm::ComponentDialogDefinition fp = mm::finPlateDialogDefinition();
    CHECK(fp.typeId == L"fin_plate", "fin plate typeId");
    CHECK(!fp.specs.empty(), "fin plate specs bos degil");
    CHECK(fp.schema.groups.size() >= 2, "fin plate gruplari");
    CHECK(fp.schema.hotspots.size() >= 4, "fin plate hotspotlari");

    // 4) Şema doğrulaması (portable core üzerinden).
    const auto v = mm::validateDialogSchema(ep.schema, ep.specs);
    CHECK(v.ok, "end plate sema dogrulamasi");
    const auto vf = mm::validateDialogSchema(fp.schema, fp.specs);
    CHECK(vf.ok, "fin plate sema dogrulamasi");

    // 5) Hit-test: hotspot dışı boş alan nullopt, hotspot içi id döner.
    const auto miss = mm::hitTestDiagramHotspot(ep.schema, mm::DiagramPoint{300.0, 10.0});
    CHECK(!miss.has_value(), "bos alan hotspot dondurmemeli");

    // 6) Tekla 146 diyalogu: SEKMELER gercekten kurulur ve "Default" onay
    //    kutusu isaretsiz birakilinca parametre sonuca YAZILMAZ.
    {
        const mm::ComponentDialogDefinition d = mm::shearPlate146DialogDefinition();
        CHECK(d.schema.useTabs, "146: sekmeli duzen");
        const std::vector<mm::CompParam> empty;
        mm::ComponentDialogWidget w146(d, empty);
        w146.resize(1000, 700);
        w146.show();

        auto* tabWidget = w146.findChild<QTabWidget*>();
        CHECK(tabWidget != nullptr, "146: QTabWidget kuruldu");
        CHECK(tabWidget && tabWidget->count() == 6, "146: 6 sekme cizildi");
        if (tabWidget) {
            CHECK(tabWidget->tabText(0) == QStringLiteral("Plates"), "146: ilk sekme Plates");
            CHECK(tabWidget->tabText(1) == QStringLiteral("Stiffeners"), "146: ikinci sekme Stiffeners");
            CHECK(tabWidget->tabText(5) == QStringLiteral("Beam cut"), "146: son sekme Beam cut");
        }

        // Baslangicta Default kutulari isaretli -> alan sonuca yazilir.
        auto params = w146.resultParams();
        bool hasStandard = false;
        for (const auto& p : params)
            if (p.name == L"boltStandard") hasStandard = true;
        CHECK(hasStandard, "146: boltStandard sonuca yazilir (kutu isaretli)");

        // "Default" onay kutusunu bul (tooltip ile) ve isareti kaldir.
        QCheckBox* opt = nullptr;
        for (auto* cb : w146.findChildren<QCheckBox*>())
            if (cb->toolTip().contains(QStringLiteral("Default"))) { opt = cb; break; }
        CHECK(opt != nullptr, "146: Default onay kutusu bulundu");
        if (opt) {
            opt->setChecked(false);
            params = w146.resultParams();
            bool stillThere = false;
            for (const auto& p : params)
                if (p.name == L"boltStandard") stillThere = true;
            CHECK(!stillThere, "146: Default isaretsiz -> parametre yazilmaz");
        }
    }

    if (failures) {
        std::printf("%d kontrol basarisiz\n", failures);
        return 1;
    }
    std::printf("component_dialog_widget: TUM KONTROLLER GECTI\n");
    return 0;
}
