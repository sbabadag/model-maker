# P2D / P1 — Akıllı pafta sığdırma + bağımsız (elle açılmış) delikler

Bu faz, P2 Window/Crossing çoklu seçim ve Move işlerini KAPSAMAZ (ayrı onay).

## 1. Akıllı pafta yöneticisi — belirleyici en-iyi sığdırma

`src/drawing_set.cpp`

- `splitSheet` yerine `packSheets`:
  1. Ana görünüşler (Front/Top/Side) tek paftada; sığmazsa sondan devam paftasına taşınır.
  2. Ana paftaya **sığdığı kadar detay görünüşü eklenir** (tek tek pafta açılmaz).
  3. Kalan görünüşler "önce-sığdır" ile paftalara doldurulur: her paftada sığdığı kadar görünüş,
     böylece sayfa sayısı en az. Aynı levhanın detayları `(levha kimliği, görünüş kimliği)` sırasıyla
     yan yana gruplanır (deterministik).
  4. Hiçbir görünüş sessizce düşmez: sığmayan görünüş kendi paftasında `NO_FIT` olarak raporlanır.
- Kalite kapısı değişmedi: `layoutSheet` + bağımsız `measureOverlaps`, çakışma 0, yazı/ölçü küçültme yok.
- Sabit kâğıt (A2 vb.) seçildiyse sınır aşılmaz; otomatik modda A3→A2→A1→A0 eskalasyonu raporlanır.
- `DrawingSetResult::plan` (yeni): pafta başına numara, kâğıt, ölçek (`1:n`), görünüş sayısı ve **sebep**
  ("kagit buyutuldu A3->A2 (yazi/olcu kucultulmedi)", "4 ek gorunus ayni paftada", "3 gorunus tek paftada (en az sayfa)").
  `statusText(result)` Qt'siz durum satırı üretir (GUI ve testler aynı kaynağı kullanır).

## 2. GUI — seçenek değişiminde pafta düzenlemelerinin korunması

`src/drawing_sheet_widget.cpp`, `src/paper_editing.cpp`

- Kâğıt/ölçek/projeksiyon/görünüş/otomatik kâğıt değişiminde `rebuild()` önce mevcut düzenlemeleri
  toplar (`collectEditedSheets`, üretim tabanı `baseline_` ile karşılaştırma).
- Yeniden üretimden sonra düzenlemeler **kaynak öğe anahtarıyla** uygulanır
  (`mergePaperEdits` → `preservePaperEdits`): görünüş türü+subject+detay levhası, ölçü rolü+featureKey+chain+yön,
  mark hedefi, metin türü+featureKey+ilk satır. Sığdırma bir görünüşü başka paftaya taşısa da ölçü yerleşimi korunur.
- Kullanıcı 2B şekilleri (`PaperShape`) pafta düzenindedir: indeks varsa o paftaya, sayfa sayısı azaldıysa
  ana paftaya eklenir; `ElementId` çakışması yeniden atanır, aynı şekil çiftlenmez.
- Düzenleme sonrası `refreshSheetQuality` çakışmayı yeniden ölçer: kullanıcı düzenlemesi kaliteyi bozarsa
  `USER_EDIT_OVERLAP` + LAYOUT/Unresolved → TASLAK (sahte READY yok).
- **Yıkıcı** değişiklik (konu değişimi: kaynak eşleşmesi yok) açık onay ister
  (`setRebuildConfirmation`, varsayılan `QMessageBox`, testler kararı enjekte eder). Reddedilirse konu eski
  hâline döner ve hiçbir düzenleme kaybolmaz.
- Durum satırı: `statusText` + "pafta duzenlemeleri korundu (N)".

## 3. Bağımsız (elle açılmış) delikler

`src/fabrication.cpp`, `src/drawing_builder.cpp`

- `CutKind::Hole` + `owner == kInvalidEntityId` artık `HOLE_OWNER_UNKNOWN` ile ATILMAZ:
  gerçek delik envantere girer (aynı eksen/derinlik/levha zarfı doğrulamasından geçer).
- Kimlik: `group:<plate>.<anonId>.<dia10>` — `anonId` = üst bit işaretli, levha+çap+yuva imzasından
  FNV-1a ile türetilen **kararlı** kimlik; gerçek bulon grubu kimlikleriyle çakışırsa deterministik sondalama.
- Ölçülendirme:
  - düzgün dizilim (bulon sahipli veya elle açılmış ızgara): çap-adet callout + adım + net kenar mesafesi (değişmedi),
  - **düzensiz** dizilim: sahte adım üretilmez; her delik için `HoleOrdinate` — levha min-u/min-v kenarından
    **gerçek merkez koordinatı** (u ve v), `measured() == value`.
  - `HOLE_PATTERN_IRREGULAR` artık TASLAK nedeni değil (konumlar gerçekten ölçülüyor); çerçeve yok / eksen eğik /
    kısmi derinlik / yuva konturu yok → UNRESOLVED + TASLAK.
- Bulon sahipli grupların davranışı ve mevcut testleri değişmedi.

## 4. Testler

| Hedef | Kapsam |
|---|---|
| `model_maker_drawing_p2d_sheet_test` | sabit A2 sınırı, A3→A2 / A3→A1 eskalasyonu, detayların aynı paftada toplanması, en az sayfa, görünüş/ölçü kaybı yok, plan+`statusText`, determinizm, JSON round-trip |
| `model_maker_p2d_holes_test` | tek/elle delik, düzensiz dizilim (ordinat), karışık bulon+elle, döndürülmüş levha, kimlik kararlılığı/çakışmazlık, çerçevesiz/eğik/kısmi derinlik → TASLAK, delik kaybı yok |
| `model_maker_drawing_p2d_gui_test` | Qt offscreen: boyut değişiminde yeniden üretim + düzenleme korunumu, yıkıcı değişiklikte onay (ret/kabul), durum satırı, PDF sayfa sayısı + metin determinizmi, JSON determinizmi, Document değişmezliği |

Komutlar (Linux):

```
cmake -S . -B build-hermes -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build-hermes -j2
ctest --test-dir build-hermes --output-on-failure
```
