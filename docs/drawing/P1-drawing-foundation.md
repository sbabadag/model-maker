# P1 — Drawing Foundation, Numbering, Minimal View-to-Sheet Pipeline

Durum: ilk calisan cekirdek (OCCT'siz, Qt'siz, Linux ctest'te kosar). GUI yok.

## Katmanlar

| Dosya | Rol |
|---|---|
| `include/model_maker/marking.hpp`, `src/marking.cpp` | Part/Assembly **mark** (P1.., A1..). EntityId'den ayri, imza ile gruplama, JSON kalicilik |
| `include/model_maker/drawing.hpp`, `src/drawing.cpp` | `mm::drawing` veri modeli: DrawingDocument / DrawingSheet / DrawingView / MarkAnnotation / Dimension2D / TitleBlock + JSON |
| `include/model_maker/drawing_builder.hpp`, `src/drawing_builder.cpp` | Document -> mark -> Front/Top/Side -> A3 yerlesim -> etiket + olcu |
| `include/model_maker/drawing_sheet_widget.hpp`, `src/drawing_sheet_widget.cpp` | GUI: `paintSheet` (QPainter; ekran/PDF/PNG tek yol), `DrawingSheetWidget` (zoom/pan), `DrawingPreviewDialog`, `exportSheetPdf`, `showDrawingDialog` |
| `include/model_maker/utf8.hpp` | wstring<->UTF-8 (component_presets, marking ortak) |

## GUI kullanimi

`Cizim > Paper Cizim (Secimden)...` — secili parca (ya da assembly) icin diyalog acar. Konu (Parca / bagli oldugu Assembly), kagit (A4..A0), projeksiyon (3./1. aci), On/Ust/Yan secimi, gizli cizgi (deneysel); her degisimde cizim yeniden uretilir. Tekerlek = yakinlastir, surukle = kaydir, cift tik / `Sigdir` = sigdir. `PDF'e aktar` ve `JSON kaydet` dugmeleri. Mark numaralari diyalog her acildiginda model sirasindan deterministik uretilir (kalici saklama MMW8 entegrasyonuyla yapilacak).

## Sozlesmeler

- **Mark != EntityId.** Mark bir *imzaya* kalici baglidir; imza = profil/malzeme/ad/yuzey islemi/uzunluk + sirali kenar-uzunluk parmak izi (donusumden bagimsiz). Ayni imza ayni numara; numara baska imzaya hic verilmez. Assembly imzasi = ana parca mark + uye mark coklu kumesi + uyeler arasi mesafe dagilimi.
- **Iki uzay.** View-yerel koordinat (model mm, olceksiz, min=(0,0)) geometriye bagli her seyi tutar (segment, olcu noktasi, mark ankraji). Kagit koordinati = `origin + scale * yerel`. Modelin dunya konumu yalniz `projectionOffset`'te kalir; modeli otelemek kagit uzayi sonucunu degistirmez (test B).
- **Projeksiyon** `projectToView`: Front (x,z), Top (x,y), Side (y,z); Z her elevation'da yukari. `Camera::standardViewRotation` kullanilmadi (bkz. asagi).
- **Yerlesim.** Standart olcek listesi (1:1..1:1000); A3 cizim alani = cerceve - title block seridi - padding. 3. aci (varsayilan) veya 1. aci. Sigmazsa standart olmayan olcek (`scaleIsStandard=false`).
- **Delikler** (`CutKind::Hole`): ekseni gorunuse dik -> daire + eksen isareti + "N x Ø22" etiketi; parca basina **delik zinciri** (kenar-delik-delik-kenar, merkezler arasi, yatay + dusey). Ekseni gorunus duzleminde olan delikler yalniz hiddenLines acikken kesik cizgi. Sinirli derinlik/yuvali delik dikkate alinmaz (tam boy, yuvarlak).
- **Assembly uye konum zinciri** (`PartPosition`): uyelerin kenar konumlari (or. plaka-kiris-plaka 12 / 3976 / 12). Zincirler genel olculerden **once** (govdeye yakin satir) dizilir; kagitta dar kalan olcu metinleri kademelenir (`textLevel`).
- **Cizgi agirliklari** (`lineWeightMm`, PDF'te gercek mm): gorunur 0.18, gizli/olcu 0.10, cerceve 0.35.
- **Olculer** gercek geometriden: `MemberLength` = ana parcanin properties ekseni (gorunus duzleminde ise), `OverallWidth/Height` = gorunusun sinir kutusu. `Dimension2D::measured()` noktalardan yeniden olcer; testler `value == measured()` ve modelden bagimsiz hesapla karsilastirir.

## Genisleme noktalari

`DrawingKind` (tek parca / montaj), `ViewKind` (Section/Detail/Iso eklenir), `LineStyle` (hidden-line iyilesir), `DimensionRole` (bolt/weld/BOM), `TitleBlock::revisions`, JSON `version`.

## Bilinen sinirlar

- Hidden line **deneysel**: yalniz arka-yuz testi, oklüzyon yok; I profilde hatali olabilir. Varsayilan kapali.
- Girdi WireframeModel kenarlaridir (OCC B-Rep'ten HLR yok). Bolt/weld/BOM/section/detail yok.
- Drawable = nokta/konstruksiyon olmayan, kenarli model; bolt gibi gorunum modelleri de parca sayilir (ayirt etme kurali henuz yok).
- Uye boyu yalniz properties'te eksen varsa ve eksen gorunus duzlemindeyse olculur.
- Document'a kaydetme (MMW8) ve GUI (Paper Space penceresi, PDF/DXF cikti) yok; `DrawingDocument::serialize()` ayri JSON verir.
- **Camera bulgusu:** `Camera::setView(Front)` +Z'yi ekranda *asagi*, `Right` +Z'yi *yatay* gosteriyor (`Camera::project` ile olculdu). Eski `orthoProject` (assembly.hpp) ayni rotasyondan beslendigi icin elevation cizimi icin kullanilamaz. 3B kameraya dokunulmadi; kasitli mi, hata mi ayrica bakilmali.
