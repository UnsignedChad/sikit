// Per-trace impedance analysis with a swappable solver engine.
//
//   Engine::ClosedForm   IPC-2141A formulas (fast, ±5–10%).
//   Engine::Fdm          In-house 2D finite-difference solver (slower,
//                        captures real stackup, ±10–15% at v0 mesh density).
//
// AnalysisStackup is consumed by both engines. For Fdm the trace is
// embedded into a synthetic cross-section built from the stackup
// parameters (single trace over a ground plane for outer copper; stripline
// between two planes for inner copper). Results are cached by
// (trace_width, layer_ordinal) so a board with many segments of the same
// width pays one solve, not one per segment.

#pragma once

#include <cstddef>
#include <vector>

#include "model/Board.h"

namespace sikit::analysis {

enum class Engine {
    ClosedForm,
    Fdm,
};

struct AnalysisStackup {
    double outer_dielectric_height = 0.2e-3;
    double inner_plane_separation = 0.4e-3;
    double copper_thickness = 35e-6;
    double epsilon_r = 4.4;
    bool   from_real_stackup = false;

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

// Single-segment closed-form (Wadell / IPC-2141A).
SegmentImpedance compute_one(double trace_width,
                             int layer_ordinal,
                             const AnalysisStackup& s);

// Single-segment FDM. Caller bears the cost of one 2D Laplace solve per
// call; use compute_all to reuse cached results across segments of the
// same width and layer.
SegmentImpedance compute_one_fdm(double trace_width,
                                  int layer_ordinal,
                                  const AnalysisStackup& s);

// FDM-based differential impedance for an edge-coupled pair. Builds a
// cross-section with two parallel traces (same width, edge-to-edge gap
// `spacing`) and excites the odd mode (V_p=+0.5, V_n=−0.5, ground=0).
// Z_diff = 2 / (c · √(C_odd · C_odd_air)). Returns 0 on solver failure.
double compute_diff_z0_fdm(double trace_width,
                            double spacing,
                            int layer_ordinal,
                            const AnalysisStackup& s);

// Batch driver. `engine` selects which single-segment kernel to use; the
// FDM path caches by (width, layer_ordinal) so total work scales with
// the number of unique trace geometries, not total segment count.
std::vector<SegmentImpedance> compute_all(const model::Board& board,
                                          const AnalysisStackup& s,
                                          Engine engine = Engine::ClosedForm);

struct ImpedanceColor {
    float r, g, b, a;
};

ImpedanceColor color_for_error(double z0_ohms, double target_ohms);

}  // namespace sikit::analysis
