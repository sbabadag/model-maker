// P2C GUI testi (Qt offscreen): coklu pafta seti -> TEK vektor PDF (her pafta bir sayfa),
// ekran/PDF ayni cizim yolu, sayfa gezinme, gercek PDF render denetimi (pdfinfo/pdftoppm/pdftotext).

#include "fab_fixture.hpp"
#include "model_maker/drawing_layout.hpp"
#include "model_maker/drawing_set.hpp"
#include "model_maker/drawing_sheet_widget.hpp"

#include <QApplication>
#include <QByteArray>
#include <QCheckBox>
#include <QPushButton>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QLabel>
#include <QProcess>
#include <QTemporaryDir>

#include <cmath>
#include <cstdio>
#include <string>

using namespace mm;
using namespace mm::drawing;

static int failures = 0;
static int checks = 0;
static void check(bool ok, const std::string& label) {
    ++checks;
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", label.c_str());
    if (!ok) ++failures;
}

static fx::Fixture makeAssembly(double len, int extraPlates) {
    fx::Fixture f = fx::make({}, len, true);
    for (int i = 0; i < extraPlates; ++i) {
        const double x = 200.0 + 130.0 * i;
        const EntityId p = fx::addFramedPlate(f, {x, 80, 0}, {1, 0, 0}, {0, 0, 1}, {0, 1, 0}, 300, 200, 10,
                                              ("gui_levha_" + std::to_string(i)).c_str());
        fx::addBolts(f, p, {x - 100, 80, 30}, {0, 1, 0}, {1, 0, 0}, 3, 2, 100, 60, 22);
        Weld w;
        w.mainPart = f.beam;
        w.secondaryPart = p;
        w.weldClass = WeldClass::Shop;
        f.doc.addWeld(w);
    }
    return f;
}

static QString runTool(const QString& program, const QStringList& args, bool* ok) {
    QProcess p;
    p.start(program, args);
    if (!p.waitForFinished(60000)) {
        if (ok) *ok = false;
        return {};
    }
    if (ok) *ok = p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0;
    return QString::fromLocal8Bit(p.readAllStandardOutput());
}

static bool hasInk(const QImage& img) {
    if (img.isNull()) return false;
    long long dark = 0;
    for (int y = 0; y < img.height(); y += 2)
        for (int x = 0; x < img.width(); x += 2) {
            const QRgb c = img.pixel(x, y);
            if (qRed(c) < 245 || qGreen(c) < 245 || qBlue(c) < 245) ++dark;
        }
    return dark > 50;
}

int main(int argc, char** argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);

    fx::Fixture f = makeAssembly(2400.0, 2);
    MarkRegistry reg;
    const EntityId subject = f.doc.assemblies().empty() ? 0 : f.doc.assemblies()[0].id;
    DrawingOptions o;
    o.layout.fixedScaleDen = 5.0;
    DrawingPreferences p;
    p.autoMax = SheetSize::A2;
    // 2026-10-29: pafta bolme varsayilan DEGIL; bu test bolme makinesini dogrular.
    p.autoMultiSheetFallback = true;
    p.allowOverflow = false;

    const DrawingSetResult r = buildDrawingSet(f.doc, reg, subject, o, p);
    check(r.ok && r.document.sheets().size() > 1 && r.layoutValid,
          "GUI: coklu pafta seti uretildi ve TUM paftalar gecerli");
    const DrawingDocument& doc = r.document;
    const std::size_t pages = doc.sheets().size();

    QTemporaryDir dir;
    check(dir.isValid(), "GUI: gecici dizin");
    const QString pdfPath = dir.filePath("set.pdf");

    // 1) Vektor PDF: tek dosya, N sayfa.
    {
        QString err;
        check(exportDrawingSetPdf(doc, pdfPath, &err), "GUI: cok-sayfali PDF yazildi " + err.toStdString());
        QFile file(pdfPath);
        check(file.open(QIODevice::ReadOnly), "GUI: PDF dosyasi acildi");
        const QByteArray bytes = file.readAll();
        check(bytes.size() > 4000, "GUI: PDF bos degil");
        const int pageObjs = bytes.count("/Type /Page") - bytes.count("/Type /Pages");
        check(pageObjs == static_cast<int>(pages), "GUI: PDF sayfa nesnesi sayisi = pafta sayisi");
        // Vektor: gomulu raster goruntu (XObject Image) YOK.
        check(!bytes.contains("/Subtype /Image"), "GUI: PDF vektor (gomulu raster goruntu yok)");
    }

    // 2) pdfinfo: sayfa sayisi + her pafta kendi kagit boyutunda.
    bool havePoppler = false;
    {
        bool ok = false;
        const QString info = runTool("pdfinfo", {pdfPath}, &ok);
        havePoppler = ok && !info.isEmpty();
        if (havePoppler) {
            check(info.contains(QStringLiteral("Pages:")) && info.contains(QString::number(pages)),
                  "GUI: pdfinfo sayfa sayisini dogruladi");
            bool okPages = false;
            const QString perPage = runTool("pdfinfo", {"-f", "1", "-l", QString::number(pages), pdfPath}, &okPages);
            int matched = 0;
            bool sizesOk = true;
            for (const QString& line : perPage.split(QLatin1Char('\n'))) {
                if (!line.startsWith(QStringLiteral("Page ")) || !line.contains(QStringLiteral("size:"))) continue;
                if (matched >= static_cast<int>(pages)) break;
                const DrawingSheet& s = doc.sheets()[static_cast<std::size_t>(matched)];
                const QString nums = line.section(QStringLiteral("size:"), 1).section(QStringLiteral("pts"), 0, 0);
                const QStringList wh = nums.split(QLatin1Char('x'));
                if (wh.size() == 2) {
                    const double w = wh[0].trimmed().toDouble();
                    const double h = wh[1].trimmed().toDouble();
                    const double wantW = s.format.width * 72.0 / 25.4;
                    const double wantH = s.format.height * 72.0 / 25.4;
                    sizesOk = sizesOk && std::fabs(w - wantW) < 2.0 && std::fabs(h - wantH) < 2.0;
                } else {
                    sizesOk = false;
                }
                ++matched;
            }
            check(okPages && matched == static_cast<int>(pages) && sizesOk,
                  "GUI: her PDF sayfasinin kendi kagit boyutu var (A2/A3)");
        } else {
            std::printf("SKIP pdfinfo yok (Windows'ta poppler yok)\n");
        }
    }

    // 3) pdftoppm ile GERCEK raster: her sayfada cizim var (bos sayfa yok).
    if (havePoppler) {
        bool ok = false;
        runTool("pdftoppm", {"-r", "60", "-png", pdfPath, dir.filePath("page")}, &ok);
        const QStringList imgs = QDir(dir.path()).entryList({"page-*.png"}, QDir::Files, QDir::Name);
        check(ok && imgs.size() == static_cast<int>(pages), "GUI: pdftoppm her pafta icin goruntu uretti");
        int withInk = 0;
        for (const QString& n : imgs) {
            QImage img(dir.filePath(n));
            if (hasInk(img)) ++withInk;
        }
        check(withInk == static_cast<int>(pages), "GUI: her PDF sayfasinda baski icerigi var (bos sayfa yok)");
    }

    // 4) pdftotext: olcu/pafta metinleri vektor metin olarak okunabiliyor (okunabilirlik).
    if (havePoppler) {
        bool ok = false;
        const QString text = runTool("pdftotext", {"-layout", pdfPath, "-"}, &ok);
        check(ok && !text.trimmed().isEmpty(), "GUI: PDF metin katmani var");
        check(text.contains(QStringLiteral("1:5")), "GUI: PDF'te olcek yazisi (1:5) okunuyor");
        check(text.contains(QString::fromStdString(doc.sheets().front().sheetNumber)),
              "GUI: PDF'te ana pafta numarasi okunuyor");
        bool linkedNumber = false;
        for (std::size_t i = 1; i < pages; ++i)
            linkedNumber = linkedNumber || text.contains(QString::fromStdString(doc.sheets()[i].sheetNumber));
        check(linkedNumber, "GUI: PDF'te bagli pafta numarasi okunuyor");
        check(text.contains(QStringLiteral("300")), "GUI: PDF'te gercek imalat olcusu (300) okunuyor");
    }

    // 5) Ekran yolu (renderSheetImage) her paftada ayni nihai plani cizer: dolu ve birbirinden farkli.
    {
        QImage prev;
        bool ink = true, differ = true;
        for (const DrawingSheet& s : doc.sheets()) {
            const QImage img = renderSheetImage(s, 700);
            ink = ink && hasInk(img);
            if (!prev.isNull()) differ = differ && (img != prev);
            prev = img;
        }
        check(ink, "GUI: onizleme (QPainter) her paftada cizim uretiyor");
        check(differ, "GUI: paftalar farkli icerik ciziyor (kopya degil)");
    }

    // 6) Diyalog: pafta secici + set dokumani; secim gorunumu degistirir.
    {
        MarkRegistry dlgReg;
        std::vector<SubjectChoice> subjects{{QStringLiteral("Assembly"), subject}};
        DrawingPreviewDialog dlg(
            subjects, 0,
            [&f, &dlgReg](EntityId s, const DrawingOptions& opt) { return buildSheet(f.doc, dlgReg, s, opt); },
            nullptr,
            [&f, &dlgReg](EntityId s, const DrawingOptions& opt, const DrawingPreferences& prefs,
                          const ProgressCallback&) {
                // Test: kullanici olcegi sabitledi + kagit siniri A2 (diyalogda sabit olcek yok; API ile verilir).
                DrawingOptions o2 = opt;
                o2.layout.fixedScaleDen = 5.0;
                DrawingPreferences p2 = prefs;
                p2.autoMax = SheetSize::A2;
                // 2026-10-29: bolme varsayilan DEGIL; bu test bagli-coklu-pafta setini dogrular.
                p2.autoMultiSheetFallback = true;
                p2.allowOverflow = false;
                return buildDrawingSet(f.doc, dlgReg, s, o2, p2);
            });
        // 2026-10-28: diyalogda "Tum gorunusler ayni olcek" varsayilan ACIK; bu
        // testin referansi (pages) varsayilan (detayli) modla uretildi -> eslestir.
        if (auto* uniform = dlg.findChild<QCheckBox*>(QStringLiteral("drawingUniformScale"))) {
            uniform->setChecked(false);
            if (auto* applyBtn = dlg.findChild<QPushButton*>(QStringLiteral("drawingApply"))) applyBtn->click();
            QApplication::processEvents();
        }
        const DrawingDocument* shownDoc = dlg.currentDocument();
        check(shownDoc && shownDoc->sheets().size() == pages, "GUI: diyalog DrawingDocument setini tasiyor");
        auto* combo = dlg.findChild<QComboBox*>(QStringLiteral("drawingSheetPage"));
        check(combo && shownDoc && combo->count() == static_cast<int>(shownDoc->sheets().size()),
              "GUI: diyalog pafta secici tum paftalari listeliyor");
        if (combo && combo->count() > 1 && shownDoc) {
            combo->setCurrentIndex(1);
            const DrawingSheet* shown = dlg.currentSheet();
            check(shown && shown->sheetNumber == shownDoc->sheets()[1].sheetNumber,
                  "GUI: pafta secimi dogru paftayi gosteriyor");
            auto* status = dlg.findChild<QLabel*>(QStringLiteral("drawingStatus"));
            check(status && status->text().contains(QStringLiteral("Pafta")),
                  "GUI: pafta secimi durum satirini guncelliyor");
        }
    }

    std::printf("\n%d/%d checks passed\n", checks - failures, checks);
    return failures == 0 ? 0 : 1;
}
