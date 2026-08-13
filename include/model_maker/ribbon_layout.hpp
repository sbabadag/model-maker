#pragma once

#include <string>
#include <vector>

namespace mm {

enum class RibbonTab {
    File,
    Drawing,
    Modify,
    View,
    Aids
};

struct UiRect {
    int left{};
    int top{};
    int right{};
    int bottom{};
};

struct RibbonButtonLayout {
    int commandId{};
    UiRect rect{};
};

struct RibbonGroupLayout {
    std::wstring label;
    UiRect rect{};
};

struct RibbonGeometry {
    int ribbonHeight{};
    UiRect canvas{};
    std::vector<RibbonButtonLayout> commandButtons;
    std::vector<RibbonGroupLayout> groups;
};

class RibbonLayout {
public:
    static constexpr int height = 110;
    static std::vector<int> commands(RibbonTab tab);
    static RibbonGeometry layout(RibbonTab tab, int windowWidth);
};

} // namespace mm
