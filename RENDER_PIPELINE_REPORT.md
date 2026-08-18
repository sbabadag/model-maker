# Render Pipeline Architecture Report

Tarih: 2026-08-18 · Branch: hermes-nightly · Hedef: AutoCAD-paritesi viewport (0 flicker, 60-120 FPS, 100k→1M primitif)

## 1. Mevcut Durum (inspection bulguları)

| Katman | Gerçeklik |
|---|---|
| **Görüntüleme** | Win32 **GDI** — `renderer.cpp` (1.878 satır) CPU'da izdüşüm yapar (`projectPoint` her görünür vertex için, her tam karede). |
| **GL backend** | `opengl_render_backend.cpp` (711 satır) VAR: wgl 3.3, shader (mvp + per-vertex renk), VAO/VBO/EBO/FBO, `glDrawElements`. **AMA ÖLÜ KOD**: `Application::onCanvasPaint` `renderer_.draw(...)` çağırır, `backend` parametresi `= nullptr` (renderer.hpp:208) — **hiçbir çağrı yeri backend üretmiyor** (`createOpenGLRenderBackend`/`createGdiRenderBackend` çağrılmıyor). `useGpuLines` her zaman false. |
| **Arka uç soyutlaması** | `IRenderBackend` (render_backend.hpp) — pen/brush/font handle'ları + çizim komutları + `isHardwareAccelerated()`. GDI implementasyonu aktif; GL implementasyonu tamamlanmamış (drawText boş, presentRasterZoom false). |
| **Retained mode (GDI karşılığı)** | Motion-base tamponu: tam karede arka plan+grid+eksenler+modeller+**statik seçim vurgusu** snapshot → hover kareleri saf BitBlt + imleç katmanı (e6107f7). Kamera parmak iziyle korunur (1041e6d, 25b9b19). |
| **Spatial index** | Median-split BVH (`document.cpp` ~612): `query2D`, `querySpatialNode`, pending-mask düzeltmesi (cb59c6b), amortize rebuild. |
| **Culling** | Viewport culling var (spatial query → visibleModels). Küçük obje LOD var (<0.5px atla, <1.5px SetPixel). |
| **Interaction LOD** | Interaktif karelerde stride örnekleme (6.000 model bütçesi), `drawModel` vertex stride (>120k vertex). |
| **Seçim** | Spatial pruning + vertex testleri. 250k'da `win2d ≈ 11 ms` (drafting.cpp:859-879) — **candidate D (planar cached-AABB) ertelendi**. |
| **Profil** | `FramePerformanceSample` (cpu ms, spatial ms, rendered/culled entities, drawCalls, projectedVertices, FPS) + F11 overlay. GL timer query YOK. |
| **Metin** | GDI `drawText` çağrı başına — glyph atlas YOK. |
| **Precision** | Dünya koordinatları double; GDI int izdüşüm; origin rebasing YOK. |
| **Benchmark** | `tests/benchmark_spatial_query.cpp` (250k sahne), `pull_build_run.sh` prob seti (9+ prob). |

## 2. Spec eşlemesi (30 kural → durum)

| # | Kural | Durum |
|---|---|---|
| 1 | Kamera hareketi geometri yeniden kurmasın | UYARI: Geometri (vertex) yeniden kurulmuyor ama **her tam karede her görünür vertex CPU'da yeniden izdüşüm** alıyor — eşdeğer maliyet duvarı. Hover çözüldü (motion base); pan/zoom çözülmedi. |
| 2 | 1 entity != 1 draw call | YOK: GDI'da entity başına GDI çağrısı. GL yolu batch'li ama aktif değil. |
| 3 | Görünmeyen renderer'a ulaşmasın | VAR: BVH + viewport culling + LOD |
| 4 | Sürükleme sahneyi yeniden kurmasın | VAR: Motion base + hayalet katmanı (GDI seviyesinde) |
| 5 | Statik geometri GPU'da kalsın | YOK: GL'de kalıcı VBO yok (batch per-frame); GDI'da base tamponu var (CPU). |
| 6 | Seçim spatial pruning kullansın | VAR (vertex testleri kaldı — D adayı) |
| 7 | Kare başına büyük alloc yok | KISMI: çoğunlukla yeniden kullanım; `projectedChain` büyümeleri sayılıyor |
| 8 | Kısmi sunum yok | VAR: Tek EndPaint/present; backbuffer→BitBlt |
| 9 | Performans ölçülsün | KISMI: CPU var, GPU timer yok, faz kırılımı kısıtlı |
| 10 | Mimari önce, mikro-optimizasyon sonra | VAR: bu raporun amacı |

## 3. Kritiklik sınıflandırması

| Seviye | Konu | Kanıt |
|---|---|---|
| **CRITICAL-1** | GL backend ÖLÜ — spec'in tüm öncülü GPU; üretim %100 GDI | renderer.hpp:208 default nullptr; application.cpp'de backend referansı yok |
| **CRITICAL-2** | Tam karelerde CPU izdüşüm duvarı (GDI) — 1M primitifte tam kare ~1s | `projectPoint` her vertex; 18k modelde PAINT 5.9-8.6ms → ölçek lineer |
| **HIGH-1** | GL backend retained değil: per-frame upload + drawElements (kalıcı VBO/arena yok) | opengl_render_backend.cpp batch yapısı |
| **HIGH-2** | Metin: atlas yok, GL'de drawText boş (GL aktif olunca metin GDI'da kalacak — kompozit maliyet) | opengl_render_backend.cpp:704 `{}` |
| **HIGH-3** | renderer.cpp god-class (izdüşüm+culling+feedback+metin+UI) — GL geçişinde risk | 1.878 satır |
| **HIGH-4** | Seçim vertex testleri 250k'da 11ms (candidate D) | benchmark |
| **MEDIUM-1** | GPU profilleme (GL_TIME_ELAPSED) yok | — |
| **MEDIUM-2** | Revision/dirty sistemi yok (GL retained mode'a geçince zorunlu) | — |
| **MEDIUM-3** | Kamera-relative/origin rebasing yok | — |
| **MEDIUM-4** | GPU picking yok (opsiyonel) | — |
| **LOW** | Threaded hazırlık, arena allocator, occlusion | spec: occlusion erken değil |

## 4. Faz planı (ölçüm kapılı)

| Faz | İçerik | Ölçüm kapısı | Risk |
|---|---|---|---|
| **F1** | **GL aktivasyonu** — `Application`'da backend üretimi + runtime toggle (F9: GDI/GL) + GL yolundaki eksikler (metin GDI üstüne kompozit) + render.log'da backend adı. Bench: 250k'da draw calls / FPS / CPU ms (GDI vs GL) | drawCalls < 100; FPS >= GDI; flicker yok (mevcut 9-prob + görsel) | YÜKSEK (görsel regresyon) — toggle ile geri dönülebilir |
| **F2** | **Kare döngüsü/çift tampon doğrulama** — tek present, VSync farkındalığı (GL swap), WM_PAINT→GL akış temizliği | FPS kararlılığı ±10% | DÜŞÜK |
| **F3** | **Kamera-matris izolasyonu** — pan/zoom yalnız view/proj matrisi; UBO (`std140` CameraData) | pan/zoom'da 0 vertex yüklemesi (log) | ORTA |
| **F4** | **RenderProxy cache + revision** — entity → proxy (bounds, vertex/index range, style key, transform); Geometry/Style/Transform revision'ları | modify sonrası kısmi sync (log bayt sayısı) | ORTA |
| **F5** | **GPU batching + kalıcı VBO/arena** — LineBatch/TriangleBatch, `glBufferStorage` + arena ofsetleri; per-frame `glBufferData` kaldır | drawCalls < 50; upload 0 KB (sabit sahne) | ORTA |
| **F6** | **Spatial + culling GL'e** — query sonucu → batch aralıkları (proxy indeks aralığı culling) | culled % oranı overlay'de | DÜŞÜK (mevcut BVH) |
| **F7** | **Statik/dinamik katman ayrımı GL'de** — statik VBO + dinamik (seçim/sürükleme/önizleme) ayrı buffer; sürükleme = GPU transform matrisi | drag'da 0 statik yükleme | ORTA |
| **F8** | **Sürükleme GPU transform** — Move/Copy/Rotate hayaletleri matris ile (commit CPU'ya) | drag frame < 8ms @250k | ORTA |
| **F9** | **Picking** — candidate D (planar cached-AABB, 2D) + gerekirse GPU ID picking (tıklamada, offscreen uint FBO) | win2d 11ms → <2ms | ORTA |
| **F10** | **Kalıcı/ring buffer** — profil upload darboğazı gösterirse | — | DÜŞÜK |
| **F11** | **Interaction LOD kademeleri** — RenderQuality::Interactive/Final, kalite katmanları (metin/antialias/join) | — | ORTA |
| **F12** | **Origin rebasing** — `float3(world - renderOrigin)`, origin kamera uzaklaşınca güncellenir | büyük koordinatlarda jitter yok | DÜŞÜK |

Öncelik: **F1 → F3 → F5 → F7/F8 → F9** (F2/F6 mevcut mimaride zaten sağlanıyor; F11/F12 sonra).

## 5. Yapılmayacaklar (spec ile uyumlu sınırlar)

- CAD domain modeline dokunma (WireframeModel/Document/komutlar aynı kalır)
- GDI yolu silinmez — toggle ile korunur (regresyon güvencesi, eski makineler)
- Occlusion/kompleks shadow/geometry shader: profil istemedikçe YOK
- Tek seferde büyük yeniden yazım YOK — her faz commit + benchmark + Windows doğrulaması

## 6. İlk adım (F1 — GL aktivasyonu)

1. `Application`'a backend üretimi: startup'ta `createOpenGLRenderBackend()` dene, `initialize()` başarısızsa GDI'ye düş (mevcut fallback mantığı renderer'da).
2. `onCanvasPaint`: backend'i `renderer_.draw(..., backend_.get())` ile geç.
3. GL yolundaki boşluklar: metin/grid/feedback GDI-üstü kompozit olarak kalır (GL sahne + GDI overlay BitBlt) — mevcut FBO altyapısı bu akış için var.
4. F9 toggle + `model-maker-render.log`'a `BACKEND GDI|GL` satırı.
5. Ölçüm: 250k benchmark + F11 overlay karşılaştırması (GDI vs GL) — draw calls, FPS, CPU ms.
