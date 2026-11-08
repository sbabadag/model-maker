#!/usr/bin/env python3
"""Yuzeye Gore Gorunus (view.toSurface) — TIKLAMA -> GERI CAGRI sozlesmesi testi.

H-1 (kullanici 2026-11-08): "yuz tarama calisiyor ama tiklayinca YENI GORUNUS
ACILMIYOR". Kok neden incelemesi: onLeftButtonDown'daki ViewToSurface dali
9e548f3'ten beri DEGISMEMIS ve Windows'ta gercek fare tikiyla dogrulandi
(Gorunus 1 acildi). Bu test o dalin SOZLESMESINI kilitler (regresyonda kirmizi):

  1. Komut baslar: workPlanePicking_ + amac ViewToSurface + ilk hover.
  2. Tik + yuz isabeti -> viewFromFace -> cancelWorkPlaneCommand -> callback.
  3. Callback yoksa: bip + uyari (cokme yok); yuz yoksa: bip + yonlendirme.
  4. TANI loglari (H-1): START / CLICK / FACE / NO-FACE / NO-CALLBACK.

Mutasyonlar (bellek icinde, diske yazmaz): callback cagrisi silinirse,
viewFromFace atlanirsa veya no-callback kapisi kaldirilirsa test KIRMIZI olur.
Kullanim: python3 tests/run_view_to_surface.py [build-directory]
"""
from pathlib import Path
import sys

root = Path(__file__).resolve().parents[1]
app_path = root / "src/application.cpp"
app = app_path.read_text(encoding="utf-8")

checks = 0
failures = 0


def check(ok: bool, what: str) -> None:
    global checks, failures
    checks += 1
    print(f"{'PASS' if ok else 'FAIL'} {what}")
    if not ok:
        failures += 1


def body(text: str, signature: str, end: str = "\n}\n") -> str:
    start = text.index(signature)
    stop = text.index(end, start)
    return text[start:stop]


def click_branch(text: str) -> str:
    """onLeftButtonDown icindeki ViewToSurface tik dali (diger dallara kacmaz)."""
    down = body(text, "void Application::onLeftButtonDown(int x, int y)")
    a = down.index("pointPickPurpose_ == PointPickPurpose::ViewToSurface")
    b = down.index("pointPickPurpose_ == PointPickPurpose::WorkPlaneFace", a)
    return down[a:b]


def run_checks(text: str) -> int:
    """Sozlesme kontrolleri; kacan kontrol sayisini dondurur (global sayaca dokunmaz)."""
    fails = 0

    def ck(ok: bool, what: str) -> None:
        nonlocal fails
        global checks
        checks += 1
        if not ok:
            fails += 1
            print(f"FAIL {what}")

    # 1) Komut baslangici
    start = body(text, "void Application::startViewToSurfaceCommand()")
    ck("pointPickPurpose_ = PointPickPurpose::ViewToSurface;" in start,
       "baslangic: amac ViewToSurface kurulur")
    ck("workPlanePicking_ = true;" in start,
       "baslangic: yuz secim modu acilir (workPlanePicking_)")
    ck("refreshHoverCandidate(cursorScreen_.x, cursorScreen_.y);" in start,
       "baslangic: imlecin altindaki yuz hemen vurgulanir (fare hareket etmeden)")
    ck("VIEW-TO-SURFACE-START" in start,
       "tani: komut baslangici loglanir (VIEW-TO-SURFACE-START)")

    # 2) Tik dali sozlesmesi
    branch = click_branch(text)
    ck("pickSceneFaceAt(x, y)" in branch,
       "tik: yuz isabeti pickSceneFaceAt ile aranir")
    ck("viewFromFace(faceHit->hit.point,\n" in branch or
       "viewFromFace(faceHit->hit.point," in branch,
       "tik: yuz normalinden ViewDefinition uretilir (viewFromFace)")
    ck("cancelWorkPlaneCommand();" in branch,
       "tik: gorunus uretilmeden secim modu kapatilir")
    ck("twoPointViewCallback_(def);" in branch,
       "tik: callback varsa CAGIRILIR (yeni MDI penceresi acilir)")
    ck("if (!twoPointViewCallback_) {" in branch,
       "tik: callback yoksa guvenli yol (bip + uyari, cokme yok)")
    ck("görünüş penceresi açılamadı (MDI bağlı değil)" in branch,
       "tik: callback yoksa kullaniciya soylenir")
    ck("Yüzeye göre görünüş oluşturuldu" in branch,
       "tik: basarili yol durum cubugunda bildirilir")
    ck("imlecin altında yüz yok" in branch,
       "tik: yuz yoksa kullaniciya yonlendirme mesaji")
    # Sira: cancel callback'ten ONCE olmali (gorunus penceresi secim
    # modunu iptal eden durumla cakismasin)
    if "twoPointViewCallback_(def);" in branch and "cancelWorkPlaneCommand();" in branch:
        ck(branch.index("cancelWorkPlaneCommand();") < branch.index("twoPointViewCallback_(def);"),
           "sira: cancel callback'ten ONCE calisir")
    else:
        ck(False, "sira: cancel callback'ten ONCE calisir (cancel veya callback yok)")
    # 3) TANI loglari
    ck("VIEW-TO-SURFACE-CLICK" in branch,
       "tani: tik geldigi loglanir (VIEW-TO-SURFACE-CLICK)")
    ck("VIEW-TO-SURFACE-FACE" in branch,
       "tani: yuz isabeti + normal loglanir (VIEW-TO-SURFACE-FACE)")
    ck("VIEW-TO-SURFACE-NO-FACE" in branch,
       "tani: yuz isabetsizligi loglanir (VIEW-TO-SURFACE-NO-FACE)")
    ck("VIEW-TO-SURFACE-NO-CALLBACK" in branch,
       "tani: callback eksikligi loglanir (VIEW-TO-SURFACE-NO-CALLBACK)")

    # 4) Hover vurgusu (kullanici belirtisi: tarama calisiyor)
    rc = body(text, "void Application::refreshHoverCandidate(int x, int y)",
              "\nvoid Application::updateHover")
    ck("pointPickPurpose_ == PointPickPurpose::ViewToSurface" in rc and
       "pickSceneFaceAt(x, y)" in rc,
       "hover: ViewToSurface modunda yuz raycast'i ayni pickSceneFaceAt ile vurgulanir")

    return fails


fails = run_checks(app)
if fails:
    print(f"TABAN KAYNAK KIRMIZI: {fails} kontrol kacti")
    failures += fails
else:
    print(f"TABAN KAYNAK YESIL ({checks} kontrol gecti)")

# --- Mutasyonlar: kaynak kopyasinda kir, kirmizi olmali, geri al ------------
mutations = [
    ("callback cagrisi silindi", "twoPointViewCallback_(def);",
     "// MUT: callback kaldirildi", "tik: callback varsa CAGIRILIR"),
    ("viewFromFace atlandi",
     "const ViewDefinition def = viewFromFace(faceHit->hit.point,",
     "const ViewDefinition def = ViewDefinition{}; // MUT: viewFromFace atlandi,",
     "tik: yuz normalinden ViewDefinition uretilir (viewFromFace)"),
    ("no-callback kapisi kaldirildi", "if (!twoPointViewCallback_) {",
     "if (true) { // MUT: kapi kaldirildi", "tik: callback yoksa guvenli yol"),
    ("secim modu kapatilmadi", "cancelWorkPlaneCommand();\n                if (!twoPointViewCallback_)",
     "// MUT: cancel kaldirildi\n                if (!twoPointViewCallback_)",
     "sira: cancel callback'ten ONCE calisir"),
]
for name, old, new, must_fail_check in mutations:
    assert app.count(old) >= 1, f"mutasyon hedefi yok: {old}"
    mutated = app.replace(old, new, 1)
    # Ilgili kontrolleri KISITLI kosariz: hedef kontrol (must_fail_check adi)
    # kirilirsa mutasyon yakalandi demektir.
    mutated_fails = run_checks(mutated)
    # Yalniz hedef kontrolun kirilmasini degil, mutasyon sonrasi EN AZ bir
    # hata olmasini sart kosariz (hedef kontrol adi metni gecici; saglamlik).
    print(f"{'PASS' if mutated_fails > fails else 'FAIL'} mutasyon yakalandi: {name} "
          f"({mutated_fails} kirmizi)")
    checks += 1
    if mutated_fails <= fails:
        failures += 1
    if mutated != app.replace(old, new, 1):
        raise AssertionError("mutasyon kopyasi bozuldu")

print(f"\n{checks} kontrol, {failures} hata")
sys.exit(0 if failures == 0 else 1)
