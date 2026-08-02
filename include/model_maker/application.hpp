#pragma once

#include "model_maker/camera.hpp"
#include "model_maker/document.hpp"
#include "model_maker/drafting.hpp"
#include "model_maker/renderer.hpp"
#include "model_maker/ribbon_layout.hpp"

#include <windows.h>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>
#include <thread>

namespace mm {

class Application {
public:
    explicit Application(HINSTANCE instance);
    ~Application();
    int run(int showCommand, std::optional<std::filesystem::path> startupDxf = std::nullopt);

private:
    static LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK canvasProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT handleMessage(UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT handleCanvasMessage(UINT message, WPARAM wParam, LPARAM lParam);

    void createMainWindow(int showCommand);
    void createControlPanel();
    HWND createButton(const wchar_t* text, int id, int x, int y, int width, int height,
                      DWORD style = BS_PUSHBUTTON);
    void layoutChildren(int width, int height);
    void paintRibbon();
    void drawOwnerButton(const DRAWITEMSTRUCT& item);
    void activateRibbonTab(RibbonTab tab);
    void onCanvasPaint();
    void onLeftButtonDown(int x, int y);
    void onLeftButtonUp(int x, int y);
    void onMouseMove(int x, int y, WPARAM buttons);
    void onCharacter(wchar_t character);
    void executeCommand(int id);
    void selectTool(DrawTool tool);
    void deactivateAllCommands();
    void startTransformCommand(TransformCommand command);
    void cancelTransformCommand();
    void toggle3DView();
    void setStandardView(StandardView view);
    void startWorkPlaneCommand();
    void zoomExtents2D();
    void startZoomWindow2D();
    void cancelZoomWindow2D();
    void completeZoomWindow2D(int x, int y);
    void cancelWorkPlaneCommand();
    void commitWorkPlanePoint(const Vec3& point);
    void commitTransformPoint(const Vec3& point);
    bool applyTrimExtendTarget(std::size_t target, const Vec3& pickPoint);
    std::optional<std::size_t> trimExtendTargetAt(int x, int y) const;
    void completeTrimExtendTargetSelection(int x, int y);
    bool toggleModelSelection(int x, int y);
    void completeWindowSelection(int x, int y);
    void commitPoint(const Vec3& point);
    void cancelDrawing();
    void clearTemporaryTracking();
    void updateHover(int x, int y);
    void updateControls();
    void updateSnapPanelVisibility();
    void refreshLayerCombo();
    EntityProperties currentEntityProperties() const;
    void addStyledModel(WireframeModel model);
    void updateStatus();
    void invalidateCanvas();
    void addCube();
    void addPyramid();
    void saveDocument();
    void openDocument();
    void importDxf();
    void beginDxfImport(const std::filesystem::path& path);
    void finishDxfImport();
    void exportDxf();
    void showError(const wchar_t* action, const std::exception& error) const;
    std::optional<std::filesystem::path> chooseFile(bool save, bool dxf = false) const;
    Vec3 screenTo2D(int x, int y) const noexcept;
    HCURSOR currentCanvasCursor() const noexcept;
    DraftView draftView() const;

    HINSTANCE instance_{};
    HWND window_{};
    HWND canvas_{};
    HWND status_{};
    HWND dxfProgressBar_{};
    HWND lineButton_{};
    HWND polylineButton_{};
    HWND rectangleButton_{};
    HWND circleButton_{};
    HWND face3DButton_{};
    HWND snapButton_{};
    HWND gridSnapButton_{};
    HWND dynamicInputButton_{};
    HWND snapSettingsButton_{};
    HWND polarTrackingButton_{};
    HWND snapPanel_{};
    std::array<HWND, 14> snapTypeCheckboxes_{};
    std::array<HWND, 3> styleLabels_{};
    HWND layerCombo_{};
    HWND colorCombo_{};
    HWND lineTypeCombo_{};
    HWND neutralButton_{};
    HWND moveButton_{};
    HWND copyButton_{};
    HWND offsetButton_{};
    HWND mirrorButton_{};
    HWND deleteButton_{};
    HWND linearArrayButton_{};
    HWND polarArrayButton_{};
    HWND trimButton_{};
    HWND extendButton_{};
    HWND filletButton_{};
    HWND view3DButton_{};
    HWND workPlaneButton_{};
    HWND zoomWindowButton_{};
    HWND visualStyleButton_{};
    HWND standardViewButton_{};
    std::vector<HWND> ribbonTabButtons_;
    std::vector<HWND> ribbonCommandButtons_;
    RibbonTab activeRibbonTab_{RibbonTab::Drawing};
    VisualStyle visualStyle_{VisualStyle::Wireframe};
    HCURSOR draftingCursor_{};
    HCURSOR modifyCursor_{};
    HCURSOR neutralCursor_{};
    HFONT uiFont_{};
    HFONT titleFont_{};
    HFONT iconFont_{};
    HBRUSH windowBrush_{};
    HBRUSH panelBrush_{};
    HBRUSH statusBrush_{};
    Document document_;
    Camera camera_;
    Renderer renderer_;
    EditMode mode_{EditMode::Draw2D};
    DrawTool tool_{DrawTool::Line};
    std::optional<Vec3> anchor_;
    std::vector<Vec3> facePoints_;
    std::optional<SnapResult> hover_;
    bool snapEnabled_{true};
    SnapTypeMask enabledSnapTypes_{};
    bool snapPanelOpen_{};
    bool gridSnapEnabled_{true};
    bool orthoEnabled_{false};
    bool polarTrackingEnabled_{false};
    bool polarTrackingLocked_{false};
    bool temporaryTrackingLocked_{false};
    std::vector<Vec3> temporaryTrackingPoints_;
    std::vector<TrackingGuide> temporaryTrackingGuides_;
    std::vector<Vec3> temporaryDerivedPoints_;
    std::optional<SnapResult> temporaryPointDwellCandidate_;
    bool dynamicInputEnabled_{true};
    bool drawingActive_{true};
    TransformCommand transformCommand_{TransformCommand::None};
    TransformCommand lastTransformCommand_{TransformCommand::None};
    TransformPhase transformPhase_{TransformPhase::Selecting};
    std::vector<std::size_t> selectedModels_;
    std::optional<POINT> selectionFirstCorner_;
    std::optional<Vec3> transformBase_;
    std::optional<double> offsetDistance_;
    double filletRadius_{1.0};
    std::optional<Vec3> filletFirstPick_;
    std::optional<std::size_t> arrayItemCount_;
    std::vector<WireframeModel> modifierBoundaries_;
    WorkPlane workPlane_{};
    bool workPlanePicking_{};
    std::vector<Vec3> workPlanePoints_;
    std::wstring input_;
    bool rotating_{};
    bool panning2D_{};
    bool wheelNavigating_{};
    double wheelPreviewFactor_{1.0};
    Vec2 wheelPreviewOffset_{};
    bool snapPreviewActive_{};
    bool snapPreviewTimerArmed_{};
    std::chrono::steady_clock::time_point lastLargeSnapEvaluation_{};
    bool zoomWindowActive_{};
    std::optional<POINT> zoomWindowFirstCorner_;
    bool viewCubeManipulating_{};
    bool viewCubeDragged_{};
    std::optional<StandardView> viewCubePressedView_;
    POINT lastMouse_{};
    POINT cursorScreen_{};
    std::jthread dxfImportThread_;
    std::mutex dxfImportMutex_;
    std::optional<Document> pendingDxfDocument_;
    std::string pendingDxfError_;
    std::atomic_bool dxfImportInProgress_{};
    std::atomic<std::uint64_t> dxfBytesRead_{};
    std::atomic<std::uint64_t> dxfTotalBytes_{};
};

} // namespace mm
