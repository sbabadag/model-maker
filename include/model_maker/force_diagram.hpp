#pragma once

#include <string>
#include <windows.h>

namespace mm {

struct ForceDiagramData {
    std::wstring label;
    double axialI{}, shearYI{}, shearZI{}, torsionI{}, momentYI{}, momentZI{};
    double axialJ{}, shearYJ{}, shearZJ{}, torsionJ{}, momentYJ{}, momentZJ{};
    double elementLength{1.0};
    std::wstring sectionName;
    double wY{};  // distributed load (kN/m) for parabolic moment diagram
    double wZ{};
};

bool registerForceDiagramClass(HINSTANCE instance);
HWND createForceDiagramWindow(HINSTANCE instance, HWND parent, const ForceDiagramData& data);

} // namespace mm