// Board model: in-memory representation of a KiCad PCB used by all of sikit.
//
// Units are SI throughout (meters, radians). KiCad's .kicad_pcb stores
// coordinates in millimeters; the parser converts at load time so downstream
// code (mesher, solver, renderer) never deals with unit mixups.

#pragma once

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sikit::model {

struct Point2 {
    double x = 0.0;
    double y = 0.0;
};

struct Layer {
    int ordinal = 0;           // KiCad layer ID (0=F.Cu, 31=B.Cu, etc.)
    std::string name;          // 'F.Cu', 'In1.Cu', 'B.Cu'
    std::string type;          // 'signal', 'power', 'mixed', 'jumper', 'user'

    bool is_copper() const noexcept {
        // KiCad copper layers occupy 0..31 inclusive.
        return ordinal >= 0 && ordinal <= 31;
    }
};

// One physical item in the board cross-section — parsed from KiCad's
// (setup (stackup ...)) block. Order in Stackup::items is top-to-bottom.
struct StackupItem {
    enum class Kind { Copper, Dielectric, SolderMask, Silkscreen, Paste, Other };

    Kind kind = Kind::Other;
    std::string name;          // copper: matches a Layer.name; dielectric: e.g. "dielectric 1"
    double thickness = 0.0;    // meters

    // Dielectric-only fields. Default 0 → "not specified".
    double epsilon_r = 0.0;
    double loss_tangent = 0.0;
    std::string material;
};

struct Stackup {
    // Logical layer table from the top-level (layers ...) form.
    std::vector<Layer> layers;
    // Physical cross-section items in top-to-bottom order. Empty if the
    // board file doesn't include a (setup (stackup ...)) block.
    std::vector<StackupItem> items;

    double total_thickness = 1.6e-3;  // meters; default 1.6mm board

    // Find the first dielectric immediately adjacent to a copper-layer item
    // with the given name. side = -1 → look above (earlier in items[]);
    // side = +1 → look below (later). Returns nullptr if none found.
    const StackupItem* adjacent_dielectric(std::string_view copper_name,
                                            int side) const noexcept;

    // Convenience: any dielectric anywhere in the stack (first one found).
    // Useful as a fallback when adjacent_dielectric returns nothing.
    const StackupItem* any_dielectric() const noexcept;
};

struct Net {
    int id = 0;
    std::string name;
};

// Track segment (a copper wire on one layer).
struct Segment {
    Point2 start;
    Point2 end;
    double width = 0.0;        // meters
    int layer_ordinal = 0;
    int net_id = 0;
};

// Through-hole, blind, or buried via.
struct Via {
    Point2 at;
    double outer_diameter = 0.0;  // pad diameter (m)
    double drill = 0.0;           // hole diameter (m)
    int from_layer = 0;
    int to_layer = 0;
    int net_id = 0;
};

// Component pad. Geometry is intentionally minimal for v0 — extend as needed.
struct Pad {
    Point2 at;
    double rotation = 0.0;     // radians
    std::vector<int> layer_ordinals;
    int net_id = 0;
};

// A closed polygon, possibly with holes (interior cutouts).
struct Polygon {
    std::vector<Point2> outline;
    std::vector<std::vector<Point2>> holes;
};

// Copper pour. The 'outline' is the user-drawn boundary; 'filled' is the
// post-processed copper after thermal reliefs and clearance subtraction.
struct Zone {
    int net_id = 0;
    std::string net_name;
    int layer_ordinal = 0;
    Polygon outline;
    std::vector<Polygon> filled;
};

struct Board {
    Stackup stackup;
    std::vector<Net> nets;
    std::vector<Segment> segments;
    std::vector<Via> vias;
    std::vector<Pad> pads;
    std::vector<Zone> zones;

    const Net* find_net(int id) const noexcept {
        auto it = std::find_if(nets.begin(), nets.end(),
                               [id](const Net& n) { return n.id == id; });
        return it == nets.end() ? nullptr : &*it;
    }

    const Net* find_net_by_name(std::string_view name) const noexcept {
        auto it = std::find_if(nets.begin(), nets.end(),
                               [name](const Net& n) { return n.name == name; });
        return it == nets.end() ? nullptr : &*it;
    }

    const Layer* find_layer(int ordinal) const noexcept {
        auto it = std::find_if(stackup.layers.begin(), stackup.layers.end(),
                               [ordinal](const Layer& l) { return l.ordinal == ordinal; });
        return it == stackup.layers.end() ? nullptr : &*it;
    }
};

}  // namespace sikit::model
