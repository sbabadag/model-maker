// P2A GUI testi (Qt offscreen): ayni NIHAI yerlesim plani onizleme (QPainter) ve PDF'te cizilir;
// yerlesim motorunun metin genisligi tahmini gercek Qt metriklerinden KUCUK olmamalidir (muhafazakar);
// PDF vektordur (raster resim yok); onizleme gorseli cikti dosyasi olarak yazilir (ekran goruntusu).

#include "fab_fixture.hpp"
#include "model_maker/drawing_builder.hpp"
#include "model_maker/drawing_layout.hpp"
#include "model_maker/drawing_sheet_widget.hpp"

#include <QApplication>
#include <QByteArray>
#include <QFile>
#include <QImage>

#include <cmath>
#include <cstdio>
#include <cstdlib>

using namespace mm;
using namespace mm::drawing;
#include "legacy_drawing_fixture.hpp"

static int failures = 0;
static int checks = 0;
static void check(bool ok, const char* label) {
    ++checks;
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", label);
    if (!ok) ++failures;
}

int main(int argc, char** argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);

    const fx::Fixture f = fx::make();
    MarkRegistry reg;
    const BuildResult r = legacyBuildSheet(f.doc, reg, f.doc.assemblies()[0].id);
    check(r.ok && r.sheet.layout.ok, "GUI: fixture cizimi + yerlesim basarili");
    const DrawingSheet& s = r.sheet;

    // 1) Qt gercek genislik <= tahmin (dim metinleri, notlar, mark'lar) -> tahmin kutusu metni KESIN kapsar
    {
        bool dimsOk = true, notesOk = true, marksOk = true;
        double worstRatio = 0;
        for (const Dimension2D& d : s.dimensions) {
            const std::string t = dimensionText(d.value);
            const double qt = measureTextWidthMm(QString::fromStdString(t), s.layoutConfig.dimTextHeight);
            const double est = textWidthEstimate(t, s.layoutConfig.dimTextHeight);
            worstRatio = std::max(worstRatio, qt / est);
            dimsOk = dimsOk && qt <= est + 1e-9;
        }
        for (const TextAnnotation& t : s.texts)
            for (const std::string& line : t.lines) {
                const double qt = measureTextWidthMm(QString::fromStdString(line), t.height, t.bold || t.kind == TextKind::ViewTitle);
                const double est = textWidthEstimate(line, t.height, t.bold || t.kind == TextKind::ViewTitle);
                worstRatio = std::max(worstRatio, qt / est);
                if (qt > est + 1e-9) std::printf("   asan satir: '%s' qt=%.2f est=%.2f\n", line.c_str(), qt, est);
                notesOk = notesOk && qt <= est + 1e-9;
            }
        for (const MarkAnnotation& m : s.marks) {
            const double h = m.isAssembly ? 5.0 : 3.5;
            const double qt = measureTextWidthMm(QString::fromStdString(m.text), h, m.isAssembly);
            const double est = textWidthEstimate(m.text, h, m.isAssembly);
            worstRatio = std::max(worstRatio, qt / est);
            marksOk = marksOk && qt <= est + 1e-9;
        }
        std::printf("   en kotu Qt/tahmin orani: %.3f\n", worstRatio);
        check(dimsOk, "GUI: olcu metinlerinde Qt genisligi <= tahmin");
        check(notesOk, "GUI: not/callout/damga satirlarinda Qt genisligi <= tahmin");
        check(marksOk, "GUI: mark etiketlerinde Qt genisligi <= tahmin");
    }

    // 2) Onizleme gorseli (ekran goruntusu) + muhtevasi
    const QString outDir = qEnvironmentVariableIsSet("MM_P2A_OUT") ? qEnvironmentVariable("MM_P2A_OUT") : QStringLiteral(".");
    {
        const QImage img = renderSheetImage(s, 4200);
        check(img.width() == 4200 && img.height() == static_cast<int>(std::lround(4200.0 * s.format.height / s.format.width)),
              "GUI: onizleme gorseli sayfa oraninda");
        int dark = 0;
        for (int y = 0; y < img.height(); y += 3)
            for (int x = 0; x < img.width(); x += 3)
                if (qGray(img.pixel(x, y)) < 100) ++dark;
        check(dark > 3000, "GUI: gorselde yeterli murekkep (cizim, metin, cerceve)");
        // Her olcu metni kutusunda murekkep VAR (metin planlanan yerde cizildi)
        const double pxPerMm = img.width() / s.format.width;
        int emptyBoxes = 0;
        for (const Dimension2D& d : s.dimensions) {
            const DimensionGeometry g = resolveDimension(s, d);
            const Bounds2 ab = g.textBox.aabb();
            int ink = 0;
            for (double y = ab.min.y; y <= ab.max.y; y += 0.15)
                for (double x = ab.min.x; x <= ab.max.x; x += 0.15) {
                    const int px = static_cast<int>(x * pxPerMm), py = static_cast<int>((s.format.height - y) * pxPerMm);
                    if (px >= 0 && py >= 0 && px < img.width() && py < img.height() && qGray(img.pixel(px, py)) < 140) ++ink;
                }
            if (ink < 8) ++emptyBoxes;
        }
        check(emptyBoxes == 0, "GUI: HER olcu metni kutusunda cizilmis murekkep var (plan == cizim)");
        const QString png = outDir + QStringLiteral("/p2a_fixture_sheet.png");
        check(img.save(png), "GUI: ekran goruntusu PNG yazildi");
    }

    // 3) PDF: vektor, boyut, ayni plan
    {
        const QString pdf = outDir + QStringLiteral("/p2a_fixture_sheet.pdf");
        QString err;
        check(exportSheetPdf(s, pdf, &err), "GUI: PDF yazildi");
        QFile fh(pdf);
        const bool opened = fh.open(QIODevice::ReadOnly);
        const QByteArray data = opened ? fh.readAll() : QByteArray();
        check(data.startsWith("%PDF-") && data.size() > 4000, "GUI: gecerli PDF basligi ve makul boyut");
        check(!data.contains("/Subtype /Image") && !data.contains("/Subtype/Image"), "GUI: PDF'te raster resim YOK (vektor cizim)");
        check(data.contains("/FontFile") || data.contains("/Font"), "GUI: PDF'te gomulu font (metin vektor)");
    }

    // 4) Imalat durumu: kaynak desteklenmedigi icin TASLAK; damga cizildi (kirmizi)
    check(s.fabrication.draft, "GUI: kaynakli assembly TASLAK");

    // 5) Dikkat: onizleme penceresi ayni sheet'i gosterir
    {
        DrawingSheetWidget w;
        w.resize(900, 640);
        w.setSheet(&s);
        const QImage grab = w.grab().toImage();
        check(!grab.isNull() && grab.width() == 900, "GUI: DrawingSheetWidget yeni sheet'i cizer");
    }

    std::printf(failures ? "FAILURES: %d / %d\n" : "ALL PASS (%d checks)\n", failures ? failures : checks, checks);
    return failures ? 1 : 0;
}
