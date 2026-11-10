// P-1/3 — PAKET SURUM YUKSELTMESINDE PARAMETRE ESLEME (ada gore)
//
// Kural (P-1 madde 3): ayni ad -> deger KORUNUR (aralik/tip disi kelepcelenir),
// yeni parametre -> VARSAYILAN, semadan kalkan parametre -> ATILIR. Hepsi
// raporlanir (kac korundu / yeni / atildi, hangi adlar).
#include "model_maker/component_engine.hpp"

#include <cstdio>
#include <string>
#include <vector>

using namespace mm;

static int checks = 0, failures = 0;
static void check(bool ok, const char* what) {
    ++checks;
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
}

static CompParamSpec lenSpec(const std::wstring& name, double def, double lo, double hi) {
    CompParamSpec s;
    s.name = name;
    s.type = CompParamType::Length;
    s.unit = L"mm";
    s.defDouble = def;
    s.minDouble = lo;
    s.maxDouble = hi;
    s.hasMinDouble = true;
    s.hasMaxDouble = true;
    return s;
}

static CompParam num(const std::wstring& n, double v) {
    CompParam p;
    p.name = n;
    p.value = v;
    return p;
}

int main() {
    // 1) Semada olan + degerde olan -> KORUNUR, rapor "degisiklik yok".
    {
        const std::vector<CompParamSpec> specs{lenSpec(L"t", 12.0, 4.0, 100.0),
                                               lenSpec(L"extra", 20.0, 0.0, 200.0)};
        ComponentParamRemap rep;
        const auto out = remapParamsToSpecs(specs, {num(L"t", 16.0), num(L"extra", 30.0)}, &rep);
        check(out.size() == 2, "1 iki parametre dondu");
        check(paramAsDouble(out, L"t", 0.0) == 16.0, "1 kullanici degeri korundu (t=16)");
        check(paramAsDouble(out, L"extra", 0.0) == 30.0, "1 kullanici degeri korundu (extra=30)");
        check(rep.kept == 2 && rep.added == 0 && rep.dropped == 0 && rep.clamped == 0,
              "1 rapor: 2 korundu, ekleme/atma/kelepce yok");
        check(!rep.anyChange(), "1 rapor: degisiklik yok");
    }

    // 2) Yeni parametre -> VARSAYILAN + rapor.
    {
        const std::vector<CompParamSpec> specs{lenSpec(L"t", 12.0, 4.0, 100.0),
                                               lenSpec(L"yeni", 25.0, 0.0, 100.0)};
        ComponentParamRemap rep;
        const auto out = remapParamsToSpecs(specs, {num(L"t", 16.0)}, &rep);
        check(paramAsDouble(out, L"yeni", -1.0) == 25.0, "2 yeni parametre varsayilani aldi");
        check(rep.added == 1 && rep.addedNames.size() == 1 &&
                  rep.addedNames[0] == L"yeni",
              "2 rapor: 1 yeni (adi ile)");
        check(rep.anyChange(), "2 rapor: degisiklik var");
    }

    // 3) Semadan KALKAN parametre -> ATILIR + rapor (deger saklanmaz).
    {
        const std::vector<CompParamSpec> specs{lenSpec(L"t", 12.0, 4.0, 100.0)};
        ComponentParamRemap rep;
        const auto out = remapParamsToSpecs(
            specs, {num(L"t", 16.0), num(L"plateFinish", 0.0)}, &rep);
        check(out.size() == 1, "3 kalkan parametre sonuca yazilmadi");
        check(findParam(out, L"plateFinish") == nullptr, "3 kalkan parametre degeri atildi");
        check(rep.dropped == 1 && rep.droppedNames.size() == 1 &&
                  rep.droppedNames[0] == L"plateFinish",
              "3 rapor: 1 atildi (adi ile)");
    }

    // 4) Aralik disi deger -> KELEPCELENIR + rapor.
    {
        const std::vector<CompParamSpec> specs{lenSpec(L"t", 12.0, 4.0, 20.0)};
        ComponentParamRemap rep;
        const auto out = remapParamsToSpecs(specs, {num(L"t", 500.0)}, &rep);
        check(paramAsDouble(out, L"t", 0.0) == 20.0, "4 deger ust sinira kelepcelendi");
        check(rep.kept == 1 && rep.clamped == 1, "4 rapor: korundu+kelepcelendi");
    }

    // 5) Kullanici degeri aralik icinde ama varsayilandan farkli -> KELEPCE YOK.
    {
        const std::vector<CompParamSpec> specs{lenSpec(L"t", 12.0, 4.0, 20.0)};
        ComponentParamRemap rep;
        const auto out = remapParamsToSpecs(specs, {num(L"t", 10.0)}, &rep);
        check(paramAsDouble(out, L"t", 0.0) == 10.0 && rep.clamped == 0,
              "5 gecerli kullanici degeri kelepcelenmedi");
    }

    // 6) Bos sema: butun degerler ATILIR (raporda adlariyla).
    {
        const std::vector<CompParamSpec> specs{};
        ComponentParamRemap rep;
        const auto out = remapParamsToSpecs(specs, {num(L"a", 1.0), num(L"b", 2.0)}, &rep);
        check(out.empty(), "6 bos semada sonuc bos");
        check(rep.dropped == 2 && rep.droppedNames.size() == 2, "6 rapor: 2 atildi");
    }

    // 7) Sema ile deger ayni oldugunda parametre SIRASI semaya uyar.
    {
        const std::vector<CompParamSpec> specs{lenSpec(L"a", 1.0, 0.0, 10.0),
                                               lenSpec(L"b", 2.0, 0.0, 10.0)};
        ComponentParamRemap rep;
        const auto out = remapParamsToSpecs(specs, {num(L"b", 5.0), num(L"a", 6.0)}, &rep);
        check(out.size() == 2 && out[0].name == L"a" && out[1].name == L"b",
              "7 sira semaya gore");
        check(paramAsDouble(out, L"a", 0.0) == 6.0 && paramAsDouble(out, L"b", 0.0) == 5.0,
              "7 adlar karismadi (a=6, b=5)");
    }

    std::printf("\n%d/%d kontrol gecti\n", checks - failures, checks);
    return failures == 0 ? 0 : 1;
}
