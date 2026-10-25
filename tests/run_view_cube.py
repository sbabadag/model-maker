#!/usr/bin/env python3
"""VIEWCUBE WIRING/SHAPE regresyonu (Windows GUI derlemesi gerektirmez).

Neden ayri bir test: modern kupun DAVRANISI `view_cube_targets_test.cpp` ile
cekirdekte (Linux'ta kosar) olculuyor; ama "kup gercekten EKRANA ciziliyor mu"
ve "tiklamada animasyonlu hedef uygulanıyor mu" sorusu uygulama katmaninda.
Bu dosya o kablolamayi sekil olarak kilitler:

  * `onCanvasPaint` kubu ciziyor (3B ve ana gorunus; GL kompozitinden sonra)
  * `ViewCubeRenderer::draw` YALNIZ kendi panel dikdortgenine dokunuyor
  * birakma -> `applyViewCubeTarget` (dogrudan `camera_.setView` DEGIL)
  * WM_TIMER 9 -> slerp karesi; hover hedefi degisince kare tazelenir
  * yuz kose kumeleri NORMALIYLE uyusuyor (Top/Front karisikligi regresyonu)
  * olu (hic cagrilmayan) ViewCube bildirimleri kaldirildi

Kullanim: python3 tests/run_view_cube.py
"""
from pathlib import Path
import re
import sys

root = Path(__file__).resolve().parents[1]
app = (root / "src/application.cpp").read_text(encoding="utf-8")
apph = (root / "include/model_maker/application.hpp").read_text(encoding="utf-8")
vc = (root / "src/view_cube.cpp").read_text(encoding="utf-8")
vch = (root / "include/model_maker/view_cube.hpp").read_text(encoding="utf-8")
vcr = (root / "src/view_cube_renderer.cpp").read_text(encoding="utf-8")
vcrh = (root / "include/model_maker/view_cube_renderer.hpp").read_text(encoding="utf-8")
cam = (root / "src/camera.cpp").read_text(encoding="utf-8")
camh = (root / "include/model_maker/camera.hpp").read_text(encoding="utf-8")

checks = 0
failures = 0


def check(ok: bool, what: str) -> None:
    global checks, failures
    checks += 1
    print(("PASS " if ok else "FAIL ") + what)
    if not ok:
        failures += 1


def slice_between(text: str, start: str, end: str) -> str:
    a = text.find(start)
    if a < 0:
        return ""
    b = text.find(end, a)
    return text[a:b if b > 0 else len(text)]


# --- 1) CIZIM: kup tuval boyamasinda, GL kompozitinden SONRA, dogru kapida --
paint = slice_between(app, "void Application::onCanvasPaint()", "\nvoid Application::onLeftButtonDown")
check("viewCubeRenderer_.draw(" in paint, "onCanvasPaint kubu ciziyor")
check(re.search(r"if \(mode_ == EditMode::View3D && !viewDef_\)\s*\n\s*viewCubeRenderer_\.draw\(",
                paint) is not None,
      "kup kapisi: yalniz 3B ve ana gorunus (gorunus penceresinde yon sabit)")
draw_idx = paint.find("viewCubeRenderer_.draw(")
composite_idx = paint.find("renderer_->draw(")
check(0 < composite_idx < draw_idx, "kup GL/GDI kompozitinden SONRA cizilir (ustte kalir)")

# --- 2) RENDERER: yalniz kendi paneli, arka tampon, ekran-olcekli -------------
draw_body = slice_between(vcr, "void ViewCubeRenderer::draw(", "\n} // namespace mm")
check("BitBlt(target, panel.left, panel.top" in draw_body,
      "renderer yalniz PANEL dikdortgenini tuvalе basar (model silinmez)")
check("FillRect(target" not in draw_body,
      "renderer TUVALI komple doldurmuyor (eski ayri-pencere davranisi kalkti)")
check("canvasBackgroundColor()" in draw_body,
      "panel arka plani tek kaynaktan (canvasBackgroundColor)")
check("kViewCubeScalePx" in vcr and "31.0" not in vcr,
      "kup olcegi tek kaynak (kViewCubeScalePx; literal 31.0 yok)")
check("RoundRect(dc, panel.left" in draw_body, "panel yuvarlatilmis (modern gorunum)")
check("viewCubeRenderer_.draw" in paint and "IRenderBackend" not in vcrh,
      "draw() olu `backend` parametresini tasimiyor")

# --- 3) TIKLAMA -> ANIMASYONLU HEDEF ----------------------------------------
check("applyViewCubeTarget(*viewCubePressedView_)" in app,
      "birakma -> applyViewCubeTarget (animasyonlu gecis)")
check("camera_.setView(*viewCubePressedView_)" not in app,
      "eski dogrudan setView cagrisi kalmadi")
check("void Application::applyViewCubeTarget(" in app,
      "applyViewCubeTarget tanimli")
check("void Application::finishViewCubeAnimation(" in app,
      "finishViewCubeAnimation tanimli (son kare: kilit/pivot)")
check("void applyViewCubeTarget(const ViewCubeTarget& target);" in apph and
      "void finishViewCubeAnimation();" in apph,
      "yeni metotlar baslikta bildirilmis")
check("std::optional<ViewCubeTarget> viewCubePressedView_;" in apph,
      "basilan hedef tipi yuz/kenar/kose ayrimli (ViewCubeTarget)")

# --- 4) TIMER 9: slerp karesi ----------------------------------------------
timer = slice_between(app, "if (wParam == 9) {", "if (wParam == 8) {")
check("viewCubeSlerpRotation(" in timer and "camera_.setRotationMatrix(" in timer,
      "WM_TIMER 9: slerp ara karesi setRotationMatrix ile yazilir")
check("finishViewCubeAnimation();" in timer, "animasyon bitince kesin apply")
check("const double eased = t * t * (3.0 - 2.0 * t);" in timer,
      "smoothstep ile yumusak baslangic/bitis")
check("setRotationMatrix" in camh and "rotationMatrix() const noexcept" in camh,
      "Camera rotasyon matrisine erisim (animasyon icin)")
# Kritik: ara kareler grid kilidini/pivotu DEGISTIRMEMELI.
setrot = slice_between(cam, "void Camera::setRotationMatrix(", "\n}")
check("orthonormalize(r);" in setrot and "lockGridViewDirection" not in setrot and
      "orbitPivot_" not in setrot,
      "setRotationMatrix grid kilidini/pivotu DEGISTIRMEZ (animasyonda grid sicramaz)")

# --- 5) HOVER: yalniz hedef DEGISINCE tazele --------------------------------
check("viewCubeHoverValid_" in app and "!(hovered == viewCubeHover_)" in app,
      "hover hedefi degisince kare tazelenir (bosuna cizim yok)")

# --- 6) YUZ KOSE KUMELERI NORMALIYLE UYUSUYOR (gercek regresyon) -----------
top = re.search(r"StandardView::Top,\s*\{([0-9, ]+)\},\s*\{0\.0,\s*0\.0,\s*1\.0\}", vc)
front = re.search(r"StandardView::Front,\s*\{([0-9, ]+)\},\s*\{0\.0,\s*1\.0,\s*0\.0\}", vc)
check(top is not None and front is not None, "Top/Front yuz tanimlari bulundu")
if top and front:
    top_set = {int(x) for x in top.group(1).split(",")}
    front_set = {int(x) for x in front.group(1).split(",")}
    check(top_set == {4, 5, 6, 7}, "Top yuzu z=+1 dortgenini kullanir (normal ile uyumlu)")
    check(front_set == {3, 2, 6, 7}, "Front yuzu y=+1 dortgenini kullanir (normal ile uyumlu)")
    check(top_set != front_set, "Top ve Front AYNI dortgeni paylasmiyor")

# --- 7) 26 YON API'SI -------------------------------------------------------
check("enum class ViewCubeTargetKind { None, Face, Edge, Corner, Home };" in vch,
      "hedef turu: yuz/kenar/kose/ISO ayrimli")
check("kViewCubeEdgeDirections" in vch and "std::array<double, 9> viewCubeSlerpRotation" in vch,
      "kenar tablosu + slerp cekirdekte tanimli")
rows = re.findall(r"\{\{(-?\d), (-?\d), (-?\d)\}\}", vch)
check(len(rows) == 12, "kenar tablosu 12 satir")
if len(rows) == 12:
    zeros_ok = all(sum(1 for c in row if c == "0") == 1 for row in rows)
    distinct = len({tuple(r) for r in rows}) == 12
    check(zeros_ok, "her kenar yonunde TAM BIR sifir bilesen")
    check(distinct, "12 kenar yonu benzersiz")

# --- 8) YUZ HEDEFI Ctrl+1..7 ile AYNI (tek kaynak) --------------------------
check("= standardViewRotation(view);" in cam,
      "setView tek kaynaktan (standardViewRotation) — kup ile Ctrl+1..7 ayni")
check("Camera::standardViewRotation(target.face)" in vc,
      "yuz hedefi de AYNI fonksiyona gider")

# --- 9) OLU BILDIRIMLER KALDIRILDI ----------------------------------------
for dead in ("onViewCubePaint", "invalidateViewCube", "viewCubeProc", "handleViewCubeMessage",
             "HWND viewCube_{}"):
    check(dead not in apph, f"olu bildirim kaldirildi: {dead}")

print(f"\n{checks} kontrol, {failures} hata")
sys.exit(1 if failures else 0)
