#pragma once

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

struct RibbonGeometry {
    int ribbonHeight{};
    UiRect canvas{};
    std::vector<RibbonButtonLayout> commandButtons;
};

class RibbonLayout {
public:
    static constexpr int height = 104;
    static std::vector<int> commands(RibbonTab tab);
    static RibbonGeometry layout(RibbonTab tab, int windowWidth);
};

} // namespace mm
