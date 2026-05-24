#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "model/Board.h"
#include "parser/KicadPcbParser.h"

using namespace sikit::model;
using sikit::parser::KicadPcbParser;
using Catch::Approx;

// 4-layer board with a realistic stackup: F.Cu / prepreg / In1.Cu (Gnd) /
// core / In2.Cu (Pwr) / prepreg / B.Cu. Per-item εr and thickness
// populated as KiCad would write them out.
constexpr auto k4LayerStackupBoard = R"(
(kicad_pcb
    (version 20240108)
    (layers
        (0  "F.Cu"  signal)
        (1  "In1.Cu" power)
        (2  "In2.Cu" power)
        (31 "B.Cu"  signal)
    )
    (setup
        (stackup
            (layer "F.SilkS" (type "Top Silk Screen"))
            (layer "F.Paste" (type "Top Solder Paste"))
            (layer "F.Mask"  (type "Top Solder Mask") (thickness 0.01))
            (layer "F.Cu"    (type "copper")     (thickness 0.035))
            (layer "dielectric 1" (type "prepreg") (thickness 0.2)  (material "FR4") (epsilon_r 4.5) (loss_tangent 0.02))
            (layer "In1.Cu"  (type "copper")     (thickness 0.018))
            (layer "dielectric 2" (type "core")    (thickness 0.71) (material "FR4") (epsilon_r 4.4) (loss_tangent 0.02))
            (layer "In2.Cu"  (type "copper")     (thickness 0.018))
            (layer "dielectric 3" (type "prepreg") (thickness 0.2)  (material "FR4") (epsilon_r 4.5) (loss_tangent 0.02))
            (layer "B.Cu"    (type "copper")     (thickness 0.035))
            (layer "B.Mask"  (type "Bottom Solder Mask") (thickness 0.01))
        )
    )
    (net 0 "")
)
)";

TEST_CASE("stackup: parser extracts items with thickness and εr", "[stackup]") {
    auto b = KicadPcbParser::parse_string(k4LayerStackupBoard);
    REQUIRE(b.stackup.items.size() == 11);

    // F.Cu copper item
    const auto& f_cu = b.stackup.items[3];
    REQUIRE(f_cu.kind == StackupItem::Kind::Copper);
    REQUIRE(f_cu.name == "F.Cu");
    REQUIRE(f_cu.thickness == Approx(35e-6));

    // First dielectric: prepreg between F.Cu and In1.Cu
    const auto& prepreg1 = b.stackup.items[4];
    REQUIRE(prepreg1.kind == StackupItem::Kind::Dielectric);
    REQUIRE(prepreg1.thickness == Approx(0.2e-3));
    REQUIRE(prepreg1.epsilon_r == Approx(4.5));
    REQUIRE(prepreg1.loss_tangent == Approx(0.02));
    REQUIRE(prepreg1.material == "FR4");

    // Core between inner planes
    const auto& core = b.stackup.items[6];
    REQUIRE(core.kind == StackupItem::Kind::Dielectric);
    REQUIRE(core.thickness == Approx(0.71e-3));
    REQUIRE(core.epsilon_r == Approx(4.4));

    // Mask and silkscreen classified
    REQUIRE(b.stackup.items[0].kind == StackupItem::Kind::Silkscreen);
    REQUIRE(b.stackup.items[1].kind == StackupItem::Kind::Paste);
    REQUIRE(b.stackup.items[2].kind == StackupItem::Kind::SolderMask);
}

TEST_CASE("stackup: adjacent_dielectric for outer and inner copper", "[stackup]") {
    auto b = KicadPcbParser::parse_string(k4LayerStackupBoard);
    const auto& s = b.stackup;

    // F.Cu's dielectric below: prepreg 1 (thickness 0.2mm)
    const auto* below_fcu = s.adjacent_dielectric("F.Cu", +1);
    REQUIRE(below_fcu != nullptr);
    REQUIRE(below_fcu->thickness == Approx(0.2e-3));
    REQUIRE(below_fcu->epsilon_r == Approx(4.5));

    // F.Cu's dielectric above: none in this stack (silk/paste/mask are not dielectric)
    REQUIRE(s.adjacent_dielectric("F.Cu", -1) == nullptr);

    // In1.Cu sits between prepreg1 (above) and core (below)
    const auto* above_in1 = s.adjacent_dielectric("In1.Cu", -1);
    REQUIRE(above_in1 != nullptr);
    REQUIRE(above_in1->thickness == Approx(0.2e-3));   // prepreg 1
    const auto* below_in1 = s.adjacent_dielectric("In1.Cu", +1);
    REQUIRE(below_in1 != nullptr);
    REQUIRE(below_in1->thickness == Approx(0.71e-3));  // core
}

TEST_CASE("stackup: any_dielectric finds the first one", "[stackup]") {
    auto b = KicadPcbParser::parse_string(k4LayerStackupBoard);
    const auto* d = b.stackup.any_dielectric();
    REQUIRE(d != nullptr);
    REQUIRE(d->thickness == Approx(0.2e-3));
    REQUIRE(d->epsilon_r == Approx(4.5));
}

TEST_CASE("stackup: missing setup/stackup block leaves items empty", "[stackup]") {
    constexpr auto no_stackup = R"(
        (kicad_pcb
            (layers (0 "F.Cu" signal) (31 "B.Cu" signal))
            (net 0 "")
        )
    )";
    auto b = KicadPcbParser::parse_string(no_stackup);
    REQUIRE(b.stackup.items.empty());
    REQUIRE(b.stackup.any_dielectric() == nullptr);
}

TEST_CASE("stackup: copper item without thickness defaults to 0", "[stackup]") {
    constexpr auto src = R"(
        (kicad_pcb
            (layers (0 "F.Cu" signal))
            (setup
                (stackup
                    (layer "F.Cu" (type "copper"))
                )
            )
        )
    )";
    auto b = KicadPcbParser::parse_string(src);
    REQUIRE(b.stackup.items.size() == 1);
    REQUIRE(b.stackup.items[0].kind == StackupItem::Kind::Copper);
    REQUIRE(b.stackup.items[0].thickness == 0.0);
}
