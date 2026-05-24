#include "analysis/ChannelSynthesis.h"

#include <cmath>
#include <complex>
#include <numbers>
#include <stdexcept>

namespace sikit::analysis {

using Complex = std::complex<double>;

namespace {

constexpr double kC0 = 2.99792458e8;
constexpr double kMu0 = 4.0 * std::numbers::pi * 1.0e-7;  // H/m

// Per-unit-length attenuation (Np/m) for a microstrip-style line at one
// frequency. Two contributions:
//   * Conductor loss: skin-effect surface resistance Rs = √(π f μ₀ / σ)
//     divided by trace width gives R(f) Ω/m; α_c = R / (2·Z₀).
//   * Dielectric loss: α_d = (π f √εr_eff / c) · tan δ.
double total_alpha(double f, double trace_width, double Z0, double eps_eff,
                    double tan_delta, double sigma_copper) {
    if (trace_width <= 0.0 || Z0 <= 0.0) return 0.0;
    const double Rs = std::sqrt(std::numbers::pi * f * kMu0 / sigma_copper);
    const double R_per_m = Rs / trace_width;
    const double alpha_c = R_per_m / (2.0 * Z0);
    const double alpha_d = std::numbers::pi * f * std::sqrt(eps_eff) /
                           kC0 * tan_delta;
    return alpha_c + alpha_d;
}

}  // namespace

sikit::touchstone::TouchstoneFile synthesize_channel(
    const ChannelSpec& spec,
    const std::vector<double>& freq_hz,
    double reference_impedance) {

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
        // γ = α + jβ. Lossless v0 just had γ = jβ (α = 0); the loss
        // model adds conductor + dielectric attenuation per metre.
        const double alpha = total_alpha(f, spec.trace_width, Z0, imp.eps_eff,
                                          spec.stackup.tan_delta,
                                          spec.stackup.sigma_copper);
        const double beta = two_pi * f / v;
        const Complex gamma(alpha, beta);
        const Complex gl = gamma * l;
        const Complex A = std::cosh(gl);
        const Complex B = Z0 * std::sinh(gl);
        const Complex C = std::sinh(gl) / Z0;
        const Complex D = std::cosh(gl);

        const Complex denom = A + B / Zr + C * Zr + D;
        const Complex S11 = (A + B / Zr - C * Zr - D) / denom;
        const Complex S12 = Complex(2.0, 0) * (A * D - B * C) / denom;
        const Complex S21 = Complex(2.0, 0) / denom;
        const Complex S22 = (-A + B / Zr - C * Zr + D) / denom;

        std::vector<Complex> flat{S11, S21, S12, S22};
        out.s_matrices.push_back(std::move(flat));
    }
    return out;
}

}  // namespace sikit::analysis
