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
#include "model_maker/view_definition.hpp"
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
// Uygulama, SpaceMouse'u yalniz 3B gorunumde kullanir ve nesneyi unique_ptr
// ile tutar. Baslik zincirine <SpaceMouse/*> yayilmamasi icin burada yalniz
// forward-declare kullaniriz (navlib makrolari Qt/class duzenini bozuyordu);
// tam tanim src/application.cpp'te include edilir.
#ifdef MM_HAS_SPACEMOUSE
class SpaceMouseNav;
#endif

class Application {
public:
    explicit Application(HINSTANCE instance, HWND parentCanvas = nullptr);
    ~Application();
    int run(int showCommand, std::optional<std::filesystem::path> startupDxf = std::nullopt);

    // ANA tuval (aktif viewport degil) — Qt kabugu boyutlandirma/odak icin.
    HWND canvasHandle() const { return mainCanvas(); }
    // Su an komut alan (aktif) tuval — odagi geri vermek icin.
    HWND activeCanvasHandle() const { return canvas_; }
    HWND windowHandle() const { return window_; }
    void createMainWindow(int showCommand = SW_SHOW);
    void selectTool(DrawTool tool);
    void deactivateAllCommands();
    void startTransformCommand(TransformCommand command);
    void zoomExtents2D();
    void startZoomWindow2D();
    void toggle3DView();
    void ensureSpaceMouseStarted();
    void applyStartupDefaults();
    // VARSAYILAN BASLANGIC: 3B + Solid + GL (guvenli). GL, canvas gecerli
    // boyut alinca acilir; acilis basarisizsa GDI'ye duser; onceki acilista
    // GL cokmesi/donmasi olduysa (guard dosyasi) bu sefer GDI ile baslar.
    void applyStartupDefaults3D();
    void tryEnableStartupGpu();
    // TEKLA-TARZI YAPI GRIDI olusturma (Qt dialogdan parametrelerle).
    // X/1-2-3 (dikey) ve Y/A-B-C (yatay) aks cizgilerini workPlane
    // duzleminde uretir; etiketler verilen harf/rakamdan baslar.
    void createModelGrid(const Vec3& origin, const Vec3& xDir, const Vec3& yDir,
                         std::vector<double> xSpacings, std::vector<double> ySpacings,
                         std::vector<double> zLevels,
                         std::vector<std::wstring> xLabels, std::vector<std::wstring> yLabels,
                         std::vector<std::wstring> zLabels);

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
    // PROFIL CIZIM MODU: dogrudan cizilen cizgiyi secili profille kirişe
    // donusturur (cizgi eklenir -> secilir -> atama akisi -> secim biter).
    void assignProfileToLine(const Vec3& from, const Vec3& to,
                             const std::string& profileName);
    // Qt profil secici: bos = normal cizim; dolu = cizim profillidir.
    void setPendingProfileName(std::string name) { pendingProfileName_ = std::move(name); }
    const std::string& pendingProfileName() const noexcept { return pendingProfileName_; }
    bool hasSelection() const noexcept { return !selectedModels_.empty(); }
    // KOLON: tıklanan noktaya dik (Z boyunca) profil ekstrude kolon koyar.
    // Profil + malzeme + Top/Bottom yukseklik props panelinden gelir.
    void setColumnProps(double top, double bottom, std::string material) {
        columnTopZ_ = top; columnBottomZ_ = bottom; columnMaterial_ = std::move(material);
    }
    void placeColumn(const Vec3& point);
    double columnTopZ() const noexcept { return columnTopZ_; }
    double columnBottomZ() const noexcept { return columnBottomZ_; }
    std::string columnMaterial() const noexcept { return columnMaterial_; }
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
    void saveDocumentAs();
    void openDocument();
    // Kayitli/açik belge yolu: Ctrl+S uzerine kaydeder (Save As istisnasi).
    std::optional<std::filesystem::path> currentFilePath_;
    void importDxf();
    void beginDxfImport(const std::filesystem::path& path);
    void finishDxfImport();
    void exportDxf();

    // Work plane (UCS) — Qt menü/ribbon erişimi için public
    // F1: GPU hatti — GL backend uretimi + F9 ile GDI/GL gecisi
    void toggleGpuLines();
    void setVisualStyle(VisualStyle style) noexcept;
    // Standart gorunus (On/Arka/Sol/Sag/Ust/Alt/ISO) — Qt menu + Ctrl+1..7.
    void setStandardView(StandardView view);
    void purgeStaleAxisLines(); // eski eksen cizgisi artiklarini temizle
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
    // IKI NOKTALI GORUNUS (Tekla): iki nokta secilir (snap/track aktif);
    // ikinci noktada ViewDefinition uretilip twoPointViewCallback_ cagrilir
    // (Qt yeni MDI penceresi acar). Esc/sag tik iptal.
    void startTwoPointViewCommand();
    void setTwoPointViewCallback(std::function<void(const ViewDefinition&)> callback) {
        twoPointViewCallback_ = std::move(callback);
    }
    // COKLU VIEWPORT (Tekla gorunusleri): ikincil gorunus, ana tuvalle AYNI
    // pencere sinifi ve AYNI olay isleyicisiyle calisan bir Win32 tuvalidir;
    // tum komutlar/kisayollar/snap/gumball aynen calisir. parent = Qt kap
    // widget'inin HWND'si. Donus: tuval HWND (hata -> nullptr).
    HWND createViewport(HWND parent, const ViewDefinition& view);
    void destroyViewport(HWND canvas);   // DestroyWindow; WM_DESTROY kaydi siler
    void fitViewport(HWND canvas);       // gorunus dilimine sigdir
    std::optional<ViewDefinition> viewportDefinition(HWND canvas) const;
    void setViewportDepth(HWND canvas, double front, double back);
    std::size_t viewportCount() const noexcept { return views_.size(); }
    // Qt kisayollari (Ctrl+Z vb.) tuval mesaji disindan degisiklik yapar;
    // Qt kabugu bunu kisa araliklarla cagirip diger pencereleri tazeler.
    void syncPassiveViewports();
    // Ikincil gorunus pencereleri icin salt-okunur erisim.
    const Document& document() const noexcept { return document_; }
    const std::vector<std::size_t>& selectedModelIndices() const noexcept { return selectedModels_; }
    VisualStyle visualStyle() const noexcept { return visualStyle_; }
    void cancelWorkPlaneCommand();
    void commitWorkPlanePoint(const Vec3& point);
    void resetWorkPlane();

    // Parametre istegi: Qt bir edit box (QInputDialog) gosterir ve degeri
    // dondurur (false = iptal). Parametreli komutlar (Offset mesafesi, Fillet
    // yaricapi, Dizi sayisi, Divide sayisi) bunu kullanir.
    void setParameterRequest(std::function<bool(const std::wstring& prompt,
                                                const std::wstring& initial,
                                                std::wstring& out)> callback) {
        parameterRequest_ = std::move(callback);
    }
    // Divide komutunu baslatir (edit box ile bolme sayisini sorar).
    void activateDivide();

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
    HWND columnButton_{};
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
    std::unique_ptr<Renderer> renderer_{std::make_unique<Renderer>()};
    ViewCubeRenderer viewCubeRenderer_;
#ifdef MM_HAS_SPACEMOUSE
    // 3Dconnexion SpaceMouse (Navlib 4.x): 3B gorunumde kamerayi surer.
    // 2B planda baslatilmaz (Navlib yalniz 3B'de anlamli). SDK varken.
    std::unique_ptr<SpaceMouseNav> spaceMouse_;
#endif
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
    // OCC kapaliyken (MM_HAS_OCC yok) TopoDS_Shape tanimli degildir;
    // uye de yoktur; tabloyu kullanan tum kod MM_HAS_OCC ile korunur.
#ifdef MM_HAS_OCC
    std::map<std::size_t, TopoDS_Shape> occShapes_;
#endif
    // 3B kati trim alt-akisi: 1 = kesim cizgisi bekleniyor, 2 = kalacak taraf
    int trimSolidPhase_{};
    std::size_t trimSolidIndex_{};
    std::size_t trimLineIndex_{};
    // Profil uc tutamagi (grip edit) durumu:
    struct ProfileGrip {
        std::size_t solidIndex{}; // profilli kati indeksi
        bool endIsTo{};           // false = from (sari), true = to (mor)
        Vec3 fixedPoint{};        // diger (sabit) uc
        bool basePicked{};        // false = baz noktasi bekleniyor, true = hedef bekleniyor
        Vec3 basePoint{};         // birinci tik (tasima baslangici)
        Vec3 trackFrom{};         // track line baslangici (moving end -> baz secilince)
        Vec3 cursorPoint{};       // canli imlec noktasi (track line bitisi)
    };
    std::optional<ProfileGrip> profileGrip_;
    void performSolidTrimByLine(std::size_t lineIndex);
    Vec3 trimPlanePoint_{};
    Vec3 trimPlaneNormal_{};
    void executeSolidTrim(bool keepPositive);
    std::optional<std::size_t> solidTrimTargetAt(int x, int y) const;
    std::optional<std::size_t> trimLineTargetAt(int x, int y) const;
    // PROFIL UC TUTAMAKLARI (grip edit): 3B'de secili profilli katinin iki
    // ucuna (sari=from, mor=to) tutamak konur. Alt+tik tutamaga basinca move
    // modu aktif olur; birinci tik tasima baslangici (baz), ikinci tik
    // destinasyondur — normal Move komutu gibi, profil o vektorle tasinir.
    std::optional<std::pair<std::size_t, bool>> profileGripAt(int x, int y) const;
    void profileGripClick(int x, int y);
    void cancelProfileGrip();
    std::optional<Vec3> gripReferencePoint() const;
#ifdef MM_HAS_OCC
    void reExtrudeProfileGrip(std::size_t solidIndex, bool endIsTo, const Vec3& newPoint);
#endif
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
    // Acilista GL istegi (kullanici talebi: varsayilan GL). tryEnableStartupGpu
    // canvas boyutu gecerli olunca TEK KEZ dener (0x0 canvas'ta GL init yok).
    bool startupGpuEnabled_ = true;
    bool startupGpuGuardArmed_ = false; // guard dosyasi yazildi, ilk GL karesi bekleniyor
    std::wstring startupNotice_;        // GDI'ye dusus nedeni (durum cubugunda)
    void clearStartupGpuGuard();
    bool backendInitTried_ = false;
    std::function<void(const std::wstring&)> statusCallback_;
    std::function<void()> profilePickerCallback_;
    EditMode mode_{EditMode::Draw2D};
    DrawTool tool_{DrawTool::Line};
    std::string pendingProfileName_; // profil-cizim modu aktif profili
    double columnTopZ_{8500.0};      // kolon ust kotu (mm) — props paneli
    double columnBottomZ_{0.0};      // kolon alt kotu (mm)
    std::string columnMaterial_;     // kolon malzemesi (props paneli)
    std::size_t divideCount_{2};     // Divide: esit parca sayisi (varsayilan 2 = orta nokta)
    std::function<bool(const std::wstring&, const std::wstring&, std::wstring&)> parameterRequest_;
    std::optional<double> requestDoubleParameter(const std::wstring& prompt,
                                                 const std::wstring& initial);
    std::optional<std::size_t> requestCountParameter(const std::wstring& prompt,
                                                     const std::wstring& initial);

    // --- RHINO TARZI GUMBALL (tasima tutamaclari) ---
    bool gumballVisible_{false};
    GumballHandle gumballHover_{GumballHandle::None};
    GumballHandle gumballDrag_{GumballHandle::None};
    POINT gumballPressPoint_{};
    bool gumballPointerMoved_{false};
    bool gumballUndoStarted_{false};
    bool gumballNumericActive_{false};
    // SHIFT + tutamac = KOPYALA: secim kopyalanir, kopyalar suruklenir,
    // orijinaller yerinde kalir (Rhino gumball kopya davranisi).
    bool gumballCopyMode_{false};   // basma aninda Shift basili miydi
    bool gumballCopyMade_{false};   // bu suruklemede kopya olusturuldu mu
    std::vector<std::size_t> gumballCopyOriginals_; // kopya oncesi secim
    void gumballCopyIfNeeded();
    void gumballRequestNumeric();
    Vec3 gumballOrigin_{};
    Vec3 gumballDragStartOrigin_{};
    Vec3 gumballDragStartWorld_{};
    Vec3 gumballDragAxisWorld_{};
    Vec2 gumballDragAxisUnit_{};
    Vec2 gumballDragScreenOrigin_{};
    double gumballDragT0_{0.0};
    double gumballDragPixelsPerWorld_{1.0};
    Vec3 gumballAppliedDelta_{};
    // Rotate/scale ek drag durumu
    double gumballDragSign_{1.0};      // rotasyon isaret (eksen izleyiciye bakiyor mu)
    double gumballDragPrevAngle_{0.0}; // ekran aci unwrap referansi
    double gumballTotalAngle_{0.0};    // toplam uygulanan aci (radyan)
    double gumballAppliedAngle_{0.0};
    double gumballDragStartDist_{0.0}; // uniform scale ekran mesafe referansi
    double gumballAppliedFactor_{1.0};
    // YEREL CERCEVE (object-local): world degil, nesnenin kendi eksenleri.
    Vec3 gumballFrame_[3]{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    void gumballUpdateFrame();
    void updateGumball();
    GumballHandle gumballHitTest(int x, int y) const;
    double gumballWorldLength() const;
    Vec2 gumballProject(const Vec3& point) const;
    Vec3 gumballPlanePoint(int x, int y, GumballHandle handle) const;
    void gumballBeginDrag(int x, int y, GumballHandle handle);
    void gumballDragMove(int x, int y);
    void gumballEndDrag();
    void gumballCancelDrag();
    void applyGumballNumeric(double value);
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
    // workPlanePicking_ akisinin amaci: 3 nokta duzlem veya 2 nokta gorunus.
    // --- COKLU VIEWPORT -------------------------------------------------
    // Gorunuse ozgu alanlar (canvas_, camera_, mode_, renderer_, workPlane_,
    // viewDef_, visualStyle_) AKTIF viewport'a aittir. Gecis = takas:
    // views_[active] her zaman bir yer tutucu tasir (gercek alanlar uyelerdedir).
    struct ViewportState {
        HWND canvas{};
        Camera camera;
        EditMode mode{EditMode::View3D};
        std::unique_ptr<Renderer> renderer;
        WorkPlane workPlane{};
        std::optional<ViewDefinition> viewDef;
        // Gorsel stil VIEWPORT BASINA: Alt+1..4 / F2 / menü yalniz AKTIF
        // pencereyi degistirir, diger pencereler kendi stilini korur.
        VisualStyle visualStyle{VisualStyle::Wireframe};
    };
    std::vector<std::unique_ptr<ViewportState>> views_; // [0] = ana gorunus
    std::size_t activeView_{0};
    std::optional<ViewDefinition> viewDef_;  // aktif viewport'un dilimi (ana: yok)
    bool creatingViewportCanvas_{false};     // WM_NCCREATE canvas_'i ezmesin
    std::uint64_t passiveSignature_{~0ull};  // diger viewport'lari tazeleme imzasi
    void ensureViewRegistry();
    std::optional<std::size_t> viewIndexOf(HWND canvas) const;
    void swapViewFields(ViewportState& slot);
    void activateViewport(std::size_t index);
    // Gorunus penceresinde yon IKI NOKTAYLA tanimlidir: dondurme yok (dilim
    // normali sabit kalsin). Orta tus = kaydirma, Ctrl+1..7 / R = yeniden
    // sigdirma. Ana gorunuste davranis degismez.
    bool viewRotationLocked() const noexcept { return viewDef_.has_value(); }

    void paintPassiveViewport(std::size_t index);
    LRESULT routeCanvasMessage(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    void invalidateOtherViewports();
    void removeViewport(std::size_t index);
    void applyPickFilter();
    HWND mainCanvas() const noexcept {
        return (views_.empty() || activeView_ == 0) ? canvas_ : views_[0]->canvas;
    }
    enum class PointPickPurpose { WorkPlane, TwoPointView };
    PointPickPurpose pointPickPurpose_{PointPickPurpose::WorkPlane};
    std::function<void(const ViewDefinition&)> twoPointViewCallback_;
    std::wstring input_;
    std::vector<std::wstring> commandHistory_;
    std::size_t commandHistoryIndex_{};
    bool rotating_{};
    bool panning2D_{};
    bool wheelNavigating_{};
    // Rotasyon yumusatma (ustel hareketli ortalama) — ani sarsintiyi filtreler.
    double rotSmoothedDx_{};
    double rotSmoothedDy_{};
    double wheelPreviewFactor_{1.0};
    // Momentum tekerlek hiz sinirlayici: patlama halindeki detentler
    // biriktirilir, 120ms'de en fazla bir zoom uygulanir (free-spin
    // tekerlek tek dokunusta 3-4 adim zoom yapiyordu — gorunum kayiyordu).
    double wheelPendingFactor_{1.0};
    unsigned long long lastWheelApplyMs_{0};
    // Sönümlemeli zoom animasyonu (Tekla benzeri akıcılık): hedef çarpana
    // üstel yakınlama, ~15ms kareler; imleç çapası sabit kalır.
    double zoomAnimTarget_{1.0};   // kalan toplam çarpan (1 = bitti)
    Vec2 zoomAnimCursor_{};
    bool zoomAnimActive_{false};
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
