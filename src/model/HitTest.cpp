#include "model/HitTest.h"

#include <cmath>
#include <limits>

namespace sikit::hittest {

const char* name(Hit::Kind k) noexcept {
    switch (k) {
        case Hit::Kind::Zone:    return "zone";
        case Hit::Kind::Segment: return "segment";
        case Hit::Kind::Via:     return "via";
        case Hit::Kind::Pad:     return "pad";
        case Hit::Kind::None:    break;
    }
    return "";
}

namespace {

double dist_squared(model::Point2 a, model::Point2 b) {
    const double dx = a.x - b.x;
    const double dy = a.y - b.y;
    return dx * dx + dy * dy;
}

// Shortest distance from point p to line segment ab.
double dist_to_segment(model::Point2 p, model::Point2 a, model::Point2 b) {
    const double dx = b.x - a.x;
    const double dy = b.y - a.y;
    const double len2 = dx * dx + dy * dy;
    if (len2 <= 0.0) return std::sqrt(dist_squared(p, a));
    double t = ((p.x - a.x) * dx + (p.y - a.y) * dy) / len2;
    if (t < 0.0) t = 0.0;
    if (t > 1.0) t = 1.0;
    const model::Point2 proj{a.x + t * dx, a.y + t * dy};
    return std::sqrt(dist_squared(p, proj));
}

// Standard ray-casting point-in-polygon. Treats holes as additive: a point
// is inside the polygon iff it is in the outline AND not in any hole.
bool point_in_ring(const std::vector<model::Point2>& ring, model::Point2 p) {
    bool inside = false;
    const std::size_t n = ring.size();
    if (n < 3) return false;
    for (std::size_t i = 0, j = n - 1; i < n; j = i++) {
        const auto& pi = ring[i];
        const auto& pj = ring[j];
        if (((pi.y > p.y) != (pj.y > p.y)) &&
            (p.x < (pj.x - pi.x) * (p.y - pi.y) / (pj.y - pi.y) + pi.x)) {
            inside = !inside;
        }
    }
    return inside;
}

bool point_in_polygon(const model::Polygon& poly, model::Point2 p) {
    if (!point_in_ring(poly.outline, p)) return false;
    for (const auto& h : poly.holes) {
        if (point_in_ring(h, p)) return false;
    }
    return true;
}

}  // namespace

Hit at_point(const model::Board& board, model::Point2 world,
             double pick_radius) {
    // Default pad radius matches PadMesher's visual size.
    constexpr double kVisualPadRadius = 0.50e-3;

    // 1. Pads (highest priority — small, on top).
    for (const auto& p : board.pads) {
        const double tol = kVisualPadRadius + pick_radius;
        if (dist_squared(world, p.at) <= tol * tol) {
            int layer = p.layer_ordinals.empty() ? 0 : p.layer_ordinals.front();
            return {Hit::Kind::Pad, p.net_id, layer};
        }
    }

    // 2. Vias.
    for (const auto& v : board.vias) {
        const double r = 0.5 * v.outer_diameter + pick_radius;
        if (dist_squared(world, v.at) <= r * r) {
            return {Hit::Kind::Via, v.net_id, v.from_layer};
        }
    }

    // 3. Segments.
    for (const auto& s : board.segments) {
        const double tol = 0.5 * s.width + pick_radius;
        if (dist_to_segment(world, s.start, s.end) <= tol) {
            return {Hit::Kind::Segment, s.net_id, s.layer_ordinal};
        }
    }

    // 4. Zones (largest, lowest priority).
    for (const auto& z : board.zones) {
        for (const auto& fp : z.filled) {
            if (point_in_polygon(fp, world)) {
                return {Hit::Kind::Zone, z.net_id, z.layer_ordinal};
            }
        }
    }

    return {};
}

}  // namespace sikit::hittest
