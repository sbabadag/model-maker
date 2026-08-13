#pragma once

#include "model_maker/camera.hpp"
#include "model_maker/document.hpp"
#include "model_maker/drafting.hpp"
#include "model_maker/render_backend.hpp"
#include "model_maker/renderer.hpp"
#include "model_maker/ribbon_layout.hpp"
#include "model_maker/view_cube_renderer.hpp"

#include <windows.h>
#include <commctrl.h>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>
#include <thread>

class QComboBox;

namespace mm {

class Application {
public:
    explicit Application(HINSTANCE instance, HWND parentCanvas = nullptr);
    ~Application();
    int run(int showCommand, std::optional<std::filesystem::path> startupDxf = std::nullopt);

    HWND canvasHandle() const { return canvas_; }
    HWND windowHandle() const { return window_; }
    void createMainWindow(int showCommand = SW_SHOW);
    void selectTool(DrawTool tool);
    void deactivateAllCommands();
    void startTransformCommand(TransformCommand command);
    void zoomExtents2D();
    void startZoomWindow2D();
    void toggle3DView();

    // Style controls (for Qt toolbar integration)
    bool snapEnabled() const noexcept { return snapEnabled_; }
    void setSnapEnabled(bool enabled) noexcept { snapEnabled_ = enabled; }
    bool orthoEnabled() const noexcept { return orthoEnabled_; }
    void setOrthoEnabled(bool enabled) noexcept { orthoEnabled_ = enabled; }
    const SnapTypeMask& enabledSnapTypes() const noexcept { return enabledSnapTypes_; }
    void toggleSnapType(SnapType type) noexcept;
    std::string currentLayer() const noexcept { return currentLayer_; }
    void setCurrentLayer(const std::string& layer) { currentLayer_ = layer; }
    bool createLayer(std::string name);
    bool deleteLayer(const std::string& name);
    bool renameLayer(const std::string& oldName, std::string newName);
    int currentColorChoice() const noexcept { return currentColorChoice_; }
    void setCurrentColorChoice(int index) noexcept;
    int currentLineTypeChoice() const noexcept { return currentLineTypeChoice_; }
    void setCurrentLineTypeChoice(int index) noexcept;
    void refreshLayerList();
    void setLayerComboWidget(QComboBox* combo) noexcept { layerComboWidget_ = combo; }
    std::vector<std::string> layerNames() const;
    const std::unordered_map<std::string, EntityProperties>& layerProperties() const;
    // Color palette
    static const std::vector<std::pair<const wchar_t*, std::optional<std::uint32_t>>>&
    colorPalette();

private:
    static LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK canvasProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK viewCubeProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK propsWndProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK layerCellEditSubclass(HWND window, UINT message, WPARAM wParam,
                                                   LPARAM lParam, UINT_PTR subclassId,
                                                   DWORD_PTR referenceData);
    LRESULT handleMessage(UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT handleCanvasMessage(UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT handleViewCubeMessage(UINT message, WPARAM wParam, LPARAM lParam);

    void createControlPanel();
    HWND createButton(const wchar_t* text, int id, int x, int y, int width, int height,
                      DWORD style = BS_PUSHBUTTON);
    void layoutChildren(int width, int height);
    void paintRibbon();
    void drawOwnerButton(const DRAWITEMSTRUCT& item);
    void activateRibbonTab(RibbonTab tab);
    void onCanvasPaint();
    void onViewCubePaint();
    void onLeftButtonDown(int x, int y);
    void onLeftButtonUp(int x, int y);
    void onMouseMove(int x, int y, WPARAM buttons);
    void onCharacter(wchar_t character);
    void executeCommand(int id);
    void cancelTransformCommand();
    void setStandardView(StandardView view);
    void startWorkPlaneCommand();
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
    void updatePropertiesPanel();
    void refreshLayerCombo();
    void syncStyleControls();
    void handleStyleComboChange(int id);
    void refreshLayerManager();
    void updateLayerManagerVisibility();
    void handleLayerManagerCommand(int id);
    LRESULT handleLayerManagerNotification(const NMHDR& notification);
    std::optional<std::string> selectedLayerName() const;
    std::string currentLayerName() const;
    void editLayerProperty(const std::string& name, int subItem, POINT screenPoint);
    void beginLayerTextEdit(const std::string& name, int row, int subItem);
    void commitLayerTextEdit();
    void pushUndoSnapshot();
    void undo();
    void redo();
    EntityProperties currentEntityProperties() const;
    void addStyledModel(WireframeModel model);
    void updateStatus();
    void invalidateCanvas();
    void invalidateViewCube();
    void activate3DNavigation();
    void addCube();
    void addPyramid();
    void saveDocument();
    void openDocument();
    void importDxf();
    void beginDxfImport(const std::filesystem::path& path);
    void finishDxfImport();
    void exportDxf();
    void exportS2K();
    void exportOpenSees();
    void runOpenSees();
    void loadOpenSeesResults();
    void clearOpenSeesResults();
    void analyzeOpenSees();
    void showOpenSeesOutput(const std::wstring& output);
    void hideOpenSeesOutput();
    void saveOptions();
    void loadOptions();
    void processCommandLine(const std::wstring& command);
    bool commandBarInput(const std::wstring& input);
    void updateCommandBar();
    void showError(const wchar_t* title, const std::exception& error) const;
    void cycleNodeConstraint();
    void clearNodeConstraintsAction();
    void setSelectedNodeConstraintsFixed();
    void setSelectedNodeConstraintsPinned();
    void setSelectedNodeConstraintsFree();
    void completeNodeWindowSelection(int x, int y, int vw, int vh);
    void applyNodeDofFromCombo();
    std::optional<std::filesystem::path> chooseFile(bool save, bool dxf = false) const;
    Vec3 screenTo2D(int x, int y) const noexcept;
    HCURSOR currentCanvasCursor() const noexcept;
    DraftView draftView() const;

    HINSTANCE instance_{};
    HWND window_{};
    HWND canvas_{};
    HWND viewCube_{};
    HWND status_{};
    HWND openseesLog_{};
    HWND openseesLogClose_{};
    HWND commandBar_{};
    HWND commandBarPrompt_{};
    HWND dxfProgressBar_{};
    HWND lineButton_{};
    HWND polylineButton_{};
    HWND rectangleButton_{};
    HWND circleButton_{};
    HWND face3DButton_{};
    HWND layerManagerButton_{};
    HWND snapButton_{};
    HWND gridSnapButton_{};
    HWND dynamicInputButton_{};
    HWND snapSettingsButton_{};
    HWND polarTrackingButton_{};
    HWND snapPanel_{};
    std::array<HWND, 14> snapTypeCheckboxes_{};
    std::array<HWND, 4> styleLabels_{};
    HWND layerCombo_{}, colorCombo_{}, lineTypeCombo_{}, profileCombo_{};
    QComboBox* layerComboWidget_{};
    HWND layerPanel_{};
    HWND layerTitle_{};
    HWND tooltipWnd_{};
    HWND layerSearch_{};
    HWND layerTree_{};
    HWND layerList_{};
    HWND layerStatus_{};
    HWND layerCellEditor_{};
    std::array<HWND, 5> layerToolbarButtons_{};
    HWND propsPanel_{};
    HWND propsSearch_{};
    HWND propsList_{};
    HWND propsClose_{};
    HWND propsFilterBtn_{};
    bool propsPanelOpen_{};
    HWND filterPopup_{};
    HWND filterLayerEdit_{};
    HWND filterColorEdit_{};
    HWND filterLengthEdit_{};
    HWND filterBtnFind_{};
    HWND filterBtnSelect_{};
    bool filterPopupOpen_{};
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
    HWND rotateButton_{};
    HWND rotate3DButton_{};
    HWND view3DButton_{};
    HWND workPlaneButton_{};
    HWND ucsButton_{};
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
    ViewCubeRenderer viewCubeRenderer_;
    std::unique_ptr<IRenderBackend> renderBackend_;
    EditMode mode_{EditMode::Draw2D};
    DrawTool tool_{DrawTool::Line};
    std::optional<Vec3> anchor_;
    std::vector<Vec3> facePoints_;
    std::optional<SnapResult> hover_;
    bool snapEnabled_{true};
    SnapTypeMask enabledSnapTypes_{};
    bool snapPanelOpen_{};
    bool layerManagerOpen_{true};
    bool showUsedLayersOnly_{};
    bool profilePanelOpen_{};
    std::string currentLayer_{"0"};
    int currentColorChoice_{};
    int currentLineTypeChoice_{};
    int currentProfileChoice_{};
    std::vector<Vec3> openseesDisplacements_;
    std::vector<double> openseesElementForces_;
    bool openseesResultsLoaded_{};
    double openseesScale_{20.0};
    enum class ResultView { None, Deformed, MomentY, MomentZ, Axial, ShearY, ShearZ, Torsion };
    ResultView resultView_{ResultView::None};
    double resultScale_{1.0};
    bool depthClipEnabled_{true};
    double depthClipZMin_{-1e100};
    double depthClipZMax_{1e100};

    std::vector<std::string> displayedLayers_;
    std::filesystem::path optionsPath_;
    std::string editingLayerName_;
    int editingLayerSubItem_{};
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
    bool performanceOverlayEnabled_{};
    bool nodeConstraintVisible_{};
    std::unordered_set<std::string> selectedNodeConstraints_;
    std::optional<POINT> nodeSelectionFirstCorner_;
    HWND nodeDofPanel_{};
    HWND nodeDofCombo_{};
    HWND nodeDofApply_{};
    HWND nodeDofClose_{};
    bool beamLoadMode_{};
    std::optional<std::size_t> beamLoadTargetIndex_;
    std::optional<BeamLoad> pendingBeamLoad_;
    bool drawingActive_{true};
    std::optional<POINT> lastRubberBandFrom_;
    std::optional<POINT> lastRubberBandTo_;
    std::optional<POINT> lastCrosshair_;
    SnapMarkerSymbol lastXorSymbol_{SnapMarkerSymbol::None};
    // Ghost-free snap marker: saved background under previous marker
    HBITMAP snapBgBitmap_{};
    POINT snapBgPos_{};
    int snapBgSize_{};
    bool snapOnlyRepaint_{};
    TransformCommand transformCommand_{TransformCommand::None};
    TransformCommand lastTransformCommand_{TransformCommand::None};
    TransformPhase transformPhase_{TransformPhase::Selecting};
    std::vector<std::size_t> selectedModels_;
    std::optional<POINT> selectionFirstCorner_;
    std::optional<Vec3> transformBase_;
    std::optional<Vec3> rotateAxis_;
    std::optional<double> offsetDistance_;
    double filletRadius_{1.0};
    std::optional<Vec3> filletFirstPick_;
    std::optional<std::size_t> arrayItemCount_;
    std::vector<WireframeModel> modifierBoundaries_;
    WorkPlane workPlane_{};
    bool workPlanePicking_{};
    std::vector<Vec3> workPlanePoints_;
    std::wstring input_;
    std::vector<std::wstring> commandHistory_;
    std::size_t commandHistoryIndex_{};
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
    bool viewCubeMouseTracking_{};
    POINT viewCubeCursor_{-1, -1};
    POINT lastMouse_{};
    POINT cursorScreen_{};
    std::jthread dxfImportThread_;
    std::mutex dxfImportMutex_;
    std::optional<Document> pendingDxfDocument_;
    std::string pendingDxfError_;
    std::atomic_bool dxfImportInProgress_{};
    std::atomic<std::uint64_t> dxfBytesRead_{};
    std::atomic<std::uint64_t> dxfTotalBytes_{};
    bool openseesOutputVisible_{};
};

} // namespace mm
