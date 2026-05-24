#include "analysis/ChannelSynthesis.h"

#include <cmath>
#include <complex>
#include <numbers>
#include <stdexcept>

namespace sikit::analysis {

using Complex = std::complex<double>;

sikit::touchstone::TouchstoneFile synthesize_channel(
    const ChannelSpec& spec,
    const std::vector<double>& freq_hz,
    double reference_impedance) {

    // Get Z₀ and v_phase from the requested engine.
    const SegmentImpedance imp =
        (spec.engine == Engine::Fdm)
            ? compute_one_fdm(spec.trace_width, spec.layer_ordinal, spec.stackup)
            : compute_one(spec.trace_width, spec.layer_ordinal, spec.stackup);
    if (imp.z0 <= 0.0 || imp.v_phase <= 0.0) {
        throw std::runtime_error(
            "synthesize_channel: impedance engine returned invalid Z₀ or v_phase");
    }

    const double Z0 = imp.z0;
    const double v  = imp.v_phase;
    const double l  = spec.length_m;
    const double Zr = reference_impedance;
    const double two_pi = 2.0 * std::numbers::pi;

    sikit::touchstone::TouchstoneFile out;
    out.num_ports = 2;
    out.format = sikit::touchstone::Format::RealImaginary;
    out.reference_impedance = Zr;
    out.frequency_scale = 1.0;
    out.frequencies = freq_hz;
    out.s_matrices.reserve(freq_hz.size());

    for (double f : freq_hz) {
        // β = 2πf / v_phase
        const double bl = two_pi * f * l / v;
        const double cosb = std::cos(bl);
        const double sinb = std::sin(bl);

        // ABCD matrix for lossless TEM line.
        // A = D = cos(βl)
        // B = j·Z₀·sin(βl), C = j·sin(βl)/Z₀
        const Complex A(cosb, 0);
        const Complex B(0, Z0 * sinb);
        const Complex C(0, sinb / Z0);
        const Complex D(cosb, 0);

        // ABCD → S, with reference impedance Zr.
        const Complex denom = A + B / Zr + C * Zr + D;
        const Complex S11 = (A + B / Zr - C * Zr - D) / denom;
        const Complex S12 = Complex(2.0, 0) * (A * D - B * C) / denom;
        const Complex S21 = Complex(2.0, 0) / denom;
        const Complex S22 = (-A + B / Zr - C * Zr + D) / denom;

        // Column-major storage: [S11, S21, S12, S22].
        std::vector<Complex> flat{S11, S21, S12, S22};
        out.s_matrices.push_back(std::move(flat));
    }
    return out;
}

}  // namespace sikit::analysis
