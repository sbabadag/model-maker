# Camera & 3D Navigation (P14)

## Matrix convention (gotcha)
`Camera::R_` is row-major, world->camera: row0 = camera right, row1 = camera up,
row2 = view direction (toward viewer). `viewTransform(v) = {row0·v, row1·v, row2·v}`.
So `viewTransform(e_x)` returns a COLUMN `{R_[0],R_[3],R_[6]}`. To reconstruct a
ROW i use `{viewTransform(e_x)[i], viewTransform(e_y)[i], viewTransform(e_z)[i]}`.
Mixing these up silently passes `asin(R_[8])` checks but breaks right/up probes.

## Turntable orbit (MMB)
- yaw = world Z (dusey): right-multiply `rz{cy,-sy,0; sy,cy,0; 0,0,1}`.
- pitch = camera X (yatay): left-multiply `rx{1,0,0; 0,cp,-sp; 0,sp,cp}`.
- Elevation = `asin(R_[8])`. Pitch changes elevation EXACTLY by pitchDelta
  (up.z = cos(elevation) holds while up points up). Clamp pitchDelta to
  `[-kMaxOrbitPitch - elevation, kMaxOrbitPitch - elevation]` (89°) -> no flip.
- camera X stays horizontal forever under yaw-Z+pitch-X -> no roll drift.
- Constants: `Camera::kOrbitRadiansPerPixel` (0.3 deg/px), `kMaxOrbitPitch` (89°).

## Input bindings (application.cpp)
- MMB = orbit (turntable), Shift+MMB = pan, wheel = zoom (cursor-anchored, existing).
- R-key rotate center (Ctrl+drag) uses Camera::orbitPivot_ (persistent, drawn);
  MMB auto-pivot is separate `Application::orbitGesturePivot_` (transient, not drawn).
- F = focusOnSelection (fit3D + setOrbitPivot to selection bbox center, keeps view
  dir). Face3D letter moved F -> H (H was free).

## Test harnesses are source-shape grep
`tests/run_*.py` grep EXACT substrings in application.cpp/camera.cpp (e.g.
`mat3mul(rx, mat3mul(R_, rz))`, `clearTemporaryTracking()` within 900 chars of
`wParam == VK_ESCAPE`, `kCtrlRotateDragThresholdPx = 4;`). Changing a formula
token or moving a code block breaks them; update the .py string in the same commit.

## Build/verify reality
- camera.cpp / drafting / core: compilable + testable on Linux (ctest build/).
- application.cpp: Win32+Qt, CANNOT compile on Linux. Verify by careful review;
  run full `ctest --test-dir build` after any camera/header change (harnesses
  re-read headers).

---

<!-- SKILL.md'den taşındı 2026-10-19 (gövde sadeleştirme) -->

## Ekran donus merkezi (R tusu) — pivot etrafinda donus

- **R tusu** = merkez secme komutu (`startRotateCenterCommand`): nokta secme akisini
  `workPlanePicking_` + `PointPickPurpose::RotateCenter` ile PAYLASIR (snap/track/Esc/sag tik
  iptali bedava gelir); tek nokta yeter, `workPlanePoints_` bossa lastik onizleme cikmaz.
  Kilitli gorunus penceresinde (`viewRotationLocked()`) yon sabit oldugu icin reddedilir.
- **Ctrl+sol tus surukleme** = o merkez etrafinda donus. Jest **basmada beklemede kalir**
  (`ctrlRotatePending_`) ve `kCtrlRotateDragThresholdPx` (4) asilinca `rotating_`'e terfi eder;
  esik asilmazsa **Ctrl+TIK coklu secimi aynen calisir**. Ctrl+sol tus sirasinda `SetCapture`
  sart (pencere disinda kopmasin). Esc / `WM_CAPTURECHANGED` / `activateViewport` bekleyeni temizler.
- Eski "gorunumu sifirla" davranisi **Ctrl+R**'ye ve menu ogresine tasindi (`resetView()` tek
  kaynak; kilitli pencerede yalnizca `fitViewport`).
- Pivot **Kamera icinde** saklanir (`Camera::orbitPivot_`), Application'da DEGIL: boylece viewport
  takasinda kendiliginden tasinir ve "viewport-basina alan" dort temas noktasina DOKUNMAK GEREKMEZ.
  `reset()/setView()/setViewBasis()` pivotu TEMIZLER (bayat pivot goruntuyu savurur); pan/zoom
  KORUR (pivot bir dunya noktasidir).
- **Matematik**: `Camera::rotateAbout(pivot,yaw,pitch)` — pivot EKRANDA sabit kalir.
  Projeksiyon ortografik oldugu icin sart: `R_yeni*(pivot - merkez_yeni) = R_eski*(pivot - merkez_eski)`
  => `merkez_yeni = pivot + W^T*(merkez_eski - pivot)`, `W = R_eski^T * R_yeni`.
- **SAYISAL TUZAK (olculdu)**: W'yi dogrudan `w = R^T*rx*R*ry` olarak KURMAK `R^T*R` carpimi
  yuzunden ortonormallik hatasini her adimda ~3-4 KATINA buyutur (adim 2: 5.5e-17 -> adim 19:
  2.0e-07) ve 60 adimda R bozulup pivot 2.8e4 px savrulur. Kararli formul: yon guncellemesi
  `rotate()` ile AYNI (`R_ = rx*R_*ry`), merkez `W^T = R_yeni^T*R_eski` ile (iki temiz matris).
  Belge: `tests/orbit_pivot_stability_probe.cpp`; regresyonu 500 adimlik dizi testi yakalar.
- Pivot isareti (capraz + halka) referans katmaninda, `DraftView::orbitPivotVisible/orbitPivot`
  dunya noktasi olarak gecer; renderer **kendi kamerasiyla** projekte eder (`projectPointPrecise`),
  pasif pencere `slot.camera`'dan doldurur. Secilemez/snap'lenemez.
