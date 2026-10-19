#pragma once

// --- Per-Monitor-V2 DPI symbols (Windows 10 1703+, NTDDI_WIN10_RS2) --------
// application.cpp and main.cpp use SetProcessDpiAwarenessContext,
// GetThreadDpiAwarenessContext, DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 and
// WM_DPICHANGED_AFTERPARENT. MinGW-w64 declares the first three only when
// NTDDI_VERSION >= NTDDI_WIN10_RS2; the version macros must therefore be set
// BEFORE any <windows.h> include. This header is the first include of every
// app TU, so set them here (idempotent: honour any value already defined).
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#ifndef WINVER
#define WINVER _WIN32_WINNT
#endif
#ifndef NTDDI_VERSION
#define NTDDI_VERSION 0x0A000003 /* NTDDI_WIN10_RS2 */
#endif

#include "model_maker/camera.hpp"
#include "model_maker/document.hpp"
#include "model_maker/drafting.hpp"
#include "model_maker/profile_database.hpp"
#include "model_maker/render_backend.hpp"
#include <functional>
#include "model_maker/render_backend.hpp"
#include "model_maker/renderer.hpp"
#include "model_maker/ribbon_layout.hpp"
#include "model_maker/ucs.hpp"
#include "model_maker/view_definition.hpp"
#include "model_maker/view_cube.hpp"
#include "model_maker/view_cube_renderer.hpp"

#include <windows.h>

// Fallback for toolchains that still omit the PMv2 symbols even with the
// version macros above. Identical to the SDK declarations, so harmless when the
// headers already provide them; guarded off when they do.
#ifndef DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
typedef HANDLE DPI_AWARENESS_CONTEXT;
#define DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 ((DPI_AWARENESS_CONTEXT)-4)
extern "C" {
WINUSERAPI BOOL WINAPI SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT value);
WINUSERAPI DPI_AWARENESS_CONTEXT WINAPI GetThreadDpiAwarenessContext(VOID);
}
#endif

#ifndef WM_DPICHANGED_AFTERPARENT
#define WM_DPICHANGED_AFTERPARENT 0x02E3
#endif

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

// OPAK B-Rep magazasi (TANIM: occ_solid_store.hpp, YALNIZ OCC TU'lari include
// eder). Application yalnizca isaretci kullanir; TopoDS_Shape bu basliga
// SIZMAZ (guard'li TopoDS_Shape.hxx disinda).
class OccSolidStore;

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
    // TEKLA-TARZI YAPI GRIDI: grid SPESIFIKASYONU ile ekle/degistir/sil.
    // Koordinat dili ve turetim `model_maker/grid.hpp`'de (parseGridCoordinateLine,
    // rebuildGridAxes). `axes` her zaman spesifikasyondan TUREtilir.
    std::size_t createModelGrid(GridDefinition grid);
    bool updateModelGrid(std::size_t index, GridDefinition grid);
    bool removeModelGrid(std::size_t index);
    bool setModelGridVisible(std::size_t index, bool visible);

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
    // KOLON KOTLARI (props paneli): secili profilli DUSEY elemanin ust/alt
    // kotunu okur. Duseysel profilli eleman yoksa false (spin kutulari
    // guncellenmez). Uygulama yolu tek: applyColumnLevels().
    bool selectedEntityColumnLevels(double& top, double& bottom) const;
    void setSelectedEntityColumnLevels(double top, double bottom);
    // Props satiri: secim VARKEN secime uygular, her durumda sonraki kolon
    // yerlesimi icin de saklar (tek kaynak; ENTER ve UYGULA ayni yolu kullanir).
    void applyColumnLevels(double top, double bottom);
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
    // MODEL AGACI (Tekla model organizer esdegeri): belgedeki tum modellerin
    // sayisi + tip kategorisi + goruntuleme adi. Agactan tik = sec, cift tik =
    // odaklan (focusOnSelection). Kategori: PROFIL / 3DFACE / NOKTA / CIZGI.
    int modelCount() const;
    std::string modelCategory(std::size_t index) const;
    std::string modelDisplayName(std::size_t index) const;
    void selectModel(std::size_t index);
    void focusModel(std::size_t index);
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
    // Polar tracking (F10) tek kaynaktan acilip kapanir: tuş yolu, kurdele
    // dugmesi ve dis pencere proc'u ayni yerden gecsin diye ayri metot.
    void togglePolarTracking();
    // Calisma duzlemi hayaleti (F7) tek kaynaktan acilip kapanir: tus yolu,
    // kurdele/menu ve dis pencere proc'u ayni yerden gecsin diye ayri metot.
    void toggleWorkPlaneGhost();
    bool workPlaneGhostVisible() const noexcept { return workPlaneGhostVisible_; }
    void setVisualStyle(VisualStyle style) noexcept;
    // Standart gorunus (On/Arka/Sol/Sag/Ust/Alt/ISO) — Qt menu + Ctrl+1..7.
    void setStandardView(StandardView view);
    // VIEWCUBE hedefi (yuz/kenar/kose/ISO): animasyonlu gecis baslatir.
    // Yuz ve ISO hedefleri setView ile AYNI matrise gider (tek kaynak);
    // kenar/kose hedefleri turntable ailesinden turetilir.
    void applyViewCubeTarget(const ViewCubeTarget& target);
    // Animasyonun son karesi: hedefe kesin oturur, grid kilidini/pivotu kurar.
    void finishViewCubeAnimation();
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
    // Durum cubuguna tek seferlik metin yayinla (Qt menü komutlarindan da
    // cagrilir — P1 named views kayit onayi gibi).
    void publishStatus(const std::wstring& text);
    IRenderBackend* activeRenderBackend() noexcept { return renderBackend_.get(); }

    void startWorkPlaneCommand();
    // KONSTRUKSIYON DUZLEMI (Tekla construction plane, P0): uc nokta
    // (snap/track aktif) -> WorkPlane::fromThreePoints -> Document'e
    // ConstructionPlane modeli eklenir. Calisma duzleminden (work plane)
    // AYRI bir kavramdir — work plane kamera/is-duzlemi, construction plane
    // ise secilebilir/kalici bir model nesnesidir. Esc/sag tik iptal.
    void startConstructionPlaneCommand();
    // KONSTRUKSIYON NESNELERI GIZLE/GOSTER: tum ConstructionKind nesnelerinin
    // gorunurlugunu tek bayrakla degistirir (Document::setConstructionVisible).
    void toggleConstructionVisibility();
    // TEKLA FITTING / PLANE CUT (P11, verb-noun): komut girilince once profil,
    // sonra konstrüksiyon duzlemi secilir (sol tik), SAG TIK komutu isletir,
    // Esc iptal. Fitting tarafi otomatik, PlaneCut keepPositive=true. OCC varsa
    // hedefi yeniden hesaplar. (Noun-verb yolu: applyCutFromSelection korunur.)
    void startFittingCommand();
    void startPlaneCutCommand();
    // TEKLA LINE CUT / POLYGON CUT (P2, noun-verb): secili (1 profil hedefi +
    // 1 sinir objesi) uzerinden CutModifier olusturur. LineCut: 2 koseli cizgi
    // (polygon = {bas,son}, planeNormal = aktif is duzlemi normali, keepPositive
    // = true). PolygonCut: face3D (4 kose -> cokgen). OCC varsa yeniden hesaplar.
    void startLineCutCommand();
    void startPolygonCutCommand();
    // TEKLA PART CUT (P3, noun-verb): secili TAM 2 model (hedef + kesici parca)
    // uzerinden BooleanCut CutModifier olusturur; hedef kimligi korunur, kesici
    // parca tasindiginda hedef yeniden hesaplanir (recomputeCutTarget).
    void startPartCutCommand();
    // TEKLA BOLT (Phase B1, verb-noun): komut girilince baglanacak IKI parca
    // SONRADAN secilir (sol tik, toggle), sag tik uygular, Esc iptal. Bulon
    // M20/22 varsayilanla iki parcanin birlesik sinir kutusunun ortasina, AKTIF
    // IS DUZLEMI normali ekseninde yerlesir. Display montaj (bas+govde+somun+
    // pul) OCC ile uretilir; delikler gercek B-Rep kesimiyle (CutKind::Hole)
    // acilir. Bulon display'i normal bir modeldir -> secilebilir, tasinabilir,
    // silinebilir (silme kaskadi bulonu da kaldirir).
    void startBoltCommand();
    // Verb-noun bulon bekleme durumu: sol tik parca ekler/cikarir (toggle),
    // sag tik uygular, Esc iptal eder. Tam 2 parca gerekli.
    void cancelPendingBoltCommand();
    void executePendingBoltCommand();
    bool pickPendingBoltAt(int x, int y);
    // Bolt yuz seciminde imlec isininin pending parcalar uzerindeki en yakin
    // yuz isabeti (model indeksi + MeshRayHit). Hover vurgusu ve tik-secimi
    // ayni fonksiyonu kullanir -> vurgulanan yuz = tiklaninca secilen yuz.
    struct PendingFaceHit { std::size_t model{}; MeshRayHit hit; };
    std::optional<PendingFaceHit> pendingBoltFaceHit(int x, int y) const;

    // --- ALIN LEVHASI BAGLANTISI (Phase C1 GUI) -------------------------------
    // Verb-noun End Plate: once MAIN (kolon), sonra SECONDARY (kiris) secilir;
    // sag tik uygular, Esc iptal. Levha + bulon grubu uretilir (OCC veya
    // fallback), kalici EndPlateConnection kaydi + MMW8 'E' blogu.
    void startEndPlateCommand();
    void cancelPendingEndPlateCommand();
    void executePendingEndPlateCommand();
    bool pickPendingEndPlateAt(int x, int y);
    void applyEndPlateFromParts(const std::vector<std::size_t>& parts);
    // SLICE 2d-2b: mevcut End Plate baglantisinin yerlesimini parcalarin GUNCEL
    // geometrisinden (kolon cercevesi/kesiti + kiris ekseni/ucu) yeniden cozer.
    // Gercek B-Rep temas (OCCT) oncelikli, lokal kesit geometrisi fallback.
    // Kiris kolonu GECTIGINDE (kiris ucu kolonun icinde) otomatik fitting bilgisi.
    // resolver yalniz TESPIT eder (belgeyi DEGISTIRMEZ); mutasyon
    // applyEndPlateFromParts'ta ayni undo kaydinda yapilir.
    struct EndPlateTrimInfo {
        bool needed{false};
        Vec3 entryPoint{};           // kiris ekseninin kolon giris yuzunu kestigi nokta
        Vec3 entryNormal{0.0, 0.0, 1.0}; // giris yuzunun dis normali (kirise dogru)
        Vec3 fixedEnd{};             // sabit kalacak uc (DISARI; swap sonrasi beamFrom)
    };
    // Basarida placement doldurulur + true; ACIK hata (paralel eksen / yuz disi /
    // belirsiz / eksik parca) err + false — cagiran belgeyi DEGISTIRMEZ.
    // `trim` verildiyse ve kiriş kolonu geciyorsa doldurulur (needed=true).
    bool resolveEndPlatePlacement(std::size_t mainIdx, std::size_t secIdx,
                                  const EndPlateParams& params,
                                  EndPlatePlacement& placement, std::wstring& err,
                                  EndPlateTrimInfo* trim = nullptr);
    // SLICE 2d-2c P0: bir End Plate baglantisinin GECICI yeniden uretim sonucu.
    // OCCT tipleri burada kalir; prepare ciktisi, commit girdisi. prepare AŞAMASINDA
    // belge DEGISMEZ (pure); commit yalnizca infallible document yazimlari yapar.
#ifdef MM_HAS_OCC
    struct PreparedEndPlate {
        std::size_t index{0};
        std::size_t secIdx{0};            // ikincil (kiris) model indeksi — edit'te yeniden fit
        EndPlatePlacement placement;      // yeni yerlesim (kiris yeniden kesimde kullanilir)
        EndPlateConnection updated;   // yeni bulon grubu (kimlikler korunur)
        TopoDS_Shape plateSolid;
        TopoDS_Shape boltSolid;
        WireframeModel plateModel;
        WireframeModel boltDisplay;
    };
    // PREPARE (pure, mutasyon yok): index'li baglantinin yeni yerlesimini coz +
    // levha/bulon/tel kafesi GECICI uret. Basarisizlikta nullopt — belge DEGISMEZ.
    std::optional<PreparedEndPlate> prepareEndPlateConnection(
        std::size_t index, const EndPlateParams* paramsOverride = nullptr);
    // COMMIT (infallible): hazirlanani ACIK undo kaydina uygular (kararli kimlik).
    // Donus: baglanti basariyla uygulandiysa true; setEndPlateConnection
    // reddederse false (cagiran transaction'i rollback etmeli).
    bool commitEndPlateConnection(std::size_t index, const PreparedEndPlate& p);
#endif
    // changedParts'e main/secondary olarak bagli TUM End Plate baglantilarini
    // yeniden hesaplar (deterministik kayit sirasi). Kolon/kiris tasininca,
    // dondurulunce veya profili degisince cagrilir. true = basarili (veya
    // etkilenen baglanti yok); false = recompute basarisiz + transaction
    // rollback edildi (cagiran component recompute'una DEVAM ETMEMELI).
    bool recomputeEndPlateConnectionsFor(const std::vector<EntityId>& changedParts);

    // --- UNIVERSAL COMPONENT (Phase P1): Fin Plate production entegrasyonu -----
    // Verb-noun Fin Plate: once MAIN (kolon), sonra SECONDARY (kiris) secilir;
    // sag tik uygular, Esc iptal. Gercek yol: ComponentRegistry -> generateFinPlate
    // -> componentPlanFromGenerationPlan -> ComponentMaterializer::prepare/commit
    // -> Document (ComponentInstanceRecord + semantic child kayitlari). End Plate
    // icine AYRI bir uretim yolu YOKTUR — Fin Plate ortak runtime'i kullanir.
    void startFinPlateCommand();
    void cancelPendingFinPlateCommand();
    void executePendingFinPlateCommand();
    bool pickPendingFinPlateAt(int x, int y);
    void applyFinPlateFromParts(const std::vector<std::size_t>& parts);
    // changedParts'e main/secondary olarak bagli TUM parametrik component
    // instance'larini (ComponentInstanceRecord) yeniden uretir. End Plate
    // baglantilari AYRI kalir (mevcut production yolu). Deterministik kayit
    // sirasi; basarisizlikta atomik rollback (eski model bozulmaz).
    void recomputeComponentInstancesFor(const std::vector<EntityId>& changedParts);
    // Secili model bir component instance'inin SAHIP OLDUGU alt nesne (levha/
    // bulon display) ise kaydini dondurur; degilse false.
    bool selectedComponentInstance(ComponentInstanceRecord& out) const;
    // Component instance parametre guncelleme (canli recompute, undo'lu).
    // typeId bilinen bir tipse parametreleri semaya hizalar, yeniden uretir.
    bool setSelectedComponentParams(EntityId compId, const std::vector<CompParam>& newParams);
    // Fin Plate parametreleri (uygulama-anı diyalogdan; kalici — son deger hatirlanir).
    void setPendingFinPlateParams(const std::vector<CompParam>& p) { pendingFinPlateParams_ = p; }
    const std::vector<CompParam>& pendingFinPlateParams() const { return pendingFinPlateParams_; }
    // Secili model bir BOLT display'i ise bulon parametrelerini okur (props
    // paneli). Donus false = secim bir bulon degil.
    bool selectedBoltProperties(double& nominalDiameter, double& holeDiameter,
                                std::string& grade) const;
    // Secili model bir BOLT display'i ise TUM BoltGroup'u dondurur (params +
    // pattern). Donus false = secim bir bulon degil. Bolt tool penceresi bunu
    // okur; setSelectedBoltGroup ile geri yazar.
    bool selectedBoltGroup(BoltGroup& out) const;
    // Secili bulon grubunu TUM alanlariyla gunceller (cap/kalite/delik/
    // bas/somun/pul + countX/countY/spacingX/spacingY). Delikler + display
    // yeniden uretilir. Secim bir bulon degilse no-op.
    void setSelectedBoltGroup(const BoltGroup& bolt);
    // Secili bulon display'inin parametrelerini gunceller (delikler + display
    // yeniden uretilir). Secim bir bulon degilse no-op.
    void setSelectedBoltProperties(double nominalDiameter, double holeDiameter,
                                   const std::string& grade);
    // BOLT SAG-TIK MENUSU: imlec altinda bir BOLT display'i varsa Win32 popup
    // menu acar (Ortala X / Ortala Y). Donus true = menu gosterildi (olay
    // tuketildi). Bulon degilse false (normal sag-tik akisina duser).
    bool showBoltContextMenu(int x, int y);
    // Secili bulon grubunu, bagli parcalarinin birlesik sinir kutusu merkezine
    // yerel x ve/veya y ekseninde ortalar (centerX/centerY). Delikler + display
    // + undo yeniden hesaplanir. Secim bir bulon degilse no-op.
    void centerSelectedBolt(bool centerX, bool centerY);
    // IKI NOKTALI GORUNUS (Tekla): iki nokta secilir (snap/track aktif);
    // ikinci noktada ViewDefinition uretilip twoPointViewCallback_ cagrilir
    // (Qt yeni MDI penceresi acar). Esc/sag tik iptal.
    void startTwoPointViewCommand();
    // PLAN GORUNUSU (Z kotu): kullanicidan kot alinir (edit box), XY'ye
    // PARALEL yatay gorunus uretilir (Tekla seviye plani). Ayni
    // twoPointViewCallback_ akisi -> yeni MDI penceresi.
    void startPlanViewCommand();
    // 3 NOKTALI GORUNUS (Tekla "using three points"): origin + X noktasi +
    // Y noktasi secilir (snap/track aktif); ucuncu noktada
    // Ucs::fromThreePoints -> viewFromUcs -> ViewDefinition -> yeni MDI
    // penceresi. Esc/sag tik iptal.
    void startThreePointViewCommand();
    // CALISMA DUZLEMINDE GORUNUS (Tekla "on work plane"): secim YOK;
    // mevcut calisma duzlemi (work plane) dogrudan yeni bir MDI penceresi
    // acar. UCS'ye Dik Gorus'un (kamerayi cevirir) AKSINE yeni pencere.
    void startViewOnWorkPlaneCommand();
    // PARCA DUZLEM GORUNUSLERI (Tekla "on part front/top/back/bottom plane"
    // + "default views of part" + "3D view of part"): secili TEK parcanin
    // (profil uyesi) eksen cercevesinden gorunus uretir. Secim yoksa uyarir.
    void startPartPlaneViewCommand(PartPlane plane);   // tek duzlem
    void startPartDefaultViewsCommand();               // front/top/end (3 pencere)
    void startPart3DViewCommand();                     // 3B'de parcaya sigdir
    // GRID CIZGILERI BOYUNCA GORUNUSLER (Tekla "along grid lines"): belgedeki
    // her grid'in aks cizgileri icin dik gorunusler acar (X/Y akslari +
    // Z katlari). Grid yoksa uyarir.
    void startGridViewsCommand();
    // EKRAN DONUS MERKEZI (R tusu, Tekla benzeri): tek nokta snap ile
    // secilir ve o nokta ekranin donus merkezi olur; kullanici yeni bir
    // merkez secene kadar Ctrl+sol tus surukleme ekranı bu merkez
    // etrafinda dondurur. Esc/sag tik iptal. Merkez yalnizca aktif
    // pencerenin kamerasinda tutulur (bkz. Camera::setOrbitPivot).
    void startRotateCenterCommand();
    void clearRotateCenter();
    bool hasRotateCenter() const noexcept { return camera_.hasOrbitPivot(); }
    // Gorunumu sifirla (eski R davranisi). Kilitli (gorunus) pencerede
    // anlamli tek sey yeniden sigdirmaktir.
    void resetView();
    // F: secime odaklan (3B navigasyon). Secili geometriyi cercevele, bakis
    // yonu korunur, orbit pivotu odak merkezine guncellenir; secim yoksa tum
    // model cercevelenir.
    void focusOnSelection();
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
    // AKTIF DURUMU DEGISTIRMEDEN sigdir (resize yolu icin; fitViewport
    // aktiflestirir). Iki noktali gorunum resize'da bunu kullanir.
    void fitViewportPassive(HWND canvas);
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
    // --- P1 UCS/VIEW (UI baglantisi) ---------------------------------------
    // Mevcut calisma duzlemine DIK gorus (Tekla "view to UCS / plan of UCS"):
    // Ucs::fromWorkPlane(workPlane_) -> viewFromUcs -> camera.setViewBasis.
    // Kamera merkezi korunur, yalniz yon degisir (setStandardView gibi).
    void setViewToWorkPlane();
    // Global calisma duzlemleri (Tekla work plane): 0=XY, 1=XZ (cepheden,
    // Z yukari), 2=YZ (sagdan, Z yukari). Orjin korunur (workPlane_.origin).
    void setGlobalWorkPlane(int which);
    // NAMED VIEWS (P1): adlandirilmis gorus listesi. Kaydet = mevcut kameranin
    // right/up/normal'i Ucs olarak saklanir; uygula = viewFromUcs ->
    // setViewBasis. Ayni isim uzerine yazar (Tekla davranisi).
    bool saveNamedView(const std::wstring& name);
    bool applyNamedView(const std::wstring& name);
    std::vector<std::wstring> namedViewNames() const;
    bool removeNamedView(const std::wstring& name);

    // Parametre istegi: Qt bir edit box (QInputDialog) gosterir ve degeri
    // dondurur (false = iptal). Parametreli komutlar (Offset mesafesi, Fillet
    // yaricapi, Dizi sayisi, Divide sayisi) bunu kullanir.
    void setParameterRequest(std::function<bool(const std::wstring& prompt,
                                                const std::wstring& initial,
                                                std::wstring& out)> callback) {
        parameterRequest_ = std::move(callback);
    }
    // Bolt tool penceresi istegi (Phase B1): bir BOLT display modeline cift
    // tiklaninca cagrilir. Qt tarafi BoltPropertiesDialog acar; OK olursa
    // setSelectedBoltGroup ile geri yazar.
    void setBoltEditRequest(std::function<void(const BoltGroup&)> callback) {
        boltEditRequest_ = std::move(callback);
    }
    // END PLATE PARAMETRELERI (faz D1): uygulama-anı parametre diyaloğu +
    // sonradan düzenleme (canlı recompute, undo'lu). UI katmanı (Qt) diyaloğu
    // açar; Application belge mutasyonunu yönetir.
    void setPendingEndPlateParams(const EndPlateParams& p) { pendingEndPlateParams_ = p; }
    const EndPlateParams& pendingEndPlateParams() const { return pendingEndPlateParams_; }
    // Secili model bir End Plate LEVHASI veya BULON DISPLAY'i ise bağlantı
    // indeksini döndürür; degilse nullopt.
    std::optional<std::size_t> selectedEndPlateConnectionIndex() const;
    // Secili model bir End Plate baglantisina aitse kaydını döndürür.
    bool selectedEndPlateConnection(EndPlateConnection& out) const;
#ifdef MM_HAS_OCC
    // Secili End Plate baglantisinin parametrelerini günceller: yeniden çözer,
    // levha/bulon/delikleri yeniden üretir (undo'lu). Baglanti kimligi (EntityId)
    // dogrudan verilir — async diyalog sirasinda selectedModels_ degisebildigi
    // icin secime guvenilmez. Cozulemezse false.
    bool setSelectedEndPlateConnection(EntityId connId, const EndPlateParams& newParams);
#endif
    // End Plate alt nesnesine (levha/bulon display) cift tik -> düzenleme
    // diyaloğu isteği. Qt tarafi openEndPlateParamsDialog açar.
    void setEndPlateEditRequest(std::function<void(const EndPlateConnection&)> cb) {
        endPlateEditRequest_ = std::move(cb);
    }
    // Divide komutunu baslatir (edit box ile bolme sayisini sorar).
    void activateDivide();

    // Aktif nesne yakalamalari (OSNAP) tur sayisi. PUBLIC olmali: application.cpp
    // icindeki `snapChoices` listesi bu sabitle DERLEME ZAMANINDA karsilastirilir
    // (static_assert) — bir tarafa tur eklenip digeri unutulursa derleme durur.
    static constexpr std::size_t kSnapChoiceCount = 15;

private:
    static LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK canvasProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK propsWndProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK layerCellEditSubclass(HWND window, UINT message, WPARAM wParam,
                                                   LPARAM lParam, UINT_PTR subclassId,
                                                   DWORD_PTR referenceData);
    LRESULT handleMessage(UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT handleCanvasMessage(UINT message, WPARAM wParam, LPARAM lParam);
    // Fitting/PlaneCut ortak yolu: secili (profil hedefi + konstrüksiyon
    // duzlemi) -> CutModifier -> addCutModifier -> (OCC) recomputeCutTarget.
    void applyCutFromSelection(CutKind kind);
    // VERB-NOUN KESME (Fitting/Plane Cut): komut girilince once profil, sonra
    // duzlem secilir; sag tik uygular, Esc iptal. applyCutFromPair cozumlenmis
    // (hedef + duzlem) uzerinden kesmeyi kurar; noun-verb ve verb-noun ortak.
    void applyCutFromPair(CutKind kind, std::size_t targetIdx, std::size_t planeIdx);
    // P12: tek duzlem, birden cok hedef (profil) — her hedefe ayni kaynak
    // duzlemle AYRI CutModifier kurulur; tek undo anlik islemi.
    void applyCutFromTargets(CutKind kind, const std::vector<std::size_t>& targets,
                             std::size_t planeIdx);
    void enterPendingCutCommand(CutKind kind);
    void cancelPendingCutCommand();
    void executePendingCutCommand();
    bool pickPendingCutAt(int x, int y);
    std::optional<std::size_t> modelIndexAt(int x, int y) const;
    // MMB orbit jesti basinda akilli pivotu cozer (imlec alti isabet ->
    // secim merkezi -> kayitli pivot -> model kutusu). Jest boyunca sabittir.
    std::optional<Vec3> resolveOrbitPivot(int x, int y) const;
    // Line Cut / Polygon Cut ortak yolu: secili (profil hedefi + sinir objesi)
    // -> CutModifier -> addCutModifier/setCutModifier -> recomputeCutTarget.
    void applyBoundaryCutFromSelection(CutKind kind);
    // Part Cut ortak yolu: secili (hedef + kesici parca) -> BooleanCut
    // CutModifier -> addCutModifier -> recomputeCutTarget.
    void applyPartCutFromSelection();
    // Bolt ortak yolu (Phase B1): verilen 2 parca -> BoltGroup + display +
    // delikler. M20/22 varsayilan. Eksen: axis verilirse o (secili yuz normali),
    // yoksa is duzlemi normali (dejenereyse dunya Z).
    void applyBoltFromParts(const std::vector<std::size_t>& parts,
                            std::optional<Vec3> axis = std::nullopt);
    // Noun-verb kullanim (eski yol): secili 2 parca -> applyBoltFromParts.
    void applyBoltFromSelection();
#ifdef MM_HAS_OCC
    // Bulon display modelini (montaj B-Rep + mesh) guncel BoltGroup'tan yeniden
    // uretir: grip -> buildBoltAssemblyShape -> putOccSolid + replaceModel.
    void rebuildBoltDisplay(const BoltGroup& bolt);
    // Tasinan bir BOLT DISPLAY'i varsa bulon origin'ini displacement kadar
    // otele, delikleri + display'i yeniden hesapla (Move/gumball yollari cagirir).
    void syncBoltGroupsForMoved(const std::vector<std::size_t>& indices,
                                const Vec3& displacement);
#endif
    // Secili TEK parcanin (profil uyesi) eksen cercevesi + eksen orijini.
    std::optional<Ucs> selectedPartFrame(Vec3& axisOrigin) const;

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
    void cancelTransformCommand();
    void cancelZoomWindow2D();
    void completeZoomWindow2D(int x, int y);
    void commitTransformPoint(const Vec3& point);
    bool applyTrimExtendTarget(std::size_t target, const Vec3& pickPoint);
    std::optional<std::size_t> trimExtendTargetAt(int x, int y) const;
    void completeTrimExtendTargetSelection(int x, int y);
    bool toggleModelSelection(int x, int y);
    void completeWindowSelection(int x, int y);
    // KESME NESNELERI SECIMI (P5): imlecin altindaki secilebilir kesme
    // modifier'ini/modeli hint olarak tazele (updateHover bos modda cagirir).
    void refreshHoverCandidate(int x, int y);
    // Secili kesme modifier'larini siler + hedeflerini yeniden hesaplar
    // (tek geri-alma adimi). Secim bos ise hicbir sey yapmaz.
    void deleteSelectedCutModifiers();
    // Imlecin altindaki kesme modifier'ini isabet-testi (2B/3B ortak yol).
    std::optional<std::size_t> hitTestCutModifierAt(int x, int y) const;
    // COMPONENT SIMGESI: End Plate levha merkezindeki buyuk simgeye tiklama
    // -> levha modelinin indeksi (normal model hit'ten ONCE denenir).
    std::optional<std::size_t> componentGlyphModelAt(int x, int y) const;
    // P8: kesme nesnesi uzerinde Alt+sol tik -> kesme yonunu tersine cevirir
    // (invertSide). PlaneCut/LineCut keepPositive'i, Fitting otomatik tarafi
    // tersler; PolygonCut/BooleanCut yonsuz oldugu icin no-op (uyari).
    void flipCutDirection(std::size_t index);
    void commitPoint(const Vec3& point);
    void cancelDrawing();
    void clearTemporaryTracking();
    // Gecici izleme noktasi (TP) kaydi: TAB tusu. Imlecin altindaki HAM snap
    // noktasini alir (polar/ortho kisitiyla oynamis hover_ DEGIL), 8 taneye
    // kadar birikir; Esc hepsini sifirlar.
    void acquireTemporaryTrackingPoint();
    void updateHover(int x, int y);
    // F8 Ortho kisiti: obje snap'lari ortho'yu EZER (serbest imlec eksene
    // kilitlenir). Tek kaynak — hem 2B hem 3B yollari burada.
    SnapResult applyOrthoConstraint(const Vec3& anchor, SnapResult candidate, int x, int y) const;
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
    void invalidateCanvas();
    void activate3DNavigation();
    void addCube();
    void addPyramid();
    void addCylinder();
    // P9: kesme HEDEFI tasinir/dondurulurse, onu kesen kaynak (duzlem/cizgi/
    // 3DFACE) varliklar da ayni rijit donusumu alir (Tekla: kesme parçaya
    // baglidir). Verilen model indeks kumesine bu bagli kaynak indekslerini
    // ekleyerek DONUSTURULECEK tam kumeyi doner (tekilleme yapar; kopya
    // modunda kopyalar yeni kimlik aldigindan bagli kaynak yoktur -> no-op).
    std::vector<std::size_t> transformIndicesWithAttachedCutSources(
        const std::vector<std::size_t>& indices) const;
#ifdef MM_HAS_OCC
    void addBooleanFuse();
    void addBooleanCommon();
    void addBooleanCut();
    // P7 (ACILAN DOSYA KATILARI): hedefin magazada B-Rep'i yoksa, varligin
    // kalici ozelliklerinden (profil + eksen + referans normal + rotasyon)
    // yeniden uretip putOccSolid ile yazar. Store'da sekil zaten varsa veya
    // uretilemiyorsa (profil yok / eksen dejenere / OCC kapali) false doner.
    bool ensureCutSolid(EntityId target);
    // P9: profilli hedefin TABAN (pre-cut) B-Rep'ini kalici ozelliklerden
    // (yeni eksen/rotasyon) YENIDEN uretir ve yeni taban surumu olarak yazar.
    // Hedef tasindiginda/donduruldugunde taban eski konumda kaldigi icin bu
    // gerekir; profilsiz (ilkel kati) hedefte uretilemez -> no-op (eski taban).
    void rebuildCutBaseSolid(EntityId target);
    // Kesme modifier'larini hedef parcaya yeniden uygular (Phase 4.2 recompute):
    // taban (pre-cut) B-Rep'ten tum etkin modifier'lari sirasiyla uygular.
    void recomputeCutTarget(EntityId target);
    // Kesici parca tasininca (canli Part Cut): bu parçayi ARAÇ olarak kullanan
    // tum BooleanCut modifier'larinin hedeflerini yeniden hesaplar.
    void recomputeTargetsUsingTool(EntityId tool);
    // P6+P9: verilen model indeksleri tasindiginda, bu varliklari KAYNAK
    // (source), ARAÇ (BooleanCut tool) veya HEDEF (target) olarak kullanan
    // kesme modifier'larinin hedeflerini yeniden hesaplar. Hedefin kendisi
    // tasindiysa taban B-Rep eski konumda kaldigi icin once rebuildCutBaseSolid
    // ile yeniden uretilir, sonra kesme zinciri uygulanir.
    void recomputeTargetsForMovedModels(const std::vector<std::size_t>& indices);
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
    // Aktif nesne yakalamalari (OSNAP) onay kutulari. Boyut PUBLIC sabitten
    // gelir (kSnapChoiceCount) ve application.cpp'deki static_assert ile tur
    // listesine kilitlidir — ciplak sayi YOK.
    std::array<HWND, kSnapChoiceCount> snapTypeCheckboxes_{};
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
    // B-Rep SEKLERI ARTIK BELGE-SAHIPLI: Document::occStore(). Application'da
    // AYRI bir harita YOKTUR → tek dogruluk kaynagi Document'tir; iki harita
    // ayrismasi / cift yazma yapisal olarak imkansizdir. Asagidaki yardimcilar
    // yalnizca Document magazasinin SOMUT (OCCT) arayuzune ince koprudur.
    // OCC kapaliyken (MM_HAS_OCC yok) derlenmezler; cagri yerleri de guard
    // icindedir.
    //   putOccSolid     : varlik indeksine karsilik gelen KIMLIK altina yaz.
    //                     (Indeks KALICI degil; kimlik sabittir — bkz. Document.)
    //   findOccSolid    : varlik indeksinin B-Rep sekli (yoksa nullptr).
    //   occSolidCount   : magazadaki kayit sayisi (teshis/log).
    //   lastLiveSolids  : kimlik (olusturma) sirasina gore en son N CANLI kati.
#ifdef MM_HAS_OCC
    // const OLMAYAN asiri yukleme YAZMA icin (putOccSolid), const olan OKUMA
    // icin (lastLiveSolids/findOccSolid/occSolidCount) kullanilir.
    OccSolidStore* occSolidStore() noexcept;
    const OccSolidStore* occSolidStore() const noexcept;
    void putOccSolid(std::size_t index, const TopoDS_Shape& shape);
    std::size_t occSolidCount() const noexcept;
    const TopoDS_Shape* findOccSolid(std::size_t index) const;
    std::vector<TopoDS_Shape> lastLiveSolids(std::size_t count) const;
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
    // ISARETLI sayisal parametre: kot gibi 0 veya NEGATIF olabilen degerler.
    // requestDoubleParameter yalniz > 0 kabul eder, o yuzden ayri yol.
    std::optional<double> requestLevelParameter(const std::wstring& prompt,
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
    // P9: bu suruklemede donusturulen tam kume (secim + kesme KAYNAKLARI).
    // Kesme hedefi tasinir/dondurulurse kaynak duzlem/cizgi/3DFACE onunla
    // rijit donusur; kume surukleme basinda bir kez kurulur ve move/rotate/
    // cancel/end ayni kumeyi kullanir (tutarlilik).
    std::vector<std::size_t> gumballTransformSet_;
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
    // Cift tik ile gumball eksenlerini UCS'ye (calisma duzlemi) hizala; tekrar
    // cift tik yerel (object-local) eksenlere dondurur. false = yerel (varsayilan).
    bool gumballAlignedToUcs_{false};
    void gumballUpdateFrame();
    // Profil kesiti REFERANS NORMALI: baslik plakalari aktif IS DUZLEMINE
    // paralel uretilir. Iki noktali gorunuste (otomatik gorunus duzlemi)
    // dunya Z doner — bkz. view_definition.hpp profileSectionReferenceNormal.
    Vec3 profileSectionReference() const;
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
    // Aktif is duzlemi her pencerede hayalet (yari saydam) duzlem olarak
    // gosterilsin mi. Yalnizca cizim — secim/snap'i etkilemez.
    // Varsayilan kWorkPlaneGhostDefaultVisible (su an: KAPALI) — tek kaynak.
    bool workPlaneGhostVisible_{kWorkPlaneGhostDefaultVisible};
    // Konstruksiyon nesnelerinin gorunurluk durumu (gizle/goster tek bayrak).
    bool constructionVisible_{true};
    bool temporaryTrackingLocked_{false};
    std::vector<Vec3> temporaryTrackingPoints_;
    std::vector<TrackingGuide> temporaryTrackingGuides_;
    std::vector<Vec3> temporaryDerivedPoints_;
    std::vector<Vec3> temporaryPerpendicularPoints_;
    // TAB ile TP kaydetmek icin son hesaplanan HAM snap adayi (polar/ortho
    // kisitindan ONCE). Eskiden 450 ms BEKLEME (dwell) zamanlayicisi vardi;
    // kullanici sarti: "TP noktalarini tab tusu ile koyalim, esc tp leri
    // sifirlar" -> otomatik dwell kaldirildi, TP yalnizca TAB ile konur.
    std::optional<SnapResult> temporaryAcquireCandidate_;
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
    // VERB-NOUN KESME (Fitting/Plane Cut): komut girilince once profil(ler),
    // sonra duzlem secilir; sag tik uygular, Esc iptal. P12: TEK duzlem
    // birden cok profili kesebilir — pendingCutTargets_ coklu hedef tutar,
    // her hedefe AYRI CutModifier yazilir (kaynak duzlem ortak).
    std::optional<CutKind> pendingCutKind_;
    std::vector<std::size_t> pendingCutTargets_;
    std::optional<std::size_t> pendingCutPlane_;
    // VERB-NOUN BOLT (Phase B1): komut girilince baglanacak iki parca sonradan
    // secilir (toggle); sag tik uygular, Esc iptal. pendingBoltParts_ en fazla
    // 2 parca tutar.
    bool pendingBolt_{};
    std::vector<std::size_t> pendingBoltParts_;
    // Secili yuzun normali (bulon ekseni). 2 parca secildikten sonra yuz
    // secimiyle doldurulur; bosken uygulama yuz secimini bekler.
    std::optional<Vec3> pendingBoltAxis_;
    // VERB-NOUN END PLATE (Phase C1 GUI): [0]=main (kolon), [1]=secondary (kiris).
    bool pendingEndPlate_{};
    std::vector<std::size_t> pendingEndPlateParts_;
    // VERB-NOUN FIN PLATE (Phase P1): [0]=main (kolon), [1]=secondary (kiris).
    bool pendingFinPlate_{};
    std::vector<std::size_t> pendingFinPlateParts_;
    // END PLATE PARAMETRELERI (faz D1): uygulama-anı diyalogdan gelen degerler
    // (startEndPlateCommand'dan önce UI set eder); applyEndPlateFromParts bunları
    // kullanır. Kalıcı — son değer hatırlanır.
    EndPlateParams pendingEndPlateParams_{};
    // FIN PLATE PARAMETRELERI (Phase P1): tipli CompParam listesi; uygulama-anı
    // diyalogdan gelir, applyFinPlateFromParts bunlari kullanir. Kalıcı.
    std::vector<CompParam> pendingFinPlateParams_;
    // End Plate alt nesnesine çift tık -> düzenleme isteği callback'i.
    std::function<void(const EndPlateConnection&)> endPlateEditRequest_;
    // BOLT TOOL PENCERESI (Phase B1): bolt display'ine cift tik -> duzenleme
    // istegi callback'i. Cift tik tespiti icin son sol tik zamani + konumu.
    std::function<void(const BoltGroup&)> boltEditRequest_;
    DWORD lastLeftClickTime_{0};
    POINT lastLeftClickPos_{-1, -1};
    // KESME NESNELERI SECIMI (P5): kesme modifier secimi (model seciminden
    // ayri) + imlecin altindaki secilebilir nesne (secim hinti).
    std::vector<std::size_t> selectedCutModifiers_;
    std::optional<std::size_t> hoverModel_;
    std::optional<std::size_t> hoverCutModifier_;
    // Bolt yuz secim adiminda imlecin altindaki yuz (hover vurgusu). Hover ile
    // tik-seciminin AYNI yuzu gostermesi icin pendingBoltFaceHit ortak kaynak.
    std::optional<std::size_t> hoverFaceModel_;
    // Bolt yuz secim adiminda hover edilen YUZUN TUM ucgenleri (ayni baglantili
    // duzlemsel yuz — tek tessellasyon ucgeni degil). Vurgu bu kumenin tamamina
    // uygulanir; boylece "ucgen yuz" goruntusu kalkar.
    std::vector<std::size_t> hoverFaceIndices_;
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
    // Iki noktali gorunum YON secim asamasinda: secilen bakis yonu (birim,
    // izleyiciye dogru). Imlecin P1-P2 cizgisinin hangi tarafinda olduguna
    // gore guncellenir; ok onizlemesi bu vektoru gosterir.
    Vec3 twoPointViewNormal_{0.0, -1.0, 0.0};
    NamedViewList namedViews_; // P1: adlandirilmis gorusler (UI listesi)
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
    // Nokta secme amaci: ayni akis (snap + track + Esc/sag tik iptal) uc
    // farkli is icin kullanilir.
    //   WorkPlane            : 3 nokta -> calisma duzlemi (UCS)
    //   TwoPointView         : 2 nokta -> yeni gorunus penceresi
    //   TwoPointViewDirection: 2 nokta secildikten SONRA bakis yonu secilir
    //                          (ok onizlemesi; tik -> o yonde view)
    //   ThreePointView       : 3 nokta -> yeni gorunus penceresi (origin+X+Y)
    //   RotateCenter         : 1 nokta -> ekran donus merkezi (R tusu; Ctrl+sol
    //                          tus ile ekran bu merkez etrafinda doner)
    enum class PointPickPurpose { WorkPlane, TwoPointView, TwoPointViewDirection, ThreePointView, RotateCenter, ConstructionPlane };
    PointPickPurpose pointPickPurpose_{PointPickPurpose::WorkPlane};
    // Ctrl+sol tus: bu esikten fazla hareket rotasyona terfi eder; esik
    // asilmazsa Ctrl+tik secimi yapilir (tek karar noktasi).
    static constexpr int kCtrlRotateDragThresholdPx = 4;
    std::function<void(const ViewDefinition&)> twoPointViewCallback_;
    std::wstring input_;
    std::vector<std::wstring> commandHistory_;
    std::size_t commandHistoryIndex_{};
    bool rotating_{};
    bool panning2D_{};
    // MMB orbit jestinin AKILLI pivotu (jeste ozgu, kalici degil). R-tusu
    // kalici pivotundan (Camera::orbitPivot_) AYRI tutulur; yalnizca MMB
    // orbiti kullanir, renderer'da kalici pivot isareti olarak cizilmez.
    std::optional<Vec3> orbitGesturePivot_{};
    // Ctrl+sol tus: basildi (henuz surukleme esigi gecmedi). Esik asilirsa
    // rotating_'e terfi eder; birakmada hareket yoksa normal Ctrl+tik
    // secimi yapilir (Ctrl+tik secimi KORUNUR).
    bool ctrlRotatePending_{};
    POINT ctrlRotateStart_{};
    bool wheelNavigating_{};
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
    // MODERN VIEWCUBE (26 yon): basilan hedef — yuz / kenar / kose / ISO.
    std::optional<ViewCubeTarget> viewCubePressedView_;
    // Fare altindaki hedef: yalniz DEGISTIGINDE kare tazelenir (bosuna cizim yok).
    std::optional<ViewCubeTarget> viewCubeHover_;
    bool viewCubeHoverValid_{};
    bool viewCubeMouseTracking_{};
    POINT viewCubeCursor_{-1, -1};
    // GECIS ANIMASYONU: hedefe ~180 ms'de quaternion slerp (yumusak, kutup
    // devrilmesi/isaret atlamasi yok). Ara kareler setRotationMatrix ile
    // yazilir — pivot TEMIZLENMEZ, grid kilidi YENILENMEZ (yoksa animasyon
    // sirasinda grid ailesi sicrardı). Son karede normal apply kilit/pivotu
    // kurar.
    bool viewCubeAnimActive_{};
    double viewCubeAnimT_{0.0};
    std::array<double, 9> viewCubeAnimFrom_{};
    std::array<double, 9> viewCubeAnimTo_{};
    std::optional<ViewCubeTarget> viewCubeAnimTarget_;
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
