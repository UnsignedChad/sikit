#pragma once

#include <QMainWindow>
#include <memory>

#include "model/Board.h"

class PcbCanvas;
class LayerPanel;
class QAction;
class QLabel;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

    bool loadKicadPcb(const QString& path);

private slots:
    void onOpenKicadPcb();
    void onOpenTouchstoneEye();

private:
    void populateLayerPanel();
    void showImpedanceOverlay(double target_z0);
    void showEyeDiagramDemo(bool severe_isi);

    PcbCanvas* canvas_;
    LayerPanel* layer_panel_;
    QLabel* hover_label_;
    QAction* use_fdm_action_;
    std::unique_ptr<sikit::model::Board> board_;
};
