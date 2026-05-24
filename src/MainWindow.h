#pragma once

#include <QMainWindow>
#include <memory>

#include "model/Board.h"

class PcbCanvas;
class LayerPanel;
class QLabel;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

    bool loadKicadPcb(const QString& path);

private slots:
    void onOpenKicadPcb();

private:
    void populateLayerPanel();
    void showImpedanceOverlay(double target_z0);

    PcbCanvas* canvas_;
    LayerPanel* layer_panel_;
    QLabel* hover_label_;
    std::unique_ptr<sikit::model::Board> board_;
};
