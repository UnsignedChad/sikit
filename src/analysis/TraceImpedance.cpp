#include "analysis/TraceImpedance.h"

#include <cmath>

#include "impedance/Impedance.h"

namespace sikit::analysis {

namespace {

bool is_outer_copper(int ord) {
    return ord == 0 || ord == 31;
}

}  // namespace

SegmentImpedance compute_one(double trace_width, int layer_ordinal,
                              const AnalysisStackup& s) {
    SegmentImpedance r;
    r.layer_ordinal = layer_ordinal;
    r.trace_width = trace_width;

    if (trace_width <= 0.0) {
        r.z0 = 0.0;
        r.in_valid_range = false;
        return r;
    }

    if (is_outer_copper(layer_ordinal)) {
        sikit::impedance::MicrostripParams mp{
            .trace_width = trace_width,
            .dielectric_height = s.outer_dielectric_height,
            .trace_thickness = s.copper_thickness,
            .epsilon_r = s.epsilon_r,
        };
        r.z0 = sikit::impedance::microstrip_z0(mp);
        r.in_valid_range = sikit::impedance::microstrip_in_valid_range(mp);
    } else {
        sikit::impedance::StriplineParams sp{
            .trace_width = trace_width,
            .plane_separation = s.inner_plane_separation,
            .trace_thickness = s.copper_thickness,
            .epsilon_r = s.epsilon_r,
        };
        r.z0 = sikit::impedance::stripline_z0(sp);
        r.in_valid_range = sikit::impedance::stripline_in_valid_range(sp);
    }
    return r;
}

std::vector<SegmentImpedance> compute_all(const model::Board& board,
                                           const AnalysisStackup& s) {
    std::vector<SegmentImpedance> out;
    out.reserve(board.segments.size());
    for (std::size_t i = 0; i < board.segments.size(); ++i) {
        const auto& seg = board.segments[i];
        const auto* L = board.find_layer(seg.layer_ordinal);
        if (!L || !L->is_copper()) continue;

        auto r = compute_one(seg.width, seg.layer_ordinal, s);
        r.segment_index = i;
        r.net_id = seg.net_id;
        out.push_back(r);
    }
    return out;
}

ImpedanceColor color_for_error(double z0_ohms, double target_ohms) {
    if (target_ohms <= 0.0 || z0_ohms <= 0.0) {
        return {0.5f, 0.5f, 0.5f, 0.8f};  // gray = unknown
    }
    const double err = std::abs(z0_ohms - target_ohms) / target_ohms;

    if (err < 0.05) {
        return {0.25f, 0.85f, 0.30f, 0.85f};  // green
    }
    if (err < 0.10) {
        return {0.95f, 0.85f, 0.20f, 0.85f};  // yellow
    }
    return {0.92f, 0.30f, 0.25f, 0.85f};      // red
}

}  // namespace sikit::analysis
