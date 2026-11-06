#pragma once

// MM-6b — OTOMATIK ONIZLEME TANIMI (duyarlilik analizi), OCCT'siz/Qt'siz.
//
// Bir komponentin elle yazilmis preview tanimi (dialog.json "preview") YOKSA,
// Length/Angle parametreleri icin plan(p) ve plan(p+δ) uretilip farkindan
// HAREKET EDEN ozellik (levha kenar/yuz/merkez, bulon sira/sutun) ve ekseni
// belirlenir; olcu, hareket etmeyen en yakin anchor'dan hareket edene kurulur.
// Hicbir plan farki uretmeyen parametre "etkisiz" raporlanir (diyalogda gri +
// uyari). Sonuc bir PreviewDefinition'dir; elle tanim ayni param icin EZER.

#include "model_maker/component_engine.hpp"
#include "model_maker/component_preview.hpp"

#include <functional>
#include <string>
#include <vector>

namespace mm {

struct AutoPreviewResult {
    PreviewDefinition definition;          // otomatik uretilen olcu + toggle + etiket tanimi
    std::vector<std::wstring> ineffective; // Length/Angle olup da plan farki URETMEYEN parametreler
    std::vector<std::wstring> ineffectiveReasons;  // ineffective[i] icin Turkce sebep (ayni sira)
    bool ok{true};
    std::wstring error;
};

// Duyarlilik analizi ile otomatik onizleme tanimi uretir. `generate` tipin
// generatoru (registry.generate(typeId, _) ile ayni), `baseInput` tipik bir
// temsili girdi, `specs` tipin parametre semasi. Deterministik: ayni girdi ->
// ayni tanim (Linux == mingw).
//
// δ = 1 mm (Length) / 1 deg (Angle); parametre araligin ust sinirindaysa -δ
// kullanilir. Boolean parametreler plan(true) vs plan(false) farkindaki roleKey
// kumesinden toggle'a cevrilir; Integer parametreler "etiket" (olcu degil) olarak
// isaretlenir; Enum/Material/String fark urettigi parca uzerinde etiketlenir.
AutoPreviewResult autoPreviewDefinition(
    const std::function<GenerationPlan(const ComponentInput&)>& generate,
    const ComponentInput& baseInput,
    const std::vector<CompParamSpec>& specs) noexcept;

// Sonucun onbellek anahtari (MM-6b item 4): typeId + parametre semasi hash'i.
// Ayni anahtar icin ayni tanim gecerlidir (deterministik).
std::string autoPreviewCacheKey(const std::wstring& typeId,
                                const std::vector<CompParamSpec>& specs) noexcept;

}  // namespace mm
