// EKRANIN ORTASINDA ILERLEME PENCERESI — Qt offscreen dogrulamasi.
//
// Kabul kosullari (urun gereksinimi):
//   * panel ekranin TAM ortasinda, pencere tum ekrani kaplar,
//   * baslik + asama etiketi + cubuk gorunur; belirli (total > 0) ve belirsiz (total <= 0) mod,
//   * report() cubugu ve etiketi GERCEKTEN gunceller,
//   * Esc pencereyi KAPATMAZ (islem yarim kalmaz); finish() kapatir ve idempotenttir,
//   * kapanistan sonra yeniden baslatilabilir (deger sifirlanir).

#include "model_maker/progress_overlay.hpp"

#include <QApplication>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QScreen>

#include <cmath>
#include <cstdio>
#include <string>

using mm::ui::ProgressOverlay;

static int failures = 0;
static int checks = 0;
static void check(bool ok, const std::string& label) {
    ++checks;
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", label.c_str());
    if (!ok) ++failures;
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    ProgressOverlay overlay;
    const QRect screen = overlay.screenRect();
    check(screen.width() > 0 && screen.height() > 0, "ekran dikdortgeni gecerli");

    check(!overlay.active(), "baslangicta pencere kapali (islem yokken gorunmez)");

    overlay.begin(QStringLiteral("Dosya a\u00e7\u0131l\u0131yor"), 10);
    check(overlay.active(), "begin -> pencere gorunur");
    check(overlay.titleText() == QStringLiteral("Dosya a\u00e7\u0131l\u0131yor"), "baslik metni dogru");
    check(overlay.barMaximum() == 10 && !overlay.barBusy(), "belirli cubuk: 0..10");
    check(overlay.barValue() == 0, "belirli cubuk sifirdan baslar");

    check(overlay.geometry() == screen, "pencere TUM ekrani kaplar (karartma)");

    const QPoint panelCenter = overlay.panelRect().center();
    const QPoint screenCenter = screen.center();
    check(std::abs(panelCenter.x() - screenCenter.x()) <= 2 &&
              std::abs(panelCenter.y() - screenCenter.y()) <= 2,
          "panel EKRANIN ortasinda (parent pencerenin degil)");
    check(overlay.panelRect().width() >= 340 && overlay.panelRect().width() <= 520,
          "panel genisligi makul aralikta");
    check(overlay.panelRect().width() < screen.width() && overlay.panelRect().height() < screen.height(),
          "panel ekrandan kucuk (tam ekrani kaplamaz)");

    overlay.report(3, 10, QStringLiteral("Profil kat\u0131lar\u0131 3/10"));
    check(overlay.barValue() == 3, "report -> cubuk degeri guncellendi (3)");
    check(overlay.detailText() == QStringLiteral("Profil kat\u0131lar\u0131 3/10"), "report -> asama etiketi guncellendi");

    overlay.report(10, 10, QStringLiteral("Profil kat\u0131lar\u0131 10/10"));
    check(overlay.barValue() == 10, "son adimda cubuk tam dolu");

    overlay.report(99, 10, QStringLiteral("tasma"));
    check(overlay.barValue() == 10, "total ustu done degeri kirpilir (cubuk tasmaz)");

    // Belirsiz mod: total <= 0
    overlay.begin(QStringLiteral("Kaydediliyor"), 0);
    check(overlay.barBusy(), "total=0 -> belirsiz (busy) cubuk");
    check(overlay.barValue() == 0, "busy modda deger sifir");

    overlay.report(0, 0, QStringLiteral("Yaz\u0131l\u0131yor"));
    check(overlay.barBusy() && overlay.detailText() == QStringLiteral("Yaz\u0131l\u0131yor"),
          "busy modda yalniz asama etiketi guncellenir");

    overlay.report(2, 4, QStringLiteral("Dosya yaz\u0131l\u0131yor"));
    check(!overlay.barBusy() && overlay.barMaximum() == 4 && overlay.barValue() == 2,
          "busy -> belirli moda gecis (toplam ogrenilince)");

    // Esc pencereyi kapatmaz.
    QKeyEvent esc(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(&overlay, &esc);
    check(overlay.active(), "Esc pencereyi KAPATMAZ (islem yarim kalmaz)");

    overlay.finish();
    check(!overlay.active(), "finish -> pencere kapandi");
    overlay.finish();
    check(!overlay.active(), "finish idempotent (ikinci cagri sorunsuz)");

    overlay.begin(QStringLiteral("Tekrar"), 5);
    check(overlay.active() && overlay.barValue() == 0 && overlay.barMaximum() == 5,
          "kapandiktan sonra yeniden baslatilabilir (deger sifirlanir)");
    overlay.finish();

    std::printf("\n%d/%d checks passed\n", checks - failures, checks);
    return failures == 0 ? 0 : 1;
}
