#pragma once
// P2C — SMART SHEET MANAGER: pafta secim politikasi, tek-pafta kalite kapisi, bagli coklu pafta seti.
//
// KURAL: yeni/paralel bir cizim modeli YOK. Uretilen sey yine `DrawingDocument`tur:
//   * her pafta `DrawingSheet` (P1/P2A semasi), set bilgisi `DrawingDocument::setInfo()`.
//   * imalat olculeri P2A envanterinden gelir; bu modul olcu DEGERI uretmez/degistirmez.
//
// KARAR SIRASI (kabul edilen urun gereksinimi):
//   1. Tercihler: sheetSize=auto, drawingMode=single, autoMultiSheetFallback=true (varsayilan).
//   2. Tek pafta: auto ise A3 -> A2 -> A1 -> A0 (autoFirst..autoMax) yatay kagitlar denenir;
//      sabit boyut secildiyse yalniz o boyut. Kucuk yazi / eksik olcu / cakisma KABUL EDILMEZ.
//   3. Tek pafta kaliteyi saglamiyorsa ve yedek acik ise: ana pafta + numarali devam/detay
//      paftalari (cift yonlu referans, ana paftada pafta indeksi, ortak revizyon/antet).
//   4. Hicbir gecerli duzen yoksa: `layoutValid=false`, acik `issues` (NO_FIT ...), READY DEGIL.
//
// Model (Document) DEGISMEZ: bu modul yalniz DrawingSheet/DrawingDocument yazar.

#include "model_maker/drawing_builder.hpp"

#include <string>
#include <vector>

namespace mm::drawing {

struct DrawingSetResult {
    bool ok{false};              // uretim basarili (kalite icin layoutValid/ready ayri raporlanir)
    std::string error;
    DrawingDocument document;
    DrawingPreferences preferences;
    fab::FabricationInventory inventory;
    bool singleSheet{false};
    bool layoutValid{false};     // tum paftalar yerlesti + bagimsiz olcumde cakisma 0
    bool ready{false};           // layoutValid && imalat verisi tam (TASLAK degil)
    std::vector<std::string> issues;   // set duzeyi cozulmemis sorunlar (makine kodu: detay)
    const DrawingSheet* primary() const noexcept { return document.primarySheet(); }
};

// Tek pafta dener; gerekirse (ve izinliyse) bagli coklu pafta seti uretir. Deterministik.
DrawingSetResult buildDrawingSet(const Document& document, MarkRegistry& registry, EntityId subject,
                                 const DrawingOptions& options = {},
                                 const DrawingPreferences& preferences = {});

// Set butunlugu: pafta kimlikleri, numaralandirma, antet/revizyon tutarliligi, cift yonlu
// referanslar, ana pafta indeksi, her paftanin yerlesimi ve bagimsiz cakisma olcumu.
// Sorun yoksa true ve `issues` bos.
bool validateDrawingSet(const DrawingDocument& document, std::vector<std::string>& issues);

} // namespace mm::drawing
