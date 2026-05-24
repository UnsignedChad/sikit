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

TEST_CASE("from_board: empty stackup → generic FR-4 defaults", "[trace]") {
    Board b;  // no stackup items
    auto s = AnalysisStackup::from_board(b);
    REQUIRE_FALSE(s.from_real_stackup);
    REQUIRE(s.epsilon_r == 4.4);
    REQUIRE(s.outer_dielectric_height == 0.2e-3);
}

TEST_CASE("from_board: picks up dielectric below F.Cu", "[trace]") {
    Board b;
    b.stackup.layers.push_back({0, "F.Cu", "signal"});
    StackupItem cu{StackupItem::Kind::Copper, "F.Cu", 35e-6, 0, 0, ""};
    StackupItem die;
    die.kind = StackupItem::Kind::Dielectric;
    die.name = "dielectric 1";
    die.thickness = 0.150e-3;
    die.epsilon_r = 3.8;
    b.stackup.items.push_back(cu);
    b.stackup.items.push_back(die);

    auto s = AnalysisStackup::from_board(b);
    REQUIRE(s.from_real_stackup);
    REQUIRE(s.outer_dielectric_height == Approx(0.150e-3));
    REQUIRE(s.epsilon_r == Approx(3.8));
    REQUIRE(s.copper_thickness == Approx(35e-6));
}

TEST_CASE("engine: FDM and closed-form agree to within ~25% on microstrip", "[trace]") {
    // Canonical 50Ω microstrip geometry; both engines should land in the
    // same neighbourhood (closed-form ~50, FDM ~50–60 at v0 mesh density).
    AnalysisStackup s;
    s.outer_dielectric_height = 1.524e-3;
    s.copper_thickness = 35e-6;
    s.epsilon_r = 4.4;

    const double W = 2.8e-3;
    auto cf = compute_one(W, 0, s);
    auto fdm = compute_one_fdm(W, 0, s);

    REQUIRE(cf.z0  > 0);
    REQUIRE(fdm.z0 > 0);
    const double rel = std::abs(fdm.z0 - cf.z0) / cf.z0;
    REQUIRE(rel < 0.25);
}

TEST_CASE("engine: compute_all caches FDM results per (width, layer)", "[trace]") {
    // Build a board with many segments of the same width and verify that
    // every segment ends up with the SAME z0 (i.e. the cache fired). Use
    // a relatively wide trace so the FDM grid stays small and the test
    // doesn't dominate CI time.
    Board b;
    b.stackup.layers.push_back({0, "F.Cu", "signal"});
    AnalysisStackup as;
    as.outer_dielectric_height = 1.0e-3;  // larger H → coarser absolute mesh
    as.copper_thickness = 35e-6;

    for (int i = 0; i < 4; ++i) {
        Segment s;
        s.start = {0, 0};
        s.end   = {1e-3, 0};
        s.width = 2.0e-3;   // identical across all segments
        s.layer_ordinal = 0;
        s.net_id = i + 1;
        b.segments.push_back(s);
    }

    auto rs = compute_all(b, as, Engine::Fdm);
    REQUIRE(rs.size() == 4);
    for (std::size_t i = 1; i < rs.size(); ++i) {
        REQUIRE(rs[i].z0 == rs[0].z0);
    }
    REQUIRE(rs[0].z0 > 0);
}

TEST_CASE("from_board: stripline B = sum of dielectric above + below inner copper",
          "[trace]") {
    Board b;
    b.stackup.layers.push_back({0,  "F.Cu",  "signal"});
    b.stackup.layers.push_back({1,  "In1.Cu","power"});
    b.stackup.layers.push_back({31, "B.Cu",  "signal"});

    auto push_item = [&](StackupItem::Kind k, std::string n, double t, double e = 0) {
        StackupItem it;
        it.kind = k; it.name = std::move(n); it.thickness = t; it.epsilon_r = e;
        b.stackup.items.push_back(it);
    };
    push_item(StackupItem::Kind::Copper,     "F.Cu",   35e-6);
    push_item(StackupItem::Kind::Dielectric, "prepreg", 0.10e-3, 4.5);
    push_item(StackupItem::Kind::Copper,     "In1.Cu", 18e-6);
    push_item(StackupItem::Kind::Dielectric, "core",    0.30e-3, 4.4);
    push_item(StackupItem::Kind::Copper,     "B.Cu",   35e-6);

    auto s = AnalysisStackup::from_board(b);
    REQUIRE(s.from_real_stackup);
    REQUIRE(s.inner_plane_separation == Approx(0.40e-3));  // 0.10 + 0.30
}
