#include "EyeWindow.h"

#include <algorithm>
#include <cmath>

#include <QColor>
#include <QImage>
#include <QPainter>

namespace {

// Same viridis stops as the heat-map shader: visually distinguishable
// at small bin counts, smooth gradient, perceptually uniform.
QColor viridis(double t) {
    t = std::clamp(t, 0.0, 1.0);
    constexpr struct { double r, g, b; } stops[5] = {
        {0.267, 0.005, 0.329},
        {0.231, 0.318, 0.545},
        {0.127, 0.567, 0.550},
        {0.369, 0.789, 0.382},
        {0.992, 0.906, 0.144},
    };
    const double seg = t * 4.0;
    int i = static_cast<int>(std::floor(seg));
    if (i >= 4) i = 3;
    const double f = seg - i;
    const auto a = stops[i];
    const auto b = stops[i + 1];
    const double r = a.r + f * (b.r - a.r);
    const double g = a.g + f * (b.g - a.g);
    const double bl = a.b + f * (b.b - a.b);
    return QColor::fromRgbF(r, g, bl, 1.0);
}

}  // namespace

EyeWindow::EyeWindow(QWidget* parent) : QWidget(parent) {
    setWindowTitle("Eye Diagram");
    resize(640, 480);
    setAttribute(Qt::WA_DeleteOnClose);
}

void EyeWindow::setEye(const sikit::eye::EyeGrid& grid) {
    eye_ = grid;
    update();
}

void EyeWindow::setTitleSubtext(const QString& text) {
    setWindowTitle(QString("Eye Diagram — %1").arg(text));
}

void EyeWindow::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), QColor(20, 20, 26));

    if (eye_.time_bins <= 0 || eye_.volt_bins <= 0 ||
        eye_.counts.empty()) {
        p.setPen(Qt::white);
        p.drawText(rect(), Qt::AlignCenter, "No eye data");
        return;
    }

    // Render the bin grid into a QImage at native bin resolution, then
    // scale to fit the plot area. Crisp colors per bin; scaling is
    // nearest-neighbor so individual bins remain visible.
    QImage img(eye_.time_bins, eye_.volt_bins, QImage::Format_RGB32);
    const int peak = std::max(1, eye_.max_count());
    for (int v = 0; v < eye_.volt_bins; ++v) {
        for (int t = 0; t < eye_.time_bins; ++t) {
            const int c = eye_.at(t, v);
            // Flip Y so larger voltage is up on screen.
            const int img_y = eye_.volt_bins - 1 - v;
            if (c == 0) {
                img.setPixelColor(t, img_y, QColor(20, 20, 26));
            } else {
                // Log-stretch the intensity so sparse traces are visible
                // without the dense baseline washing everything out.
                const double t01 = std::log1p(c) / std::log1p(peak);
                img.setPixelColor(t, img_y, viridis(t01));
            }
        }
    }

    constexpr int margin = 50;
    const QRect plot(margin, margin / 2,
                     width() - 2 * margin, height() - margin - margin / 2);
    p.drawImage(plot, img);

    // Axes + frame.
    p.setPen(QColor(140, 140, 150));
    p.drawRect(plot);

    // X axis label: one unit interval.
    p.setPen(Qt::white);
    p.drawText(plot.left(), plot.bottom() + 18, "0");
    p.drawText(plot.right() - 24, plot.bottom() + 18, "1 UI");
    p.drawText(plot.center().x() - 4, plot.bottom() + 18, "½");

    // Y axis labels: v_min and v_max from the data range.
    p.drawText(plot.left() - 36, plot.top() + 10,
               QString::number(eye_.v_max, 'f', 2));
    p.drawText(plot.left() - 36, plot.bottom(),
               QString::number(eye_.v_min, 'f', 2));

    // Caption above the plot.
    p.drawText(plot.left(), plot.top() - 6,
               QString("%1×%2 bins · peak=%3 samples · range=[%4, %5]")
                   .arg(eye_.time_bins).arg(eye_.volt_bins).arg(peak)
                   .arg(eye_.v_min, 0, 'f', 3)
                   .arg(eye_.v_max, 0, 'f', 3));
}
