// Per-trace impedance analysis with a swappable solver engine.
//
//   Engine::ClosedForm   IPC-2141A formulas (fast, ±5–10%).
//   Engine::Fdm          In-house 2D finite-difference solver (slower,
//                        captures real stackup, ±10–15% at v0 mesh density).

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
    double v_phase = 0.0;     // m/s — phase velocity along the trace
    double eps_eff = 0.0;     // effective relative permittivity
    bool in_valid_range = true;
};

SegmentImpedance compute_one(double trace_width,
                             int layer_ordinal,
                             const AnalysisStackup& s);

SegmentImpedance compute_one_fdm(double trace_width,
                                  int layer_ordinal,
                                  const AnalysisStackup& s);

double compute_diff_z0_fdm(double trace_width,
                            double spacing,
                            int layer_ordinal,
                            const AnalysisStackup& s);

std::vector<SegmentImpedance> compute_all(const model::Board& board,
                                          const AnalysisStackup& s,
                                          Engine engine = Engine::ClosedForm);

struct ImpedanceColor {
    float r, g, b, a;
};

ImpedanceColor color_for_error(double z0_ohms, double target_ohms);

}  // namespace sikit::analysis
