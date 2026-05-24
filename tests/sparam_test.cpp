#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "sparam/SParam.h"

using namespace sikit::sparam;
using Catch::Approx;

namespace {

Eigen::Matrix2cd passthrough() {
    // S11=0, S21=1, S12=1, S22=0 — ideal lossless reciprocal line.
    Eigen::Matrix2cd s;
    s(0, 0) = Complex(0, 0);
    s(0, 1) = Complex(1, 0);
    s(1, 0) = Complex(1, 0);
    s(1, 1) = Complex(0, 0);
    return s;
}

Eigen::Matrix2cd attenuator_dB(double db) {
    // Lossy, matched, reciprocal: S11=S22=0, S21=S12=10^(-db/20).
    const double mag = std::pow(10.0, -db / 20.0);
    Eigen::Matrix2cd s;
    s(0, 0) = Complex(0, 0);
    s(0, 1) = Complex(mag, 0);
    s(1, 0) = Complex(mag, 0);
    s(1, 1) = Complex(0, 0);
    return s;
}
}  // namespace

TEST_CASE("sparam: s_to_t round-trip is identity", "[sparam]") {
    Eigen::Matrix2cd s;
    s(0, 0) = Complex( 0.1,  0.2);
    s(0, 1) = Complex( 0.3,  0.4);
    s(1, 0) = Complex( 0.5, -0.1);
    s(1, 1) = Complex(-0.2,  0.3);

    auto round_trip = t_to_s(s_to_t(s));
    for (int i = 0; i < 2; ++i)
        for (int j = 0; j < 2; ++j) {
            REQUIRE(round_trip(i, j).real() == Approx(s(i, j).real()).margin(1e-12));
            REQUIRE(round_trip(i, j).imag() == Approx(s(i, j).imag()).margin(1e-12));
        }
}

TEST_CASE("sparam: cascading passthrough leaves network unchanged", "[sparam]") {
    auto pass = passthrough();
    auto atten3 = attenuator_dB(3.0);

    auto c1 = cascade(pass, atten3);
    auto c2 = cascade(atten3, pass);

    REQUIRE(std::abs(c1(1, 0)) == Approx(std::abs(atten3(1, 0))).margin(1e-9));
    REQUIRE(std::abs(c2(1, 0)) == Approx(std::abs(atten3(1, 0))).margin(1e-9));
}

TEST_CASE("sparam: cascaded attenuators add in dB", "[sparam]") {
    auto a3 = attenuator_dB(3.0);
    auto a6 = attenuator_dB(6.0);
    auto cascaded = cascade(a3, a6);

    const double db = insertion_loss_db(cascaded(1, 0));
    REQUIRE(db == Approx(9.0).margin(1e-6));
}

TEST_CASE("sparam: cascading three attenuators stacks", "[sparam]") {
    auto a = attenuator_dB(1.0);
    auto b = attenuator_dB(2.0);
    auto c = attenuator_dB(3.0);
    auto ab = cascade(a, b);
    auto abc = cascade(ab, c);
    REQUIRE(insertion_loss_db(abc(1, 0)) == Approx(6.0).margin(1e-6));
}

TEST_CASE("sparam: zero-S21 throws on s_to_t", "[sparam]") {
    Eigen::Matrix2cd s;
    s.setZero();
    REQUIRE_THROWS_AS(s_to_t(s), SParamError);
}

TEST_CASE("sparam: insertion / return loss in dB", "[sparam]") {
    // |S21| = 0.5 → 6.02 dB
    REQUIRE(insertion_loss_db(Complex(0.5, 0)) == Approx(6.020599913).margin(1e-6));
    // |S11| = 0.1 → 20 dB return loss
    REQUIRE(return_loss_db(Complex(0.1, 0)) == Approx(20.0).margin(1e-6));
}

TEST_CASE("sparam: touchstone cascade is frequency-by-frequency", "[sparam]") {
    using namespace sikit::touchstone;

    TouchstoneFile a;
    a.num_ports = 2;
    a.format = Format::RealImaginary;
    a.reference_impedance = 50.0;
    a.frequency_scale = 1.0;
    a.frequencies = {1e9, 2e9};
    // Two attenuators at 3 dB and 6 dB across two frequency points.
    {
        const double m3 = std::pow(10.0, -3.0 / 20.0);
        a.s_matrices.push_back({Complex(0,0), Complex(m3,0), Complex(m3,0), Complex(0,0)});
        const double m1 = std::pow(10.0, -1.0 / 20.0);
        a.s_matrices.push_back({Complex(0,0), Complex(m1,0), Complex(m1,0), Complex(0,0)});
    }

    TouchstoneFile b = a;
    {
        const double m6 = std::pow(10.0, -6.0 / 20.0);
        b.s_matrices[0] = {Complex(0,0), Complex(m6,0), Complex(m6,0), Complex(0,0)};
        const double m2 = std::pow(10.0, -2.0 / 20.0);
        b.s_matrices[1] = {Complex(0,0), Complex(m2,0), Complex(m2,0), Complex(0,0)};
    }

    auto c = cascade(a, b);
    REQUIRE(c.frequencies.size() == 2);
    // S21 is at index 1 in column-major 2-port storage.
    REQUIRE(insertion_loss_db(c.s_matrices[0][1]) == Approx(9.0).margin(1e-6));
    REQUIRE(insertion_loss_db(c.s_matrices[1][1]) == Approx(3.0).margin(1e-6));
}

TEST_CASE("sparam: touchstone cascade rejects mismatched grids", "[sparam]") {
    using namespace sikit::touchstone;
    TouchstoneFile a;
    a.num_ports = 2;
    a.frequencies = {1e9, 2e9};
    a.s_matrices = {{Complex(0,0), Complex(1,0), Complex(1,0), Complex(0,0)},
                    {Complex(0,0), Complex(1,0), Complex(1,0), Complex(0,0)}};
    TouchstoneFile b = a;
    b.frequencies = {1e9, 3e9};  // mismatch at point 1

    REQUIRE_THROWS_AS(cascade(a, b), SParamError);
}

TEST_CASE("sparam: single-ended-to-differential basics", "[sparam]") {
    // Build a 4-port that's an ideal passthrough between (port1+,port2+)
    // and (port1-,port2-): S31=S13=1, S42=S24=1, all else zero.
    // Differential mode should also pass through (Sdd21 = 1).
    Eigen::Matrix4cd s = Eigen::Matrix4cd::Zero();
    s(2, 0) = Complex(1, 0); s(0, 2) = Complex(1, 0);  // port 1 → port 3
    s(3, 1) = Complex(1, 0); s(1, 3) = Complex(1, 0);  // port 2 → port 4

    auto sdd = sikit::sparam::single_ended_to_differential(s);
    // |Sdd21| should be 1 (ideal differential passthrough).
    REQUIRE(std::abs(sdd(1, 0)) == Approx(1.0).margin(1e-9));
}
