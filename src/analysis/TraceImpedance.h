// Per-trace impedance analysis driven by closed-form formulas (engine 1).
//
// Computes single-ended Z₀ for each segment on the board, picking the
// microstrip or stripline formula based on whether the layer is outer
// (F.Cu / B.Cu — air above one side) or inner copper. Uses an
// AnalysisStackup struct of geometry assumptions since the KiCad parser
// doesn't yet extract per-layer dielectric / thickness from the .kicad_pcb.

#pragma once

#include <cstddef>
#include <vector>

#include "model/Board.h"

namespace sikit::analysis {

// PCB stackup assumptions for impedance analysis. Defaults match a typical
// 4-layer FR-4 prepreg stackup. When the parser learns to read the KiCad
// (stackup ...) form these can be filled from the loaded board directly.
struct AnalysisStackup {
    double outer_dielectric_height = 0.2e-3;  // H for F.Cu / B.Cu (m)
    double inner_plane_separation = 0.4e-3;   // B for inner stripline (m)
    double copper_thickness = 35e-6;          // 1oz copper (m)
    double epsilon_r = 4.4;                   // FR-4 nominal
};

struct SegmentImpedance {
    std::size_t segment_index = 0;  // index into board.segments
    int layer_ordinal = 0;
    int net_id = 0;
    double trace_width = 0.0;       // m
    double z0 = 0.0;                // Ω (single-ended)
    bool in_valid_range = true;     // false if formula extrapolating
};

// Compute Z₀ for one segment of given width on `layer_ordinal`.
// F.Cu (0) and B.Cu (31) → microstrip; other copper layers → stripline.
SegmentImpedance compute_one(double trace_width,
                             int layer_ordinal,
                             const AnalysisStackup& s);

// Compute Z₀ for every copper segment in the board.
std::vector<SegmentImpedance> compute_all(const model::Board& board,
                                          const AnalysisStackup& s);

// Color-coding helpers for the canvas overlay:
//   |z0 - target| / target  is the relative error.
//     <  5%   → green (on spec)
//     <  10%  → yellow (warn)
//     >= 10%  → red (out of tolerance)
// Returns RGBA in [0,1] for direct upload to a fragment uniform.
struct ImpedanceColor {
    float r, g, b, a;
};

ImpedanceColor color_for_error(double z0_ohms, double target_ohms);

}  // namespace sikit::analysis
