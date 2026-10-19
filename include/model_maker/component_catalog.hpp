#pragma once

// GENERIC COMPONENT CATALOG (C7).
//
// Amac: yeni bir component Windows UI'ya ayrıca Application veya Qt menü kodu
// yazmadan eklenebilsin. Katalog, ComponentRegistry (tip + generator + catalog
// metadata) ile ComponentDialogRegistry (dialog semasi) kayitlarindan OKUNAN
// bir VIEW'dir — ikinci bir registry, geometry kernel, serializer veya
// materializer DEGILDIR. ComponentType'a bir ComponentCatalogMeta alani eklenir;
// buildComponentCatalog bu alanı + dialog kaydını birleştirip UI'nin tek
// döngüyle menü üretmesi için yassı bir CatalogEntry listesi döndürür.
//
// Yeni component ekleme adımları (application.cpp / qt_main_window.cpp
// DEĞİŞMEZ):
//   1) ComponentType (typeId + displayName + params + catalog meta) tanımla,
//   2) ComponentGenerator (GenerationPlan üreten) yaz,
//   3) ComponentDialogDefinition (specs + şema + gruplar) tanımla,
//   4) registerBuiltInComponents / registerBuiltInDialogs kataloglarına ekle.
// Katalog ve generic komut akışı bunları otomatik keşfeder.
//
// Saf veri modeli (OCCT'siz, Qt'siz, windows.h'siz). Linux'ta derlenir/test
// edilir.

#include "model_maker/component_engine.hpp"
#include "model_maker/component_dialog.hpp"

#include <string>
#include <vector>

namespace mm {

// Katalogun bir satırı: tip metadata + dialog varlığı/bölümleri. Registry'lerden
// derlenir; kalıcı bir kayıt değildir.
struct CatalogEntry {
    std::wstring typeId;
    std::wstring displayName;
    std::wstring sourceComponentNumber;   // "146", "144", ... (bos = yok)
    std::wstring category;
    std::wstring primaryMemberType;       // secim ipucu
    std::wstring secondaryMemberType;     // secim ipucu
    std::vector<std::wstring> supportedFeatures;
    std::wstring coverageStatus;          // DOCUMENTED_ONLY / PARTIALLY_IMPLEMENTED / ...
    int pickCount{2};                     // generic komutun sececegi parca sayisi
    bool catalogVisible{true};
    bool hasGenerator{false};             // registry'de ureteci var mi
    bool hasDialog{false};                // dialog registry'sinde tanim var mi
    std::vector<std::wstring> dialogGroups;  // dialog grup/sekme basliklari
};

// ComponentRegistry + ComponentDialogRegistry kayitlarindan katalogu derler.
// Yalnizca catalogVisible girisler doner; sirala: category (art), sonra
// sourceComponentNumber (lexicographic; bos numara sonda). Deterministik.
std::vector<CatalogEntry> buildComponentCatalog(const ComponentRegistry& reg,
                                                const ComponentDialogRegistry& dialogs) noexcept;

// typeId'li katalog girisini bulur (yoksa nullptr).
const CatalogEntry* findCatalogEntry(const std::vector<CatalogEntry>& catalog,
                                     const std::wstring& typeId) noexcept;

} // namespace mm
