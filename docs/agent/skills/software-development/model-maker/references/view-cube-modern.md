# MODERN VIEWCUBE (26 yon) — ayrintilar

Kullanici: "kendi view cube umuzu kullanalim, modernlestirelim".

## Once "ciziliyor mu" dogrula

`ViewCubeRenderer::draw` repoda HICBIR yerden cagrilmiyordu. Qt6 ribbon
yeniden yaziminda (commit `4f4a232`) `onViewCubePaint`, `invalidateViewCube`,
`viewCubeProc`, `handleViewCubeMessage`, `viewCube_` yalnizca BASLIKTA kaldi
(olu bildirim) ve `viewCubeRenderer_` uye olarak duruyordu. Buna ragmen tuvalin
sag-ustunde `ViewCube::containsWidget` tiklama bolgesi CANLIYDI: oraya tiklamak
`camera_.setView(...)` ile gorunumu ziplatiyor, suruklemek donduruyordu.
Ders: bir ozelligi "modernlestirmeden" once kodun GERCEKTEN cizip cizmedigini
olc; olu bildirim + canli tiklama bolgesi kombinasyonu sessiz bir tuzaktir.

## Katmanlar

- **Cekirdek** (`src/view_cube.cpp`, `model_maker_core`, Linux'ta kosar):
  `ViewCubeTarget` (kind = None/Face/Edge/Corner/Home + dx/dy/dz), 26 hedef
  tablosu (`kViewCubeEdgeDirections` 12 satir), `viewCubeTargetDirection`,
  `viewCubeTurntableAngles`, `viewCubeTargetRotation`, `viewCubeSlerpRotation`,
  `ViewCube::layout/hitTest/containsWidget`.
- **Renderer** (`src/view_cube_renderer.cpp`, yalniz Windows): modern panel +
  hover vurgusu. `draw(HDC, client, cursor, camera)` — `backend` parametresi
  kaldirildi (olu `unused-parameter` uyarisi).
- **Uygulama** (`src/application.cpp`): `applyViewCubeTarget`, WM_TIMER 9
  (animasyon karesi), `finishViewCubeAnimation`, `onCanvasPaint` cizim cagrisi,
  hover blogu (`viewCubeHover_` — yalniz hedef DEGISINCE tazele).

## Olceklendirme / tiklama

`kViewCubeScalePx = 31.0` (baslikta) — `layout`, `hitTest` ve renderer AYNI
degeri kullanir; literal tekrari tiklanan kenari cizilenden kaydirir.
`kViewCubeCornerRadiusPx = 13`, `kViewCubeEdgeHalfWidthPx = 11`: kup kenari
62 px oldugu icin yuz merkezi (31 px uzakta) ikisinin de disinda kalir.
Tiklama sirasi KOSE -> KENAR -> YUZ (kose en belirgin).

## Oryantasyon: 6 yuz = roll=0 turntable

`Camera::buildRotation(yaw, pitch, roll)` ile 6 standart gorunusun TAMAMI
`(yaw, pitch, 0)` ailesinden: Top (0,0), Bottom (pi,0), Front (0,pi/2),
Back (0,-pi/2), Left (pi/2,0), Right (-pi/2,0). ISO ise roll=2pi/3 tasir —
bu yuzden ISO hedefi `standardViewRotation(Isometric)`'a gider, turntable'a
DEGIL. Kenar/kose yonleri ayni aileden: `pitch = asin(d.y)`,
`yaw = atan2(-d.x, d.z)` (cp~0 ise yaw=0 — Front/Back kutbu).

Hedef rotasyonunun 3. SATIRI dunya bakis yonudur; testte en guclu invaryant
budur (26 hedefin hepsi saglar).

## Yuz kose kumesi NORMALIYLE uyusmali (gercek hata)

Orijinal `definitions` tablosunda `Top` y=+1 dortgenini `{3,2,6,7}`, `Front`
z=+1 dortgenini `{4,7,6,5}` kullaniyordu. Normal YALNIZCA gorunurluk icin
kullanildigi icin hata gizli kaldi; belirti: UST gorunusunde "ust yuz" dort
noktasi da ayni y'ye projekte olup EKRANDA BIR CIZGIYE coker ve yuz merkezine
tiklamak hicbir sey secmez. Dogru kumeler: Top `{4,5,6,7}` (z=+1), Front
`{3,2,6,7}` (y=+1). `faceIndexOfNormal` indeksleri `definitions` sirasiyla
birebir eslesmeli.

## Animasyon

- `Camera::rotationMatrix()` / `setRotationMatrix(R)`: ortogonalize eder,
  pivotu TEMIZLEMEZ, grid kilidini YENILEMEZ. Ara kareler bununla yazilir —
  aksi halde animasyon boyunca grid ailesi sicrar.
- Son kare `finishViewCubeAnimation`: Face -> `setView(face)` (Ctrl+1..7 ile
  ayni yol), Home -> `setView(Isometric)`, kenar/kose -> hedef rotasyonun
  satirlarindan `setViewBasis(right, up, center3D)` (kilit+pivot burada kurulur).
- slerp: matris -> quaternion (Shepperd) -> kisa yol (dot<0 ise isaret cevir) ->
  t>0.9995 ise dogrusal, degilse aci formulu. `t<=0` from, `t>=1` to.
- **Tuzak**: quaternion->matris terim/isaret hatalari saf X/Y/Z donuslerinde
  MASKELENIR (xy, wz gibi terimler sifir olur). Gidis-donus testini ISO gibi
  DORT bileseni de sifirdan farkli bir rotasyonla yap; yoksa mutasyon kacar.

## Test/harness notlari

- `tests/view_cube_targets_test.cpp`: 44 kontrol, OCC'siz, gercek ViewCube +
  gercek Camera. ctest `model_maker_view_cube_targets_test`.
- `tests/run_view_cube.py`: kablolama/sekil (kup ciziliyor mu, BitBlt yalniz
  panel mi, birakmada `applyViewCubeTarget` mi, olu bildirimler gitti mi,
  Top/Front kume regresyonu). ctest `model_maker_view_cube`.
- `tests/mutate_view_cube.py`: 13 mutasyon (10 logic, 3 shape); LOGIC
  mutasyonlari testi YENIDEN DERLEYIP kosar.
- Python harness'lari (`run_gumball_interaction.py`, `run_multi_viewport.py`)
  sahte `Application` uyelerini KENDILERI bildirir: uygulamaya yeni bir TIP ya
  da uye eklemek (or. `std::optional<ViewCubeTarget> viewCubePressedView_`,
  `window_`, `KillTimer` stub'i) harness'i da guncellemeyi gerektirir; yoksa
  ctest derleme hatasi verir.

## STANDART GORUNUS TABANI (2026-10 duzeltmesi)

Eski Euler tablosu (`yaw,pitch,roll=0`) YAN gorunuslerde up=+Y, ARKA'da up=-Z
veriyordu: goruntu 90 derece yan yatiyor/ters geliyordu. Kural: ON/ARKA/SOL/SAG
matrisi TABANDAN kurulur — `rotationFromBasis(right, up)`, satirlar =
`sag, yukari, izleyiciye-bakan`, `toward = up x right` (Front ile dogrulandi:
right=+X, up=+Z -> toward=+Y = on yuz normali, derinlik +1).
Olculen dogru tabanlar (Z hep yukari):

| Gorunus | right | up | duzlem |
|---|---|---|---|
| ON | +X | +Z | XZ |
| ARKA | -X | +Z | XZ (aynali) |
| SOL | +Y | +Z | YZ |
| SAG | -Y | +Z | YZ (aynali) |
| UST | +X | +Y | XY (plan) |
| ALT | -X | +Y | XY |

UST/ALT ve ISO Euler yolunda kalir (dogruydular). `buildRotation` private;
taban kurmak icin serbest `rotationFromBasis` kullan. Test:
`viewcube_layout_test` (6 yuz hedefi kendi yuzunu one getirir) +
`camera_default_orientation_tests`.

---

<!-- SKILL.md'den taşındı 2026-10-19 (gövde sadeleştirme) -->

## MODERN VIEWCUBE (26 yon) — once "ciziliyor mu" dogrula (v11)

`ViewCubeRenderer::draw` HICBIR yerden cagrilmiyordu (Qt6 ribbon yeniden
yaziminda `onViewCubePaint`/`invalidateViewCube`/`viewCubeProc`/
`handleViewCubeMessage`/`viewCube_` olu bildirim olarak kaldilar), AMA tuvalin
sag-ustunde `ViewCube::containsWidget` tiklama bolgesi CANLIYDI: tiklama
gorunumu ziplatiyor, surukleme donduruyordu. Bir UI ozelligini degistirmeden
once kodun gercekten CIZDIGINI dogrula; olu bildirim + canli tik bolgesi
sessiz bir tuzaktir.

- Kup `onCanvasPaint` icinde `renderer_->draw(...)` SONRASINDA cizilir; renderer
  YALNIZ kendi panel dikdortgenine dokunur (`BitBlt(target, panel.left, ...)`).
  Tuvali komple dolduran eski "ayri pencere" davranisi modeli SILER.
- Yuz hedefi `Camera::standardViewRotation(face)` (Ctrl+1..7 ile AYNI matris);
  kenar/kose hedefleri ayni roll=0 "turntable" ailesinden. 6 standart gorunusun
  TAMAMI (yaw,pitch,0) ailesindedir; ISO roll tasir -> `setView(Isometric)`.
  Ikinci bir oryantasyon kurali yazma.
- Testte en guclu invaryant: hedef rotasyon matrisinin 3. SATIRI = hedef yonu.
- Yuz kose kumesi NORMALIYLE uyusmali. Normal yalniz GORUNURLUK icin
  kullanildigi icin uyumsuzluk gizlenir; belirti: eksenel gorunuste (or. UST)
  yuz EKRANDA BIR CIZGIYE coker ve merkezine tiklamak secmez.
- Animasyon ara kareleri `Camera::setRotationMatrix` ile yazilir: grid kilidini
  ve pivotu DEGISTIRMEZ (degistirse animasyonda grid ailesi sicrar); son kare
  `setView`/`setViewBasis` ile kilidi kurar.
- Slerp terim/isaret hatalari saf X/Y/Z donuslerinde MASKELENIR; gidis-donus
  testini ISO gibi DORT bileseni de dolu bir rotasyonla yap.
- Python harness'lari (`run_gumball_interaction.py`, `run_multi_viewport.py`)
  sahte `Application` uyelerini KENDILERI bildirir: uygulamaya yeni TIP/uye
  eklemek harness'i da guncellemeyi gerektirir (yoksa ctest derlenmez).

Ayrintilar: `references/view-cube-modern.md`.
