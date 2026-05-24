#include "parser/KicadPcbParser.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <fstream>
#include <sstream>
#include <unordered_map>

#include "sexpr/SExpr.h"

namespace sikit::parser {

using sexpr::Node;
using sexpr::parse;

namespace {

// Convert KiCad mm → SI meters.
constexpr double kMmToM = 1.0e-3;

// Convert degrees → radians.
constexpr double kDegToRad = 0.017453292519943295;  // M_PI / 180.0

[[noreturn]] void fail(const Node& n, const std::string& msg) {
    throw KicadParseError(msg, n.line, n.col);
}

// Find first child of `n` whose head symbol matches `tag`. Returns nullptr if none.
const Node* find_child(const Node& n, std::string_view tag) {
    if (!n.is_list()) return nullptr;
    for (const auto& c : n.children) {
        if (c.is_list() && c.tag() == tag) return &c;
    }
    return nullptr;
}

// Collect every child of `n` whose head symbol matches `tag`.
std::vector<const Node*> find_children(const Node& n, std::string_view tag) {
    std::vector<const Node*> out;
    if (!n.is_list()) return out;
    for (const auto& c : n.children) {
        if (c.is_list() && c.tag() == tag) out.push_back(&c);
    }
    return out;
}

double expect_number(const Node& n) {
    if (!n.is_number()) fail(n, "expected number");
    return n.number;
}

std::string_view expect_string_or_symbol(const Node& n) {
    if (n.is_string() || n.is_symbol()) return n.text;
    fail(n, "expected string or symbol");
}

// Read a 2-element (x y) tail starting at child index `start` in `node`.
// Used for both (at X Y) and (xy X Y) — and (start X Y) / (end X Y).
model::Point2 read_xy_tail(const Node& node, std::size_t start = 1) {
    if (node.children.size() < start + 2) fail(node, "expected at least two numeric coordinates");
    return {
        expect_number(node.children[start]) * kMmToM,
        expect_number(node.children[start + 1]) * kMmToM,
    };
}

// Helper: walks (layers ...) inside a section and returns layer name tokens.
// Both segment-style ('(layer "F.Cu")') and via-style ('(layers "F.Cu" "B.Cu")')
// store names as strings; quoted in modern KiCad, sometimes bare symbols in old files.
std::vector<std::string> read_layer_names(const Node& list_form) {
    std::vector<std::string> names;
    for (std::size_t i = 1; i < list_form.children.size(); ++i) {
        names.emplace_back(expect_string_or_symbol(list_form.children[i]));
    }
    return names;
}

class Walker {
public:
    explicit Walker(const Node& root) : root_(root) {}

    model::Board build() {
        if (!root_.is_list() || root_.tag() != "kicad_pcb") {
            fail(root_, "top-level form must be (kicad_pcb ...)");
        }
        parse_general();
        parse_layers();
        parse_setup_stackup();
        parse_nets();
        parse_segments();
        parse_vias();
        parse_zones();
        parse_footprints();
        return std::move(board_);
    }

private:
    int layer_id_(std::string_view name, const Node& ctx) {
        auto it = layer_name_to_id_.find(std::string(name));
        if (it == layer_name_to_id_.end()) {
            fail(ctx, std::format("unknown layer name '{}'", name));
        }
        return it->second;
    }

    void parse_general() {
        if (const Node* g = find_child(root_, "general")) {
            if (const Node* t = find_child(*g, "thickness")) {
                if (t->children.size() >= 2) {
                    board_.stackup.total_thickness = expect_number(t->children[1]) * kMmToM;
                }
            }
        }
    }

    void parse_layers() {
        const Node* layers = find_child(root_, "layers");
        if (!layers) return;  // unusual but tolerable

        for (std::size_t i = 1; i < layers->children.size(); ++i) {
            const Node& row = layers->children[i];
            if (!row.is_list() || row.children.size() < 3) {
                fail(row, "expected (ordinal name type [user_name]) layer row");
            }
            model::Layer L;
            L.ordinal = static_cast<int>(expect_number(row.children[0]));
            L.name = std::string(expect_string_or_symbol(row.children[1]));
            L.type = std::string(expect_string_or_symbol(row.children[2]));
            layer_name_to_id_[L.name] = L.ordinal;
            board_.stackup.layers.push_back(std::move(L));
        }
    }

    // (setup (stackup (layer "F.Cu" (type "copper") (thickness 0.035)) ...))
    // KiCad 6+ describes the physical cross-section in the setup block.
    // Each (layer ...) item here is a stack entry, not a logical layer
    // (which we already parsed from the top-level (layers ...) form).
    void parse_setup_stackup() {
        const Node* setup = find_child(root_, "setup");
        if (!setup) return;
        const Node* stackup = find_child(*setup, "stackup");
        if (!stackup) return;

        for (const Node* it : find_children(*stackup, "layer")) {
            if (it->children.size() < 2) continue;
            model::StackupItem item;
            item.name = std::string(expect_string_or_symbol(it->children[1]));

            if (const Node* tnode = find_child(*it, "type")) {
                if (tnode->children.size() >= 2) {
                    std::string t = std::string(expect_string_or_symbol(tnode->children[1]));
                    std::transform(t.begin(), t.end(), t.begin(),
                                   [](unsigned char c) { return std::tolower(c); });
                    if (t == "copper") {
                        item.kind = model::StackupItem::Kind::Copper;
                    } else if (t.find("dielectric") != std::string::npos ||
                               t == "core" || t == "prepreg") {
                        item.kind = model::StackupItem::Kind::Dielectric;
                    } else if (t.find("solder mask") != std::string::npos) {
                        item.kind = model::StackupItem::Kind::SolderMask;
                    } else if (t.find("silk") != std::string::npos) {
                        item.kind = model::StackupItem::Kind::Silkscreen;
                    } else if (t.find("paste") != std::string::npos) {
                        item.kind = model::StackupItem::Kind::Paste;
                    }
                }
            }
            if (const Node* th = find_child(*it, "thickness")) {
                if (th->children.size() >= 2) {
                    item.thickness = expect_number(th->children[1]) * kMmToM;
                }
            }
            if (const Node* er = find_child(*it, "epsilon_r")) {
                if (er->children.size() >= 2) item.epsilon_r = expect_number(er->children[1]);
            }
            if (const Node* lt = find_child(*it, "loss_tangent")) {
                if (lt->children.size() >= 2) item.loss_tangent = expect_number(lt->children[1]);
            }
            if (const Node* m = find_child(*it, "material")) {
                if (m->children.size() >= 2) {
                    item.material = std::string(expect_string_or_symbol(m->children[1]));
                }
            }
            board_.stackup.items.push_back(std::move(item));
        }
    }

    void parse_nets() {
        for (const Node* netn : find_children(root_, "net")) {
            if (netn->children.size() < 3) fail(*netn, "expected (net id name)");
            model::Net n;
            n.id = static_cast<int>(expect_number(netn->children[1]));
            n.name = std::string(expect_string_or_symbol(netn->children[2]));
            board_.nets.push_back(std::move(n));
        }
    }

    void parse_segments() {
        for (const Node* segn : find_children(root_, "segment")) {
            model::Segment s;
            if (const Node* start = find_child(*segn, "start")) s.start = read_xy_tail(*start);
            if (const Node* end   = find_child(*segn, "end"))   s.end   = read_xy_tail(*end);
            if (const Node* w     = find_child(*segn, "width")) s.width = expect_number(w->children.at(1)) * kMmToM;
            if (const Node* lay   = find_child(*segn, "layer")) {
                auto names = read_layer_names(*lay);
                if (names.empty()) fail(*lay, "segment missing layer name");
                s.layer_ordinal = layer_id_(names[0], *lay);
            }
            if (const Node* netr  = find_child(*segn, "net"))   s.net_id = static_cast<int>(expect_number(netr->children.at(1)));
            board_.segments.push_back(s);
        }
    }

    void parse_vias() {
        for (const Node* vn : find_children(root_, "via")) {
            model::Via v;
            if (const Node* at    = find_child(*vn, "at"))    v.at = read_xy_tail(*at);
            if (const Node* sz    = find_child(*vn, "size"))  v.outer_diameter = expect_number(sz->children.at(1)) * kMmToM;
            if (const Node* dr    = find_child(*vn, "drill")) v.drill = expect_number(dr->children.at(1)) * kMmToM;
            if (const Node* lay   = find_child(*vn, "layers")) {
                auto names = read_layer_names(*lay);
                if (names.size() < 2) fail(*lay, "via layers requires two names");
                v.from_layer = layer_id_(names[0], *lay);
                v.to_layer   = layer_id_(names[1], *lay);
            }
            if (const Node* netr  = find_child(*vn, "net")) v.net_id = static_cast<int>(expect_number(netr->children.at(1)));
            board_.vias.push_back(v);
        }
    }

    // Read a (pts (xy X Y) (xy X Y) ...) form into a polygon outline.
    static std::vector<model::Point2> read_pts(const Node& pts) {
        std::vector<model::Point2> out;
        for (std::size_t i = 1; i < pts.children.size(); ++i) {
            const Node& xy = pts.children[i];
            if (!xy.is_list() || xy.tag() != "xy") fail(xy, "expected (xy X Y)");
            out.push_back(read_xy_tail(xy));
        }
        return out;
    }

    void parse_zones() {
        for (const Node* zn : find_children(root_, "zone")) {
            model::Zone z;
            if (const Node* netr  = find_child(*zn, "net"))      z.net_id = static_cast<int>(expect_number(netr->children.at(1)));
            if (const Node* nm    = find_child(*zn, "net_name")) z.net_name = std::string(expect_string_or_symbol(nm->children.at(1)));
            // Modern KiCad uses (layer "F.Cu") for single-layer zones, (layers ...) for multi.
            if (const Node* lay   = find_child(*zn, "layer")) {
                auto names = read_layer_names(*lay);
                if (!names.empty()) z.layer_ordinal = layer_id_(names[0], *lay);
            } else if (const Node* lays = find_child(*zn, "layers")) {
                auto names = read_layer_names(*lays);
                if (!names.empty()) z.layer_ordinal = layer_id_(names[0], *lays);
            }

            // User-drawn outline: (polygon (pts ...))
            if (const Node* poly = find_child(*zn, "polygon")) {
                if (const Node* pts = find_child(*poly, "pts")) {
                    z.outline.outline = read_pts(*pts);
                }
            }

            // Post-pour filled regions: zero or more (filled_polygon (layer "F.Cu") (pts ...))
            for (const Node* fp : find_children(*zn, "filled_polygon")) {
                model::Polygon p;
                if (const Node* pts = find_child(*fp, "pts")) {
                    p.outline = read_pts(*pts);
                }
                z.filled.push_back(std::move(p));
            }

            board_.zones.push_back(std::move(z));
        }
    }

    void parse_footprints() {
        for (const Node* fp : find_children(root_, "footprint")) {
            // Footprint origin (mm) + rotation (deg), applied to each pad's local (at).
            model::Point2 fp_at{0, 0};
            double fp_rot = 0.0;
            if (const Node* at = find_child(*fp, "at")) {
                fp_at = read_xy_tail(*at);
                if (at->children.size() >= 4 && at->children[3].is_number()) {
                    fp_rot = at->children[3].number * kDegToRad;
                }
            }

            for (const Node* pad : find_children(*fp, "pad")) {
                model::Pad p;
                if (const Node* at = find_child(*pad, "at")) {
                    model::Point2 local = read_xy_tail(*at);
                    // Rotate local by fp_rot then translate by fp_at.
                    double cs = std::cos(fp_rot), sn = std::sin(fp_rot);
                    p.at.x = fp_at.x + cs * local.x - sn * local.y;
                    p.at.y = fp_at.y + sn * local.x + cs * local.y;
                    if (at->children.size() >= 4 && at->children[3].is_number()) {
                        p.rotation = at->children[3].number * kDegToRad;
                    }
                }
                if (const Node* lay = find_child(*pad, "layers")) {
                    for (const auto& name : read_layer_names(*lay)) {
                        // Wildcard layers like "*.Cu" we skip — they expand to all copper
                        // layers, which we'd materialize in a follow-up commit.
                        auto it = layer_name_to_id_.find(name);
                        if (it != layer_name_to_id_.end()) {
                            p.layer_ordinals.push_back(it->second);
                        }
                    }
                }
                if (const Node* netr = find_child(*pad, "net")) {
                    if (netr->children.size() >= 2) {
                        p.net_id = static_cast<int>(expect_number(netr->children[1]));
                    }
                }
                board_.pads.push_back(p);
            }
        }
    }

    const Node& root_;
    model::Board board_;
    std::unordered_map<std::string, int> layer_name_to_id_;
};

}  // namespace

KicadParseError::KicadParseError(const std::string& msg, std::size_t ln, std::size_t cl)
    : std::runtime_error(std::format("kicad_pcb parse error at line {}, col {}: {}", ln, cl, msg)),
      line(ln),
      col(cl) {}

model::Board KicadPcbParser::parse_string(std::string_view src) {
    Node root = parse(src);  // may throw sexpr::ParseError
    return Walker(root).build();
}

model::Board KicadPcbParser::parse_file(const std::filesystem::path& path) {
    std::ifstream in(path);
    if (!in) {
        throw std::runtime_error(std::format("cannot open kicad_pcb file: {}", path.string()));
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return parse_string(ss.str());
}

}  // namespace sikit::parser
