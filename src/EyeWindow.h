#pragma once

#include <QWidget>

#include "eye/Eye.h"

// Standalone window that renders an EyeGrid as a viridis heatmap with
// time/voltage axes. Drawn with QPainter (CPU-side) — fast enough for
// the typical 128×128 bin grid and avoids spinning up a second GL context.
class EyeWindow : public QWidget {
    Q_OBJECT
public:
    explicit EyeWindow(QWidget* parent = nullptr);

    void setEye(const sikit::eye::EyeGrid& grid);
    void setTitleSubtext(const QString& text);  // shown in the title bar

protected:
    void paintEvent(QPaintEvent* e) override;

private:
    sikit::eye::EyeGrid eye_;
};
