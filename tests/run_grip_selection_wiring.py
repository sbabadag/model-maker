#!/usr/bin/env python3
"""Kontrol noktasi coklu secimi — UYGULAMA BAGLANTISI kaynak-sekli testi.

KULLANICI SARTI (Selahattin 2026-10-31):
  "control noktalarının multiple olarak seçilmesini istiyorum, yerdeğiştirme
   işlemleri toplu olarak bu noktalara uygulanacak; alt a basılıyken window
   selection demek önceki seçili kontrol point varsa bırak window içinde kalan
   noktaları seç; shift + window selection eskileri koru yenilerini ekle;
   ayrıca noktalar seçildikten sonra move delta komutu noktaları taşımak için
   ayrıca çalışsın; tabii sadece seçili profillerin kontrol noktaları açılır."

Saf kural (pencere/ekleme/ayiklama) tests/grip_selection_test.cpp'de GERCEK
kamera projeksiyonuyla olculur. Burada olculen sey Application kodunun bu
kurali DOGRU yerlere baglamasidir (Windows'a ozel fare yolu Linux'ta
derlenemedigi icin kaynak-sekli; diger run_*.py testleriyle ayni desen).

Kullanim: python3 tests/run_grip_selection_wiring.py
"""
from pathlib import Path
import sys

root = Path(__file__).resolve().parents[1]
app = (root / "src/application.cpp").read_text(encoding="utf-8")
renderer = (root / "src/renderer.cpp").read_text(encoding="utf-8")
header = (root / "include/model_maker/application.hpp").read_text(encoding="utf-8")

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


# --- 1) Alt + surukleme pencere secimini BASLATIR ----------------------------
down = body(app, "void Application::onLeftButtonDown(int x, int y)")
check("gripSelectionFirstCorner_ = POINT{x, y};" in down,
      "Alt+sol tik (tutamak isabeti yok) kontrol noktasi penceresini baslatiyor")
check("gripSelectionAdditive_ = shiftDown;" in down,
      "pencere modu Shift durumunu tasiyor (eskileri koru / birak)")
check("if (!selectedModels_.empty()) {" in down,
      "pencere yalniz SECILI profil varken baslar (secilebilir nokta yoksa bosuna mod yok)")

# --- 2) Birakmada tamamlama: surukleme = secim, tik = secimi birak ----------
up = body(app, "void Application::onLeftButtonUp(int x, int y)")
check("completeGripWindowSelection(first, POINT{x, y}, additive);" in up,
      "surukleme birakildiginda pencere secimi tamamlaniyor (additive aktarilir)")
check("clearSelectedGrips();" in up,
      "Alt+tik (surukleme yok) nokta secimini birakiyor")

# --- 3) Kural cekirdekten gelir (tek kaynak) ---------------------------------
complete = body(app, "void Application::completeGripWindowSelection(POINT first")
check("mm::gripsInWindow(" in complete,
      "pencere icerigi mm::gripsInWindow ile hesaplaniyor (tek kaynak kural)")
check("mm::mergeGripSelection(selectedGrips_, picked, additive)" in complete,
      "Alt = birak / Shift+Alt = ekle kurali mm::mergeGripSelection'dan geliyor")

# --- 4) Yalniz secili profillerin noktalari ---------------------------------
candidates = body(app, "std::vector<mm::GripCandidate> Application::gripCandidates() const")
check("for (const std::size_t index : selectedModels_)" in candidates,
      "aday noktalar YALNIZ secili modellerden uretiliyor")
check('m.properties().profileName.empty()' in candidates and "m.faces().empty()" in candidates,
      "adaylar yalniz profilli B-Rep katilari (cizgi/levha tutamak tasimaz)")
check("pruneSelectedGrips();" in body(app, "void Application::updateControls()", "\n    const auto check"),
      "kati secimden cikinca noktalari da dusuyor (her kontrol tazelemesinde)")

# --- 5) Move / Move Delta kumeyi tasir --------------------------------------
move = body(app, "void Application::commitTransformPoint(const Vec3& point)")
move_block = move[move.index("if (isMoveTransform(transformCommand_))"):
                  move.index("if (isMoveTransform(transformCommand_))") + 900]
check("moveSelectedGripsBy(displacement);" in move_block,
      "Move komutu nokta seciliyken kumeyi tasiyor (model degil)")
check("document_.moveModels(" in move,
      "kume bos oldugunda eski model tasima yolu KORUNUYOR (regresyon yok)")

apply = body(app, "void Application::applyLinearDelta(const Vec3& delta, int count)")
check("if (!copying && !selectedGrips_.empty()) {" in apply and
      "moveSelectedGripsBy(worldDelta);" in apply,
      "Move Delta (X/Y/Z paneli) noktalari toplu tasiyor (UCS deltasi dunyaya cevrilir)")
check(apply.count("pushUndoSnapshot") == 1,
      "delta yolunda undo kaydi tek yerde (kume tasima kendi kaydini acar)")

# --- 6) Toplu tasima TEK undo + TEK recompute turu --------------------------
move_fn = body(app, "void Application::moveSelectedGripsBy(const Vec3& displacement)")
check(move_fn.count("pushUndoSnapshot();") == 1,
      "N nokta icin TEK undo kaydi (nokta basina ayri adim yok)")
check("reExtrudeProfileGripCore(grip.solidIndex, grip.endIsTo, end + displacement," in move_fn,
      "her nokta cekirdekten geciriliyor (tek geometri yolu)")
check("recomputeEndPlateConnectionsFor(changedParts)" in move_fn,
      "bagli component'ler tek turda yenileniyor")
check("registerUndo=*/false" in move_fn,
      "toplu tasimada cekirdek undo ACMIYOR (tek adim korunur)")

# --- 7) Cizim: secili noktalar ayirt edilir + canli pencere ------------------
check("view.selectedGrips = selectedGrips_;" in app,
      "secim renderer'a aktariliyor")
check("view.gripSelectionFirstCorner = gripSelectionFirstCorner_;" in app,
      "canli pencere dikdortgeni renderer'a aktariliyor")
check("gripIsSelected(draft.selectedGrips, GripRef{gi, false})" in renderer and
      "gripIsSelected(draft.selectedGrips, GripRef{gi, true})" in renderer,
      "renderer secili noktalari farkli renk/halka ile ciziyor")
check("draft.gripSelectionFirstCorner" in renderer,
      "renderer kontrol noktasi penceresini ciziyor")

# --- 8) ESC kumeyi birakir ---------------------------------------------------
check("else if (!selectedGrips_.empty()) {" in app,
      "Esc aktif komut yokken nokta secimini birakiyor")

# --- 9) Baslikta kume durumu tek yerde --------------------------------------
check("std::vector<mm::GripRef> selectedGrips_;" in header,
      "Application nokta kumesini tek uye olarak tutuyor")
check("bool deltaMovesSelection{};" in header,
      "tek tutamak akisi ile kume tasima ayrimi ProfileGrip icinde tasiniyor")

print(f"\n{checks} kontrol, {failures} hata")
sys.exit(0 if failures == 0 else 1)
