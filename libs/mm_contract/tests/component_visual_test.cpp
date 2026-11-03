// LEVHA GORUNUM KURALI (kalici) — tek kaynak: mm_contract/component_visual.hpp
//
// Levhalar her zaman gridir: RGB(170,170,170) = 0xAAAAAA. Studio bu degeri
// ayni sabitten okur; iki tarafta ayri sabit tutulmaz. Secim/hover vurgusu bu
// rengin ustune cizilir (levha rengini degistirmez), profiller ve bulonlar
// mevcut renklerini korur.
#include "mm_contract/component_visual.hpp"
#include "mm_contract/contract_version.hpp"

#include <cstdio>

static int checks = 0, failures = 0;
static void check(bool ok, const char* what) {
    ++checks;
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
}

int main() {
    check(mm::kPlateGreyRgb == 0xAAAAAAu, "levha grisi 0xAAAAAA");
    check(mm::kPlateGreyR == 170u, "kirmizi kanal 170");
    check(mm::kPlateGreyG == 170u, "yesil kanal 170");
    check(mm::kPlateGreyB == 170u, "mavi kanal 170");
    check(mm::kPlateGreyR == mm::kPlateGreyG && mm::kPlateGreyG == mm::kPlateGreyB,
          "kanallar esit (gercek gri)");
    check(mm::kPlateGreyRgb != 0x808080u,
          "eski 0x808080 sabiti kullanilmiyor (tek deger)");
    // Sabit 1.6.0'dan itibaren public sozlesmede.
    check(mm::kContractVersionMajor == 1 && mm::kContractVersionMinor >= 7,
          "contract >= 1.7.0 (gorsel sabit public)");

    if (failures) {
        std::printf("%d FAIL\n", failures);
        return 1;
    }
    std::printf("ALL PASS\n");
    return 0;
}
