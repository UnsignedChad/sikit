#pragma once

#include <QWidget>

#include "eye/Eye.h"
#include "specs/EyeMask.h"

class EyeWindow : public QWidget {
    Q_OBJECT
public:
    explicit EyeWindow(QWidget* parent = nullptr);

    void setEye(const sikit::eye::EyeGrid& grid);
    void setMask(const sikit::specs::EyeMask* mask);  // nullptr = no mask overlay
    void setTitleSubtext(const QString& text);

protected:
    void paintEvent(QPaintEvent* e) override;

private:
    sikit::eye::EyeGrid eye_;
    const sikit::specs::EyeMask* mask_ = nullptr;
};
