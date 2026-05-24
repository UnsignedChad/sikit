// Per-trace impedance analysis driven by closed-form formulas (engine 1).
//
// Computes single-ended Z₀ for each segment on the board, picking the
// microstrip or stripline formula based on whether the layer is outer
// (F.Cu / B.Cu — air above one side) or inner copper. Uses an
// AnalysisStackup struct of geometry assumptions which can be populated
// from the board's parsed (setup (stackup ...)) data, or fall back to
// generic 4-layer FR-4 defaults if the file lacks that information.

#pragma once

#include <cstddef>
#include <vector>

#include "model/Board.h"

namespace sikit::analysis {

struct AnalysisStackup {
    double outer_dielectric_height = 0.2e-3;  // H for F.Cu / B.Cu (m)
    double inner_plane_separation = 0.4e-3;   // B for inner stripline (m)
    double copper_thickness = 35e-6;          // 1oz copper (m)
    double epsilon_r = 4.4;                   // FR-4 nominal
    bool   from_real_stackup = false;         // true → derived from board file

    // Populate from a board's parsed (setup (stackup ...)) when available.
    // Falls through to generic defaults for any field the file doesn't supply.
    static AnalysisStackup from_board(const model::Board& b);
};

struct SegmentImpedance {
    std::size_t segment_index = 0;
    int layer_ordinal = 0;
    int net_id = 0;
    double trace_width = 0.0;
    double z0 = 0.0;
    bool in_valid_range = true;
};

SegmentImpedance compute_one(double trace_width,
                             int layer_ordinal,
                             const AnalysisStackup& s);

std::vector<SegmentImpedance> compute_all(const model::Board& board,
                                          const AnalysisStackup& s);

struct ImpedanceColor {
    float r, g, b, a;
};

ImpedanceColor color_for_error(double z0_ohms, double target_ohms);

}  // namespace sikit::analysis
