#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

#include "analysis/ChannelSynthesis.h"

using namespace sikit::analysis;
using Catch::Approx;

namespace {
ChannelSpec basic_spec() {
    ChannelSpec s;
    s.trace_width = 2.8e-3;
    s.layer_ordinal = 0;
    s.length_m = 0.05;
    s.stackup.outer_dielectric_height = 1.524e-3;
    s.stackup.copper_thickness = 35e-6;
    s.stackup.epsilon_r = 4.4;
    s.engine = Engine::ClosedForm;
    return s;
}
}

TEST_CASE("synthesize: zero-length channel is the identity 2-port", "[synth]") {
    auto spec = basic_spec();
    spec.length_m = 0.0;
    std::vector<double> freqs{1e9, 5e9, 10e9};

    auto t = synthesize_channel(spec, freqs);
    REQUIRE(t.num_ports == 2);
    REQUIRE(t.frequencies.size() == 3);
    for (const auto& mat : t.s_matrices) {
        // Column-major: [S11, S21, S12, S22]
        REQUIRE(std::abs(mat[0]) == Approx(0.0).margin(1e-12));   // S11 = 0
        REQUIRE(std::abs(mat[1]) == Approx(1.0).margin(1e-12));   // |S21| = 1
        REQUIRE(std::abs(mat[2]) == Approx(1.0).margin(1e-12));   // |S12| = 1
        REQUIRE(std::abs(mat[3]) == Approx(0.0).margin(1e-12));   // S22 = 0
    }
}

TEST_CASE("synthesize: matched line (Z₀ = Zref) has |S21|=1 and S11=0", "[synth]") {
    auto spec = basic_spec();
    // Pick a freq where this geometry's Z₀ from closed-form is ~50Ω.
    // We pass Zref equal to that Z₀ so |S21| should be unity (lossless,
    // perfectly matched).
    SegmentImpedance imp = compute_one(spec.trace_width, spec.layer_ordinal,
                                        spec.stackup);
    REQUIRE(imp.z0 > 0);

    std::vector<double> freqs{1e9, 5e9};
    auto t = synthesize_channel(spec, freqs, imp.z0);
    for (const auto& mat : t.s_matrices) {
        REQUIRE(std::abs(mat[0]) == Approx(0.0).margin(1e-9));   // S11
        REQUIRE(std::abs(mat[1]) == Approx(1.0).margin(1e-9));   // |S21|
    }
}

TEST_CASE("synthesize: mismatched line has non-zero |S11|", "[synth]") {
    // Use a deliberately mismatched reference (75Ω vs the trace's ~50Ω).
    // S11 will be non-zero at any frequency where βl is not a multiple of π.
    auto spec = basic_spec();
    std::vector<double> freqs{1e9, 5e9, 10e9};

    auto t = synthesize_channel(spec, freqs, /*Zref=*/75.0);

    bool saw_reflection = false;
    for (const auto& mat : t.s_matrices) {
        if (std::abs(mat[0]) > 0.01) { saw_reflection = true; break; }
    }
    REQUIRE(saw_reflection);
}

TEST_CASE("synthesize: reciprocal — S12 == S21", "[synth]") {
    auto spec = basic_spec();
    std::vector<double> freqs{1e9, 3.5e9, 7e9, 10e9};

    auto t = synthesize_channel(spec, freqs);
    for (const auto& mat : t.s_matrices) {
        REQUIRE(mat[1].real() == Approx(mat[2].real()).margin(1e-12));
        REQUIRE(mat[1].imag() == Approx(mat[2].imag()).margin(1e-12));
    }
}

TEST_CASE("synthesize: phase delay matches v_phase · l", "[synth]") {
    // For a matched line S21 = e^(-jβl). atan2 returns the phase wrapped
    // to (-π, π], so we wrap the expected phase the same way before
    // comparing.
    auto spec = basic_spec();
    SegmentImpedance imp = compute_one(spec.trace_width, spec.layer_ordinal,
                                        spec.stackup);
    const double f = 2e9;
    auto t = synthesize_channel(spec, {f}, imp.z0);
    const auto s21 = t.s_matrices[0][1];
    const double measured_phase = std::atan2(s21.imag(), s21.real());
    const double raw_expected = -2.0 * 3.14159265358979 * f * spec.length_m /
                                 imp.v_phase;
    const double expected_phase = std::remainder(raw_expected, 2.0 * 3.14159265358979);
    REQUIRE(measured_phase == Approx(expected_phase).margin(1e-6));
}

TEST_CASE("synthesize: throws on invalid geometry", "[synth]") {
    ChannelSpec spec;
    spec.trace_width = 0.0;  // invalid
    spec.layer_ordinal = 0;
    spec.length_m = 0.05;
    REQUIRE_THROWS(synthesize_channel(spec, {1e9}));
}
