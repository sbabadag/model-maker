// COMPONENT AYAR SETLERI (Tekla Save / Save as / Load) — saf cekirdek testi (OCC'siz, Qt'siz).
//
// Dogrular: ad dogrulama, "standard" her zaman ilk, put=ekle/ustune yaz, tipler dahil
// JSON gidis-donus (double/int/bool/string/entity, Turkce karakter), bilesen basina izole
// adlar, remove, bozuk/eksik dosya, atomik yazim (gecici dosya kalmaz).

#include "model_maker/component_presets.hpp"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace mm;

static int failures = 0;
static void check(bool ok, const char* label) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", label);
    if (!ok) ++failures;
}

static std::filesystem::path tempJson(const char* tag) {
    return std::filesystem::temp_directory_path() / (std::string("mm_presets_test_") + tag + ".json");
}

int main() {
    // --- ad dogrulama ------------------------------------------------------
    check(ComponentPresetStore::validName(L"Cerceve A"), "gecerli ad kabul");
    check(!ComponentPresetStore::validName(L""), "bos ad reddedilir");
    check(!ComponentPresetStore::validName(L"   "), "yalniz bosluk reddedilir");
    check(!ComponentPresetStore::validName(L"a\nb"), "kontrol karakteri reddedilir");
    check(!ComponentPresetStore::validName(std::wstring(65, L'x')), "65 karakter reddedilir");
    check(ComponentPresetStore::validName(std::wstring(64, L'x')), "64 karakter kabul");
    check(ComponentPresetStore::trimmed(L"  ab c \t") == L"ab c", "bas/son bosluk kirpilir");

    ComponentPresetStore store;
    const std::wstring haunch = L"tekla_haunch_40";
    const std::wstring plate = L"tekla_shear_plate_146";

    // --- "standard" her zaman ilk, kayitli olmasa da -----------------------
    {
        const auto n = store.names(haunch);
        check(n.size() == 1 && n[0] == L"standard", "bos depoda tek ad: standard");
        check(store.find(haunch, L"standard") == nullptr,
              "kayitli degil standard -> nullptr (cagiran sema varsayilanini kullanir)");
    }

    // --- put: ekle, ustune yaz, gecersiz ad, tip izolasyonu -----------------
    const std::vector<CompParam> a{
        {L"haunchLength", CompParamValue{600.0}},
        {L"upperBoltCountX", CompParamValue{3}},
        {L"createBeamStiffener", CompParamValue{true}},
        {L"plateMaterial", CompParamValue{std::wstring(L"S355J2 \u00e7elik \u015f")}},
        {L"mainRef", CompParamValue{EntityId{4242}}}};
    check(store.put(haunch, L"  Cerceve A ", a), "put yeni -> true (ad kirpildi)");
    check(store.contains(haunch, L"Cerceve A"), "kirpilmis ad ile bulunur");
    check(!store.put(haunch, L"Cerceve A", {{L"haunchLength", CompParamValue{700.0}}}),
          "ayni ad ustune yazar -> false");
    const auto* over = store.find(haunch, L"Cerceve A");
    check(over && over->params.size() == 1 &&
              std::get<double>(over->params[0].value) == 700.0,
          "ustune yazma degerleri degistirdi");
    check(!store.put(haunch, L"   ", a) && store.size() == 1, "gecersiz ad depoyu DEGISTIRMEZ");
    check(!store.put(L"", L"x", a), "bos typeId reddedilir");
    store.put(haunch, L"Cerceve A", a);
    store.put(haunch, L"Berkitmeli", a);
    store.put(haunch, L"standard", {{L"haunchLength", CompParamValue{500.0}}});
    store.put(plate, L"Cerceve A", {{L"plateT", CompParamValue{12.0}}});
    {
        const auto n = store.names(haunch);
        check(n.size() == 3 && n[0] == L"standard" && n[1] == L"Berkitmeli" && n[2] == L"Cerceve A",
              "adlar: standard ilk, digerleri alfabetik");
        check(store.names(plate).size() == 2, "baska bilesenin adlari izole");
        check(store.find(plate, L"Cerceve A")->params.size() == 1,
              "ayni ad farkli bilesende ayri set");
    }

    // --- JSON gidis-donus ---------------------------------------------------
    const auto path = tempJson("roundtrip");
    std::filesystem::remove(path);
    std::string err;
    check(store.save(path, err), "save basarili");
    check(!std::filesystem::exists(path.string() + ".tmp"), "atomik yazim: gecici dosya kalmadi");
    ComponentPresetStore loaded;
    check(loaded.load(path, err) && err.empty(), "load basarili");
    check(loaded.size() == store.size(), "set sayisi korunur");
    const auto* back = loaded.find(haunch, L"Berkitmeli");
    bool typesOk = back && back->params.size() == a.size();
    if (typesOk) {
        typesOk &= std::get<double>(back->params[0].value) == 600.0;
        typesOk &= std::get<int>(back->params[1].value) == 3;
        typesOk &= std::get<bool>(back->params[2].value) == true;
        typesOk &= std::get<std::wstring>(back->params[3].value) == L"S355J2 \u00e7elik \u015f";
        typesOk &= std::get<EntityId>(back->params[4].value) == EntityId{4242};
        typesOk &= back->params[0].name == L"haunchLength";
    }
    check(typesOk, "double/int/bool/string(Turkce)/entity tipleri birebir korunur");
    check(loaded.find(haunch, L"standard") != nullptr &&
              std::get<double>(loaded.find(haunch, L"standard")->params[0].value) == 500.0,
          "kayitli standard geri gelir");

    // --- ustune yazan ikinci save ------------------------------------------
    loaded.put(haunch, L"Berkitmeli", {{L"haunchLength", CompParamValue{999.0}}});
    check(loaded.save(path, err), "ikinci save (var olan dosyanin ustune)");
    ComponentPresetStore again;
    again.load(path, err);
    check(std::get<double>(again.find(haunch, L"Berkitmeli")->params[0].value) == 999.0,
          "ikinci save degeri kalici yapar");

    // --- remove -------------------------------------------------------------
    check(again.remove(haunch, L"Berkitmeli") && !again.contains(haunch, L"Berkitmeli"),
          "remove calisir");
    check(!again.remove(haunch, L"Berkitmeli"), "olmayan set remove -> false");

    // --- dosya yok / bos / bozuk -------------------------------------------
    {
        ComponentPresetStore e;
        check(e.load(tempJson("yok_dosya"), err) && e.size() == 0, "dosya yok -> bos depo, hata yok");
        const auto empty = tempJson("bos");
        { std::ofstream(empty, std::ios::trunc); }
        check(e.load(empty, err) && e.size() == 0, "bos dosya -> bos depo");
        const auto bad = tempJson("bozuk");
        { std::ofstream(bad, std::ios::trunc) << "{ not json"; }
        check(!e.load(bad, err) && !err.empty() && e.size() == 0,
              "bozuk JSON -> false + mesaj, depo bos");
        const auto wrong = tempJson("yanlis_kok");
        { std::ofstream(wrong, std::ios::trunc) << "{\"x\":1}"; }
        check(!e.load(wrong, err), "presets dizisi olmayan kok reddedilir");
        const auto partial = tempJson("kismi");
        { std::ofstream(partial, std::ios::trunc)
              << "{\"presets\":[{\"typeId\":\"t\",\"name\":\"ok\",\"params\":"
                 "[{\"name\":\"a\",\"type\":\"double\",\"value\":1.5},"
                 "{\"name\":\"b\",\"type\":\"int\",\"value\":\"kotu\"}]},"
                 "{\"typeId\":\"t\",\"name\":\"\",\"params\":[]}]}"; }
        check(e.load(partial, err) && e.size() == 1 &&
                  e.find(L"t", L"ok") && e.find(L"t", L"ok")->params.size() == 1,
              "kismi bozuk kayit: gecersiz param ve gecersiz adli set atlanir, gerisi yuklenir");
        std::filesystem::remove(empty);
        std::filesystem::remove(bad);
        std::filesystem::remove(wrong);
        std::filesystem::remove(partial);
    }
    std::filesystem::remove(path);

    std::printf(failures ? "FAILURES: %d\n" : "ALL PASS\n", failures);
    return failures ? 1 : 0;
}
