# Model-Maker — Gece Çalışması Raporu (Nightly)

**Tarih:** 13 Ağustos 2026
**Branch:** `hermes-nightly`
**Kapsam:** CAD viewport performansını AutoCAD-benzeri akıcılığa yaklaştırma (pan/zoom/çok-nesne/selection)

---

## 1. Başlangıç Durumu

- Repo `hermes-nightly` branch'indeydi, `origin/hermes-nightly` ile güncel.
- Çalışma ağacında yalnızca `build-debug/` artefaktları modifiye idi (kaynak temiz).
- Mevcut mimari büyük ölçüde iyiydi:
  - **Çift tampon (back buffer)** → pan flicker'i zaten giderilmiş.
  - **Raster zoom preview** (`StretchBlt`, >5000 obje) → düşük latency zoom zaten mevcut.
  - **BVH uzaysal indeks** (`Document::query2D/queryBounds`) → culling + snap + selection.
  - **FrameIndexStampSet** → O(1) seçim üyeliği (linear taramaya göre ~1170x).
  - **interactiveNavigation** → pan/rotate sırasında stride + tile coverage ile iş sınırlama.

## 2. Bulunan Darboğazlar

| # | Darboğaz | Etki | Yapıldı mı |
|---|---|---|---|
| 1 | `Camera::viewTransform/project` her vertex'te 6× sin/cos | render + culling + snap/selection | ✅ Düzeltildi |
| 2 | `Document::effectiveProperties` her entity'de `unordered_map` lookup + `EntityProperties` kopyası | render döngüsü (çok obje) | ✅ Düzeltildi |
| 3 | `drawModel` ortak vertex'leri tekrar projeksiyonluyor | düşük (çizgi ağırlıklı çizimlerde) | ⏭️ Ertelendi (Windows doğrulaması gerekir) |
| 4 | `entityPen` std::string key + map lookup (renderer.cpp, Windows) | orta | ⏭️ Ertelendi (Windows doğrulaması gerekir) |
| 5 | Kısmi redraw (dirty region) yok | yüksek ama riskli | ⏭️ Ertelendi (büyük refactor) |

## 3. Yapılan Değişiklikler

### 3.1 Kamera trig önbelleği — `camera.hpp`, `camera.cpp`
`Camera::viewTransform` her çağrıda `cos/sin(yaw/pitch/roll)` (6 trig) hesaplıyordu. Bu,
3B'de render (vertex başına), culling (`projectedBoundsIntersectsViewport` — bounds başına
8 köşe) ve snap/selection yollarının tamamında en sıcak koddu. Çözüm: 6 trig değeri
`mutable` alanda tembelce önbelleğe alındı; `rotate/setView/reset` çağrılarında invalidate
edildi. Çıktı **bit-identical** (ayrı doğrulama testi ile kanıtlandı).

### 3.2 Çözümlenmiş özellik (effective properties) önbelleği — `document.hpp/cpp`, `renderer.cpp`
Render döngüsü her görünür entity için `effectiveProperties` çağırıyordu; bu her seferinde
katman `unordered_map` lookup'ı + 5×std::string içeren `EntityProperties` kopyası yapıyordu.
Çözüm: model başına tembel önbellek (`effectiveCache_`), uzaysal indeksle aynı `invalidateDerivedState`
yolundan ve katman/özellik mutasyonlarında (`setLayerProperties`, `createLayer`, `deleteLayer`,
`renameLayer`, `setModelLayer/Color/Profile/LineType`) invalidate ediliyor. Renderer artık
`effectiveProperties(index)` ile `const&` referans okuyor (kopya + lookup yok).

> ⚠️ Burada bir tuzak yakalandı: invalidasyon önce döngü başına konmuştu ama döngü içindeki
> `modelIsEditable` çağrısı cache'i yeniden kurup dirty bayrağını tüketiyordu. İnvalidasyon
> mutasyon döngüsünün **sonrasına** taşındı; 12 kontrollü doğrulama testi eklendi.

## 4. Commit SHA'ları (yerel — push henüz yapılamadı, bkz. §7)

| SHA | Açıklama |
|---|---|
| `c0efedf` | bench: add isolated projection micro-benchmark |
| `8c01749` | perf: cache camera view-rotation trig (avoid 6 sin/cos per vertex) |
| `9b7b6cb` | perf: cache resolved entity properties per model in Document |
| `8bb1105` | bench: add cached effective-properties scan metric |

## 5. Benchmark Öncesi / Sonrası (Linux, g++ 14.2, -O2, 100k obje)

| Metrik | Önce | Sonra | Kazanım |
|---|---|---|---|
| `projection_1m_ms` (1M vertex projeksiyonu) | 29.33 ms | **6.84 ms** | **~4.3x** |
| `projected_3d_query_ms` (3B culling) | 0.34 ms | **0.10 ms** | **~3.4x** |
| `effectiveProperties` (100k tarama) | ~2.97 ms | **~0.20–0.33 ms** | **~9–15x** |
| Diğer metrikler (snap, selection, bounds cache) | — | değişmedi | regresyon yok |

> Not: `linear_selection_ms` (~196 ms) kasten ölçülen bir anti-pattern'dir; gerçek kod
> `stamped_selection_ms` (~0.17 ms) yolunu kullanır — bu zaten optimize idi.

## 6. Test Sonuçları

- **`model_maker_render_prep_benchmark`**: ✅ çalışıyor, exit 0 (Linux, platform-bağımsız çekirdek + benchmark).
- **`model_maker_tests` (`test_core.cpp`)**: ❌ Linux'ta derlenemiyor — `renderer.hpp` → `windows.h` bağımlılığı.
  → Yerine platform-bağımsız doğrulama probları yazıldı ve geçti:
  - Kamera: 38 kontrol (Front/Top/Left/Back/reset/rotate/iso + project↔unproject round-trip) — **tümü bit-identical PASS**.
  - Effective-properties önbelleği: 12 kontrol (katman yayılımı, invalidation, index kayması, rename/freeze) — **PASS**.

## 7. Bloke Edici Sorun — GitHub Push Yapılamıyor

`git push origin hermes-nightly` **kimlik bilgisi olmadığı için başarısız**:
```
fatal: could not read Username for 'https://github.com': No such device or address
```
- `gh` CLI yok, `GITHUB_TOKEN` yok, `.netrc`/`.git-credentials` yok, credential.helper yok.
- `~/.ssh/id_ed25519.pub` mevcut ama GitHub'da yetkili değil (`Permission denied (publickey)`).
- **Gerekli:** Kullanıcının ya (a) GitHub Personal Access Token (`repo` + `workflow`) sağlaması,
  ya da (b) `~/.ssh/id_ed25519.pub` anahtarını GitHub hesabına eklemesi gerekir.
- Tüm commit'ler **yerelde** hazır ve push'a hazırdır; kimlik sağlandığında tek komutla gönderilir.

## 8. Windows Üzerinde Ayrıca Doğrulanması Gereken Noktalar

1. **Tam build**: `cmake --build build` + `ctest` (MSVC/MinGW) — Linux'ta yalnızca çekirdek derlendi.
2. **Görsel doğruluk**: katman rengi/çizgi tipi/lineweight çözümü değişmedi, ancak görsel smoke
   testlerin (scripts/) Windows'ta koşulması önerilir.
3. **`renderer.cpp` iki satırı** (`const auto&` + index overload) derlenmeli — basit ve başlıkta
   bildirilen overload'a dayanıyor, düşük risk.
4. **Pan/zoom akıcılığı**: `F11` performans overlay ile frame süresi ölçümü yapılmalı.

## 9. Sonraki Öneriler

1. **Render batching (GDI)**: aynı pen/style'a sahip ardışık entity'leri tek `Polyline`/`PolyPolyline`
   çağrısında birleştir — 100k entity için GDI çağrı sayısını büyük ölçüde azaltır. **Windows doğrulaması şart.**
2. **`entityPen` anahtar optimizasyonu**: `std::map` + std::string key yerine hafif bir key (intern/pointer).
   **Windows doğrulaması şart.**
3. **Kısmi redraw (dirty region)**: yalnızca değişen viewport bölgesini yeniden çiz — en yüksek getirili ama
   en riskli refactor; ayrı bir çalışma olarak ele alınmalı.
4. **`query2D/queryBounds` sonuç sıralaması**: tüketiciler sıraya bağımlı değilse kaldırılabilir (O(k log k)
   tasarruf); görsel/sıra doğruluğu dikkatle doğrulanmalı.
5. **CI**: Linux'ta çekirdek + benchmark'ı derleyip koşan bir GitHub Actions işi, bu tür regresyonları
   erkenden yakalar.

---

## Task 2 — Spatial Query / Selection Optimization

**Kapsam:** 10.000–250.000 CAD objesi ölçeğinde selection/picking, snap candidate search,
viewport culling ve BVH/spatial query performansı.

### Başlangıç Mimarisi

- `Document::query2D` / `queryBounds`: medyan-bölmeli BVH (`SpatialNode`) üzerinden lazy build,
  sonuç **her zaman artan index sırasına göre sıralı** döndürülüyor.
- `hitTestModel2D` / `selectModelsInRect2D` / `SnapEngine::snap`: zaten `query2D` uzaysal filtresi kullanıyor (hızlı).
- **`hitTestModel3D` ve `selectModelsInRect3D` uzaysal filtre KULLANMIYORDU** — her çağrıda
  tüm modelleri tarıyordu (`allIndices = 0..N-1`). 3B hover/click (`toggleModelSelection`,
  `trimExtendTargetAt`) her fare hareketinde bu tam taramayı çalıştırıyordu.

### Bulunan Darboğazlar

| Darboğaz | Etki |
|---|---|
| `hitTestModel3D` tam tarama (her hover/click) | 250k objede ~9.2 ms/çağrı — O(N) |
| `selectModelsInRect3D` tam tarama | 250k objede ~4.6–5.3 ms/pencere seçimi |
| 3B prefilter'da 8 köşe projeksiyonlu bounds testi | ilk denemede orta boy dikdörtgenlerde tam taramadan yavaş |
| `query2D/queryBounds` sıralaması | **reddedildi** (kanıtla) — bkz. aşağıda |

### Yapılan Optimizasyonlar

1. **3B picking uzaysal prefilter** (`hitTestModel3D`): imlece `tolerance` piksel içinde
   olabilecek modeller `projectedCandidates` (BVH sorgusu) ile ön filtreleniyor; adaylar
   artan sırada kaldığı için `<=` tie-break davranışı birebir korunuyor.
2. **3B pencere seçimi adaptif prefilter** (`selectModelsInRect3D`): dikdörtgen alanı viewport'un
   <%6'sı ise BVH prefilter, değilse doğrudan tarama. Her iki yol da **birebir aynı sonuç vektörünü**
   üretiyor (prefilter muhafazakâr süper küme).
3. **Ucuz bounds reddi — bounding-sphere testi**: dünya AABB'sinin ekran-uzayı çemberi
   (merkez + yarım köşegen × `pixelsPerUnit × zoom`) **tek projeksiyonla** test ediliyor
   (önceki 8 köşe yerine). Kesin ölçek için `Camera::pixelsPerUnit()` getter'ı eklendi.
   Aynı test `projectedCandidates` (snap3D prefilter'ı) için de kullanılıyor.

### Değiştirilmiş Dosyalar

- `include/model_maker/camera.hpp` — `pixelsPerUnit()` getter
- `src/camera.cpp` — getter uygulaması
- `src/drafting.cpp` — sphere yardımcıları, `projectedCandidates`, `hitTestModel3D`,
  `selectModelsInRect3D` (adaptif)
- `tests/benchmark_spatial_query.cpp` — **yeni** çok ölçekli benchmark
- `CMakeLists.txt` — yeni benchmark hedefi

### Commit SHA'ları

| SHA | Açıklama |
|---|---|
| `0279185` | bench: add multi-scale spatial query benchmark (10k-250k) |
| `637d203` | perf: spatial-prefilter 3D picking and window selection |

### Benchmark Önce → Sonra (Linux, g++ 14.2, -O2, izometrik fit kamera)

**3B nokta picking** (`hitTestModel3D`, ızgara merkezi, 10px tolerans):

| Ölçek | Önce (tam tarama) | Sonra (prefilter) | Hızlanma |
|---|---|---|---|
| 10k | 0.324 ms | 0.012 ms | **27x** (%96 azalma) |
| 50k | 1.615 ms | 0.022 ms | **73x** (%98.6) |
| 100k | 3.369 ms | 0.027 ms | **125x** (%99.2) |
| 250k | 9.227 ms | 0.035 ms | **264x** (%99.6) |

**3B pencere seçimi, küçük dikdörtgen (%1.4 viewport alanı):**

| Ölçek | Sonra (prefilter) | Eşdeğer tam tarama | Hızlanma |
|---|---|---|---|
| 10k | 0.057 ms | 0.201 ms | ~3.5x |
| 50k | 0.322 ms | 0.820 ms | ~2.5x |
| 100k | 0.664 ms | 1.687 ms | ~2.5x |
| 250k | 1.099 ms | 4.604 ms | **~4.2x** |

**Büyük dikdörtgen (%16 alan, doğrudan tarama yolu):** 0.20 / 0.82 / 1.69 / 4.60 ms — **regresyon yok**
(aynı kod yolu korundu).

**Diğer:** `snap3d` 0.07–0.26 ms (tüm ölçekler), `pick2d`/`snap2d` ~0.00 ms (zaten uzaysal), `cull3d`
0.01–0.26 ms (Task 1 trig cache faydası korunuyor).

### Correctness Testleri

- **Eşdeğerlik probu (Linux, geçti):** rastgele 400 dikdörtgen (window+crossing × 4 boyut sınıfı)
  `selectModelsInRect3D` sonuç vektörü referans tam taramayla **birebir aynı**; rastgele 400 nokta
  `hitTestModel3D` referansla aynı (üst üste/çakışık objeler dahil).
- **Snap:** endpoint / midpoint / kesişim tipi ve konumu doğru.
- **Katman görünürlüğü:** gizli katmandaki entity pick edilemiyor, crossing seçime girmiyor.
- **Viewport sınırı:** kenar noktalarında pick referansla aynı.
- **Regresyon:** Task 1 benchmark'ı değişmedi (exit 0; projection_1m=6.7ms, cull3d=0.10ms).

### Başarısız / Reddedilen Optimizasyon Fikirleri

1. **`query2D/queryBounds` sıralamasını kaldırmak — REDDEDİLDİ (kanıtlı).** Sıralama davranışsal
   olarak gerekli: (a) renderer'ın sonuç-görünümü pasosu `visibleModels[vi]` ile `nodeDisp[vi]`'yi
   konumsal eşleştiriyor — sıra değişirse Windows'ta deformasyon eşleşmesi bozulur; (b) pick'teki
   `<=` tie-break ve snap'teki `min_element` ilk-minimum, çakışık/eşit-uzaklıktaki objelerde
   iterasyon sırasına duyarlı (davranış değişir); (c) pencere seçimi sonuç sırası `selectedModels_`
   sırasını belirliyor. → Sıralama tüm tüketicilerde gerekli, kaldırılmadı.
2. **8 köşe projeksiyonlu AABB prefilter — DEĞİŞTİRİLDİ.** Orta boy dikdörtgenlerde (28% alan,
   250k) 19 ms ile tam taramadan (5.3 ms) yavaş kaldı; yerine tek projeksiyonlu sphere testi geldi.
3. **Sphere ölçeğini eksen ölçümüyle bulmak — BAŞARISIZ OLDU (false negative).** İzometrik görünümde
   dünya X ekseni ekranda ~0.816x kısalıyor; `project(1,0,0)` ile ölçülen ölçek gerçek maksimum
   germeyi (pixelsPerUnit×zoom) küçümsüyor → prefilter 2 testte model kaçırdı (eşdeğerlik probu
   yakaladı). Kesin ölçek `Camera::pixelsPerUnit()` ile alınarak düzeltildi; prob sonrası tam geçti.
4. **Büyük BVH refactor** — talimat gereği yapılmadı.
5. **`queryBounds`'ta `std::function`'ı şablona çevirmek** — ertelendi (ölçülen pay küçük, API'yi
   header'a taşımayı gerektirir).

### Windows'ta Yapılması Gereken Doğrulamalar

1. Tam build + `ctest` (MSVC/MinGW) — Linux'ta yalnızca platform-bağımsız çekirdek derlendi.
2. 3B hover/click akıcılığı (büyük modelde) — `F11` overlay ile doğrulanmalı.
3. Trim/Extend hedef seçimi (`trimExtendTargetAt`) artık prefilter kullanıyor — davranış birebir
   aynı olmalı; elle smoke test önerilir.
4. `renderer.cpp` Task 2'de değiştirilmedi.

### Sonraki En Yüksek Getirili Optimizasyon

1. **GDI render batching** (aynı pen/style'a sahip ardışık entity'leri tek Polyline/PolyPolyline'da
   birleştirmek) — bu görevde hariç tutuldu; 10k+ görünür entity'de kalan en büyük kazanç.
2. Renderer'da `entityPen` anahtarını hafifletmek (std::string → intern/pointer).
3. Dirty-region (kısmi) redraw — en yüksek getirili ama en riskli refactor.


---

## Qt Merge — main'deki Qt6 Değişikliklerinin hermes-nightly'ye Uygulanması

**Merge commit:** `29c9374` (parents: `cc2927c` + `8f4a232`)
**Kaynak:** `main` üzerindeki `8f4a232` "feat: Qt6 ribbon UI integration + layer manager crash fixes"

### Ne Getirildi

- Qt6 ribbon UI entegrasyonu (application/renderer/main + CMakeLists: AUTOMOC/AUTORCC/AUTOUIC,
  Qt6::Widgets/Core/Gui, `qt_main_window`, OpenGL render backend referansı, GPU hattı
  (`useGpuLines`), `draft.snapOnly` hızlı yolu, GL metrikleri)
- Layer manager çökme düzeltmeleri (document/dxf/ribbon_layer değişiklikleri)

### Çakışma Çözümleri

- **Perf tarafı korundu** (kendi değişikliklerimin olduğu dosyalar): camera trig cache +
  `pixelsPerUnit()` getter; document effective-properties cache; drafting sphere-prefilter
  (adaptif 3B pick/pencere seçimi); renderer'da `effectiveProperties(index)` satırları
  Qt sürümüne yeniden uygulandı.
- **Qt tarafı korundu** (dokunmadığım dosyalar): application.hpp/cpp, renderer.hpp, dxf.cpp,
  main.cpp, ribbon_layout.hpp/cpp.
- **Checkpoint (3032c96) + Qt commit (8f4a232) örtüşmesinden doğan mükerrer tanımlar**
  (`setModelLayer/Color/Profile/LineType`, `Camera::setOrbitCenter`) temizlendi — cache
  invalidation'lı sürümler korundu.
- **CMakeLists adaptasyonu:** `find_package(Qt6 QUIET ...)` + eksik-kaynak koruması eklendi;
  Qt bulunamazsa `model-maker` exe hedefi atlanıyor, çekirdek + testler + benchmark'lar
  her zaman derleniyor.

### ⚠️ Qt Commit'i Eksik Dosyalar İçeriyordu — ÇÖZÜLDÜ

`8f4a232` commit'i `qt_main_window.cpp/hpp` ve `opengl_render_backend.cpp/hpp`
dosyalarını referans ediyor ama commit içermiyordu. Bu dosyalar Windows'tan
`afba550` ("feat: add render backend abstraction, Qt main window and performance
HUD sources") ile gönderildi ve `314c80b` merge'iyle hermes-nightly'ye alındı.
Qt6 bulunan bir Windows ortamında exe hedefi artık derlenebilir (CMake koruması:
Qt veya kaynaklar yoksa exe atlanır, çekirdek/test/benchmark etkilenmez).

### Doğrulama (Linux)

- Çekirdek (`model_maker_core`) + her iki benchmark derlendi, exit 0.
- Task 1 benchmark: değişmedi (projection_1m=6.7ms, cull3d=0.10ms).
- Task 2 benchmark: değişmedi (pick3d 264x kazanç korunuyor, sonuçlar referansla birebir aynı).
- 3 correctness probu (kamera bit-exact, effective-cache, 800 karşılaştırmalı eşdeğerlik) — ALL PASS.

### Windows'ta Yapılması Gereken Doğrulamalar

1. Eksik 4 Qt/OpenGL dosyasını commit etmek (yukarıdaki liste).
2. `cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release` (Qt6 6.9.3 mingw_64 ile) + `ctest`.
3. Qt ribbon UI, layer manager ve OpenGL render backend smoke testleri (scripts/).
4. GDI + OpenGL yollarının görsel karşılaştırması (F11 overlay).


---

## Trim Düzeltmeleri (UI + Circle Desteği)

- **Qt menü/ribbon komut girişleri artık ekranı tazeliyor** (`98ccdcb`): `startTransformCommand`,
  `deactivateAllCommands`, `toggle3DView`, `zoomExtents2D`, `startZoomWindow2D` metodları artık
  kendiliğinden `updateHover + updateControls + updateStatus + invalidateCanvas` çağırıyor.
  GDI araç çubuğu ve klavye yolları bu tazelemeyi zaten yapıyordu; Qt ribbon/menü doğrudan
  çağırıp atlıyordu → komut anında ekran eski karede kalıyordu.
- **Circle/rectangle/polyline trim desteği** (`66310ef`): `trimLine2D` tek segmentli çizgi
  dışındaki her modeli reddediyordu. Artık tüm eğri `kenarİndeksi+yerelT` ile parametrize
  ediliyor; açık eğriler ve kapalı döngüler (wrap yayı dahil) doğru trim ediliyor.
  Tek segmentli davranış bit-identical (300 rastgele vaka referansla doğrulandı).
- **Başarısız trim tıklamasına uyarı sesi** (`2d6a44f`): kesişim yoksa/basarısızsa sessiz
  no-op yerine bip.
- Doğrulama: 654 kontrollü trim probu + 28 kontrollü uygulama-akış probu (hitTest → sınır
  kopyası → applyTrimExtendTarget → replaceModel; circle-circle ve 3D dahil) — hepsi PASS.
  Kullanıcı Windows'ta görsel olarak doğruladı. ✓
