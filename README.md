## sikit

[![build](https://github.com/UnsignedChad/sikit/actions/workflows/build.yml/badge.svg)](https://github.com/UnsignedChad/sikit/actions/workflows/build.yml)

Open-source Signal Integrity (SI) analysis for KiCad PCBs.

Sister project to [sikit](https://github.com/UnsignedChad/sikit) (Power Integrity). Both share the same KiCad-import + render foundation; sikit specializes in high-speed signal analysis.

## Status

Pre-alpha. The GUI loads a `.kicad_pcb` and renders board geometry (zones, tracks, vias, pads) per layer with mouse pan/zoom, layer-visibility panel, and hover-net inspection. SI-specific analysis (impedance, S-params, eye diagrams) is the next milestone.

## Why

High-speed PCB design — PCIe, DDR, USB, SerDes — is dominated by commercial tools (Keysight ADS, HyperLynx, Sigrity SystemSI) that cost tens of thousands per seat. The OSS landscape has good *primitives* (scikit-rf, PyBERT, OpenEMS) but no integrated workflow:

> *"Load my KiCad project, tell me which traces are out of impedance spec, show me the eye for my SerDes lane, flag the crosstalk problems."*

sikit aims at exactly that integration.

## Approach

Three stages of impedance-solver fidelity behind a stable analysis API — ship the integration story first, swap the engine deeper over time:

1. **Closed-form formulas (Wadell, IPC-2141)** — microstrip, stripline, edge-coupled diff pair, broadside diff pair. Covers ~80% of real boards. v0.1.
2. **FasterCap wrapper** — fallback for non-standard cross-sections (offset stripline, asymmetric stackups). v0.2.
3. **Custom 2.5D MoM field solver** — Method of Moments with stratified Green's function for layered PCB dielectrics. Bounded scope (2D, rectangular conductors, planar layers). v0.3 — this is the moat.

S-parameter cascade (Eigen-backed complex matrix math) feeds eye-diagram rendering with pass/fail against protocol spec masks (PCIe, USB, DDR). IBIS-AMI model loading completes the channel-sim story.

References:
- **Wadell**, *Transmission Line Design Handbook* — closed-form bible
- **Harrington**, *Field Computation by Moment Methods* — MoM canonical
- **Pozar**, *Microwave Engineering* — sanity-checks
- **IBIS** specification — vendor model interchange

## Build (Debian/Ubuntu)

```
sudo apt install -y qt6-base-dev qt6-base-dev-tools libqt6opengl6-dev libqt6openglwidgets6 \
                    libeigen3-dev libsuitesparse-dev libcgal-dev \
                    libspdlog-dev libcli11-dev libboost-dev \
                    ninja-build cmake clang catch2
```

```
CC=clang CXX=clang++ cmake -B build -G Ninja
cmake --build build
ctest --test-dir build
./build/sikit                            # empty window
./build/sikit --open my_board.kicad_pcb  # loads on startup
```

g++ also works; both are CI-tested.

## License

GPL-3.0 — see [LICENSE](LICENSE).
