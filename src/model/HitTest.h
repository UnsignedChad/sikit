// Point-in-board hit testing.
//
// Given a world-space point and a board, returns the highest-priority piece
// of geometry under the point: Pad > Via > Segment > Zone. Used by the canvas
// for hover info.
//
// Coordinates and tolerance are in SI meters (matching the rest of sikit).

#pragma once

#include <string>

#include "model/Board.h"

namespace sikit::hittest {

struct Hit {
    enum class Kind { None, Zone, Segment, Via, Pad };
    Kind kind = Kind::None;
    int net_id = 0;
    int layer_ordinal = 0;
};

// Returns the first Hit found in priority order. `pick_radius` is added to
// each geometry's intrinsic tolerance (segment width/2, via radius, pad
// radius) — set it to about a few screen pixels in world units.
Hit at_point(const model::Board& board, model::Point2 world,
             double pick_radius);

// Human-readable name for a hit kind ("zone", "segment", "via", "pad", "").
const char* name(Hit::Kind k) noexcept;

}  // namespace sikit::hittest
