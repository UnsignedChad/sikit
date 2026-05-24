// Synthesize a Touchstone 2-port S-parameter file from a single trace's
// geometry and length. Closes the loop on the SI workflow:
//
//   KiCad PCB → trace width + layer + length → Z₀, v_phase (via engine)
//             → ABCD per frequency → S-params (Touchstone)
//             → eye-diagram channel
//
// This lets us draw an eye from any board trace without needing a
// measured / simulated S2P from an external tool. Math assumes a
// lossless TEM transmission line for v0; loss models (resistive,
// dielectric tangent δ, skin effect) land in a follow-up.

#pragma once

#include <vector>

#include "analysis/TraceImpedance.h"
#include "touchstone/Touchstone.h"

namespace sikit::analysis {

struct ChannelSpec {
    double trace_width = 0.0;       // m
    int    layer_ordinal = 0;
    double length_m = 0.0;          // physical trace length
    AnalysisStackup stackup;
    Engine engine = Engine::ClosedForm;
};

// Generate a 2-port Touchstone file representing `spec.length_m` of
// trace with the given cross-section, sampled at the requested
// frequencies. Reference impedance defaults to 50 Ω.
sikit::touchstone::TouchstoneFile synthesize_channel(
    const ChannelSpec& spec,
    const std::vector<double>& freq_hz,
    double reference_impedance = 50.0);

}  // namespace sikit::analysis
