# sikit

[![build](https://github.com/UnsignedChad/sikit/actions/workflows/build.yml/badge.svg)](https://github.com/UnsignedChad/sikit/actions/workflows/build.yml)

Open-source Signal Integrity (SI) analysis for KiCad PCBs.

Sister project to [**pdnkit**](https://github.com/UnsignedChad/pdnkit) (Power Integrity). Both share the same KiCad-import + render foundation; sikit specializes in high-speed signal analysis.

## What it does

Open a `.kicad_pcb`, get:

- **Impedance overlay** — every trace coloured by Z₀ error vs target (50 / 75 / 100 Ω). Diff pairs auto-detected; their Z_diff overlaid in a separate mode. Stackup parsed from the board's `(setup (stackup ...))` block when present; falls back to FR-4 defaults otherwise.
- **Eye-diagram analysis** — four ways in:
  1. analytic RC channel demo (clean or heavy ISI),
  2. open a measured `.s2p` Touchstone file,
  3. synthesize the channel from a selected high-speed net (PCB W + L + stackup → S-params → eye),
  4. drive the synthesized channel with a Djordjevic-Sarkar dispersion model so εr / tan δ change with frequency the way they actually do in FR-4.
- **Mask check** — USB 2.0 HS mask overlaid on the eye, PASS / FAIL verdict, numerical eye height / width / jitter reported in the EyeWindow caption.
- **Crosstalk** — N-conductor RLGC matrix extraction with NEXT coefficient and modal-mismatch κ from the in-house FDM solver.
- **Export** — any net to Touchstone `.s2p` for use in Keysight ADS / Sigrity / scikit-rf, or to a 7-column CSV (`freq, S11_dB, S11_phase, S21_dB, S21_phase, Z_in_real, Z_in_imag`) for Excel / pandas / GNUmeric.
- **IBIS-AMI integration** — `.ibs` and `.ami` parsers, plus a dynamic `.so` loader (dlopen + AMI_Init / AMI_GetWave / AMI_Close per the spec) so vendor SerDes models drive the channel.

230+ Catch2 unit tests, CI matrix on gcc + clang, suite under 3 seconds.

## Why

High-speed PCB design — PCIe, DDR, USB, SerDes — is dominated by commercial tools (Keysight ADS, HyperLynx, Sigrity SystemSI) costing tens of thousands per seat. The OSS landscape has good *primitives* (scikit-rf, PyBERT, OpenEMS) but no integrated KiCad-aware workflow:

> *"Load my KiCad project, tell me which traces are out of impedance spec, show me the eye for my SerDes lane, flag the crosstalk problems."*

sikit aims at exactly that integration.

## Architecture

Modular cross-section impedance engine with three tiers behind a stable API:

| Tier | Engine | Where it lives |
|---|---|---|
| 1 | Closed-form (IPC-2141A / Wadell) — microstrip, stripline, edge-coupled diff pair | `src/impedance/` |
| 2 | ~~FasterCap wrapper~~ | *Skipped — in-house FDM covers the use case* |
| 3 | In-house 2D FDM Laplace solver — SOR on uniform grid, harmonic-mean ε at faces, multi-conductor RLGC | `src/em2d/` |

Behind that, the rest of the SI pipeline:

```
src/sexpr/        S-expression parser
src/parser/       KiCad .kicad_pcb → typed Board model
src/model/        Board, Layer, Stackup, Net, Segment, Via, Pad, Zone, HitTest
src/render/       Camera2D, ZoneMesher, SegmentMesher, ViaMesher, LayerColors, CircleHelper
src/analysis/     TraceImpedance, ChannelSynthesis (with optional dispersion model)
src/highspeed/    Diff-pair detector, high-speed-net heuristic
src/dispersion/   Djordjevic-Sarkar wideband Debye model
src/touchstone/   .s1p/.s2p/.s4p reader + writer + CSV exporter
src/sparam/       Pozar T-parameter cascade math
src/dsp/          Radix-2 FFT, frequency-domain channel application
src/eye/          PRBS-7, NRZ TX, RC LPF, UI-fold bin grid, IBIS ramp, eye metrics
src/specs/        Protocol eye masks + pass/fail (USB 2.0 HS, generic centered)
src/ibis/         .ibs file parser, .ami parameter parser, AMI .so dlopen wrapper
src/em2d/         CrossSection, FdmGrid, FdmSolver, RLGC matrix + crosstalk
src/              Qt6 shell — MainWindow, PcbCanvas, LayerPanel, EyeWindow
```

## Validation

Three tiers shipped, top tier still TODO (needs hardware):

1. **Unit tests on each formula** — 230+ Catch2 cases. `ctest --test-dir build`.
2. **Reference-data suite** — `tests/reference_data_test.cpp` checks sikit against tabulated Z₀ values from IPC-2141A / Polar Si9000 canonical cases.
3. **Cross-engine consistency** — closed-form vs FDM should agree within ~20% on canonical 50 Ω microstrip; verified.
4. **(TODO) Real hardware** — see `docs/validation_guide.pdf` for the suggested test-board + VNA setup (~$330 total, custom JLCPCB coupon + LiteVNA 64).

## Build (Debian / Ubuntu)

```bash
sudo apt install -y qt6-base-dev qt6-base-dev-tools libqt6opengl6-dev libqt6openglwidgets6 \
                    libeigen3-dev libsuitesparse-dev libcgal-dev \
                    libspdlog-dev libcli11-dev libboost-dev \
                    ninja-build cmake clang catch2 git-filter-repo
```

```bash
CC=clang CXX=clang++ cmake -B build -G Ninja
cmake --build build
ctest --test-dir build
./build/sikit                            # empty window
./build/sikit --open my_board.kicad_pcb  # loads on startup
```

g++ also works; both compilers are CI-tested.

## Key keyboard shortcuts

| | |
|---|---|
| `Ctrl+O` | Open KiCad PCB |
| `Ctrl+T` | Open Touchstone `.s2p` → eye diagram |
| `Ctrl+Shift+S` | Export selected net as Touchstone `.s2p` |
| `Ctrl+Shift+C` | Export selected net as frequency-sweep CSV |
| `Home` | Fit board to viewport |
| `Ctrl+D` | Toggle 3D stackup view (orbit camera; LMB rotate, MMB pan, wheel zoom) |
| `Ctrl+1 / 2 / 3` | Trace impedance overlay at 50 / 90 / 100 Ω |
| `Ctrl+Shift+2 / 3` | Diff-pair impedance overlay at 90 / 100 Ω |
| `Ctrl+0` | Clear overlay |
| `Ctrl+E` / `Ctrl+Shift+E` | Eye diagram with analytic RC channel (clean / ISI demo) |
| `Ctrl+Y` | Synthesize eye from a high-speed net's geometry |

The Analyze menu has a toggle for "Use FDM solver for impedance" that swaps the engine behind the Ctrl+1/2/3 overlays.

## References

- **Wadell**, *Transmission Line Design Handbook* (Artech House) — closed-form bible
- **Pozar**, *Microwave Engineering* — S-parameter and ABCD math
- **Djordjevic, Sarkar et al.**, "Wideband frequency-domain characterization of FR-4 and time-domain causality" (2001) — dispersion model
- **Hammerstad-Jensen** — microstrip effective permittivity
- **IBIS Open Forum** — IBIS + AMI specifications
- **IPC-2141A** — Design Guide for High-Speed Controlled-Impedance Circuit Boards

## License

GPL-3.0 — see [LICENSE](LICENSE).
