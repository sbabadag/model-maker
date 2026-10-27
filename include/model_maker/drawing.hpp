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
enum class ViewKind { Front, Top, Side };
enum class LineStyle { Visible, Hidden };
enum class ProjectionMethod { ThirdAngle, FirstAngle };
enum class SheetSize { A4, A3, A2, A1, A0 };
enum class DimensionDirection { Horizontal, Vertical, Aligned };
enum class DimensionRole {
    MemberLength,   // ana parca ekseni boyu (gercek 3B uzunluk)
    OverallWidth,   // gorunus sinir kutusu genisligi
    OverallHeight,  // gorunus sinir kutusu yuksekligi
    HoleSpacing,    // delik zinciri: kenar -> delik -> delik -> kenar (merkezler arasi)
    PartPosition    // montaj uyelerinin kenar konumlari zinciri (assembly)
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
    int textLevel{0};   // 0: olcu cizgisi ustunde; >0: dar aralikta metin yukari/yana kaydirilir (kademe)
    EntityId source{kInvalidEntityId};
    // p1-p2'den yeniden olculen uzunluk (yon'e gore).
    double measured() const noexcept;
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
    ElementId nextId{1};

    ElementId allocateId() noexcept { return nextId++; }
    const DrawingView* findView(ElementId id) const noexcept;
    const DrawingView* viewOfKind(ViewKind kind) const noexcept;
    // Cizim cercevesi icinde (title block ayrilmis) kullanilabilir alan, kagit mm.
    Bounds2 drawingArea() const noexcept;
};

class DrawingDocument {
public:
    DrawingSheet& addSheet(DrawingSheet sheet);
    const std::vector<DrawingSheet>& sheets() const noexcept { return sheets_; }
    std::vector<DrawingSheet>& sheets() noexcept { return sheets_; }
    ElementId allocateSheetId() noexcept { return nextSheetId_++; }

    std::string serialize() const;
    static bool deserialize(const std::string& json, DrawingDocument& out, std::string& error);

private:
    std::vector<DrawingSheet> sheets_;
    ElementId nextSheetId_{1};
};

} // namespace mm::drawing
