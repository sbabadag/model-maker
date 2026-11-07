// UI-3 OZELLIK PANELI — Ad / Yuzey islemi / Malzeme undo-redo (OCC'siz core).
//
// Document katmanini dogrular (Application secim akisindan bagimsiz):
//  * setModelName / setModelFinish / setModelMaterial: PropertyReplace kaydi,
//    undo/redo ile onceki deger TAM olarak geri gelir (diger alanlar bozulmaz).
//  * TEK pushSnapshot + uc setter = TEK undo adimi (UYGULA batch yolu).
//  * Ayni deger yeniden atanirsa kayit OLUSMAZ (skip-if-same); bos kayitlar
//    undo siramasinda sessizce atlanir.
//
// windows.h yok; Linux'ta kosar.

#include "model_maker/document.hpp"

#include <cstdio>
#include <string>

using namespace mm;

static int g_checks = 0;
static int g_failures = 0;
static void check(bool ok, const char* what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("  FAIL %s\n", what);
    }
}

int main() {
    Document doc;

    // Taban: iki cizgi (coklu secim senaryosu icin).
    doc.pushSnapshot();
    doc.addLine({0, 0, 0}, {100, 0, 0});
    doc.addLine({0, 50, 0}, {100, 50, 0});
    check(doc.models().size() == 2, "iki cizgi eklendi");

    // --- 1) Ad: undo/redo ---
    doc.pushSnapshot();
    check(doc.setModelName({0}, "K1") == 1, "ad atandi (K1)");
    check(doc.models()[0].properties().name == "K1", "ad K1 okunuyor");
    check(doc.undo(), "undo ad");
    check(doc.models()[0].properties().name.empty(), "undo sonrasi ad bos");
    check(doc.redo(), "redo ad");
    check(doc.models()[0].properties().name == "K1", "redo sonrasi ad K1");
    check(doc.undo(), "undo (temizlik)");
    check(doc.models()[0].properties().name.empty(), "ad tekrar bos");

    // --- 2) Yuzey islemi: undo/redo ---
    doc.pushSnapshot();
    check(doc.setModelFinish({1}, "Galvaniz") == 1, "yuzey atandi (Galvaniz)");
    check(doc.models()[1].properties().finish == "Galvaniz", "yuzey Galvaniz okunuyor");
    check(doc.undo(), "undo yuzey");
    check(doc.models()[1].properties().finish.empty(), "undo sonrasi yuzey bos");
    check(doc.redo(), "redo yuzey");
    check(doc.models()[1].properties().finish == "Galvaniz", "redo sonrasi yuzey Galvaniz");
    check(doc.undo(), "undo (temizlik)");
    check(doc.models()[1].properties().finish.empty(), "yuzey tekrar bos");

    // --- 3) Malzeme: undo/redo ---
    doc.pushSnapshot();
    check(doc.setModelMaterial({0}, "S235JR") == 1, "malzeme atandi (S235JR)");
    check(doc.models()[0].properties().material == "S235JR", "malzeme S235JR okunuyor");
    check(doc.undo(), "undo malzeme");
    check(doc.models()[0].properties().material.empty(), "undo sonrasi malzeme bos");
    check(doc.redo(), "redo malzeme");
    check(doc.models()[0].properties().material == "S235JR", "redo sonrasi malzeme S235JR");
    check(doc.undo(), "undo (temizlik)");

    // --- 4) TEK kayit = TEK undo (UYGULA batch yolu) ---
    doc.pushSnapshot();  // TEK pushSnapshot — Application::applySelectedEntityBatch deseni
    check(doc.setModelName({0, 1}, "KIRIS") == 2, "iki modele ad atandi");
    check(doc.setModelFinish({0, 1}, "Boya") == 2, "iki modele yuzey atandi");
    check(doc.setModelMaterial({0, 1}, "S275JR") == 2, "iki modele malzeme atandi");
    check(doc.models()[0].properties().name == "KIRIS" &&
              doc.models()[1].properties().name == "KIRIS",
          "batch: ad iki modelde");
    check(doc.undo(), "batch: TEK undo");
    check(doc.models()[0].properties().name.empty() &&
              doc.models()[0].properties().finish.empty() &&
              doc.models()[0].properties().material.empty(),
          "batch: model 0 tamamen geri alindi");
    check(doc.models()[1].properties().name.empty() &&
              doc.models()[1].properties().finish.empty() &&
              doc.models()[1].properties().material.empty(),
          "batch: model 1 tamamen geri alindi");
    check(doc.redo(), "batch: redo");
    check(doc.models()[0].properties().name == "KIRIS" &&
              doc.models()[0].properties().finish == "Boya" &&
              doc.models()[0].properties().material == "S275JR",
          "batch: redo model 0 tam geri geldi");
    check(doc.undo(), "batch: undo (temizlik)");

    // --- 5) Ayni deger = kayit YOK (skip-if-same) ---
    doc.pushSnapshot();
    check(doc.setModelName({0}, "X") == 1, "ad X atandi");
    check(doc.setModelName({0}, "X") == 0, "ayni ad tekrar: degisiklik yok");
    check(doc.setModelFinish({0}, "") == 0, "ayni yuzey tekrar: degisiklik yok");
    check(doc.models()[0].properties().name == "X", "ad X duruyor");
    check(doc.undo(), "undo X");
    check(doc.models()[0].properties().name.empty(), "X geri alindi");
    check(!doc.undo() || doc.models().empty(),
          "bos kayit siraya girmemis / yalnizca taban kaydi kaldi");

    std::printf("%d/%d kontrol gecti\n", g_checks - g_failures, g_checks);
    return g_failures == 0 ? 0 : 1;
}
