#pragma once

#include "model_maker/camera.hpp"
#include "model_maker/document.hpp"
#include "model_maker/drafting.hpp"
#include "model_maker/renderer.hpp"

#include <windows.h>
#include <filesystem>
#include <optional>
#include <string>

namespace mm {

class Application {
public:
    explicit Application(HINSTANCE instance);
    ~Application();
    int run(int showCommand);

private:
    static LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK canvasProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT handleMessage(UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT handleCanvasMessage(UINT message, WPARAM wParam, LPARAM lParam);

    void createMainWindow(int showCommand);
    void createControlPanel();
    HWND createButton(const wchar_t* text, int id, int x, int y, int width, int height, DWORD style = BS_PUSHBUTTON);
    void layoutChildren(int width, int height);
    void onCanvasPaint();
    void onLeftButtonDown(int x, int y);
    void onMouseMove(int x, int y, WPARAM buttons);
    void onCharacter(wchar_t character);
    void executeCommand(int id);
    void selectTool(DrawTool tool);
    void commitPoint(const Vec3& point);
    void cancelDrawing();
    void updateHover(int x, int y);
    void updateControls();
    void updateStatus();
    void invalidateCanvas();
    void addCube();
    void addPyramid();
    void saveDocument();
    void openDocument();
    void showError(const wchar_t* action, const std::exception& error) const;
    std::optional<std::filesystem::path> chooseFile(bool save) const;
    Vec3 screenTo2D(int x, int y) const noexcept;
    DraftView draftView() const;

    HINSTANCE instance_{};
    HWND window_{};
    HWND panel_{};
    HWND canvas_{};
    HWND status_{};
    HWND lineButton_{};
    HWND polylineButton_{};
    HWND rectangleButton_{};
    HWND circleButton_{};
    HWND snapButton_{};
    HWND gridSnapButton_{};
    HWND dynamicInputButton_{};
    HFONT uiFont_{};
    HFONT titleFont_{};
    Document document_;
    Camera camera_;
    Renderer renderer_;
    EditMode mode_{EditMode::Draw2D};
    DrawTool tool_{DrawTool::Line};
    std::optional<Vec3> anchor_;
    std::optional<SnapResult> hover_;
    bool snapEnabled_{true};
    bool gridSnapEnabled_{true};
    bool dynamicInputEnabled_{true};
    bool drawingActive_{true};
    double workPlaneZ_{};
    std::wstring input_;
    bool rotating_{};
    POINT lastMouse_{};
    POINT cursorScreen_{};
};

} // namespace mm
