#include "model_maker/ribbon_layout.hpp"

#include <algorithm>

namespace mm {

std::vector<int> RibbonLayout::commands(RibbonTab tab) {
    switch (tab) {
    case RibbonTab::File: return {100, 101, 102, 103, 104};
    case RibbonTab::Drawing: return {200, 201, 202, 203};
    case RibbonTab::Modify: return {509, 500, 501, 502, 503, 504, 505, 506, 507, 508};
    case RibbonTab::View: return {300, 301, 302, 303, 304, 305, 306};
    case RibbonTab::Aids: return {400, 401, 402, 403, 404};
    }
    return {};
}

RibbonGeometry RibbonLayout::layout(RibbonTab tab, int windowWidth) {
    RibbonGeometry result;
    result.ribbonHeight = height;
    result.canvas = {0, height, std::max(1, windowWidth), height};

    constexpr int left = 10;
    constexpr int top = 39;
    constexpr int buttonWidth = 64;
    constexpr int buttonHeight = 57;
    constexpr int gap = 6;
    int x = left;
    for (const int command : commands(tab)) {
        result.commandButtons.push_back({command, {x, top, x + buttonWidth, top + buttonHeight}});
        x += buttonWidth + gap;
    }
    return result;
}

} // namespace mm
