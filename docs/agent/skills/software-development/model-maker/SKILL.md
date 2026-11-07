---
name: model-maker
description: "Use when working on the model-maker CAD repo (sbabadag/model-maker): C++20 Win32/Qt6/OpenGL CAD, OCCT geometry, component framework, MMW8 persistence, OSNAP/grid/gumball, Windows build + Linux tests."
version: 2.1.0
author: Hermes Agent
license: MIT
platforms: [linux, windows]
metadata:
  hermes:
    tags: [C++, CAD, Win32, Qt6, OpenGL, benchmark, OCCT]
    related_skills: [codebase-inspection, systematic-debugging, model-maker-component-development]
prerequisites:
  commands: [git, g++, cmake, ninja]
---

# model-maker (sbabadag/model-maker)

BU DOSYA KISA TUTULUR (hedef 25-40k karakter). Ayrıntı `references/` altındadır; hangi dosyanın
ne zaman yükleneceği aşağıdaki **REFERANS DIZINI** tablosundadır. Kural burada, anlatı referansta.

## KISA GÖREV PROTOKOLÜ (hız — Selahattin talimatı)

Kısa kod inceleme / teşhis görevlerinde:

1. İlgili dosyalar açıkça verildiyse repo keşfini TEKRARLAMA; verilen dosyaları oku.
2. Bağımsız okuma ve aramaları AYNI turda paralel çalıştır.
3. Aynı olguyu doğrulamak için art arda grep yapma: tip tanımı + tek gerçek kullanım yeri yeterli kanıttır.
4. Kanıt yeterli olunca dur ve cevap ver; "bir de şuna bakayım" turlarını kes.
5. Alt ajan / Codex reviewer YALNIZ açıkça istenirse çağrılır.
6. Hız uğruna test veya güvenlik kontrolü ATLANMAZ.
7. Cevap sonunda gerçek ölçümü yaz: araç çağrısı sayısı, süre (ölçülmediyse "ölçülmedi" — sayı uydurma).

Büyük refactor, OCCT geometri üretimi ve kritik kod değişikliklerinde tam doğrulama (build + test) sürer.
Ayrıntı: `references/short-task-speed-rules.md`.

Başlangıç üçlüsü (repo keşfini tekrarlamamak için):
`git rev-parse --abbrev-ref HEAD` → hedefli `search_files` (tip + tek kullanım) → `cmake --build build`.

## REFERANS DIZINI — hangi dosya ne zaman yüklenir

Gövde kural verir; aşağıdaki dosyalar ayrıntı/teshis verir. Yalnız o göreve giriyorsan yükle.

| Referans | Ne zaman yüklenir |
|---|---|
| `references/tekla-reference-rule.md` | Tekla referans politikası: özellikten ÖNCE doküman/ekran görüntüsü; "Tekla'da nasıl?" sorusu |
| `references/component-engine-rule.md` | Yeni bileşen tipi/parametre şeması eklerken (registry, CompParamSpec) |
| `references/fin-plate-placement.md` | Fin plate / shear tab yerleşimi (kiriş gövdesine paralel, kolon yüzüne kaynak) |
| `references/component-dialog-param-editor.md` | Component dialog / parametre editörü (sematik + hotspot + Qt widget) üzerinde çalışırken |
| `references/component-recompute-atomic-rule.md` | Bileşen recompute: atomiklik, rollback, undo tek kaydı |
| `references/occ-geometry-ownership.md` | OCCT sahiplik, EntityId ataması, sahip olunan alt nesne silme kuralları |
| `references/brep-serialization.md` | B-Rep blob biçimi, OCCT sürüm/BinTools uyumu, kayıt-açma bozulması |
| `references/mmw8-codec.md` | MMW8 kodlayıcı blok yapısı ('I' bloğu), alan ekleme |
| `references/end-plate-phase.md` | End plate fazı: yerleşim, bulon, kesim zinciri |
| `references/end-plate-recompute-rule.md` | End plate recompute kuralları |
| `references/bolt-phase.md` | Bulon fazı: bulon grubu üretimi, MMW8 kaydı |
| `references/bolt-face-selection.md` | Bulon yüzü seçimi/eşlemesi |
| `references/cut-commands.md` | Kesme komutları (fitting / plane / line / polygon) |
| `references/cutting-objects.md` | Kesme nesneleri ve kesme zinciri |
| `references/cut-modifier-multitarget.md` | Çoklu hedefli kesme modifier'ı |
| `references/snap-types.md` | Snap türleri, ortho+OSNAP önceliği, ProfileCenter, snap işaretçileri |
| `references/grid-snap-view-plane.md` | Tekla tarzı yapı gridi (v7-v9), grid görünüş düzlemi/kilit, grid-snap bağı |
| `references/profil-kesiti-geometri.md` | Profil kesiti referans normali (v2), yerleşimi (v4), çerçeve, grip/uç tutamakları, profil seçimi |
| `references/view-commands.md` | MENÜ/RIBBON: tek kaynak komut tablosu `commandCategories()` (v11, menü==ribbon), görünüm komutları, varsayılan 3B açı, açılış varsayılanı |
| `references/view-cube-modern.md` | Modern ViewCube (26 yön), görünürlük teşhisi |
| `references/camera-navigation.md` | Kamera/orbit/pivot (R tuşu dönüş merkezi) |
| `references/two-point-view-mdi.md` | İki noktalı görünüm + MDI, seçilen noktanın iş düzlemine iz düşürülmesi |
| `references/gumball.md` | Gumball (Rhino tarzı) + yerel çerçeve + v2 ayrıntıları |
| `references/keyboard-and-tp.md` | F-tuşları/sistem tuşları (F10 tuzağı), geçici izleme (TP) katmanı |
| `references/windows-build.md` | Windows OCC/MSYS2 UCRT64 derleme ayrıntısı, OCC kurulum yolu |
| `references/props-panel-application.md` | Props paneli uygulama ayrıntıları (hangi elemana uygulanır) |
| `references/reference-layer-zoom-cost.md` | Referans katmanı flicker/zoom maliyeti, GL kare maliyeti (kontur pass), ghost plane, track/guide renkleri |
| `references/haunch40-independent-groups.md` | Haunch 40 bileşeni: bağımsız üst/alt bulon grupları, net delik-dış-kenar mesafesi, eski kayıt migrasyonu, testler |
| `references/short-task-speed-rules.md` | Kısa görev hız kurallarının tam metni (yukarıdaki protokolün ayrıntısı) |
| `references/drawing-p2a-fabrication.md` | Paper-space çizim P2A: otomatik imalat ölçüleri, FabricationInventory, yerleşim motoru tuzakları, test komutları |
| `references/drawing-p2d-layout-holes.md` | Pafta sığdırma (packSheets), GUI düzenleme koruma, bağımsız elle delikler, SheetSize/PDF test tuzakları, bant (Window/Crossing) seçim + snap'li grup taşıma |
| `references/progress-overlay.md` | Ekranın ortasında ilerleme penceresi (dosya aç/kaydet + çizim): header-only Qt overlay, olay pompalama, IoProgressSink, buildDrawingSet imza değişikliğinin test maliyeti |


## When to Use

- Any task on the `sbabadag/model-maker` repo: reading it, building it, fixing a bug, adding a
  command/modifier, or measuring viewport performance.
- When a request mentions the CAD app, ribbon/OSNAP/DXF/Qt-GDI paths, or a GDI-vs-OpenGL render change.
- Before answering "how does X work" about this codebase — the reports and probe below answer most of it
  far more cheaply than re-reading 16k lines.

AutoCAD-style 2D/3D wireframe CAD, C++20, Windows-first: Win32 + GDI canvas, Qt6 ribbon shell,
optional OpenGL line backend. Docs/commits are Turkish-English mixed. The repo-root reports
(`NIGHTLY_REPORT.md`, `RENDER_PIPELINE_REPORT.md`) are the running changelog of previous agent
sessions — read the tail of NIGHTLY_REPORT.md first for in-flight work and known traps.

## TEKLA REFERANS KURALI — özellikten ÖNCE doküman (kalıcı)

Her CAD özelliğinden ÖNCE Tekla 2026 dokümanını incele: https://support.tekla.com/doc/tekla-structures/2026
Öncelikli URL'ler, 8 soruluk liste, Fitting/Cut ayrımı, Main/Secondary Part, Model vs Drawing View,
"Tekla Reference" rapor başlığı → `references/tekla-reference-rule.md` (OKU). Erişilemeyen sayfayı
erişilmiş gibi raporlama; tahmini doğrulanmış davranıştan ayır.

## COMPONENT ENGINE KURALI — yeni birleşim eklerken (kalıcı)

Yeni çelik birleşim ekleyen programcı ortak elemanları/parametre altyapısını YENİDEN YAZMADAN
ekleyebilmeli. Registry/factory + tipli parametre + üretim planı + sahiplik kuralları:
`references/component-engine-rule.md` (OKU). Eski `component.hpp` (ComponentModel) yalnız kendi
testiyle kullanılır; yeni işler için `component_engine.hpp/cpp` kullanılır.

## KOPYA = COMPONENT KOPYASI (kalıcı)

`Document::copyModels` yalnız geometri kopyalamaz: kopya kimlik haritasını kurup
`cloneDependentsForCopies` çağırır → gorsel modeli kopyalanan BULON GRUPLARI, sahip olunan
TÜM canlı alt nesneleri kopyalanan COMPONENT INSTANCE'ları ve legacy `EndPlateConnection`
kayıtları klonlanır (kimlik remap + origin/çerçeve öteleme, tek undo adımı). Yeni bir kopya
yolu açarsan `copyModels` üzerinden geçir; `addModel` ile elle kopyalarsan component kaybolur.
Yarım kopya (ana parça ya da bir alt nesne seçilmedi) kasten instance AÇMAZ — çift sahiplik
olmaz. Regresyon: `tests/component_copy_test.cpp`.

**Rebind (Tekla davranışı):** kopyalanan component'in parçaları kopya olarak gelmediyse
`Application::rebindCopiedComponents` yeni konumdaki ana/ara parçaları bulur ve bağlar — çapa =
sahip olunan levhaların bbox merkezi, rol seçimi orijinal bağlantının eksen yönüne göre
(main paralel, secondary dik), kesit yarıçapı payı ile. Çekirdek `component_rebind.hpp`
(OCC'siz test edilir: `tests/component_rebind_test.cpp`). Rebind sonrası geometri
`recomputeComponentInstancesFor` ile yeni parçalara göre üretilir (OCC şart). Kopya parçalar
seçiliyse rebind yapılmaz.

**KOPYA B-REP (kalıcı kural):** `copyModels` yalnız WIREFRAME kopyalar — kopya EntityId'nin OCC magazasında katısı YOKTUR. Materializer `recompute` kaynak (weld) adımı `store_->find(part)` null olunca "Weld parcasi B-Rep yok" deyip TÜM değişikliği atomik geri alır; belirti: "kopya componentte parametre değiştiriyorum, birleşim değişmiyor". Bu yüzden kopya sonrası `Application::copySelectionBy` her kopya için `rebuildCutBaseSolid` çağırır (kalıcı profilden etkin+taban B-Rep; profilsizde no-op) ve `setSelectedComponentParams` materializer'dan önce `ensureCutSolid(main/secondary)` yapar. Recompute başarısızlığı artık `solidStatusMessage_` ile görünür yazılır. Kanıt testi: `tests/occ_copy_brep_gap_test.cpp` (OCC-açık build).

**WELD = ASSEMBLY (kalıcı kural):** assembly'ler YALNIZ shop-weld grafindan türetilir
(`Document::reassembleFromWelds`). Bu yüzden yeni bir kopya yolu eklerken weld kayıtlarını da
klonlamak ZORUNLUDUR — `cloneDependentsForCopies` (2) adımı: **iki ucu da kopya** ise ikisi de
kopyaya eşlenir; **tek ucu kopya** ise kaynak yine klonlanır ve diğer uç, orijinal partner hâlâ
temas bölgesinde (≤400 mm) ise o, değilse kopyanın yeni konumundaki **en yakın yapısal üye**
(profilli model) olur; temas eden üye yoksa kaynak klonlanmaz (uydurma kayıt yok). Görsel model
seçimde varsa o kullanılır, yoksa ötelenerek kopyalanır ve `remap[weld]=yeni id` yapılır; aksi
halde (a) kopya assembly oluşmaz, (b) component child tablosundaki weld EntityId eşlenmediği için
sonraki recompute orijinal kaynağı kopyanın parçalarına bağlar. Regresyon: `tests/component_copy_test.cpp`
D (tam kopya), E (yarım kopya), F (levha+kaynak → yeni üyeye bağlanır), G (temas yok → klonlanmaz).

## SECIM/ISLEM PERFORMANSI (kalıcı)

Secim vurgusu GPU batch'inde RENK olarak taşınır (`kGpuSelectionHighlight`); GDI'da her karede
3px kalemle seçili geometriyi yeniden çizmek orbit/pan/zoom'u ~5 kat yavaşlatıyordu — GDI vurgu
pass'i yalnız `!useGpuLines` yolunda kalır. `cachedGpuBatch_` girdisi `GpuModelRef`tir (isaretçi
+ etkin renk): eskiden her revizyonda TÜM modellerin derin kopyası alınıyordu (kopyala/taşı/sil
işlemleri ve secim değişimi bu maliyeti tekrarlıyordu). Onbellek anahtarına `models().data()`
da girer (yeniden tahsiste bayat isaretçi olmasın). Dosyalar: render_backend.hpp (GpuModelRef),
contour_builder.hpp, renderer.cpp, opengl_render_backend.cpp.

## DOGRUSAL KOPYA / TASIMA (ΔX/ΔY/ΔZ) — panel tabanli (kalici)

`TransformCommand::CopyLinear` / `MoveLinear` (butonlar ⧉Δ / ⇥Δ). KULLANICI TALEBI: komut
baslayinca (secim hazirsa) ekranda **X/Y/Z delta paneli** acilir; kullanici degerleri yazar ve
islem tam o kadar oteye yapilir. Ayrica **"Adet"** alani (1..`kLinearCopyMaxCount`) N kopya uretir:
ofsetler `linearCopyOffsets` = delta, 2*delta, ... N*delta, TEK undo adimi (`pushUndoSnapshot` bir
kez, dongude `copySelectionBy`). Tasimada adet satiri gizlenir (anlamsiz). Ayrica **"Iki Nokta Sec"**
dugmesi deltayi iki tiklamayla OLCER — olcum tek basina islem YAPMAZ: panel kapanir, iki nokta
tiklanir, olculen delta panele yazilir ve panel yeniden acilir; kopyalama/tasima ancak
**"Uygula"** deyince olur. Uygulama: `Application::setLinearDeltaRequest` (Qt:
`promptLinearDeltaDialog`; girdi/cikti `LinearDeltaInput` — alan eklemek imzayi bozmaz) +
`requestLinearDeltaPanel()` / `applyLinearDelta(delta, count)` / `copySelectionBy()` /
`publishCopyStatus()`. Kopya/tasima govdesi (component rebind, kesme/bulon senkronu) TEK kaynakta
kalir ve komut tek seferde biter (coklu kopya yalniz klasik Copy'de). Olcu dali `commitTransformPoint`
icindedir (`linearPickMode_` → `linearDeltaFromPick_ = true` + paneli yeniden ac, `return`).
Panel callback'i bagli degilse eski taban+hedef akisi gecerlidir (portable testler bunu kullanir);
klavyeden "dx,dy,dz" girisini de koru (`axis_delta.hpp` → `parseAxisDeltaText`).
Testler: `tests/linear_delta_offsets_test.cpp` (k*delta cekirdegi),
`tests/run_linear_delta_panel.py` (gercek govdeler + kaynak kilidi: panel 3 cagri yeri, adet tek undo)
ve `tests/run_linear_delta_dialog.py` (Qt panel sozlesmesi, offscreen: on-doldurma, Uygula/Iki Nokta/
Iptal, Adet ve tasimada gizlenmesi). Yeni kopya/tasima varyanti
eklerken `isCopyTransform/isMoveTransform` kullan; komut enum'una deger ekleyince
`modifierPreselectionAction` switch'ine de ekle, yoksa preselection "SelectEntities"e duser.

## COMPONENT DIALOG + PARAMETRE EDİTÖRÜ (Tekla-tarzı, kalıcı)

Birleşim diyaloğu iki katmandır: taşınabilir çekirdek `component_dialog.hpp/cpp` (şema: 2B sematik
çizim + hotspot + parametre grupları + EndPlateParams adaptörü + ComponentDialogRegistry) ve Qt kabuk
`component_dialog_widget.hpp/cpp` (solda şema, sağda grup grup editörler; hotspot<->satır vurgusu çift
yönlü). Yeni birleşim ekleyen YALNIZ dialog tanımını yazar (specs + şema + hotspot/grup); editör/çizim/
hit-test TEK KAYNAK. Kural + spinbox -1e9 sentinel tuzağı + doğrulama: `references/component-dialog-param-editor.md` (OKU).

## Branches (checked with `git rev-parse`)

- **Aktif geliştirme (Ekim 2026+): `overnight-features-20261015`** — kopyası **worktree'de**: `/home/ubuntu/model-maker-overnight`. Kullanıcı talimatı: Model Maker işlemleri bundan sonra ORADA yapılır. Ana ağaç `occ-hybrid-geometry`'de kilitli (git aynı dalı ikinci worktree'de açmaz → ana ağaçta `git switch overnight-features-20261015` "already used by worktree" verir).
- Deneysel: `tmp/component-preview-context-p4p6-20261025` (Haunch 40 bağımsız gruplar; `main`'e sessizce yazma).
- Çizim hattı: `feature/p1-drawing-foundation` → `p2a-auto-fab-dimensions` → `p2c-smart-sheet-manager-20261027`
  → `p2b-paper-space-editor-20261027` (21935d6) → `p2d-paper-space-layout-snap-20261027` (P1 509e96c,
  P2 cdd3a6d, worktree `/home/ubuntu/model-maker-p2d`). P2C P2B'nin ALTINDA: base commit akıllı pafta iskeletini içerir.
- UI: `feature/ui-progress-overlay-20261028` (3480752) — dosya aç/kaydet + çizim üretiminde ekranın
  ortasında ilerleme penceresi. Kural + tuzaklar: `references/progress-overlay.md`
- `main` == `hermes-nightly` == same tip — both carry the perf/GL work.
- `OCC` — 400-commit line with OpenCASCADE / 3Dconnexion-spacemouse (navlib) work.
- `master` — 1 commit, original parametric CAD + OpenSees FEA snapshot.
- Perf changes historically land on `hermes-nightly`, merged from `main`.

## Architecture map (LOC, ~16k total)

| Layer | Files |
|---|---|
| Core (portable, no windows.h) | `geometry`, `camera`, `view_cube`, `ribbon_layout`, `performance`, `document` (median-split BVH), `drafting` (snap/hit-test/modifiers), `dxf` |
| Win32 UI | `application.cpp` (3.3k) + `application.hpp` (390 lines of HWND handles) |
| Rendering | `renderer.cpp` (GDI, 2k) → `IRenderBackend` (`render_backend.hpp`) implemented by `gdi_render_backend.cpp` / `opengl_render_backend.cpp` |
| Qt shell | `qt_main_window.cpp`, `main.cpp` (QApplication + dark Fusion palette; the Win32 window still owns the canvas) |
| FEA | `force_diagram.cpp`, `NodeConstraint`/`BeamLoad` in `geometry.hpp` — OpenSees result views |

Key contracts:
- `Document::query2D/queryBounds` return indices **sorted ascending**; consumers depend on that order
  (renderer passes, pick tie-break, `selectedModels_` order). Do not drop the sort.
- Undo is delta-based: `recordUndoOp` **drops the op when no record is open**. Callers must
  `pushSnapshot()` (= `Application::pushUndoSnapshot()`) *before* mutating, or nothing is undoable.
- `constrainOrtho(anchor, cursor, axis)` picks the dominant of |dx|,|dy| and keeps `cursor.z` (2D ortho
  does not zero Z). **F8 ORTHO OBJE SNAP'INI EZMEZ** — obje snap'i (uc/orta/kesisim/dikme) ortho'yu
  ezer, `preserveObjectSnaps=true` gecilmelidir (aşağıdaki "Ortho + OSNAP" bolumu).
- Frames: motion base + backbuffer in `Renderer`; the GL path (`F6` toggle, `gpuLinesEnabled_`) renders to
  an offscreen FBO in a hidden 1x1 window and alpha-blends into the GDI backbuffer. Never SetPixelFormat
  on the canvas — it turns the canvas black under DWM.

## MİMARİ SÖZLEŞMELER — yeri ve bağlayıcılığı

Bunlar özet DEĞİL, işaretçidir; kuralın tam metni referanstadır ve bağlayıcıdır.

- **Bileşen üretimi**: `ComponentRegistry` (tip + parametre şeması) → `GenerationPlan` →
  `ComponentPlan` (roleKey korunur) → `ComponentMaterializer::prepare/commit/recompute`.
  prepare Document'a YAZMAZ; commit tek undo kaydı altında uygular; weld için sessiz başarı YOK.
  → `references/component-engine-rule.md`, `references/component-recompute-atomic-rule.md`
- **OCCT sahipliği / EntityId**: sahip olunan alt nesne EntityId'si component kaydında tutulur,
  silme sahiplik zincirini izler. → `references/occ-geometry-ownership.md`
- **Kalıcılık (MMW8)**: `ComponentInstanceRecord` 'I' bloğu; B-Rep zarflı blob + OCCT/BinTools
  sürüm uyumu. → `references/mmw8-codec.md`, `references/brep-serialization.md`
- **planDigest KIMLIK-BAGIMSIZDIR (MM-2b, 1.9.0)**: digest yalniz GEOMETRI esdegerligini olcer;
  uye referanslari EntityId yerine rol anahtariyla hash'lenir (WeldSpec.partARole/partBRole,
  BoltGroup.partRoles, CutModifier.targetRole). `inputRoleFor()` EntityId->rol eslemesi; generator
  VE recipe bu alanlari DOLDURMALI (EntityId alanlari malzemelesme icin kalir). Yeni plan/rol
  alani ekleyince `generationPlanCanonicalJson`'daki kimlik-normalizasyonunu da guncelle.
- **GOLDEN DISIPLINI (kalıcı)**: digest/geometri DEGISTIREN her commit'te golden dosyalari AYRI
  commit'te guncellenir. Golden yeniden uretiminden ONCE golden'dan BAGIMSIZ bir GEOMETRIK kanit
  testi yesil olmali (gen==rec; levha koseleri/bulon eksen-grip/kesim duzlemi/kaynak rol 1e-9; tek
  fark id->rol). Golden commit mesaji her dosya icin eski->yeni digest listesi + gerekce tasir.
  Golden dosyalari: tests/golden/*_digests.json + libs/mm_contract/packages/end_plate_pkg/tests/*.json.
- **Kesme zinciri**: modifier → hedef B-Rep → yeni sürüm; başarısızlıkta belge DEĞİŞMEZ.
  → `references/cut-commands.md`, `references/cutting-objects.md`
- **Tekla referans politikası** (kalıcı): özellik yazmadan ÖNCE doküman/ekran görüntüsü;
  varsayımla davranış uydurulmaz. → `references/tekla-reference-rule.md`

## DELEGATE POLİTİKASI (kalıcı — Selahattin)

- Ana ajan günlük kodlamayı KENDİSİ yapar (model: deepseek-flash, provider custom → api.deepseek.com).
- Delegate yalnız şu üç durumda: (a) zor C++/OCCT hatası, (b) iki başarısız düzeltme denemesinden SONRA,
  (c) kritik değişikliğin incelemesi. Delegate modeli: openai-codex / gpt-6-sol (OAuth, kota).
- **Aynı anda EN FAZLA BİR delegate.** Paralel çocuk açma; iş bölünecekse sırayla ver.
- Değişiklikten sonra ilgili derleme + testleri çalıştır (Linux: `cmake --build build` + `ctest`),
  Windows'ta makineye özel blok. Çalıştıramadığın kontrolü raporda AÇIKÇA yaz — "test edildi" deme.
- API anahtarı/token değerlerini sohbete veya dosyaya yazma; yalnız varlığını raporla.

## BAKIM KURALI — bu dosya şişmesin

Yeni ayrıntı (teşhis, sürüm anlatısı, kod bloğu) doğrudan `references/` altına yazılır; SKILL.md'ye
yalnız kural + referans satırı eklenir. SKILL.md 40.000 karakteri geçerse yeni bölüm eklemek yerine
en büyük bölüm referansa taşınır. Gövdede adı geçen her referans dosyası diskte gerçekten var olmalıdır.

## Yeni makine teshisi (doctor.ps1)

Kullanici makine degistirdiginde (or. HP ZBook) ONCE envanter al: `scripts/doctor.ps1`
(read-only, hicbir seyi degistirmez). Qt / MSYS2-UCRT64 / OpenCASCADE / derleyici / repo durumunu
rapor eder ve hangi derleme yolunun uygun oldugunu soyler:

```powershell
cd <repo>; powershell -ExecutionPolicy Bypass -File .\scripts\doctor.ps1
```

- `~/.local/pwsh/pwsh` (PowerShell 7.4.6, Linux host'a tarball ile kurulu) ile .ps1 sozdizimi
  DOGRULANABILIR ve betik Linux'ta CALISTIRILABILIR (Windows'a ozel yollar 'YOK' cikar — bu
  mantik testidir). Sozdizimi: `[System.Management.Automation.Language.Parser]::ParseFile(path,[ref]$toks,[ref]$errs)`.
  `dotnet tool install --global PowerShell` CALISMAZ (DotnetToolSettings.xml yok) — GitHub releases
  tarball'ini kullan.
- **Yanlis negatif:** doctor OCC DLL'ini `TKernel.dll` adiyla arar; MSYS2'de adi
  **`libTKernel.dll`** (import kitapligi `libTKernel.dll.a`). "TKernel DLL: YOK" satiri OCC
  kurulu DEGIL demek degildir — `OpenCASCADE: VAR` satirina ve configure ciktisina bak.
- PowerShell'e cok satirli blok yapistirinca satirlar bazen TEK satira biner ve `cd` hepsini
  argüman sanar (`Set-Location : A positional parameter cannot be found`). Komutlari `;` ile
  tek satir olarak ver (ayni sey MSYS2 bracket-paste `^[[200~` icin de gecerli).

- **KOMUT BICIMI (kullanici kurali):** derleme/calistirma komutlarini HER ZAMAN AYRI AYRI,
  her komut kendi satirinda/kendi kod blogunda ver. Kullanici blok halinde kopyalamakta
  zorlaniyor. Bir blokta birden fazla komut olmasin; "once bunu, sonra sunu" diye sirala.
  Her komut TEK satir olsun (`;` ile zincirlenmis) ki kopyalanınca dogru calissin.
  **HER ZAMAN** komut dizisinin SONUNA exe'yi calistirma satirini da ekle (kullanici
  talebi): derleme bitince uygulamayi acan tek satir (PowerShell, or.
  `C:\work\model-maker-p2d\build-occ\model-maker.exe`). "exe nasil calisir" diye tekrar
  sormak zorunda kalmasin. NOT: `/c/mm-build-p2d` YOKTUR — dogru yol iki makinede de
  `C:\work\model-maker-p2d\build-occ` (PowerShell).

## Windows'ta derleme/calistirma — HANGI MAKINE? (TETIKLEYICI SOZCUKLER)

KULLANICI TALIMATI: hangi makinede oldugunu sozcukle belirtir; ona gore komut ver.

| Kullanici ne derse | Makine | Asagidaki blok |
|---|---|---|
| **"evde calisiyorum"**, **"ev"**, **"evden"** | **ASUS** (ev) | `Asus = EV makinesi` |
| **"is"**, **"iste"**, **"isyerinde"** | **ZBook** (is) | `ZBook = IS makinesi` |

- Bu sozcuk GECMEZSE makineyi SORMADAN varsayma: yanlis makinenin yollarini vermek
  kullaniciyi dogrudan derleme hatasina goturur. Tek satirlik "ev mi is mi?" sorusu sor.
  (Baglam ipucu: son derleme hangi makinede yapildiysa o.)
- Komutlari **o makinenin repo yolu + toolchain'i** ile ver; iki makine farkli yol,
  farkli derleyici ve farkli build dizini kullanir. Birinin komutunu digerine verme.
- Makine degistiginde / yeni bir makinede ilk kez calisirken once `scripts/doctor.ps1`
  (read-only envanter) calistir. Iki makine goruldu:

Makineye ozel derleme/calistirma bloklari (ZBook + Asus, tam komutlar):
`references/windows-build.md` (OKU). Ozet: iki makine farkli yol + toolchain; ZBook'ta
Qt'nin kendi MinGW'i (tercih edilen), Asus'ta WinLibs MinGW (`cmake`/`ninja`/`g++` ayni
`mingw64\bin`'de — `cmake not recognized` = PATH eksik; Qt'in kendi MinGW'i YOK).

> Taşınan/kopyalanan repo tuzağı: MEGA ile kopyalanmış çalışma ağacı git HEAD'den farklı olur
> (izlenen dosyalar değişik, OCC dosyaları untracked). Temizleme: `git stash push --include-untracked`
> sonra `git checkout -f -B OCC origin/OCC`.

- **Tuzak (kanıtlanmış):** yalnız PATH'e Qt bin (veya Qt+MinGW bin) eklemek YETMEZ —
  uygulama "no Qt platform plugin could be initialized" ile açılmaz. Ölçüm: `qwindows.dll`
  sağlam (1.14 MB), MinGW runtime DLL'leri (libstdc++-6, libgcc_s_seh-1, libwinpthread-1)
  PATH'te, `QT_QPA_PLATFORM`/`QT_PLUGIN_PATH` boş — buna rağmen eklenti bulunamıyor.
  Çözüm `windeployqt --compiler-runtime` ile eklentiyi/Kt DLL'lerini **exe'nin yanına**
  kopyalamak; sonrasında PATH manipülasyonu gerekmez, exe kendi başına açılır.
  `scripts/pull_build_run.ps1` yalnız PATH ekler → aynı tuzağa düşer, güvenme.

- **Tuzak (P0 DPI, kanıtlanmış):** MinGW-w64 `SetProcessDpiAwarenessContext` / `GetThreadDpiAwarenessContext` / `DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2` sembollerini yalnızca `NTDDI_VERSION >= NTDDI_WIN10_RS2` (0x0A000003) iken bildirir; sürüm makroları tanımsızsa `was not declared in this scope` (ayrıca `WM_DPICHANGED_AFTERPARENT` aynı garda gizlenir). Çözüm: `application.hpp` başında `_WIN32_WINNT=0x0A00` + `WINVER` + `NTDDI_VERSION=0x0A000003`, windows.h sonrasına da fallback bildirimleri. Bu hata Linux ctest'te YAKALANMAZ (GUI dosyaları WIN32-only) — Windows-only sembol değişikliklerini bu host'taki `x86_64-w64-mingw32-g++` ile ayrı bir .cpp'de derleyip doğrula.

## Verify on Linux (the GUI cannot build or run here)

The GUI needs Windows (`windows.h`, `gdi32`, Qt6 mingw). Core + both benchmarks build and run on Linux —
this is the fast regression loop.

```bash
sudo apt-get install -y --no-install-recommends build-essential cmake ninja-build   # fresh VM
cd /home/ubuntu/model-maker-overnight
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build build --target model_maker_core model_maker_render_prep_benchmark model_maker_spatial_query_benchmark
./build/model_maker_render_prep_benchmark
./build/model_maker_spatial_query_benchmark
```

- Qt6 is `find_package(Qt6 QUIET)`; missing Qt only skips the `model-maker` exe target (expect CMake
  dev warnings about AUTOGEN).
- Qt6 IS installed here (qt6-base-dev): the component-dialog Qt widget is verified headless on Linux
  (`model_maker_component_dialog_widget_test`, QT_QPA_PLATFORM=offscreen auto-set). The Win32
  canvas/GDI app still cannot build here.
- Build only inside `build/` — `.gitignore` covers `build/` and `build-local/`, nothing else.
- **OCC-ACIK LINUX BUILD CALISIR (kanitlandi):** `cmake -S . -B build-occ-hermes -G Ninja -DCMAKE_BUILD_TYPE=Release -DOCC_ROOT=/usr -DMM_OCC_LINK_DIRECT=ON -DBUILD_TESTING=ON` → `MODELMAKER_OCC_READY` acilir, gerçek OCCT testleri (materializer, weld, kesme, kopya B-Rep) Linux'ta koşar. `find_package(OpenCASCADE)` yolu (VTK hedefleri) hâlâ patlar; ELLE `OCC_ROOT` yolu kullanılır. `-DCMAKE_DISABLE_FIND_PACKAGE_OpenCASCADE=ON` yalnız OCC'siz hızlı döngü içindir.
- `model_maker_tests` (tests/test_core.cpp) **cannot compile on Linux**: it includes `renderer.hpp`,
  which includes `windows.h` (same for `application.hpp`, `view_cube_renderer.hpp`, `force_diagram.hpp`).
  Verify behavior with standalone probes instead:
  `g++ -std=c++20 -O2 -I include probe.cpp build/libmodel_maker_core.a`.
  A working 34-check probe (document/BVH, undo, camera round-trip, work plane, snap, ortho,
  offset/mirror/trim/fillet, DXF + .mmw round-trip) lives at `~/.hermes/cache/scratch/probe_learn.cpp` —
  copy and extend it rather than writing a harness from scratch.
- `scripts/pull_build_run.sh` is the upstream nightly loop but hardcodes `REPO=/workspace/model-maker`,
  demands branch `hermes-nightly`, and expects `/opt/data/mm_probe_*` correctness probes that are **not in
  the repo** (missing = step skipped). Fix `REPO` for a local clone.
- `scripts/smoke_*.py` are Windows-only UI smoke tests (ctypes user32/gdi32 + PIL `PrintWindow`).
- **OCCT ile GERCEK dogrulama (bu host'ta kurulu):** `sudo apt-get install -y --no-install-recommends
  libocct-foundation-dev libocct-modeling-data-dev libocct-modeling-algorithms-dev
  libocct-data-exchange-dev`. Ubuntu'nun `OpenCASCADEConfig.cmake`'i de VTK hedeflerine referans
  verir → `find_package(OpenCASCADE)` PATLAR (MSYS2 tuzaginin aynisi) ve OCC_ROOT olmadan bile
  configure'i dusuruur. Bu yuzden Linux yapilandirmasi `-DCMAKE_DISABLE_FIND_PACKAGE_OpenCASCADE=ON`
  ile yapilir (OCC'siz cekirdek + tum ctest kosar); OCC'ye bagli iddialar gercek kaynagi ayri
  derleyerek dogrulanir:

  ```
  g++ -std=c++20 -O1 -DMM_HAS_OCC -I include -I /usr/include/opencascade <probe>.cpp \
    src/occ_geometry.cpp src/geometry.cpp -lTKernel -lTKMath -lTKG2d -lTKG3d -lTKGeomBase \
    -lTKBRep -lTKGeomAlgo -lTKTopAlgo -lTKPrim -lTKBO -lTKBool -lTKShHealing -lTKMesh \
    -lTKXSBase -lTKDESTEP -o probe && ./probe
  ```

  `tests/occ_smoke.cpp` (+`src/occ_bridge.cpp`) aynen boyle derlenip kosulur — icindeki
  yalniz-yazdiran bloklari gercek kapiya (`return 1`) cevirmek icin bu yolu kullan.

## Git / push erişimi (bu AWS host)

HTTPS ile push yapılamaz (token yok, `gh` kurulu değil). SSH deploy anahtarı kurulu:

- `~/.ssh/modelmaker_deploy` + `~/.ssh/config` içinde `Host github-model-maker` takma adı.
- Repoda ikinci uzak ad: `origin-ssh` = `git@github-model-maker:sbabadag/model-maker.git`.
  Push: aktif çalışma dizininden `git push origin-ssh <dal>` (overnight worktree'de `git push origin-ssh overnight-features-20261015`; eski `OCC` örneği yalnız tarihî). `origin` (HTTPS) salt-okunur fetch için kalıyor, tracking ref'i günceller.
- Deploy anahtarı GitHub'da **Allow write access** işaretli olmalı; değilse `Permission denied (publickey)`.
- Hermes Project: `model-maker` (id `p_a3877be4`), `/home/ubuntu/model-maker` klasörüne bağlı.
- Push sonrası doğrula: `git ls-remote origin-ssh refs/heads/OCC` == `git rev-parse OCC`, sonra `git fetch origin` ile `git rev-list --left-right --count` = `0 0`.

## test_core.cpp ROT (Windows test hedefi derlenmiyor)

`tests/test_core.cpp` (hedef `model_maker_tests`, yalniz `if(WIN32)`) **BAYAT**: `SnapEngine::snap`/
`snap3D` imzasi degismis (eski `objectTolerance`'dan SONRAKI `double gridTolerance` parametresi
KALDIRILDI; grid snap artik `document.grids()` uzerinden) ama 14 cagri yeri eski imzada kalmis.
`x86_64-w64-mingw32-g++ -std=c++20 -fsyntax-only -I include -I /tmp/qstub tests/test_core.cpp`
→ 14 hata (satirlar ~514, 524, 1329-1334, 1372-1429, 1823). Bunlar HEAD'de de var (bu oturumdan
once); kullanici yalniz `--target model-maker` derledigi icin fark edilmemis.

- Yeni cekirdek davranis testini BURAYA YAZMA (kosmaz). Platformdan bagimsiz ayri bir hedefe koy:
  `tests/<konu>_test.cpp` + `model_maker_core` linki + `add_test` (orn. `ortho_snap_core_test.cpp`,
  `section_frame_test.cpp`, `perpendicular_tracking_test.cpp`) — ctest Linux'ta da kosar.
- `win_syntax_check.sh` YALNIZ `src/`+`include/` tarar; `tests/` altindaki dosyalari AYRICA
  capraz kontrolden gecir (`x86_64-w64-mingw32-g++ -fsyntax-only -I include -I /tmp/qstub <dosya>`).
- test_core.cpp'yi duzeltmek ayri bir is (calistirilamadigi icin yalniz derleme dogrulanabilir).

## 3B rotasyon (orbit)

- Orta-tus surukleme (3B, Alt yok) rotasyon: **pivot DEGISTIRILMEZ** — mevcut `center3D_`
  etrafinda yerinde doner. Pivot'u imlecin altina tasimak goruntuyu "ziplatir". `center3D_`
  fit3D (zoom extents) ve pan ile model merkezinde tutulur.
- Yatay (yaw) isaret: `camera_.rotate(-dx*0.008, dy*0.008)` — saga surukle = sag taraf gelir
  (Tekla yonu). Dikey (pitch) pozitif: yukari surukle = ustten bak. Delta EMA ile yumusatilir
  (`rotSmoothedDx_/Dy_`, k=0.5). ViewCube suruklemesi de ayni isareti kullanir (-dx).
- Standart gorunusler: Ctrl+1..7 = On/Arka/Sol/Sag/Ust/Alt/ISO (hem Qt menu hem WM_KEYDOWN).
  `setStandardView` kendine yeter (SpaceMouse + update'ler dahil).

## Divide komutu

- `D` (veya Çizim > Böl): snap açıkken iki nokta seçilir; girilen **adet kadar** point objesi
  iki uç arasına eşit aralıklı atılır (N adet -> t=1/(N+1)..N/(N+1), uçlar hariç) ve
  `WireframeModel::point` ile yerleştirilir (SnapType::Node olarak snap'lenir). Sayı edit box
  ile `activateDivide()`'da sorulur (2..1000).
- `currentEntityProperties()` ile stil alınır, tek `pushUndoSnapshot` + N-1 `addModel` (tek undo adımı).
- `DrawTool::Divide` enum'a eklendi; `toolLabel`'da DIVIDE/COLUMN case'i de var.

## PROPS PANELI: ozellik SECILI elemana uygulanir (kolon boyu tuzagi)

Secim varken HER satir secime ANINDA uygulanmali + secimden OKUNMALI. Kolon kotu =
profilli DUSEY katiyi iki ucun Z'siyle yeniden uretmek; tek yol `applyColumnLevels`.
ByLayer secmek override'i temizlemeli. Detay + rota tablosu + test/mutasyon deseni:
`references/props-panel-application.md`.

## OCC (OpenCASCADE) — MSYS2 UCRT64 yolu (tam ozellik)

Windows/MinGW'de OCC'yi kullanicinin Qt'siyle karistirmak iki farkli `libstdc++-6.dll`
cakismasi yaratir (Qt kendi MinGW'sinden, OCC MSYS2'den ister). Cozum: **hepsi MSYS2 ucrt64**
→ tek toolchain, OCC dogrudan baglanir, TAM ozellik (profil ekstruzyonu dahil).

```bash
# MSYS2 UCRT64 kabugunda:
pacman -S --needed mingw-w64-ucrt-x86_64-gcc cmake ninja qt6-base qt6-tools opencascade
# repo'da (build-occ .gitignore'da):
cmake -S <repo> -B <repo>/build-occ -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DQT_ROOT=C:/msys64/ucrt64 -DOCC_ROOT=C:/msys64/ucrt64 -DMM_OCC_LINK_DIRECT=ON
cmake --build <repo>/build-occ --target model-maker
<repo>/build-occ/model-maker.exe   # UCRT64 kabugundan calistir (OCC DLL'leri PATH'te)
```

Tuzaklar (hepsi yasandi):
- **Yeni makine / ilk kurulum:** prefix'leri KARISTIRMA — ucrt64 Qt + ucrt64 g++ + ucrt64 OCC.
  Qt 6.9.3 (C:/Qt) ve mingw64 GCC 16 kombinasyonlari codecvt link hatasi / ilk komutta segfault
  verir; belirti->kok neden tablosu: `references/windows-build.md` ("ZBook ilk kurulum").
- `find_package(OpenCASCADE)` MSYS2'de **PATLAR**: OpenCASCADEConfig VTK hedeflerini
  (TKIVtk->VTK::CommonCore, TKIVtkDraw->VTK::IOImage) referans verir ama VTK config'i yok.
  Bu yuzden **-DOCC_ROOT (elle) yolu** kullanilir, config yolu DEGIL.
- Elle yolda app normalde OCC'ye baglanmaz (kopru/mm_occ.dll). **MM_OCC_LINK_DIRECT=ON**
  ile dogrudan baglanir (yalniz tek toolchain sartinda guvenli).
- `-static` link bayragi OCC'nin `libTK*.dll.a` IMPORT kutuphanelerini cozemez
  ("have you installed the static version of the TKernel library?") ->
  MM_OCC_LINK_DIRECT modunda `-static` birakilir (`-static-libgcc/-static-libstdc++` kalir).
- `OCC_MANUAL_LIBS`'te **TKMesh** sart (shapeToWireframeWithFaces -> BRepMesh_IncrementalMesh/Poly_Triangulation).
- App'i MSYS2 UCRT64 KABUGUNDAN calistir; yoksa libTK*.dll/libstdc++-6.dll bulunamaz.
- Windows'ta PATH'e Qt bin eklemek YETMEZ; qwindows.dll exe yaninda olmali (windeployqt),
  ama MSYS2 kabugundan calistirinca Qt6 DLL'leri zaten PATH'te.

## TEKLA COMPONENT BRIDGE (Tekla -> Model Maker import, kalıcı)

- Girdi: `.mmbridge` paket klasörü (manifest/component/parameters/objects/geometry/diagnostics,
  formatVersion 2.0). Okuyucu `bridge_package.cpp`, OCC-on import `bridge_occ_adapter.cpp`
  (`importBridgeComponent`: plaka kati + bulon delikleri + Document atomik + undo/redo).
- **Windows GUI**: File > Import > Tekla Component Bridge → `Application::importTeklaBridge`
  (okur + doğrular + atomik import + plakayı seç + zoom extents). OCC'siz derlemede uyarı, false.
- ASUS ham `bridge-geometry-probe-2` JSON'unu `.mmbridge`'e çeviren dönüştürücü:
  `tools/model-maker-bridge/scripts/convert_probe_to_mmbridge.py` (eksik alan UYDURULMAZ,
  unresolved diagnostic; kritik eksikte paket yazılmaz).
- CMake: `model_maker_occ_bridge_146_import_test` artık kalıcı ctest hedefi (ASUS'ta elle
  CMake yaması gerekmez); Linux'ta OCC find_package VTK yüzünden patladığı için bu test
  `g++ -DMM_HAS_OCC ...` tek-satırıyla ayrı koşulur (komut: `tools/model-maker-bridge/README.md`).
- TAM belge + fixture + komutlar: `tools/model-maker-bridge/README.md` (OKU).

## B-Rep serializer — zarfli blob + OCCT 8.0.1 BinTools cokme tuzagi

- serializeBRep/deserializeBRep (occ_brep_serialize.cpp) raw BinTools payload'unu "MMBREP01"
  magic'li surumlu ZARFA sariyor (52 bayt baslik: magic[8] + version u32 + len u64 + SHA-256[32]
  + raw payload). Neden: OCCT 8.0.1 bozuk binary girdide Standard_Failure FIRLATMAZ;
  BinTools_Curve2dSet::Curve2d icinde erisim ihlali (SEH) verir — try/catch yakalayamaz.
- deserializeBRep: magic varsa zarf dogrulanir (version/len/SHA-256), basarisizsa BinTools::Read
  HIC cagrilmaz. Magic yoksa = eski zarfsiz blob -> BinTools text baslik on-kontrolu
  ("\nOpen CASCADE Topology V1-4") + dogrudan okuma (geriye donuk uyum).
- Uretimde mmw8_codec blob basina AYRI SHA-256 saklar (mmw8_codec.cpp ~464), eski zarfsiz
  dosyalar orada zaten korunur; serializer zarfı yalniz dogrudan API cagrilari icin ek guvencedir.
- KALAN RISK: gecerli zarfa (dogru SHA-256) sarilmis KOTU payload yine cokebilir; eski raw blobda
  basligi saglam DERIN bozulma da cokebilir. Gercek izolasyon Windows SEH (__try/__except) veya
  ayri islem gerektirir — zarf bunu bilerek cozmez (raporlanmali, gizlenmemeli).
- cokme teshisi: test exe'sini ctest DISINDA konsolda calistir (ctest pipe'a stdout'u tam tamponlar,
  segfault'ta kaybolur); gdb `bt` -> BinTools_Curve2dSet::Curve2d <- BinTools_ShapeSet::ReadShape.
- vcpkg x64-mingw-static triplet: OCCT statik .a (DLL siniri yok) -> libstdc++/DLL ABI suphesi elenir;
  sorun upstream OCCT 8.0.1 okuma-saglamlik gerilemesidir.
- **TUZAK (2026-10, kanitlanmis):** MSYS2 `mingw-w64-ucrt-x86_64-opencascade 7.9.3-3` (Build Date
  2026-08-10) **GCC 16.1/16.2** ile -O3'te derlenmis; `BRepClass3d_SClassifier::Perform` IPA-CP
  klonlamasinda yanlis derlenir -> kenar/kose uzerinde nokta siniflayan HER boolean
  (`Extrema_ExtCC::Perform` icinde) deterministik segfault. Upstream: OCCT issue #1637 / GCC PR127917.
  Belirti YALNIZ Windows'ta (Linux OCCT 7.9.2 = GCC 13/14, etkilenmez); gdb DIŞINDA ve gdb altinda da
  olur. Bisect deseni: serialize ONCESI bile coker -> serializer masum; test geometrisi masum.
  Tani: `pacman -Qi mingw-w64-ucrt-x86_64-opencascade` (7.9.3-3) + `g++ --version` (16.1/16.2).
  Cozum (repo'da YAPILACAK BIR SEY YOK): MSYS2 GCC 16.3 / OCCT 8.0 paketi cikinca `pacman -Syu`;
  veya OCCT'yi `-fno-ipa-cp-clone` ile yeniden derle. Uygulamanin kendisi etkilenmez (bridge 146
  51/51, GUI import calisir — o boolean'lar kose siniflandirmasina denk gelmez); yalniz
  `occ_brep_serialize_test`'in boolean adimi coker. Testi gizleme/retry ile sahte yesile cevirme.
  **DUZELTME (uygulama ETKILENIR):** 146 + kaynak Windows'ta coktu: `BRepExtrema_DistShapeShape(vertex, KATI)`
  -> SolidTreatment -> BRepClass3d_SClassifier -> Extrema_ExtCC SIGSEGV. Kural: src'de
  `BRepClass3d_SolidClassifier` YASAK (ctest `model_maker_no_brepclass3d_in_src` yakalar) ve
  DistShapeShape'e KATI verme — `boundaryFacesOf(shape)` (yuz compound'u) ver; ic/dis icin
  `pointSolidState` (3 egik isin parite oylamasi, occ_geometry.hpp). Linux bu cokmeyi ASLA gostermez;
  Windows crash'inde `gdb -batch -ex run -ex bt model-maker.exe` ile backtrace iste.
  **Cokme #2 (ayni kok):** `BRepAlgoAPI_Fuse` da coker (Fuse -> BOPAlgo_BuilderSolid -> ComputeState ->
  SClassifier). Kaynak dikis katilari Fuse ile BIRLESTIRILMEZ; `compoundOfShapes` (boolean'siz compound)
  kullanilir; ayni cmake kontrolu `booleanFuseShape(fused, solids` desenini yasaklar. Kalan risk: bulon
  deligi/kesme Cut'lari da ayni yola girebilir — yeni backtrace'te Cut gorunurse gercek cozum OCC paketi.
  Windows sozdizimi: `x86_64-w64-mingw32-g++ -fsyntax-only -D_USE_MATH_DEFINES -DMM_HAS_OCC -I include
  -I /usr/include/opencascade <dosya>` (M_PI icin _USE_MATH_DEFINES sart).
  `SetParallelMode(false)` NO-OP'tur (7.9'da paralel mod varsayilan KAPALI, myGlobalRunParallel=false)
  — "paralel boolean yarisi" teorisi YANLIS, stack'te OSD_Parallel::For seri modda da gorunur.

## OccSolidStore::restoreBaseSolid — ETKIN surumu EZME (kayit/ac kesme kaybi)

- **Tuzak (olculdu, P12):** `OccSolidStore::set()` yeni surumu AKTIF yapar. `Document::commitFromMmw8`
  yuklemede ONCE `restoreActiveSolid` (kesilmis kati, B blobu) SONRA `restoreBaseSolid`
  (kesilmemis taban, V blobu) cagirir. restoreBaseSolid icindeki `set()` tabani AKTIF yapar ve
  kesilmis katiyi EZER -> her kayit/ac sonrasi magazadaki aktif kati sessizce "kesilmemis"e doner
  (mesh/kesme modifier'i DOGRU gelir, yalniz OCC aktif katisi bozulur; Part Cut/re-cut yanlis taban
  kullanir). FIX: `const VersionId prevActive = activeVersion(id);` -> set -> setBaseVersion ->
  `restoreActive(id, prevActive)` (onceki aktif yoksa taban ASLA kendiliginden aktif olmaz).
- **Round-trip regresyon testi deseni:** `tests/occ_store_roundtrip_test.cpp` (store birimi: aktif
  = cut, taban = uncut) + `tests/occ_document_roundtrip_test.cpp` (tam Document: construction plane
  + kesme modifier + OCC kati save->load). CMake'te `if(MODELMAKER_OCC_READY)` icinde `model_maker_occ`
  linkiyle baglanir (model_maker_occ PUBLIC model_maker_core + MM_HAS_OCC + TKDESTEP tasir). Linux
  OCC-disabled ctest bunlari KOSMAZ; basliklardaki g++ tek-satir komutuyla ayri kosulur
  (`-DMM_HAS_OCC ... -lTKDESTEP` — `-lTKSTEP` YOK, STEP tek TKDESTEP altinda).

## Renk tuzagi (entity/point rengi)

Entity'ye ozel renk verirken yalniz `effectiveColor` YETMEZ: `Document::resolveEffectiveProperties`
BYLAYER (`colorIndex==256`) + **trueColor yoksa** effectiveColor'i **katman renginden** yeniden
hesaplar (degeri ezer). Ozel renk icin **`props.trueColor = 0xRRGGBB`** de set edilmeli.
Dogrulama probe'u: `~/.hermes/cache/scratch/divide_color_probe.cpp`.

## WELD (kaynak) kati — OCC fillet uretimi (W3)

- `cornerFaceNormal` (weld_geometry.cpp) temas yuzu normalini secerken yuzun normali kaynak yonune
  (j.direction) DIK olmali — kesit d'ye dik duzlemde cizilir. d'ye paralel normali olan "uc" yuzu
  (or. kirisin temas yuzu) secilirse `cross(d,n)` sifir olur, fillet kati uretilemez; neden
  "BRepCheck gecersiz" gosterilir ama gercek neden dejenere cross'tur. Belirti: kutu testi gecer,
  GERCEK I-profil T-birlestimi kati uretmez. Regresyon: `occ_weld_profile_beam_test`.
- Kaynak gibi TUREMIS ozellik yollari hedef parcalarin B-Rep katisini LAZY uretmeli (`ensureCutSolid`)
  — `Document::load()` magazayi bosaltir; `store->find` null donerse ozellik sessizce glif'e duser
  (assembly kurulur, kati yok). Kesme akisi (`recomputeCutTarget`) yapar; yeni ozellik yolu da yapmali.
- Basarisizlik nedeni (`unsupportedReason`) kullaniciya GORSTERILMELI — AMA `publishStatus` tek basina YETMEZ: click handler sonrasi `updateStatus()` durum cubugunu bastan kurar ve onu ezer (kullanici eski mesaji gorur, nedeni goremez). Kalici mesaj icin `solidStatusMessage_` kullan (End Plate/Shear Plate ayni alani kullanir); uzaktan teshis icin modal `MessageBoxW` (skill 3d-cad-viewport'un kanitlanmis yontemi).
- Kaynak kati icin iki parca birbirine DOGRUSAL TEMAS KENARI boyunca degmeli; kiris-kolon arasinda bitis levhasi varsa secilen iki parca degmiyor olabilir ("temas etmiyor" nedeni) — kullaniciya actionable mesaj ver.
- KAYNAK KONUMU parca ICINE dusebilir: `cornerFaceNormal` sinirsiz DUZLEM mesafesine bakiyordu ve ana parca icin `refNormal={0,0,0}` gecildigi icin dik-yuz testi HER ZAMAN dogru cikiyordu → noktadan gecen ILK duzlem seciliyordu. Parca koselerinde iki yuz duzlemi ayni noktadan gecer; yanlis yuz secilince fillet kesiti malzemenin icine duser. Kural: yuz seciminde (a) GERCEK yuz mesafesi kullan (`BRepExtrema` vertex↔face, `gp_Pln::Distance` DEGIL), (b) TUM aday normalleri topla, (c) katı urettikten sonra hacim merkezini `BRepClass3d_SolidClassifier` ile iki parca icin de sina ve ICERIDE kalan yerlesimi reddet/ters cevir. "Kati icerde kaldi" sikayetinde once bunu kontrol et; varsayimla duzeltme yapma. Regresyon: `occ_weld_corner_placement_test`.
- Kaynak "kucuk/yanlis yerde" gorunuyorsa bakilacak ilk sey DIKIS UZUNLUGU: parcalar yalniz kalinlik kenarindan degiyorsa kaynak 10-20 mm'lik bir kose parcasi olur ve dogru yerde olsa bile "kaymis" gorunur. Durum mesajina gercek dikis uzunlugunu yaz (bacak + dikis mm) — kullanici tahmin etmek zorunda kalmasin.
- COMPONENT (146 vb.) KAYNAGI (W7): generator `WeldSpec` uretir — `partA` = kullanici parcasi (EntityId bilinir, or. kolon), `partB` = owned levha icin `partBRole` (roleKey; EntityId commit'te atanir). Materializer `prepare()` referanslari dogrular (EntityId VEYA roleKey sart), `commit()/recompute()` `resolveWeldPart` ile roleKey→EntityId cozup `buildWeldSolids` + `doc.addWeld` ile GERCEK fillet katisi uretir. Gecersiz birlesimde kayit metadata olarak eklenir ama `geometrySupported=false` + `unsupportedReason` dolu (sessiz basari YOK). `WeldSpec.throat` legacy; generator `size` (bacak) kullanir.
- Diyalog kaynak tablosu → generator baglantisi: `openComponentParamsDialog` OK'te `resultWeldRows()` okuyup `createWelds` (en az bir satir acik mi) + `weldSize` (ilk acik satirin olcusu) degerlerini EKSTRA parametre olarak iter. Bunlari `type->params`'a EKLEME — semada olmayan parametre `alignParamsToSpecs` tarafindan KORUNUR (kaybolmaz) ve generator `paramAsBool/paramAsDouble` ile okur; boylece dialogda grup/sekme eklemeye ve "Diğer" sekmesi UB'sine gerek kalmaz.
- SENTETIK 146 FIXTURE TUZAGI: `occ_shear_plate_146_end_to_end_test` kolonu Top-ankrajli ekstruze eder (kati y[-300,0]) ama MemberGeometry centroid varsayar (`sectionCenterOffset=0`) → tab kolon katisina 150mm uzakta DEGMEZ, kaynak dogru olarak "temas yok" der. Gercek uygulama `memberGeometryFor` ile `sectionCenterOffset`'i dogru kurar (kullanici modelinde kaynak uretildi). Bu fixture'da `geometrySupported` iddia ETME; kayit bagini (welds.size, partA=kolon, partBRole, size) dogrula. Kati uretimini DOKAN kutularla ayri testte dogrula (`component_materializer_test` Weld bolumu: 100 kutu + x[50,60] levha → hacim 0.5*leg^2*100).

## HAUNCH 40 — bağımsız üst/alt bulon grupları (kalıcı)

**EĞİK KİRİŞ (kalıcı kural):** Haunch 40 bağlantı çerçevesi kiriş eksenine DİK DEĞİL, kolon yüzü normali yönündeki YATAY `ax` ekseniyle kurulur (`ax = normalize({lenDir.x,lenDir.y,0})`, `tanT = lenDir.z/hlen`, `cosT = hlen`). Alın levhası ve kiriş kesimi DÜŞEY (plumb) olmalı — levha kolon yüzüne OTURUR; aksi halde eğik kirişte alt kenar kolonun içine girer, üst kenar açıkta kalır ve haunch kolondan kopuk görünür. Gövde/basınç flanşı düşey ön kenarlı gerçek dörtgendir: `PlateSpec::outline` serbest konturu (noktalar levha merkezine göre (u,v), CCW) kullanılır; materializer + canlı önizleme + imalat konturu AYNI konturu okur. Temas noktası düşey kolon yüzü düzlemiyle kesiştirilir (resolveShearTabPlacement kirişe dik düzlem kullanır → eğikte 2-3 mm kayar). Yatay kirişte `ax == dir, tanT == 0, cosT == 1` olduğu için eski davranış BİREBİR korunur; regresyon `tests/haunch_inclined_test.cpp` (10 yukarı / 10 aşağı / yatay).

**PROFİL DONUSU (kalıcı kural):** `MemberGeometry.profileRotation` kesit cercevesine UYGULANMALI — `rotateSectionFrame` (90°'de genislik↔yukseklik), hem kiris hem kolon cercevesine `resolveShearTabPlacement` icinde. Donus yok sayilirsa donuk kolonda alin levhasi kolon yuzunden ~110 mm iceri atar. Ayrıntı: `references/haunch40-independent-groups.md`.

**EAVES / ÜST BAĞLANTI (kalıcı kural):** Kiriş kolon tepesindeyken (kiriş ekseni ≈ kolon ucu) alın levhası kolon ucundan YUKARI TAŞMAZ — taşan kısmın arkasında flanş yoktur. Üst kenar kolon ucuna kırpılır, ALT kenar sabit kalır; bulon grupları yeni yükseklikten konumlanır. Kapak levhası (`haunch_top_plate`) DÜNYA-YUKARI değil KOLON EKSENİNE DİK olmalı (hafif eğik kolonda yamuk oturur); eğim sınırı 0.50. Kanıt: `tests/haunch_inclined_test.cpp` eaves + 5° eğik kolon blokları.

Üst (`haunch_bolts_upper`) ve alt (`haunch_bolts_lower`) gruplar BAĞIMSIZDIR. Düşey mesafe grup başına NET (plaka kenarı → en dış deliğin DIŞ kenarı); merkez = mesafe + delikÇapı/2. Her mesafe yalnız kendi grubunu kaydırır; plakayı boyutlandırma. Toplam bulon = üst.nx·ny + alt.nx·ny. Eski `boltEdgeTop/Bottom` merkez ölçüsüydü → migrasyonda delik yarıçapını çıkar, kayıtlı bağımsız ayarı ezme. `resultParams()` değer döndürür (geçici vektörden işaretçi alma). Ayrıntı + test listesi: `references/haunch40-independent-groups.md`.

**SİMETRİK MERTEK 180° ROLL (kalıcı kural, MM-4b/D):** haunch'ın "alt tarafı" ve üst/alt bulon sırası kesit çerçevesinin işaretine/rotation'a DEĞİL `worldUp`'a bağlıdır — `haunchFrameAnalysis` g.H aşağı bakarsa g.W/g.H'yi ters çevirir (`−g.H` hep aşağı). `memberGeometryFromProfile` çift-simetrik kesitte (centerOffset==0) rotation'ı `[0,180)`'e indirir (180→0, 270→90); asimetrikte (centerOffset!=0) `[0,360)` kalır. İkisi birlikte HEA300 mertek 180° roll'ünün planı değiştirmemesini sağlar (Studio ST-4·2). Test: `mm_contract_haunch40_symmetric_roll_test`. Bu dönüş, kirişi 180° roll eden Studio adaptörünün (refNormal aşağı + rot 180) aynı MemberGeometry'yi vermesi içindir; digest yön duyarlı değildir.

## GL arka uç etiketi tuzağı (Display/görünüm ayarı)

`OpenGLRenderBackend` çizgi/yüz/kontur tamponları `contentRevision` etiketiyle anahtarlıdır; etiket aynıysa GPU'ya YENİDEN YÜKLEMEZ. Belgeyi değiştirmeyen bir görünüm ayarı (Display bulon OFF) için etiket olarak yalnız `document.revision()` verme — `gpuBatchCache_.contentTag()` (revizyon + statik parmak izi) ver; yoksa Renderer/CPU önbellekleri doğru olsa da açık GL görünüm bayat kalır, yeni 2B görünüm doğru çıkar (belirti: 'yeni görünümde doğru, açık görünümde değil'). Kaynak-düzeyi kilit: `cmake/check_display_apply_wiring.cmake`; mantık testi: `tests/display_apply_test.cpp` (sahte GL etiket arka ucu). Win32 TU çapraz kontrol: `x86_64-w64-mingw32-g++ -std=c++20 -fsyntax-only -DUNICODE -D_UNICODE -D_USE_MATH_DEFINES -I include -I /tmp/qstub src/application.cpp` (UNICODE olmadan sahte hata verir).

## Repo hygiene

- `build-debug/` (Windows .obj + CMakeCache) is committed even though `.gitignore` only covers `build/`.
  Don't add more build output; mention it when touching .gitignore. Bu host'ta Linux yapilandirmasi
  `build-hermes/` (artik `.gitignore`'da); `git status` kirli gorunuyorsa once oraya bak.
- Linux configure'i SISTEM OpenCASCADE'ini bulup generate'i dusurebilir (VTK hedefleri, MSYS2
  tuzaginin aynisi) → her zaman `-DCMAKE_DISABLE_FIND_PACKAGE_OpenCASCADE=ON` ile yapilandir,
  yoksa `cmake -S . -B <dir>` "Generate step failed" verir.
- Runtime loglari (`model-maker-render.log`, `-startup.log`, `-trim.log`) repo kokunde kalir ve
  `git add -A` ile commit'e sizar; `.gitignore`'da olduklarindan emin ol. Commit'e log/liste
  dosyasi karistigini gorursen `git rm --cached <dosya>` + `git commit --amend --no-edit` ile duzelt.
- `git status` should be clean after a Linux build; if not, something was built outside `build/`.
- Numbers drift on a small VM (2 vCPU / 908 MB): `projection_1m_ms` ~11 ms here vs ~6.8 ms on the report's
  machine. Compare before/after on the *same* machine, never against the report's absolute figures.
