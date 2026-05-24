#include "MainWindow.h"

#include <algorithm>

#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QStatusBar>
#include <spdlog/spdlog.h>

#include "LayerPanel.h"
#include "PcbCanvas.h"
#include "analysis/TraceImpedance.h"
#include "parser/KicadPcbParser.h"

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle("sikit");
    resize(1280, 800);

    canvas_ = new PcbCanvas(this);
    setCentralWidget(canvas_);

    layer_panel_ = new LayerPanel(this);
    auto* dock = new QDockWidget("Layers", this);
    dock->setWidget(layer_panel_);
    dock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    addDockWidget(Qt::RightDockWidgetArea, dock);
    connect(layer_panel_, &LayerPanel::visibility_changed,
            canvas_, &PcbCanvas::setLayerVisibility);

    auto* fileMenu = menuBar()->addMenu("&File");
    auto* openAct = fileMenu->addAction("&Open KiCad PCB...");
    openAct->setShortcut(QKeySequence::Open);
    connect(openAct, &QAction::triggered, this, &MainWindow::onOpenKicadPcb);
    fileMenu->addSeparator();
    fileMenu->addAction("E&xit", this, &QWidget::close);

    auto* viewMenu = menuBar()->addMenu("&View");
    auto* fitAct = viewMenu->addAction("&Fit to Board");
    fitAct->setShortcut(QKeySequence(Qt::Key_Home));
    connect(fitAct, &QAction::triggered, canvas_, &PcbCanvas::fitToBoard);
    viewMenu->addAction(dock->toggleViewAction());

    auto* analyzeMenu = menuBar()->addMenu("&Analyze");
    auto* z50 = analyzeMenu->addAction("Trace impedance overlay (50 Ω)");
    z50->setShortcut(QKeySequence("Ctrl+1"));
    connect(z50, &QAction::triggered, this,
            [this]() { showImpedanceOverlay(50.0); });
    auto* z90 = analyzeMenu->addAction("Trace impedance overlay (90 Ω, USB-style)");
    z90->setShortcut(QKeySequence("Ctrl+2"));
    connect(z90, &QAction::triggered, this,
            [this]() { showImpedanceOverlay(90.0); });
    auto* z100 = analyzeMenu->addAction("Trace impedance overlay (100 Ω, PCIe/HDMI-style)");
    z100->setShortcut(QKeySequence("Ctrl+3"));
    connect(z100, &QAction::triggered, this,
            [this]() { showImpedanceOverlay(100.0); });
    analyzeMenu->addSeparator();
    auto* clearAct = analyzeMenu->addAction("&Clear overlay");
    clearAct->setShortcut(QKeySequence("Ctrl+0"));
    connect(clearAct, &QAction::triggered, canvas_, &PcbCanvas::clearImpedanceOverlay);

    hover_label_ = new QLabel(this);
    hover_label_->setMinimumWidth(300);
    statusBar()->addPermanentWidget(hover_label_);
    connect(canvas_, &PcbCanvas::hoverInfo, hover_label_, &QLabel::setText);

    statusBar()->showMessage("Ready");
}

void MainWindow::onOpenKicadPcb() {
    const QString path = QFileDialog::getOpenFileName(
        this, "Open KiCad PCB", QString(),
        "KiCad PCB (*.kicad_pcb);;All files (*)");
    if (path.isEmpty()) return;
    loadKicadPcb(path);
}

void MainWindow::showImpedanceOverlay(double target_z0) {
    if (!board_) {
        QMessageBox::information(this, "Impedance overlay",
                                 "Open a KiCad PCB first.");
        return;
    }
    sikit::analysis::AnalysisStackup stackup;  // defaults: 4-layer FR-4 prepreg
    auto results = sikit::analysis::compute_all(*board_, stackup);
    canvas_->setImpedanceOverlay(results, target_z0);

    // Quick on-spec tally for the status bar.
    int on_spec = 0, warn = 0, fail = 0;
    for (const auto& r : results) {
        const double err = std::abs(r.z0 - target_z0) / target_z0;
        if (err < 0.05) ++on_spec;
        else if (err < 0.10) ++warn;
        else ++fail;
    }
    statusBar()->showMessage(
        QString("Impedance overlay @ %1 Ω: %2 on-spec (<5%), %3 warn (<10%), %4 fail (≥10%)")
            .arg(target_z0, 0, 'f', 0)
            .arg(on_spec).arg(warn).arg(fail));
    spdlog::info("impedance overlay target={}Ω: on-spec={} warn={} fail={}",
                 target_z0, on_spec, warn, fail);
}

void MainWindow::populateLayerPanel() {
    if (!board_) {
        layer_panel_->setLayers({});
        return;
    }
    std::vector<LayerPanel::Entry> entries;
    for (const auto& L : board_->stackup.layers) {
        if (!L.is_copper()) continue;
        entries.push_back({L.ordinal, QString::fromStdString(L.name)});
    }
    layer_panel_->setLayers(entries);
}

bool MainWindow::loadKicadPcb(const QString& path) {
    try {
        auto board = std::make_unique<sikit::model::Board>(
            sikit::parser::KicadPcbParser::parse_file(path.toStdString()));

        const auto net_count   = board->nets.size();
        const auto seg_count   = board->segments.size();
        const auto via_count   = board->vias.size();
        const auto pad_count   = board->pads.size();
        const auto zone_count  = board->zones.size();
        const auto layer_count = board->stackup.layers.size();
        const auto copper_layers = std::count_if(
            board->stackup.layers.begin(), board->stackup.layers.end(),
            [](const auto& l) { return l.is_copper(); });

        board_ = std::move(board);
        canvas_->setBoard(board_.get());
        populateLayerPanel();

        spdlog::info("loaded {}: {} layers ({} copper), {} nets, {} segments, "
                     "{} vias, {} pads, {} zones",
                     path.toStdString(), layer_count, copper_layers,
                     net_count, seg_count, via_count, pad_count, zone_count);

        const QString summary = QString(
            "Loaded %1  —  %2 nets, %3 segments, %4 vias, %5 pads, %6 zones (%7 copper layers)")
            .arg(QFileInfo(path).fileName())
            .arg(net_count).arg(seg_count).arg(via_count)
            .arg(pad_count).arg(zone_count).arg(copper_layers);
        statusBar()->showMessage(summary);
        setWindowTitle(QString("sikit — %1").arg(QFileInfo(path).fileName()));
        return true;
    } catch (const std::exception& e) {
        QMessageBox::critical(this, "Open KiCad PCB failed", e.what());
        spdlog::error("failed to load {}: {}", path.toStdString(), e.what());
        return false;
    }
}
