#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "analysis/TraceImpedance.h"

using namespace sikit::analysis;
using namespace sikit::model;
using Catch::Approx;

TEST_CASE("trace impedance: F.Cu uses microstrip, inner uses stripline", "[trace]") {
    AnalysisStackup s;
    const double W = 0.2e-3;

    auto outer = compute_one(W, 0, s);          // F.Cu
    auto inner = compute_one(W, 1, s);          // inner copper
    auto bottom = compute_one(W, 31, s);        // B.Cu

    REQUIRE(outer.z0 > 0.0);
    REQUIRE(inner.z0 > 0.0);
    REQUIRE(bottom.z0 > 0.0);

    // Outer and bottom should use the same formula (microstrip).
    REQUIRE(outer.z0 == Approx(bottom.z0));

    // For the typical defaults (H=0.2mm outer, B=0.4mm inner, εr=4.4),
    // both come out near 50–60Ω — they shouldn't be identical though,
    // since the formulas and the geometry differ.
    REQUIRE(outer.z0 != Approx(inner.z0));
}

TEST_CASE("trace impedance: zero width returns invalid", "[trace]") {
    AnalysisStackup s;
    auto r = compute_one(0.0, 0, s);
    REQUIRE(r.z0 == 0.0);
    REQUIRE_FALSE(r.in_valid_range);
}

TEST_CASE("trace impedance: compute_all skips non-copper segments", "[trace]") {
    Board b;
    b.stackup.layers.push_back({0,  "F.Cu",    "signal"});
    b.stackup.layers.push_back({32, "F.SilkS", "user"});

    Segment cu;
    cu.start = {0, 0};
    cu.end   = {1e-3, 0};
    cu.width = 0.2e-3;
    cu.layer_ordinal = 0;
    cu.net_id = 1;
    b.segments.push_back(cu);

    Segment silk = cu;
    silk.layer_ordinal = 32;
    b.segments.push_back(silk);

    auto rs = compute_all(b, {});
    REQUIRE(rs.size() == 1);
    REQUIRE(rs[0].layer_ordinal == 0);
    REQUIRE(rs[0].net_id == 1);
}

TEST_CASE("trace impedance: per-segment index lines up with input order", "[trace]") {
    Board b;
    b.stackup.layers.push_back({0, "F.Cu", "signal"});

    for (int i = 0; i < 3; ++i) {
        Segment s;
        s.width = (0.1 + 0.05 * i) * 1e-3;
        s.layer_ordinal = 0;
        s.net_id = i + 1;
        b.segments.push_back(s);
    }

    auto rs = compute_all(b, {});
    REQUIRE(rs.size() == 3);
    REQUIRE(rs[0].segment_index == 0);
    REQUIRE(rs[1].segment_index == 1);
    REQUIRE(rs[2].segment_index == 2);
    // Narrower trace → higher impedance.
    REQUIRE(rs[0].z0 > rs[1].z0);
    REQUIRE(rs[1].z0 > rs[2].z0);
}

TEST_CASE("color_for_error: tolerance bands", "[trace]") {
    auto green  = color_for_error(50.0, 50.0);
    auto yellow = color_for_error(53.5, 50.0);   // 7% off
    auto red    = color_for_error(60.0, 50.0);   // 20% off

    REQUIRE(green.g  > green.r);
    REQUIRE(yellow.r > yellow.b);
    REQUIRE(yellow.g > yellow.b);
    REQUIRE(red.r    > red.g);
}

TEST_CASE("color_for_error: zero target returns gray", "[trace]") {
    auto c = color_for_error(50.0, 0.0);
    REQUIRE(c.r == Approx(0.5f));
    REQUIRE(c.g == Approx(0.5f));
    REQUIRE(c.b == Approx(0.5f));
}
