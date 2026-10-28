#pragma once
// EKRANIN ORTASINDA ILERLEME PENCERESI (Qt Widgets) — dosya ac/kaydet + assembly cizimi.
//
// Davranis (istenen urun davranisi):
//   * Pencere TUM ekrani kaplar ve hafif karartir -> islem surerken kazara tiklama olmaz.
//   * Panel ekranin TAM ortasindadir (parent pencerenin ortasi DEGIL).
//   * Baslik + asama etiketi + cubuk: total > 0 ise belirli, total <= 0 ise belirsiz (busy).
//   * report() olaylari pompalar (kullanici girdisi HARIC) -> cubuk/etiket gercekten guncellenir.
//   * Esc pencereyi KAPATMAZ (islem yarim kalmaz); Finish() tek kapatma yoludur.
//
// Q_OBJECT YOK (sinyal/slot gerekmiyor) -> moc/CMake ek yuku yok, header-only.
// Qt'siz cekirdek tarafi mm::ProgressReport uretir (progress_report.hpp); bu sinif onu gosterir.

#include <QApplication>
#include <QEventLoop>
#include <QFrame>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QLabel>
#include <QPaintEvent>
#include <QPainter>
#include <QProgressBar>
#include <QScreen>
#include <QString>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>

namespace mm::ui {

class ProgressOverlay : public QWidget {
public:
    explicit ProgressOverlay(QWidget* owner = nullptr) : QWidget(nullptr), owner_(owner) {
        setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
        setAttribute(Qt::WA_TranslucentBackground, true);
        setAttribute(Qt::WA_ShowWithoutActivating, true);
        setFocusPolicy(Qt::NoFocus);
        setObjectName(QStringLiteral("progressOverlay"));

        panel_ = new QFrame(this);
        panel_->setObjectName(QStringLiteral("progressPanel"));
        panel_->setStyleSheet(QStringLiteral(
            "QFrame#progressPanel { background: #23272e; border: 1px solid #4a515b; border-radius: 8px; }"));

        auto* layout = new QVBoxLayout(panel_);
        layout->setContentsMargins(20, 16, 20, 16);
        layout->setSpacing(8);

        title_ = new QLabel(panel_);
        title_->setObjectName(QStringLiteral("progressTitle"));
        title_->setStyleSheet(QStringLiteral("color: #f2f4f8; font-size: 10.5pt; font-weight: 600;"));
        detail_ = new QLabel(panel_);
        detail_->setObjectName(QStringLiteral("progressDetail"));
        detail_->setStyleSheet(QStringLiteral("color: #aab2bd; font-size: 9pt;"));
        detail_->setWordWrap(true);
        detail_->setMinimumHeight(detail_->fontMetrics().height());

        bar_ = new QProgressBar(panel_);
        bar_->setObjectName(QStringLiteral("progressBar"));
        bar_->setTextVisible(false);
        bar_->setFixedHeight(10);
        bar_->setRange(0, 0);   // baslangicta belirsiz
        bar_->setStyleSheet(QStringLiteral(
            "QProgressBar { background: #171a1f; border: 1px solid #3b414a; border-radius: 5px; }"
            "QProgressBar::chunk { background: #5a8fd6; border-radius: 4px; }"));

        layout->addWidget(title_);
        layout->addWidget(detail_);
        layout->addWidget(bar_);
        hide();
    }

    // Islem basi: pencere hemen gorunur, cubuk sifirlanir. total <= 0 -> belirsiz cubuk.
    void begin(const QString& title, int total = 0) {
        title_->setText(title);
        detail_->clear();
        applyRange(total);
        bar_->setValue(0);
        reveal();
    }

    // Asama guncellemesi: cubuk degeri + etiket. Pencere kapaliysa yeniden acilir.
    void report(int done, int total, const QString& detail) {
        applyRange(total);
        if (total > 0) bar_->setValue(std::clamp(done, 0, total));
        if (!detail.isEmpty()) detail_->setText(detail);
        if (!isVisible()) reveal();
        pump();
    }

    // Islem sonu: pencere kapanir. Idempotent (iki kez cagrilabilir).
    void finish() {
        if (!isVisible()) return;
        hide();
    }

    bool active() const noexcept { return isVisible(); }

    // --- testler/teshis icin salt-okunur durum ---
    QString titleText() const { return title_->text(); }
    QString detailText() const { return detail_->text(); }
    int barValue() const noexcept { return bar_->value(); }
    int barMaximum() const noexcept { return bar_->maximum(); }
    bool barBusy() const noexcept { return bar_->minimum() == 0 && bar_->maximum() == 0; }
    QRect panelRect() const { return panel_->geometry(); }
    QRect screenRect() const { return overlayScreenRect(); }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter painter(this);
        painter.fillRect(rect(), QColor(8, 10, 13, 150));   // hafif karartma
    }

    void resizeEvent(QResizeEvent* event) override {
        QWidget::resizeEvent(event);
        centerPanel();
    }

    // Esc/Enter pencereyi kapatmaz: islem bitmeden gizlenirse kullanici "dondu" sanir.
    void keyPressEvent(QKeyEvent* event) override { event->accept(); }

private:
    void applyRange(int total) {
        const int max = std::max(0, total);
        if (bar_->maximum() != max || bar_->minimum() != 0) bar_->setRange(0, max);
        // Qt: range 0..0 = belirsiz (busy) animasyonu.
    }

    QRect overlayScreenRect() const {
        if (owner_) {
            if (QScreen* s = owner_->screen()) return s->availableGeometry();
        }
        if (QScreen* s = QGuiApplication::primaryScreen()) return s->availableGeometry();
        return QRect(0, 0, 800, 600);
    }

    void centerPanel() {
        const QRect screen = overlayScreenRect();
        const int width = std::clamp(screen.width() / 3, 340, 520);
        panel_->setFixedWidth(width);
        panel_->adjustSize();
        const QSize panelSize = panel_->size().expandedTo(panel_->sizeHint());
        const QPoint topLeft((this->width() - panelSize.width()) / 2,
                             (this->height() - panelSize.height()) / 2);
        panel_->setGeometry(QRect(topLeft, panelSize));
    }

    void reveal() {
        setGeometry(overlayScreenRect());
        centerPanel();
        show();
        raise();
        pump();
    }

    // Islemi yapan ayni iş parçaciginda oldugumuz icin cubugun cizilmesi ancak olay
    // pompalayarak mumkun. Kullanici girdisi HARIC: islem surerken tiklama islenmez.
    void pump() {
        QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    }

    QWidget* owner_{};
    QFrame* panel_{};
    QLabel* title_{};
    QLabel* detail_{};
    QProgressBar* bar_{};
};

} // namespace mm::ui
