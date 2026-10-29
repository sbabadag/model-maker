// DRAWING GUI (P1) — Qt offscreen: sayfa render'i, PDF, onizleme diyalogu.
//
// Ortam: QT_QPA_PLATFORM=offscreen. MM_DRAWING_PNG=<dosya> verilirse render PNG olarak yazilir
// (gozle kontrol icin).

#include "model_maker/drawing_sheet_widget.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QPushButton>
#include <QComboBox>
#include <QFile>
#include <QImage>
#include <QLabel>
#include <QPainter>
#include <QTemporaryDir>

#include <cmath>
#include <cstdio>
#include <cstdlib>

using namespace mm;
using namespace mm::drawing;
#include "legacy_drawing_fixture.hpp"

static int failures = 0;
static void check(bool ok, const char* label) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", label);
    if (!ok) ++failures;
}

static WireframeModel makeBox(Vec3 lo, Vec3 hi, const char* profile, const char* name) {
    std::vector<Vec3> v;
    for (int i = 0; i < 8; ++i)
        v.push_back({(i & 1) ? hi.x : lo.x, (i & 2) ? hi.y : lo.y, (i & 4) ? hi.z : lo.z});
    std::vector<Edge> e;
    for (std::size_t i = 0; i < 8; ++i)
        for (std::size_t b = 1; b < 8; b <<= 1)
            if (!(i & b)) e.push_back({i, i | b});
    std::vector<Face> f{{0, 2, 6, 4}, {1, 3, 7, 5}, {0, 1, 5, 4}, {2, 3, 7, 6}, {0, 1, 3, 2}, {4, 5, 7, 6}};
    WireframeModel m(std::move(v), std::move(e), std::move(f));
    EntityProperties p;
    p.profileName = profile;
    p.name = name;
    p.axisFromX = lo.x; p.axisFromY = (lo.y + hi.y) / 2; p.axisFromZ = (lo.z + hi.z) / 2;
    p.axisToX = hi.x;   p.axisToY = (lo.y + hi.y) / 2;   p.axisToZ = (lo.z + hi.z) / 2;
    m.setProperties(p);
    return m;
}

static EntityId add(Document& doc, WireframeModel m) {
    doc.addModel(std::move(m));
    return doc.modelId(doc.models().size() - 1);
}

// Sayfa mm -> piksel (y ters), renderSheetImage'in kullandigi esleme.
static QPoint toPx(const DrawingSheet& s, const QImage& img, double x, double y) {
    const double k = img.width() / s.format.width;
    return QPoint(static_cast<int>(std::lround(x * k)), static_cast<int>(std::lround(img.height() - y * k)));
}
static int darkCount(const QImage& img, const QRect& r, int threshold = 110) {
    int n = 0;
    for (int y = std::max(0, r.top()); y <= std::min(img.height() - 1, r.bottom()); ++y)
        for (int x = std::max(0, r.left()); x <= std::min(img.width() - 1, r.right()); ++x)
            if (qGray(img.pixel(x, y)) < threshold) ++n;
    return n;
}

int main(int argc, char** argv) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);

    // --- model: ana kiris + iki uc levhasi (assembly) + bagimsiz tek parca ---
    Document doc;
    const EntityId beam = add(doc, makeBox({0, 0, 0}, {4000, 120, 240}, "IPE240", "Ana"));
    const EntityId pl1 = add(doc, makeBox({0, -10, 0}, {12, 130, 240}, "PL12", "Levha"));
    const EntityId pl2 = add(doc, makeBox({3988, -10, 0}, {4000, 130, 240}, "PL12", "Levha"));
    Weld w1, w2;
    w1.mainPart = beam; w1.secondaryPart = pl1;
    w2.mainPart = beam; w2.secondaryPart = pl2;
    doc.addWeld(w1);
    doc.addWeld(w2);
    const EntityId lone = add(doc, makeBox({0, 3000, 0}, {2500, 3150, 300}, "HEA300", "Tek"));
    check(doc.assemblies().size() == 1, "kurulum: 1 assembly");
    const EntityId asmId = doc.assemblies()[0].id;

    MarkRegistry reg;
    const BuildResult built = legacyBuildSheet(doc, reg, asmId);
    check(built.ok, "assembly cizimi uretildi");
    if (!built.ok) return 1;
    const DrawingSheet& sheet = built.sheet;

    // --- A) render ---
    const QImage img = renderSheetImage(sheet, 2100);
    check(img.width() == 2100 && img.height() == 1485, "A3 orani 420:297 -> 2100x1485");
    if (const char* png = std::getenv("MM_DRAWING_PNG")) img.save(QString::fromLocal8Bit(png));
    check(qGray(img.pixel(5, 5)) > 250, "kagit beyaz (cerceve disi)");
    // Cerceve: sol kenar (margin = 10 mm) koyu.
    {
        const QPoint c = toPx(sheet, img, sheet.format.margin, sheet.format.height / 2);
        check(darkCount(img, QRect(c.x() - 3, c.y() - 3, 7, 7)) > 0, "sayfa cercevesi cizili");
        // Title block icinde metin pikselleri var.
        const double tbx = sheet.format.width - sheet.format.margin - sheet.format.titleWidth;
        const QPoint a = toPx(sheet, img, tbx, sheet.format.margin + sheet.format.titleHeight);
        const QPoint b = toPx(sheet, img, sheet.format.width - sheet.format.margin, sheet.format.margin);
        check(darkCount(img, QRect(a, b)) > 400, "title block cizili (cizgi + metin)");
    }
    // Her gorunusun kagit siniri icinde koyu cizgiler var; Front'un sol-alt kosesinde cizgi kesisimi.
    for (const DrawingView& v : sheet.views) {
        const Bounds2 pb = v.paperBounds();
        const QPoint a = toPx(sheet, img, pb.min.x, pb.max.y);
        const QPoint b = toPx(sheet, img, pb.max.x, pb.min.y);
        check(darkCount(img, QRect(a, b).adjusted(-3, -3, 3, 3)) > 60, "gorunus bolgesinde cizgi pikselleri var");
    }
    {
        const DrawingView* f = sheet.viewOfKind(ViewKind::Front);
        const Bounds2 pb = f->paperBounds();
        const QPoint bl = toPx(sheet, img, pb.min.x, pb.min.y);
        check(darkCount(img, QRect(bl.x() - 3, bl.y() - 3, 7, 7)) > 0, "Front sol-alt kose dolu (y ekseni dogru yonde)");
        // Y ters cevrilmediyse (yukari = kucuk piksel y) Front'un ustu altindan yukarida: ust kenar cizgisi.
        const QPoint tl = toPx(sheet, img, pb.min.x + pb.width() * 0 + 5, pb.max.y);
        check(tl.y() < bl.y(), "kagit y yukari -> piksel y asagi (ters cevirme dogru)");
    }
    // Dimension / mark renkli (mavi / kirmizi-yesil) pikseller: renk sayimi.
    {
        int blue = 0, red = 0, green = 0;
        for (int y = 0; y < img.height(); y += 1)
            for (int x = 0; x < img.width(); x += 1) {
                const QRgb c = img.pixel(x, y);
                if (qBlue(c) > 120 && qRed(c) < 60 && qGreen(c) < 140) ++blue;
                if (qRed(c) > 130 && qGreen(c) < 70 && qBlue(c) < 70) ++red;
                if (qGreen(c) > 90 && qRed(c) < 70 && qBlue(c) < 90) ++green;
            }
        check(blue > 300, "olcu cizgileri/metinleri (mavi) cizili");
        check(red > 30, "assembly etiketi (kirmizi) cizili");
        check(green > 30, "parca etiketi (yesil) cizili");
    }
    // Tekrarlanabilir: ayni sayfa -> ayni piksel.
    check(renderSheetImage(sheet, 700) == renderSheetImage(sheet, 700), "render deterministik");

    // --- B) PDF ---
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("a1.pdf"));
        if (const char* keep = std::getenv("MM_DRAWING_PDF")) exportSheetPdf(sheet, QString::fromLocal8Bit(keep));
        QString err;
        check(exportSheetPdf(sheet, path, &err) && err.isEmpty(), "PDF yazildi");
        QFile f(path);
        const bool open = f.open(QIODevice::ReadOnly);
        const QByteArray head = open ? f.read(8) : QByteArray();
        check(open && head.startsWith("%PDF") && f.size() > 3000, "PDF gecerli baslik ve makul boyut");
        QString err2;
        check(!exportSheetPdf(sheet, dir.filePath(QStringLiteral("yok/klasor/a.pdf")), &err2) && !err2.isEmpty(),
              "yazilamayan yol -> false + hata");
    }

    // --- C) diyalog ---
    {
        int calls = 0;
        DrawingOptions last;
        EntityId lastSubject = kInvalidEntityId;
        MarkRegistry dlgReg;
        const auto builder = [&](EntityId subject, const DrawingOptions& o) {
            ++calls;
            last = o;
            lastSubject = subject;
            return legacyBuildSheet(doc, dlgReg, subject, o);
        };
        std::vector<SubjectChoice> subjects{{QStringLiteral("Assembly A1"), asmId},
                                            {QStringLiteral("Par\u00e7a: Tek"), lone}};
        DrawingPreviewDialog dlg(subjects, 0, builder);
        dlg.show();
        QApplication::processEvents();
        check(calls == 1 && dlg.currentSheet() && dlg.currentSheet()->kind == DrawingKind::Assembly,
              "C: acilista assembly cizimi uretildi (1 cagri)");
        auto* subjectCombo = dlg.findChild<QComboBox*>(QStringLiteral("drawingSubject"));
        auto* sizeCombo = dlg.findChild<QComboBox*>(QStringLiteral("drawingSize"));
        auto* projCombo = dlg.findChild<QComboBox*>(QStringLiteral("drawingProjection"));
        auto* hidden = dlg.findChild<QCheckBox*>(QStringLiteral("drawingHidden"));
        auto* sectionCheck = dlg.findChild<QCheckBox*>(QStringLiteral("drawingSectionViews"));
        auto* topCheck = dlg.findChild<QCheckBox*>(QStringLiteral("drawingTop"));
        auto* sideCheck = dlg.findChild<QCheckBox*>(QStringLiteral("drawingSide"));
        auto* frontCheck = dlg.findChild<QCheckBox*>(QStringLiteral("drawingFront"));
        auto* status = dlg.findChild<QLabel*>(QStringLiteral("drawingStatus"));
        // 2026-10-28: onay kutulari aninda uretmez; islem "Uygula" ile yapilir.
        auto* applyBtn = dlg.findChild<QPushButton*>(QStringLiteral("drawingApply"));
        const auto applyNow = [&]() {
            if (applyBtn) applyBtn->click();
            QApplication::processEvents();
        };
        check(subjectCombo && sizeCombo && projCombo && hidden && topCheck && sideCheck && frontCheck && status,
              "C: denetimler bulundu");
        if (!(subjectCombo && sizeCombo && projCombo && hidden && topCheck && sideCheck && frontCheck && status))
            return 1;
        check(status->text().contains(QStringLiteral("A1")) && status->text().contains(QStringLiteral("3 g\u00f6r\u00fcn\u00fc\u015f")),
              "C: durum satiri A1 ve 3 gorunus");

        check(sectionCheck && sectionCheck->isChecked() && last.sectionViews,
              "C: kesit kutusu varsayilan acik ve builder'a iletildi");
        if (sectionCheck) {
            const int beforeToggle = calls;
            sectionCheck->setChecked(false);
            check(calls == beforeToggle && applyBtn && applyBtn->isEnabled(),
                  "C: kesit kapatildi, Uygula bekleniyor");
            applyNow();
            check(!last.sectionViews, "C: kesit kapatma builder'a iletildi");
            sectionCheck->setChecked(true);
            applyNow();
            check(last.sectionViews, "C: kesit yeniden acildi");
        }
        subjectCombo->setCurrentIndex(1);
        check(dlg.currentSheet() && dlg.currentSheet()->kind == DrawingKind::SinglePart &&
                  lastSubject == lone && dlg.currentSheet()->name == "P3",
              "C: konu 'Parca: Tek' -> tek parca cizimi, P3 (modeldeki 3. farkli imza)");
        sizeCombo->setCurrentText(QStringLiteral("A4"));
        check(dlg.currentSheet() && std::fabs(dlg.currentSheet()->format.width - 297) < 1e-9 && last.sheet == SheetSize::A4,
              "C: kagit A4");
        projCombo->setCurrentIndex(1);
        check(last.projection == ProjectionMethod::FirstAngle && dlg.currentSheet() &&
                  dlg.currentSheet()->projection == ProjectionMethod::FirstAngle,
              "C: 1. aci");
        hidden->setChecked(true);
        check(applyBtn && applyBtn->isEnabled(), "C: onay kutusu isaretlendi -> Uygula etkin (aninda uretim yok)");
        applyNow();
        check(last.hiddenLines, "C: gizli cizgi secenegi Uygula ile builder'a gecti");
        topCheck->setChecked(false);
        sideCheck->setChecked(false);
        applyNow();
        check(dlg.currentSheet() && dlg.currentSheet()->views.size() == 1 && last.views.size() == 1 &&
                  last.views[0] == ViewKind::Front,
              "C: yalniz Front");
        const int before = calls;
        frontCheck->setChecked(false);
        applyNow();
        check(dlg.currentSheet() == nullptr && calls == before && status->text().contains(QStringLiteral("En az")),
              "C: hic gorunus secili degil -> cizim yok, builder cagrilmaz, hata metni");
        frontCheck->setChecked(true);
        applyNow();
        check(dlg.currentSheet() != nullptr, "C: gorunus geri secilince cizim geri gelir");

        // Builder hatasi: durum satirinda gorunur, eski cizim kalmaz.
        DrawingPreviewDialog bad(subjects, 0, [](EntityId, const DrawingOptions&) {
            BuildResult r;
            r.error = "test hatasi";
            return r;
        });
        auto* badStatus = bad.findChild<QLabel*>(QStringLiteral("drawingStatus"));
        check(bad.currentSheet() == nullptr && badStatus && badStatus->text().contains(QStringLiteral("test hatasi")),
              "C: builder hatasi durum satirinda");
    }

    // --- E) delikli plaka: daire render'i + ince cizgi agirliklari ---
    {
        using namespace mm::drawing::lineWeightMm;
        check(visible <= 0.2 && hidden <= 0.12 && dimension <= 0.12 && frame <= 0.4 && titleOuter <= 0.3,
              "E: cizgi agirliklari ince (gorunur <=0.2, gizli/olcu <=0.12, cerceve <=0.4 mm)");
        Document dp;
        const EntityId plate = add(dp, makeBox({0, 0, 0}, {300, 20, 200}, "PL20", "Plaka"));
        for (double x : {50.0, 250.0})
            for (double z : {40.0, 160.0}) {
                CutModifier h;
                h.kind = CutKind::Hole;
                h.target = plate;
                h.planeOrigin = {x, 10, z};
                h.planeNormal = {0, 1, 0};
                h.diameter = 22;
                dp.addCutModifier(h);
            }
        MarkRegistry rp;
        const BuildResult hp = legacyBuildSheet(dp, rp, plate);
        check(hp.ok, "E: delikli plaka cizimi");
        if (hp.ok) {
            const QImage himg = renderSheetImage(hp.sheet, 2100);
            if (const char* png = std::getenv("MM_DRAWING_PNG_HOLES")) himg.save(QString::fromLocal8Bit(png));
            const DrawingView* f = hp.sheet.viewOfKind(ViewKind::Front);
            const double pxPerMm = himg.width() / hp.sheet.format.width;
            int ringOk = 0, insideClear = 0;
            for (const Circle2& c : f->circles) {
                const Vec2 pc = f->toPaper(c.center);
                const double rpx = c.radius * f->scale * pxPerMm;
                const QPoint ctr = toPx(hp.sheet, himg, pc.x, pc.y);
                // Halka uzerinde (sag, sol, ust, alt) en az 3 nokta koyu piksel icerir.
                int hits = 0;
                for (const QPoint d : {QPoint(int(rpx), 0), QPoint(-int(rpx), 0), QPoint(0, int(rpx)), QPoint(0, -int(rpx))})
                    hits += darkCount(himg, QRect(ctr + d - QPoint(2, 2), QSize(5, 5)), 170) > 0;
                ringOk += hits >= 3;
                // Capraz ic noktada (eksen cizgisi yok) cizgi yok.
                const QPoint inner = ctr + QPoint(int(rpx * 0.45), int(rpx * 0.45));
                insideClear += darkCount(himg, QRect(inner - QPoint(1, 1), QSize(3, 3)), 200) == 0;
            }
            check(f->circles.size() == 4 && ringOk == 4, "E: 4 delik dairesi halkasi gercekten cizili");
            check(insideClear == 4, "E: daire ici bos (dolgu yok), yalniz eksen isaretleri");
        }
    }

    // --- D) secimden konu cikarma (menu komutunun mantigi) ---
    {
        int def = -1;
        const auto idxOf = [&](EntityId id) { return *doc.indexOfEntity(id); };
        auto c = subjectChoicesFor(doc, {idxOf(beam)}, false, def);
        check(c.size() == 2 && c[0].id == beam && c[1].id == asmId && def == 0 &&
                  c[1].label.startsWith(QStringLiteral("Assembly")),
              "D: assembly uyesi secili -> [Parca, Assembly], varsayilan parca");
        c = subjectChoicesFor(doc, {idxOf(beam)}, true, def);
        check(def == 1, "D: assembly secim modu -> varsayilan Assembly");
        c = subjectChoicesFor(doc, {idxOf(beam), idxOf(pl1)}, false, def);
        check(def == 1 && c.size() == 2, "D: birden fazla secim -> varsayilan Assembly");
        c = subjectChoicesFor(doc, {idxOf(lone)}, true, def);
        check(c.size() == 1 && c[0].id == lone && def == 0, "D: assembly'siz parca -> yalniz Parca");
        c = subjectChoicesFor(doc, {}, false, def);
        check(c.empty(), "D: secim yok -> bos");
        c = subjectChoicesFor(doc, {9999}, false, def);
        check(c.empty(), "D: gecersiz indeks -> bos");
    }

    std::printf(failures ? "FAILURES: %d\n" : "ALL PASS\n", failures);
    return failures ? 1 : 0;
}
