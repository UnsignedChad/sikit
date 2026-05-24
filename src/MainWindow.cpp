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

#include <cmath>

#include "EyeWindow.h"
#include "LayerPanel.h"
#include "PcbCanvas.h"
#include "analysis/ChannelSynthesis.h"
#include "analysis/TraceImpedance.h"
#include "dsp/ChannelResponse.h"
#include "eye/Eye.h"
#include "highspeed/DiffPair.h"
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
    use_fdm_action_ = analyzeMenu->addAction("Use FDM solver for impedance (slower, more accurate)");
    use_fdm_action_->setCheckable(true);
    use_fdm_action_->setChecked(false);
    analyzeMenu->addSeparator();
    auto* eyeOpen = analyzeMenu->addAction("Eye diagram — clean RC channel (demo)");
    eyeOpen->setShortcut(QKeySequence("Ctrl+E"));
    connect(eyeOpen, &QAction::triggered, this,
            [this]() { showEyeDiagramDemo(/*severe_isi=*/false); });
    auto* eyeISI = analyzeMenu->addAction("Eye diagram — heavy ISI RC channel (demo)");
    eyeISI->setShortcut(QKeySequence("Ctrl+Shift+E"));
    connect(eyeISI, &QAction::triggered, this,
            [this]() { showEyeDiagramDemo(/*severe_isi=*/true); });
    auto* eyeSynth = analyzeMenu->addAction("Eye diagram — &synthesized from trace geometry...");
    eyeSynth->setShortcut(QKeySequence("Ctrl+Y"));
    connect(eyeSynth, &QAction::triggered, this,
            &MainWindow::onSynthesizeEye);

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
    const auto stackup = sikit::analysis::AnalysisStackup::from_board(*board_);
    const auto engine = use_fdm_action_ && use_fdm_action_->isChecked()
                            ? sikit::analysis::Engine::Fdm
                            : sikit::analysis::Engine::ClosedForm;
    auto results = sikit::analysis::compute_all(*board_, stackup, engine);
    canvas_->setImpedanceOverlay(results, target_z0);

    int on_spec = 0, warn = 0, fail = 0;
    for (const auto& r : results) {
        const double err = std::abs(r.z0 - target_z0) / target_z0;
        if (err < 0.05) ++on_spec;
        else if (err < 0.10) ++warn;
        else ++fail;
    }
    const QString stackup_src = stackup.from_real_stackup
                                    ? "real stackup"
                                    : "default FR-4";
    const QString engine_name =
        (engine == sikit::analysis::Engine::Fdm) ? "FDM" : "closed-form";
    statusBar()->showMessage(
        QString("Impedance @ %1 Ω · %2 · %3: %4 on-spec (<5%), %5 warn (<10%), %6 fail (≥10%)")
            .arg(target_z0, 0, 'f', 0)
            .arg(engine_name)
            .arg(stackup_src)
            .arg(on_spec).arg(warn).arg(fail));
    spdlog::info("impedance overlay target={}Ω engine={} stackup={} εr={:.2f}: "
                 "on-spec={} warn={} fail={}",
                 target_z0, engine_name.toStdString(),
                 stackup.from_real_stackup ? "real" : "default",
                 stackup.epsilon_r,
                 on_spec, warn, fail);
}

void MainWindow::onSynthesizeEye() {
    bool ok = false;
    double width_mm = 0.20;
    double length_mm = 50.0;
    QString net_label;  // for the title bar

    // If a board is loaded, offer to pre-fill from a high-speed net.
    if (board_) {
        auto hs_ids = sikit::highspeed::find_high_speed_nets(*board_);
        if (!hs_ids.empty()) {
            QStringList items;
            items << "(enter width/length manually)";
            for (int nid : hs_ids) {
                if (const auto* n = board_->find_net(nid)) {
                    items << QString::fromStdString(n->name);
                }
            }
            const QString choice = QInputDialog::getItem(
                this, "Synthesize eye",
                "Pick a high-speed net (or enter manually):",
                items, 0, /*editable=*/false, &ok);
            if (!ok) return;

            if (choice != items.first()) {
                // Find the selected net id by name.
                int target_net = -1;
                if (const auto* n = board_->find_net_by_name(choice.toStdString())) {
                    target_net = n->id;
                }
                if (target_net >= 0) {
                    // Compute median trace width and total length on F.Cu.
                    std::vector<double> widths;
                    double total_length = 0.0;
                    for (const auto& s : board_->segments) {
                        if (s.net_id != target_net) continue;
                        if (s.layer_ordinal != 0) continue;  // F.Cu only for v0
                        widths.push_back(s.width);
                        const double dx = s.end.x - s.start.x;
                        const double dy = s.end.y - s.start.y;
                        total_length += std::sqrt(dx * dx + dy * dy);
                    }
                    if (!widths.empty()) {
                        std::sort(widths.begin(), widths.end());
                        width_mm = widths[widths.size() / 2] * 1e3;
                    }
                    if (total_length > 0.0) length_mm = total_length * 1e3;
                    net_label = choice;
                }
            }
        }
    }

    width_mm = QInputDialog::getDouble(
        this, "Synthesize eye", "Trace width (mm):",
        width_mm, 0.05, 5.0, 3, &ok);
    if (!ok) return;
    length_mm = QInputDialog::getDouble(
        this, "Synthesize eye", "Trace length (mm):",
        length_mm, 1.0, 5000.0, 1, &ok);
    if (!ok) return;
    const double baud_gbps = QInputDialog::getDouble(
        this, "Synthesize eye", "Bit rate (Gbps):",
        1.0, 0.01, 100.0, 2, &ok);
    if (!ok) return;

    // Use the loaded board's stackup if available, else generic FR-4.
    sikit::analysis::AnalysisStackup stackup =
        board_ ? sikit::analysis::AnalysisStackup::from_board(*board_)
               : sikit::analysis::AnalysisStackup{};

    sikit::analysis::ChannelSpec spec;
    spec.trace_width = width_mm * 1e-3;
    spec.layer_ordinal = 0;            // F.Cu microstrip by default
    spec.length_m = length_mm * 1e-3;
    spec.stackup = stackup;
    spec.engine = sikit::analysis::Engine::ClosedForm;

    // Frequency grid wide enough to cover the TX signal's spectrum.
    // PRBS-7 NRZ at B Gbps has significant content up to ~5B.
    constexpr int kBitCount = 2000;
    constexpr int kSpu = 32;
    const double baud = baud_gbps * 1e9;
    const double fs = baud * kSpu;
    std::vector<double> freqs;
    const double f_step = baud / 50.0;
    const double f_max = fs / 2.0;       // up to Nyquist of the TX waveform
    for (double f = f_step; f <= f_max; f += f_step) freqs.push_back(f);

    sikit::touchstone::TouchstoneFile channel;
    try {
        channel = sikit::analysis::synthesize_channel(spec, freqs, 50.0);
    } catch (const std::exception& e) {
        QMessageBox::critical(this, "Synthesize eye failed", e.what());
        return;
    }

    auto bits = sikit::eye::prbs7(kBitCount);
    auto tx = sikit::eye::nrz_waveform(bits, kSpu);
    std::vector<double> rx;
    try {
        rx = sikit::dsp::apply_channel(tx, fs, channel);
    } catch (const std::exception& e) {
        QMessageBox::critical(this, "Apply synthesized channel failed", e.what());
        return;
    }
    auto eye = sikit::eye::build_eye(rx, kSpu, 128, 96, /*warmup=*/8);
    const auto& mask = sikit::specs::usb20_hs_template1();

    // Pull Z0 / v_phase for the caption.
    auto imp = sikit::analysis::compute_one(spec.trace_width, spec.layer_ordinal,
                                             spec.stackup);

    auto* w = new EyeWindow(this);
    w->setAttribute(Qt::WA_DeleteOnClose);
    const QString head = net_label.isEmpty()
                             ? QString("synthesized trace")
                             : QString("net %1").arg(net_label);
    w->setTitleSubtext(
        QString("%1 · W=%2mm  L=%3mm  Z₀=%4Ω  %5 Gbps")
            .arg(head)
            .arg(width_mm, 0, 'f', 3).arg(length_mm, 0, 'f', 1)
            .arg(imp.z0, 0, 'f', 1).arg(baud_gbps, 0, 'f', 2));
    w->setEye(eye);
    w->setMask(&mask);
    w->show();

    const int violations = sikit::specs::count_violations(eye, mask);
    statusBar()->showMessage(
        QString("Synthesized eye: %1mm × %2mm trace · Z₀=%3Ω · %4 Gbps · violations=%5 %6")
            .arg(width_mm, 0, 'f', 3).arg(length_mm, 0, 'f', 1)
            .arg(imp.z0, 0, 'f', 1).arg(baud_gbps, 0, 'f', 2)
            .arg(violations)
            .arg(violations == 0 ? "PASS" : "FAIL"));
    spdlog::info("synthesized eye W={:.3f}mm L={:.1f}mm Z0={:.1f}Ω "
                 "{} Gbps {} violations",
                 width_mm, length_mm, imp.z0, baud_gbps, violations);
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
