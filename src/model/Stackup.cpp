// Stackup helpers: out-of-line because they walk a non-trivial container.

#include "model/Board.h"

namespace sikit::model {

const StackupItem* Stackup::adjacent_dielectric(std::string_view copper_name,
                                                  int side) const noexcept {
    // Locate the copper item by name.
    std::size_t idx = items.size();
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (items[i].kind == StackupItem::Kind::Copper && items[i].name == copper_name) {
            idx = i;
            break;
        }
    }
    if (idx >= items.size()) return nullptr;

    if (side < 0) {
        // Walk backward looking for the first dielectric.
        for (std::size_t i = idx; i > 0; --i) {
            if (items[i - 1].kind == StackupItem::Kind::Dielectric) return &items[i - 1];
        }
    } else {
        for (std::size_t i = idx + 1; i < items.size(); ++i) {
            if (items[i].kind == StackupItem::Kind::Dielectric) return &items[i];
        }
    }
    return nullptr;
}

const StackupItem* Stackup::any_dielectric() const noexcept {
    for (const auto& it : items) {
        if (it.kind == StackupItem::Kind::Dielectric) return &it;
    }
    return nullptr;
}

}  // namespace sikit::model
