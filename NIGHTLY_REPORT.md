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
