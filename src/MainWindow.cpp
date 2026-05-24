#include "MainWindow.h"

#include <algorithm>

#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QStatusBar>
#include <spdlog/spdlog.h>

#include "EyeWindow.h"
#include "LayerPanel.h"
#include "PcbCanvas.h"
#include "analysis/TraceImpedance.h"
#include "dsp/ChannelResponse.h"
#include "eye/Eye.h"
#include "parser/KicadPcbParser.h"
#include "specs/EyeMask.h"
#include "touchstone/Touchstone.h"

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
    auto* openTs = fileMenu->addAction("Open &Touchstone for eye...");
    openTs->setShortcut(QKeySequence("Ctrl+T"));
    connect(openTs, &QAction::triggered, this, &MainWindow::onOpenTouchstoneEye);
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
    analyzeMenu->addSeparator();
    auto* eyeOpen = analyzeMenu->addAction("Eye diagram — clean RC channel (demo)");
    eyeOpen->setShortcut(QKeySequence("Ctrl+E"));
    connect(eyeOpen, &QAction::triggered, this,
            [this]() { showEyeDiagramDemo(/*severe_isi=*/false); });
    auto* eyeISI = analyzeMenu->addAction("Eye diagram — heavy ISI RC channel (demo)");
    eyeISI->setShortcut(QKeySequence("Ctrl+Shift+E"));
    connect(eyeISI, &QAction::triggered, this,
            [this]() { showEyeDiagramDemo(/*severe_isi=*/true); });

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

void MainWindow::onOpenTouchstoneEye() {
    const QString path = QFileDialog::getOpenFileName(
        this, "Open Touchstone (.s2p) for eye analysis", QString(),
        "Touchstone 2-port (*.s2p);;All files (*)");
    if (path.isEmpty()) return;

    bool ok = false;
    const double baud_gbps = QInputDialog::getDouble(
        this, "Baud rate", "Bit rate (Gbps):", 1.0, 0.01, 100.0, 2, &ok);
    if (!ok) return;

    sikit::touchstone::TouchstoneFile channel;
    try {
        channel = sikit::touchstone::TouchstoneReader::read_file(path.toStdString());
    } catch (const std::exception& e) {
        QMessageBox::critical(this, "Open Touchstone failed", e.what());
        return;
    }
    if (channel.num_ports != 2) {
        QMessageBox::warning(this, "Open Touchstone",
                             QString("Expected 2-port file; got %1 ports.")
                                 .arg(channel.num_ports));
        return;
    }

    // TX waveform sized to give us enough cycles for a stable eye and a
    // sample rate well above the Touchstone's Nyquist so the interpolation
    // is bounded.
    constexpr int kBitCount = 2000;
    constexpr int kSpu = 32;
    const double baud = baud_gbps * 1e9;
    const double fs = baud * kSpu;

    auto bits = sikit::eye::prbs7(kBitCount);
    auto tx = sikit::eye::nrz_waveform(bits, kSpu);

    std::vector<double> rx;
    try {
        rx = sikit::dsp::apply_channel(tx, fs, channel);
    } catch (const std::exception& e) {
        QMessageBox::critical(this, "Apply channel failed", e.what());
        return;
    }

    auto eye = sikit::eye::build_eye(rx, kSpu, 128, 96, /*warmup=*/8);
    const auto& mask = sikit::specs::usb20_hs_template1();
    const int violations = sikit::specs::count_violations(eye, mask);

    auto* w = new EyeWindow(this);
    w->setAttribute(Qt::WA_DeleteOnClose);
    w->setTitleSubtext(
        QString("%1 · %2 Gbps PRBS-7")
            .arg(QFileInfo(path).fileName())
            .arg(baud_gbps, 0, 'f', 2));
    w->setEye(eye);
    w->setMask(&mask);
    w->show();

    statusBar()->showMessage(
        QString("Eye from %1 @ %2 Gbps: mask=%3 · violations=%4 %5")
            .arg(QFileInfo(path).fileName())
            .arg(baud_gbps, 0, 'f', 2)
            .arg(QString::fromStdString(mask.name))
            .arg(violations)
            .arg(violations == 0 ? "PASS" : "FAIL"));
    spdlog::info("touchstone eye: {} @ {} Gbps, {} freq points, {} violations",
                 path.toStdString(), baud_gbps,
                 channel.frequencies.size(), violations);
}

void MainWindow::showImpedanceOverlay(double target_z0) {
    if (!board_) {
        QMessageBox::information(this, "Impedance overlay",
                                 "Open a KiCad PCB first.");
        return;
    }
    sikit::analysis::AnalysisStackup stackup;
    auto results = sikit::analysis::compute_all(*board_, stackup);
    canvas_->setImpedanceOverlay(results, target_z0);

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

void MainWindow::showEyeDiagramDemo(bool severe_isi) {
    constexpr int kBitCount = 2000;
    constexpr int kSpu = 32;
    constexpr double kBaud = 1.0e9;
    const double dt = 1.0 / (kBaud * kSpu);
    const double fc = severe_isi ? kBaud / 3.0 : kBaud * 2.0;

    auto bits = sikit::eye::prbs7(kBitCount);
    auto tx = sikit::eye::nrz_waveform(bits, kSpu);
    auto rx = sikit::eye::rc_lowpass(tx, dt, fc);
    auto eye = sikit::eye::build_eye(rx, kSpu, 128, 96, /*warmup=*/8);

    const auto& mask = sikit::specs::usb20_hs_template1();

    auto* w = new EyeWindow(this);
    w->setAttribute(Qt::WA_DeleteOnClose);
    w->setTitleSubtext(
        QString("PRBS-7 · %1 Gbps NRZ · RC channel fc=%2 %3")
            .arg(kBaud / 1e9, 0, 'f', 1)
            .arg(fc / 1e6, 0, 'f', 0)
            .arg(severe_isi ? "MHz (heavy ISI)" : "MHz (clean)"));
    w->setEye(eye);
    w->setMask(&mask);
    w->show();

    const int violations = sikit::specs::count_violations(eye, mask);
    statusBar()->showMessage(
        QString("Eye: %1 bits, %2 samples/UI, fc=%3 MHz · mask=%4 · violations=%5 %6")
            .arg(kBitCount).arg(kSpu).arg(fc / 1e6, 0, 'f', 0)
            .arg(QString::fromStdString(mask.name))
            .arg(violations)
            .arg(violations == 0 ? "PASS" : "FAIL"));
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
