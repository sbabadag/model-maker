# Generic Component Catalog (C7)

Yeni bir component'i Windows UI'ya Application veya Qt menü/switch KODU yazmadan
eklemek için ortak yol. Katalog bir VIEW'dir (ikinci registry/kayıt deposu DEĞİL).

## Yeni component ekleme adımları (application.cpp / qt_main_window.cpp DEĞİŞMEZ)
1. `ComponentType` tanımla: typeId + displayName + params + `catalog` (meta).
2. `ComponentGenerator` yaz (GenerationPlan üretir; main + secondary[1..]).
3. `ComponentDialogDefinition` tanımla (specs + şema + gruplar).
4. `registerBuiltInComponents` / `registerBuiltInDialogs` kataloglarına ekle.
Katalog menüsü ve generic komut akışı bunları otomatik keşfeder.

## ComponentCatalogMeta alanları
- `sourceComponentNumber` (örn "146"), `category`, `primary/secondaryMemberType`,
  `supportedFeatures`, `coverageStatus`, `pickCount` (varsayılan 2),
  `catalogVisible` (varsayılan FALSE — açıkça opt-in gerekir).
- End Plate `catalogVisible=false`: kendi zengin komut/üretim yolunu korur.

## Generic komut akışı (Application)
- `startComponentCommand(typeId)` / `pickPendingComponentAt` /
  `executePendingComponentCommand` / `cancelPendingComponentCommand` /
  `applyComponentFromParts(typeId, parts, params)`.
- `applyComponentFromParts` = eski applyFinPlateFromParts gövdesinin parametrize
  hâli; fin plate artık ona delege eder (davranış değişmez).
- `pendingComponentType_` (boş değilse etkin) mevcut pendingFinPlate_/
  pendingEndPlate_ İLE PARALEL çalışır; her komut geçişinde çapraz iptal gerekir:
  selectTool, deactivateAllCommands, newDocument, openDocument, startZoomWindow2D,
  startWorkPlaneCommand, startCutCommand, startBoltCommand, startTransformCommand,
  startEndPlateCommand, startFinPlateCommand.

## Tuzak (kanıtlanmış): source-extraction harness stub'ı
`tests/run_two_point_view.py` application.cpp'den `startWorkPlaneCommand` vb.
gövdelerini MARKER ile çıkarıp bir stub Application sınıfına derler. Bu stub yalnızca
listelenen metodları bilir. Çıkarılan bir fonksiyonun İÇİNE yeni bir Application
metod çağrısı eklediğinde (örn `cancelPendingComponentCommand()`), stub listesine
(`void cancelPendingComponentCommand() {}`) aynı metodu EKLEMEZSEN
`model_maker_two_point_view` testi derleme hatası verir. Kural: çıkarılan gövdeye
yeni metod çağrısı eklendiğinde run_two_point_view.py stub listesini de güncelle.

## Doğrulama
- OCC-off: `ctest` (component_catalog_test dahil).
- OCC-on: fin plate e2e testi test başlığındaki tek-satır g++ komutuyla
  (sistem OCCT cmake paketi VTK yüzünden bozuk; cmake ile değil elle derle).
