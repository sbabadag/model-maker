#pragma once

#include "model_maker/camera.hpp"
#include "model_maker/document.hpp"
#include "model_maker/drafting.hpp"
#include "model_maker/profile_database.hpp"
#include "model_maker/render_backend.hpp"
#include <functional>
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
#include <map>
#include <thread>
#ifdef MM_HAS_OCC
#include <TopoDS_Shape.hxx>
#endif

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
    bool gridSnapEnabled() const noexcept { return gridSnapEnabled_; }
    void setGridSnapEnabled(bool enabled) noexcept { gridSnapEnabled_ = enabled; }
    const SnapTypeMask& enabledSnapTypes() const noexcept { return enabledSnapTypes_; }
    void toggleSnapType(SnapType type) noexcept;
    std::string currentLayer() const noexcept { return currentLayer_; }
    void setCurrentLayer(const std::string& layer);
    bool createLayer(std::string name);
    bool deleteLayer(const std::string& name);
    bool renameLayer(const std::string& oldName, std::string newName);
    int currentColorChoice() const noexcept { return currentColorChoice_; }
    void setCurrentColorChoice(int index) noexcept;
    int currentLineTypeChoice() const noexcept { return currentLineTypeChoice_; }
    void setCurrentLineTypeChoice(int index) noexcept;
    void refreshLayerList();
    void setLayerComboWidget(QComboBox* combo) noexcept { layerComboWidget_ = combo; }
    // The visible application shell is Qt; expose the profile catalog and
    // assignment action so its native toolbar can own the real picker.
    std::vector<std::string> profileNames();
    // Ozellikler kutusu icin: secili nesnenin degerleri (secim yoksa -1/bos)
    int selectedModelIndex() const;
    std::string selectedEntityProfile() const;
    std::string selectedEntityLayer() const;
    int selectedEntityColorIndex() const;
    std::string selectedEntityTypeLabel() const;
    std::string selectedEntityLineType() const;
    std::string selectedEntityMaterial() const;
    double selectedEntityProfileRotation() const;
    void setSelectedEntityProfileRotation(double degrees);
    void setSelectedEntityLineType(const std::string& lineType);
    void setSelectedEntityMaterial(const std::string& material);
    std::string selectedEntityLengthLabel() const;
    void assignProfileToSelection(const std::string& profileName);
    void setProfilePickerCallback(std::function<void()> callback) {
        profilePickerCallback_ = std::move(callback);
    }
    void pushUndoSnapshot();
    void undo();
    void redo();
    std::vector<std::string> layerNames() const;
    const std::unordered_map<std::string, EntityProperties>& layerProperties() const;
    // Color palette
    static const std::vector<std::pair<const wchar_t*, std::optional<std::uint32_t>>>&
    colorPalette();
    static const std::vector<std::string>& lineTypePalette();

    // File operations (Qt menu / shortcuts)
    void newDocument();
    void saveDocument();
    void openDocument();
    void importDxf();
    void beginDxfImport(const std::filesystem::path& path);
    void finishDxfImport();
    void exportDxf();

    // Work plane (UCS) — Qt menü/ribbon erişimi için public
    // F1: GPU hatti — GL backend uretimi + F9 ile GDI/GL gecisi
    void toggleGpuLines();
    void setVisualStyle(VisualStyle style) noexcept;
    // F5: GDI ve GL arkaplanlarini script'li orbit/zoom/pan ile otomatik
    // olculer; sonuclar BENCH-RESULT satirlariyla render.log'a yazilir.
    void runRenderBenchmark();
    bool gpuLinesEnabled() const noexcept { return gpuLinesEnabled_; }
    // Qt durum cubuguna canli metin akisi (GDI status_ STATIC'i Qt penceresinde
    // gorunmuyor — updateStatus metni bu callback ile Qt'ye tasinir).
    void setStatusCallback(std::function<void(const std::wstring&)> callback) {
        statusCallback_ = std::move(callback);
    }
    IRenderBackend* activeRenderBackend() noexcept { return renderBackend_.get(); }

    void startWorkPlaneCommand();
    void cancelWorkPlaneCommand();
    void commitWorkPlanePoint(const Vec3& point);
    void resetWorkPlane();

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
    void cancelZoomWindow2D();
    void completeZoomWindow2D(int x, int y);
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
    EntityProperties currentEntityProperties() const;
    void addStyledModel(WireframeModel model);
    void updateStatus();
    void publishStatus(const std::wstring& text);
    void invalidateCanvas();
    void invalidateViewCube();
    void activate3DNavigation();
    void addCube();
    void addPyramid();
    void addCylinder();
#ifdef MM_HAS_OCC
    void addBooleanFuse();
    void addBooleanCommon();
    void addBooleanCut();
#endif
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
    HWND profileButton_{};
    HWND profilePopup_{};
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
    // Son kati komutunun olcum mesaji (hacim) — updateStatus bunu status
    // cubuguna ekler; sadece yeni bir kati komutu degistirir.
#ifdef MM_HAS_OCC
#endif
    // Profil atama (X): Tekla .lis katalogundan kesit secimi
    void startProfileAssignment();
    void commitProfileAssignment();
    void ensureProfileCatalog();
    void refreshProfileCombo();
    void toggleProfilePopup();
    void applyProfilePopupSelection();
    std::vector<SteelProfile> profileCatalog_;
    // BRep sekil tablosu: model indeksi -> sekil (B/S/J/extrude/trim).
    // std::map = indekse gore sirali; boolean "son iki"yi sondan alir.
    std::map<std::size_t, TopoDS_Shape> occShapes_;
    // 3B kati trim alt-akisi: 1 = kesim cizgisi bekleniyor, 2 = kalacak taraf
    int trimSolidPhase_{};
    std::size_t trimSolidIndex_{};
    std::size_t trimLineIndex_{};
    void performSolidTrimByLine(std::size_t lineIndex);
    Vec3 trimPlanePoint_{};
    Vec3 trimPlaneNormal_{};
    void executeSolidTrim(bool keepPositive);
    std::optional<std::size_t> solidTrimTargetAt(int x, int y) const;
    std::optional<std::size_t> trimLineTargetAt(int x, int y) const;
    bool profileCatalogTried_{};
    bool profileAssignmentActive_{};
    std::wstring profileInput_;

    std::wstring solidStatusMessage_;
    std::unique_ptr<IRenderBackend> renderBackend_;
#ifdef _WIN32
    // OCC kopru DLL'i (mm_occ.dll) — C-API uzerinden, ABI riski yok.
    HMODULE occBridgeDll_{};
    const char* (*occBridgeVersion_)();
    int (*occBridgeSolidBox_)(double, double, double, float**, int*, unsigned int**, int*);
    void (*occBridgeFree_)(void*);
    void ensureOccBridge();
#endif
    bool gpuLinesEnabled_ = false; // GL yolu dogrulanana kadar varsayilan GDI (F9 = GL)
    bool backendInitTried_ = false;
    std::function<void(const std::wstring&)> statusCallback_;
    std::function<void()> profilePickerCallback_;
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
    std::wstring lastTrimExtendStatus_{};
    std::optional<std::size_t> polylineModelIndex_{};
    bool trimRegionRefreshed_{false};
    bool trimExtendPreviewSuppressed_{false};
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
    bool performanceOverlayEnabled_{false}; // F11 performans overlayi
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
    // Momentum tekerlek hiz sinirlayici: patlama halindeki detentler
    // biriktirilir, 120ms'de en fazla bir zoom uygulanir (free-spin
    // tekerlek tek dokunusta 3-4 adim zoom yapiyordu — gorunum kayiyordu).
    double wheelPendingFactor_{1.0};
    unsigned long long lastWheelApplyMs_{0};
    Vec2 wheelPreviewOffset_{};
    bool snapPreviewActive_{};
    bool snapPreviewTimerArmed_{};
    int paintSequence_{};
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
