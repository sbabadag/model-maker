# P2D / P2 — Window/Crossing seçim + snap'li grup taşıma + iki-tık çizim

P1'in (`docs/drawing/P2D-paper-space-layout-and-manual-holes.md`) devamı. Kapsam: pafta düzenleme
etkileşimi. **Mühendislik verisi kuralı değişmedi:** ölçü `value/p1/p2` ve görünüş geometrisi
kâğıt düzenlemesiyle yazılmaz.

## 1. Bant seçimi (Window / Crossing)

`src/paper_editing.cpp`, `include/model_maker/paper_editing.hpp` (Qt'siz çekirdek)

- `paperBandMode(down, up, minDragMm)`: **soldan sağa** (dx ≥ 0) → `Window`, **sağdan sola** (dx < 0)
  → `Crossing`, `minDragMm` altındaki hareket → `None` (tık sayılır, kazara seçim yok).
- `selectPaperElements(sheet, band, mode)`:
  - `Window` = elemanın kâğıt sınırları bant içinde **tamamen**;
  - `Crossing` = sınırlar bantla **kesişiyor** (tamamen içinde olan da seçilir).
  - Sıra deterministik: şekil → metin → mark → ölçü, her tür kendi vektör sırasında.
  - **Görünüşler (View) bantla seçilmez:** büyük bir görünüş `Crossing` modunda her seferinde
    seçilir ve yanlışlıkla taşınırdı; görünüş tıklanarak seçilir.
- `paperElementBounds(sheet, hit)`: elemanın kâğıt mm sınırları — ölçü için olçu çizgileri + metin
  kutusu (`Obb::aabb()`), daire için merkez ± yarıçap. Seçim çerçevesi ve bant testleri **aynı**
  fonksiyonu kullanır (ekran ile seçim uyuşmazlığı olmaz).
- `movePaperElements(sheet, selection, delta)`: tüm seçimi tek delta ile taşır; dönen değer taşınan
  eleman sayısıdır (0 → hiçbir şey değişmedi).

## 2. Snap'li taşıma

- `snapPaperPoint(sheet, p, tolMm)`: en yakın aday kazanır (eşitlikte önce eklenen).
  Adaylar: şekil uçları/orta noktası/merkezi ve daire kadranları, görünüş `origin` + köşeleri,
  metin `pos`, mark `labelPos`, çerçeve ve çizim alanı köşeleri/merkezi.
- Tolerans **piksel değil mm**: GUI `6 px × mmPerPixel()` verir; zoom değişince snap yarıçapı
  fiziksel olarak sabit kalır.
- `PaperSnap.what` etiketi durum/geri bildirim için taşınır ("sekil ucu", "gorunus", ...).

## 3. GUI etkileşimi (`src/drawing_sheet_widget.cpp`)

| Araç | Davranış |
|---|---|
| Seç (0) | Elemana tık → seç + sürükle-taşı. Boş alana bas → bant; bırakırken yön modu belirler. |
| Taşı (4) | 1. tık taban (snap'li), 2. tık hedef → **tüm seçim** tek adımda taşınır. Seçim boşsa önce eleman seçer. |
| Çizgi (1) / Daire (2) / Merkez (3) | Tık-tık **veya** sürükle-bırak. 1. tık sonrası lastik-bant önizleme. |

- Araç kutusu indeksleri geriye dönük uyumlu: `1=Cizgi, 2=Daire, 3=Merkez`; `Taşı` **4** olarak
  eklendi (eski testler indeks 1'i çizgi olarak kullanıyor).
- `Esc`: açık çizimi, bandı ve taşıma tabanını iptal eder. **Sürükleme sırasında** Esc
  `PaperEditHistory::cancel()` ile geometriyi snapshot'a döndürür (yarım taşıma kalmaz, yığın kirlenmez).
- `Delete/Backspace`: **tüm seçimi** gizler (`evaluateAnnotationStatus` → gerekli ölçü/not gizlendiyse TASLAK).
- Grup taşıma/geri alma: `PaperEditHistory::begin() → movePaperElements(...) → commit()` = **tek**
  geri-al adımı; `undo()` tüm seçimi birlikte geri alır.
- Önizleme katmanı (`paintEditOverlay`) yalnız **ekran**: seçim çerçeveleri, bant (mavi=Window,
  yeşil=Crossing), lastik-bant, taşıma çizgisi, snap işaretçisi. PDF `paintSheet` kullanmaya devam
  ettiği için **ekran/PDF çıktı paritesi bozulmaz** (önizleme PDF'e sızmaz).

## 4. Testler

| Hedef | Kapsam |
|---|---|
| `model_maker_drawing_p2d_select_test` (Qt'siz) | bant yönü/normalizasyon, eleman sınırları, Window vs Crossing, görünüş hariç tutma, grup taşıma, ölçü değerinin değişmemesi, snap (en yakın aday / tolerans dışı / tol=0), tek undo, `cancel()` |
| `model_maker_drawing_p2d_select_gui_test` (Qt offscreen) | gerçek fare olayları: iki-tık çizim + ilk tıkta önizleme boyası, Esc iptali, Window/Crossing seçim, snap'li iki-tık grup taşıma, tek undo, PDF sayfa sayısı = pafta sayısı, Document revizyonu değişmedi |

`set_tests_properties(... ENVIRONMENT QT_QPA_PLATFORM=offscreen)` daha önce **yanlış hedefe**
(`component_dialog_widget_test`) yazılıydı; artık çizim GUI testlerine uygulanıyor.

## 5. Kalanlar

- Sıkı kâğıt/ölçek yerleşimi (P1'den devam): sabit A3 + uzun parça → `NO_FIT`, otomatik ölçekte
  `DIMENSION_UNPLACEABLE`. P2 bunları çözmez, yalnız dürüst raporlar.
- Windows'ta (ZBook/Asus) GUI kabulü yapılmadı; bu host'ta yalnız çekirdek kaynaklar mingw ile
  sözdizimi kontrolünden geçirilebilir.
- Bant seçimi görünüşleri kapsamıyor (bilinçli); kullanıcı görünüşü tıklayarak taşır.
