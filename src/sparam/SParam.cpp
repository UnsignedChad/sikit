#include "sparam/SParam.h"

#include <cmath>
#include <format>

namespace sikit::sparam {

namespace {

constexpr double kEpsZero = 1e-300;

bool nearly_zero(Complex z) {
    return std::abs(z) < kEpsZero;
}

}  // namespace

Eigen::Matrix2cd s_to_t(const Eigen::Matrix2cd& s) {
    if (nearly_zero(s(1, 0))) {
        throw SParamError("S21 is zero — cannot convert to T-parameters (blocking network)");
    }
    const Complex S11 = s(0, 0);
    const Complex S12 = s(0, 1);
    const Complex S21 = s(1, 0);
    const Complex S22 = s(1, 1);
    const Complex det = S11 * S22 - S12 * S21;

    Eigen::Matrix2cd T;
    T(0, 0) = -det / S21;
    T(0, 1) =  S11 / S21;
    T(1, 0) = -S22 / S21;
    T(1, 1) =  Complex(1.0) / S21;
    return T;
}

Eigen::Matrix2cd t_to_s(const Eigen::Matrix2cd& t) {
    if (nearly_zero(t(1, 1))) {
        throw SParamError("T22 is zero — cannot convert back to S-parameters");
    }
    const Complex T11 = t(0, 0);
    const Complex T12 = t(0, 1);
    const Complex T21 = t(1, 0);
    const Complex T22 = t(1, 1);

    Eigen::Matrix2cd S;
    S(0, 0) = T12 / T22;
    S(0, 1) = (T11 * T22 - T12 * T21) / T22;
    S(1, 0) = Complex(1.0) / T22;
    S(1, 1) = -T21 / T22;
    return S;
}

Eigen::Matrix2cd cascade(const Eigen::Matrix2cd& a, const Eigen::Matrix2cd& b) {
    return t_to_s(s_to_t(a) * s_to_t(b));
}

touchstone::TouchstoneFile cascade(const touchstone::TouchstoneFile& a,
                                    const touchstone::TouchstoneFile& b) {
    if (a.num_ports != 2 || b.num_ports != 2) {
        throw SParamError(std::format(
            "cascade requires 2-port files; got {} and {}", a.num_ports, b.num_ports));
    }
    if (a.frequencies.size() != b.frequencies.size()) {
        throw SParamError(std::format(
            "cascade requires matching frequency grids; got {} vs {} points",
            a.frequencies.size(), b.frequencies.size()));
    }
    // Tolerance for "same frequency": 1 part in 1e9.
    for (std::size_t i = 0; i < a.frequencies.size(); ++i) {
        const double ref = std::max(std::abs(a.frequencies[i]),
                                    std::abs(b.frequencies[i]));
        if (std::abs(a.frequencies[i] - b.frequencies[i]) > 1e-9 * ref) {
            throw SParamError(std::format(
                "frequency grid mismatch at point {}: {} vs {} Hz",
                i, a.frequencies[i], b.frequencies[i]));
        }
    }

    touchstone::TouchstoneFile out;
    out.num_ports = 2;
    out.format = a.format;
    out.reference_impedance = a.reference_impedance;
    out.frequency_scale = a.frequency_scale;
    out.frequencies = a.frequencies;
    out.s_matrices.reserve(a.frequencies.size());

    for (std::size_t k = 0; k < a.frequencies.size(); ++k) {
        // Touchstone storage is column-major: S[r + c*2].
        Eigen::Matrix2cd A, B;
        A(0, 0) = a.s_matrices[k][0];
        A(1, 0) = a.s_matrices[k][1];
        A(0, 1) = a.s_matrices[k][2];
        A(1, 1) = a.s_matrices[k][3];

        B(0, 0) = b.s_matrices[k][0];
        B(1, 0) = b.s_matrices[k][1];
        B(0, 1) = b.s_matrices[k][2];
        B(1, 1) = b.s_matrices[k][3];

        const Eigen::Matrix2cd C = cascade(A, B);
        std::vector<Complex> flat(4);
        flat[0] = C(0, 0);
        flat[1] = C(1, 0);
        flat[2] = C(0, 1);
        flat[3] = C(1, 1);
        out.s_matrices.push_back(std::move(flat));
    }
    return out;
}

Eigen::Matrix2cd single_ended_to_differential(const Eigen::Matrix4cd& s_se) {
    // Standard mixed-mode transform. Convention: port order is
    //     [P1+, P1-, P2+, P2-]
    // i.e. positive halves at indices 0, 2; negative halves at 1, 3.
    //
    // M = 1/√2 * [[1, -1,  0,  0],
    //            [0,  0,  1, -1],
    //            [1,  1,  0,  0],
    //            [0,  0,  1,  1]]
    // S_mm = M * S_se * M^{-1};  Sdd is the upper-left 2×2 block.
    Eigen::Matrix4cd M;
    const double k = 1.0 / std::sqrt(2.0);
    M << k, -k,  0,  0,
         0,  0,  k, -k,
         k,  k,  0,  0,
         0,  0,  k,  k;
    const Eigen::Matrix4cd S_mm = M * s_se * M.inverse();
    return S_mm.topLeftCorner<2, 2>();
}

double insertion_loss_db(Complex s21) {
    const double mag = std::abs(s21);
    if (mag <= 0.0) return std::numeric_limits<double>::infinity();
    return -20.0 * std::log10(mag);
}

double return_loss_db(Complex s11) {
    const double mag = std::abs(s11);
    if (mag <= 0.0) return std::numeric_limits<double>::infinity();
    return -20.0 * std::log10(mag);
}

}  // namespace sikit::sparam
