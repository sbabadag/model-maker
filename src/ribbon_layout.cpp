#include "model_maker/ribbon_layout.hpp"

#include <algorithm>

namespace mm {

std::vector<int> RibbonLayout::commands(RibbonTab tab) {
    switch (tab) {
    case RibbonTab::File: return {100, 101, 102, 103, 104};
    case RibbonTab::Drawing: return {200, 201, 202, 203, 204};
    case RibbonTab::Modify: return {509, 500, 501, 502, 503, 504, 505, 506, 507, 508, 510};
    case RibbonTab::View: return {300, 301, 302, 303, 304, 305, 306, 307, 308};
    case RibbonTab::Aids: return {400, 401, 402, 403, 404};
    }
    return {};
}

RibbonGeometry RibbonLayout::layout(RibbonTab tab, int windowWidth) {
    RibbonGeometry result;
    result.ribbonHeight = height;
    result.canvas = {0, height, std::max(1, windowWidth), height};

    constexpr int left = 8;
    constexpr int top = 64;
    constexpr int buttonWidth = 64;
    constexpr int buttonHeight = 62;
    constexpr int gap = 3;
    constexpr int groupGap = 10;
    std::vector<std::pair<std::wstring, std::size_t>> groups;
    switch (tab) {
    case RibbonTab::File: groups = {{L"File", 5}}; break;
    case RibbonTab::Drawing: groups = {{L"Draw", 5}}; break;
    case RibbonTab::Modify:
        groups = {{L"Tools", 1}, {L"Modify", 5}, {L"Pattern", 2}, {L"Edit", 3}};
        break;
    case RibbonTab::View: groups = {{L"Model", 5}, {L"Navigate", 4}}; break;
    case RibbonTab::Aids: groups = {{L"Drafting Aids", 5}}; break;
    }
    const auto tabCommands = commands(tab);
    int x = left;
    std::size_t commandIndex{};
    for (const auto& [label, groupSize] : groups) {
        const int groupLeft = x - 3;
        for (std::size_t index = 0; index < groupSize && commandIndex < tabCommands.size();
             ++index, ++commandIndex) {
            result.commandButtons.push_back(
                {tabCommands[commandIndex], {x, top, x + buttonWidth, top + buttonHeight}});
            x += buttonWidth + gap;
        }
        result.groups.push_back({label, {groupLeft, 61, x, height}});
        x += groupGap;
    }
    return result;
}

} // namespace mm
