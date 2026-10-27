#pragma once
// DRAWING VERI MODELI (P1) — paper-space cizim: model uzayindan AYRI, serilestirilebilir.
//
// Namespace mm::drawing: eski mm::DrawingView/PaperSpace (assembly.hpp, P5 gorunus listesi)
// ile CARPISMAZ; o kayit yalniz "hangi assembly hangi projeksiyonda" listesidir, burasi
// cizimin kendisidir.
//
// KOORDINAT SOZLESMESI (iki uzay KARISMAZ):
//   * View-yerel (model birimi, mm): Segment2 / bounds / Dimension2D noktalari / mark
//     ankrajlari. Olceksiz, gorunus kendi orijininde; GEOMETRIYE bagli kalan her sey
//     burada saklanir (olcek degisince bozulmaz).
//   * Kagit (mm): DrawingView::origin + scale * yerel. Sheet cercevesi/title block/etiket
//     konumlari kagit mm'dir. Orijin sol-alt, y yukari.
//
// GENISLEME NOKTALARI (bilincli olarak acik birakildi):
//   * DrawingKind (SinglePart / Assembly)   -> tek parca vs montaj cizimi.
//   * ViewKind (Front/Top/Side)             -> Section/Detail/Isometric eklenir; kaynak
//     `DrawingView::source` + `sectionPlane` alanlari sonradan eklenebilir.
//   * LineStyle (Visible/Hidden)            -> hidden-line sonradan iyilesir.
//   * Dimension2D::role                     -> bolt/weld/BOM etiketleri yeni role/kind.
//   * TitleBlock::revisions                 -> revision tablosu.
//   * JSON "version" alani                  -> geriye uyumlu okuma.
//
// OCCT'siz, Qt'siz; Linux ctest'te kosar.

#include "model_maker/entity_id.hpp"
#include "model_maker/geometry.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace mm::drawing {

using ElementId = std::uint64_t; // cizim ici kimlik (EntityId ile KARISMAZ); 0 = gecersiz

enum class DrawingKind { SinglePart, Assembly };
enum class ViewKind { Front, Top, Side, Detail };   // Detail: levha yuzu detay gorunumu (kendi olcegi)
enum class LineStyle { Visible, Hidden };
enum class ProjectionMethod { ThirdAngle, FirstAngle };
enum class SheetSize { A4, A3, A2, A1, A0 };
enum class SheetRole { Primary, Continuation, DetailSheet };   // P2C: coklu pafta seti
enum class DrawingMode { Single };                              // P2C: varsayilan tek pafta modu
enum class DimensionDirection { Horizontal, Vertical, Aligned };
enum class DimensionRole {
    MemberLength,   // ana parca ekseni boyu (gercek 3B uzunluk)
    OverallWidth,   // gorunus sinir kutusu genisligi
    OverallHeight,  // gorunus sinir kutusu yuksekligi
    HoleSpacing,    // delik zinciri: kenar -> delik -> delik -> kenar (merkezler arasi) [P1 yedek zincir]
    PartPosition,   // montaj uyelerinin kenar konumlari zinciri (assembly)
    // --- P2A: otomatik imalat olculeri (fab::FabricationInventory'den; featureKey ile izlenir) ---
    PlateWidth,     // levha u boyu (levha yuzunu gosteren gorunumde)
    PlateHeight,    // levha v boyu
    HolePitch,      // bir bulon grubunda ardisik delik merkezleri arasi (yatay/dusey adim)
    HoleEdge,       // levha zarf kenari -> grubun en dis deligin DIS kenari (NET kenar mesafesi)
    HoleOrdinate,   // P2D: duzensiz/manual dizilimde delik MERKEZININ levha kenarindan gercek koordinati
                    // (uydurma adim YOK; her delik icin gercek u/v degeri)
    PlateOffsetX,   // uye BASLANGICINDAN levha yakin yuzune, uye ekseni (x') boyunca
    PlateOffsetY,   // uye baslangicindan levha zarfi min y' (uye-yerel)
    PlateOffsetZ    // uye baslangicindan levha zarfi min z' (uye-yerel)
};

// Gorunumun 3B cercevesi. Varsayilan (dunya) Front/Top/Side ile ayni; egik/donuk uyede UYE cercevesi.
//   proj(P) = (P.right, P.up); normal = izleyiciye (gozden bakana) dogru, right x up = normal.
struct ViewFrame {
    Vec3 right{1.0, 0.0, 0.0};
    Vec3 up{0.0, 0.0, 1.0};
    Vec3 normal{0.0, -1.0, 0.0};
    bool memberAligned{false};   // true: dunya eksenlerine degil uye eksenine hizali
    Vec2 project(const Vec3& p) const noexcept {
        return {p.x * right.x + p.y * right.y + p.z * right.z, p.x * up.x + p.y * up.y + p.z * up.z};
    }
};

struct Segment2 {
    Vec2 a{};
    Vec2 b{};
    LineStyle style{LineStyle::Visible};
};

struct Bounds2 {
    Vec2 min{};
    Vec2 max{};
    bool empty{true};
    double width() const noexcept { return empty ? 0.0 : max.x - min.x; }
    double height() const noexcept { return empty ? 0.0 : max.y - min.y; }
    Vec2 centre() const noexcept { return {(min.x + max.x) * 0.5, (min.y + max.y) * 0.5}; }
    void include(const Vec2& p) noexcept;
};

// Delik (ekseni gorunus duzlemine dik): view-yerel merkez + yaricap. `label` yalniz gruptaki ilk
// delikte dolu: "4 x \xC3\x98" + cap (UTF-8). Eksen-gorunus paralel delikler Segment2(Hidden) olur.
struct Circle2 {
    Vec2 center{};
    double radius{0.0};
    LineStyle style{LineStyle::Visible};
    EntityId owner{kInvalidEntityId};   // deligin ait oldugu parca
    std::string label;
};

// Gorunumdeki bir uyenin (parca) yerel sinirlari — part mark etiketi icin.
struct ViewMember {
    EntityId entity{kInvalidEntityId};
    Bounds2 bounds;
};

struct DrawingView {
    ElementId id{0};
    ViewKind kind{ViewKind::Front};
    EntityId subject{kInvalidEntityId};   // ana eleman veya assembly (Document EntityId)
    double scale{1.0};                    // kagit mm / model mm
    Vec2 origin{};                        // kagit: yerel (0,0)'in konumu
    Vec2 projectionOffset{};              // ham 2B projeksiyonda yerel (0,0)'in konumu:
                                          // yerel = projectToView(P, kind) - projectionOffset
    std::vector<Segment2> segments;       // view-yerel, olceksiz
    std::vector<Circle2> circles;         // delikler (eksen gorunuse dik), view-yerel
    Bounds2 bounds;                       // segments'in sinirlari (view-yerel)
    std::vector<ViewMember> members;
    std::string label;                    // "FRONT", "TOP"... (UTF-8)
    ViewFrame frame;                      // P2A: 3B -> 2B cerceve (gercek uzunluk kurali bunu kullanir)
    EntityId detailPlate{kInvalidEntityId};   // kind == Detail: gosterilen levha (Document EntityId)

    Vec2 toPaper(const Vec2& local) const noexcept {
        return {origin.x + local.x * scale, origin.y + local.y * scale};
    }
    Bounds2 paperBounds() const noexcept;
};

// Part/Assembly mark etiketi. anchor: view-yerel (geometriye bagli); label: kagit.
struct MarkAnnotation {
    ElementId id{0};
    ElementId viewId{0};
    EntityId target{kInvalidEntityId};    // etiketlenen parca/assembly (Document EntityId)
    std::string text;                     // "P1" / "A1" (UTF-8)
    bool isAssembly{false};
    Vec2 anchor{};                        // view-yerel
    Vec2 labelPos{};                      // kagit
    bool visible{true};                   // P2B: kullanici gizledi
};

// 2B olcu. p1/p2: view-yerel (model mm) — geometriden turetilir; value: GERCEK model
// uzunlugu (uye ekseni 3B uzunlugu / sinir kutusu). offset: kagit mm, olcu cizgisinin
// olculen kenardan uzakligi.
struct Dimension2D {
    ElementId id{0};
    ElementId viewId{0};
    DimensionRole role{DimensionRole::OverallWidth};
    DimensionDirection direction{DimensionDirection::Horizontal};
    Vec2 p1{};
    Vec2 p2{};
    double value{0.0};
    double offset{12.0};
    // --- DUZENLENEBILIR YERLESIM PARAMETRELERI (P2B kancasi; MUHENDISLIK DEGERINI DEGISTIRMEZ) ---
    int side{1};          // +1: gorunumun alt(yatay)/sag(dusey)/dis(hizali) tarafi; -1: ters taraf
    double textT{0.5};    // metin merkezinin olcu cizgisi boyunca konumu (0=p1, 1=p2; disi serbest)
    int textSide{-1};     // -1: metin cizginin gorunume bakan tarafinda; +1: dis tarafinda
    int textLevel{0};     // >0: metin cizgiden kademe kademe uzaklasir (kilavuz cizgisiyle)
    ElementId chain{0};   // ayni zincir/satir kimligi (yerlesim bunu birlikte kaydirir)
    EntityId source{kInvalidEntityId};
    // --- KAYNAK / SURUM (P2A): bu deger HANGI imalat ozelliginden, HANGI model surumunden geldi ---
    std::string featureKey;        // fab::featureKey (bos: P1 geometri olcusu)
    std::string basis;             // insan okur: "plate-local u pitch col1-col2"
    std::uint64_t documentRevision{0};
    bool visible{true};            // P2B: kullanici gizledi (imalat olcusu gizlenirse cizim INCOMPLETE)
    // p1-p2'den yeniden olculen uzunluk (yon'e gore).
    double measured() const noexcept;
};

enum class TextKind { HoleCallout, PlateNote, GeneralNote, Legend, StatusStamp, ViewTitle };

// Serbest metin (callout/not/etiket). Konum kagit mm (sol-alt); anchor view-yerel (modele bagli).
struct TextAnnotation {
    ElementId id{0};
    ElementId viewId{0};              // 0: sayfa duzeyi
    TextKind kind{TextKind::GeneralNote};
    std::vector<std::string> lines;   // UTF-8
    double height{3.5};               // kagit mm (okunabilir minimum yerlesim ayarindan)
    bool hasAnchor{false};
    Vec2 anchor{};                    // view-yerel
    Vec2 pos{};                       // kagit: metin kutusunun sol-alt kosesi
    bool leader{false};               // anchor'a kilavuz cizgisi
    bool bold{false};
    EntityId source{kInvalidEntityId};
    std::string featureKey;
    bool visible{true};            // P2B: kullanici gizledi
};

// P2B: kullanici tarafindan kagida eklenen 2B cizim (muhendislik geometrisi DEGIL; model olcusu degismez).
enum class ShapeKind { Line, Circle, Centerline };
struct PaperShape {
    ElementId id{0};
    ShapeKind kind{ShapeKind::Line};
    Vec2 a{};                       // Line/Centerline baslangic
    Vec2 b{};                       // Line/Centerline bitis
    Vec2 center{};                  // Circle merkez
    double radius{0.0};
    LineStyle style{LineStyle::Visible};
    bool visible{true};
    EntityId source{kInvalidEntityId};   // ilgili model elemani (varsa)
    std::string featureKey;              // provenance: "user:line" vb.
};

enum class ChecklistState { Satisfied, Unresolved, NotApplicable };

struct ChecklistItem {
    std::string key;       // makine kodu: PLATE_OUTLINE, HOLE_DIAMETER, ...
    std::string label;     // insan metni (UTF-8)
    ChecklistState state{ChecklistState::Unresolved};
    std::string detail;
    std::string featureKey;
};

// Imalat tamlik durumu: `draft == false` YALNIZ tum gerekli oge Satisfied/NotApplicable ise.
struct FabricationStatus {
    bool evaluated{false};
    bool draft{true};
    std::vector<ChecklistItem> checklist;
    std::vector<std::string> unresolvedCodes;
};

struct LayoutConfig {
    double dimTextHeight{3.0};     // kagit mm (buyuk harf/rakam yuksekligi)
    double noteTextHeight{3.5};
    double minDimText{3.0};        // alt sinir: bunun altina ASLA inilmez (ihlal -> sorun raporu)
    double minNoteText{3.5};
    double clearance{1.0};         // metin/cizgi/gorunus arasi minimum bosluk (kagit mm)
    double rowPitch{0.0};          // 0: otomatik (metin yuksekligi + 4.5)
    double firstOffset{10.0};
    SheetSize minSheet{SheetSize::A4};
    SheetSize maxSheet{SheetSize::A1};
    double fixedScaleDen{0.0};     // >0: sadece bu olcek (ornegin 10 -> 1:10); 0: standart olceklerden otomatik
    int maxIterations{6};
    bool operator==(const LayoutConfig&) const = default;
};

struct LayoutIssue {
    std::string code;      // TEXT_OVERLAP, ELEMENT_UNPLACEABLE, TEXT_TOO_SMALL, NO_FIT, ...
    std::string detail;
    ElementId element{0};
    bool operator==(const LayoutIssue&) const = default;
};

struct LayoutReport {
    bool ran{false};
    bool ok{false};                    // true: tum zorunlu ogeler cakismasiz yerlesti
    SheetSize requestedSheet{SheetSize::A3};
    SheetSize finalSheet{SheetSize::A3};
    double scaleDenominator{0.0};
    int attempts{0};
    int escalations{0};                // buyuk kagida gecis sayisi
    std::vector<LayoutIssue> issues;
};

struct Revision {
    std::string id;
    std::string date;
    std::string description;
    bool operator==(const Revision&) const = default;
};

struct TitleBlock {
    std::string project;
    std::string title;
    std::string drawingNumber;
    std::string drawnBy;
    std::string date;
    std::string scaleText;     // "1:20"
    std::string revision;
    std::vector<Revision> revisions;
    bool operator==(const TitleBlock&) const = default;
};

struct SheetFormat {
    SheetSize size{SheetSize::A3};
    double width{420.0};   // yatay
    double height{297.0};
    double margin{10.0};   // cerceve ic payi
    double titleWidth{190.0};
    double titleHeight{55.0};
};
SheetFormat sheetFormat(SheetSize size);          // landscape olculer
const char* sheetSizeName(SheetSize size) noexcept;

// --- P2C: kullanici tercihleri (cizimle birlikte JSON'a yazilir) -------------------------
// Varsayilanlar kabul edilen urun gereksinimi: sheetSize=auto, drawingMode=single,
// autoMultiSheetFallback=true. autoFirst -> autoMax arasi A-serisi yatay kagitlar denenir.
struct DrawingPreferences {
    bool autoSheetSize{true};                          // sheetSize = auto
    SheetSize sheetSize{SheetSize::A3};                // autoSheetSize == false ise tek boyut
    DrawingMode mode{DrawingMode::Single};
    bool autoMultiSheetFallback{true};
    SheetSize autoFirst{SheetSize::A3};
    SheetSize autoMax{SheetSize::A0};
    bool operator==(const DrawingPreferences&) const = default;
};

// Bagli paftaya / gorunuse capraz referans (cift yonlu: hedef de ana paftayi gosterir).
struct SheetReference {
    ElementId targetSheetId{0};
    ElementId targetViewId{0};                    // 0: pafta duzeyi
    std::string targetNumber;                     // hedef pafta numarasi ("A100-01")
    std::string label;                            // insan okur: "DETAY P12 -> A100-01"
    EntityId sourceEntity{kInvalidEntityId};      // ilgili model elemani (varsa)
    bool operator==(const SheetReference&) const = default;
};

// Ana paftadaki pafta indeksi satiri.
struct SheetIndexEntry {
    ElementId sheetId{0};
    std::string number;
    std::string title;
    SheetRole role{SheetRole::Continuation};
    bool operator==(const SheetIndexEntry&) const = default;
};

// DrawingSet kimligi + kalite durumu (ayri bir drawing modeli DEGIL: DrawingDocument'un ust bilgisi).
struct DrawingSetInfo {
    bool present{false};
    std::string id;                    // kalici set kimligi (deterministik)
    ElementId primarySheetId{0};
    bool multiSheet{false};
    bool layoutValid{false};           // tum paftalar yerlesti, olculen cakisma 0
    bool ready{false};                 // layoutValid && imalat verisi tam (draft degil)
    std::vector<std::string> issues;   // set duzeyi cozulmemis sorunlar
    std::vector<SheetIndexEntry> index;
};

struct DrawingSheet {
    ElementId id{0};
    std::string name;
    SheetFormat format;
    DrawingKind kind{DrawingKind::SinglePart};
    ProjectionMethod projection{ProjectionMethod::ThirdAngle};
    EntityId subject{kInvalidEntityId};
    double scale{1.0};
    bool scaleIsStandard{true};
    TitleBlock titleBlock;
    std::vector<DrawingView> views;
    std::vector<MarkAnnotation> marks;
    std::vector<Dimension2D> dimensions;
    std::vector<TextAnnotation> texts;
    std::vector<PaperShape> shapes;      // P2B: kullanici 2B cizimleri (kagit mm)
    FabricationStatus fabrication;
    LayoutConfig layoutConfig;
    LayoutReport layout;
    std::uint64_t documentRevision{0};   // cizimin uretildigi Document surumu
    ElementId nextId{1};
    // --- P2C: coklu pafta seti baglantilari (tek sayfada da anlamli) ---
    SheetRole role{SheetRole::Primary};
    ElementId parentSheetId{0};          // Primary: 0; bagli pafta: ana pafta
    std::string sheetNumber;             // "A100", "A100-01"
    int sheetIndex{1};                   // set icinde 1 tabanli sira
    std::vector<SheetReference> references;

    ElementId allocateId() noexcept { return nextId++; }
    const DrawingView* findView(ElementId id) const noexcept;
    const DrawingView* viewOfKind(ViewKind kind) const noexcept;
    // Not/legend icin ayrilan serit: title block'un SOLU (cerceve ici), kagit mm.
    Bounds2 notesArea() const noexcept;
    Bounds2 titleBlockRect() const noexcept;
    // Cizim cercevesi icinde (title block ayrilmis) kullanilabilir alan, kagit mm.
    Bounds2 drawingArea() const noexcept;
};

class DrawingDocument {
public:
    DrawingSheet& addSheet(DrawingSheet sheet);
    const std::vector<DrawingSheet>& sheets() const noexcept { return sheets_; }
    std::vector<DrawingSheet>& sheets() noexcept { return sheets_; }
    ElementId allocateSheetId() noexcept { return nextSheetId_++; }

    // --- P2C ---
    const DrawingPreferences& preferences() const noexcept { return preferences_; }
    void setPreferences(const DrawingPreferences& p) noexcept { preferences_ = p; }
    const DrawingSetInfo& setInfo() const noexcept { return setInfo_; }
    DrawingSetInfo& setInfo() noexcept { return setInfo_; }
    const DrawingSheet* primarySheet() const noexcept;

    std::string serialize() const;
    static bool deserialize(const std::string& json, DrawingDocument& out, std::string& error);

private:
    std::vector<DrawingSheet> sheets_;
    DrawingPreferences preferences_{};
    DrawingSetInfo setInfo_{};
    ElementId nextSheetId_{1};
};

} // namespace mm::drawing
