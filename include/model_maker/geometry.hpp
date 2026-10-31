#pragma once

#include <cstddef>
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace mm {

struct Vec2 {
    double x{};
    double y{};
};

struct Vec3 {
    double x{};
    double y{};
    double z{};

    Vec3 operator+(const Vec3& other) const noexcept;
    Vec3 operator-(const Vec3& other) const noexcept;
    Vec3 operator*(double scalar) const noexcept;
    bool operator==(const Vec3& other) const noexcept = default;
};

// Profil kesit cercevesi — TEK KAYNAK: hem kati uretimi (extrudeProfileSolid)
// hem gumball yerel cercevesi bu kurali kullanir, boylece cizilen kati ile
// tutamaclar asla celismez.
//
// KURAL (kullanici): "Profil cizimlerinde her zaman ALT BASLIK XY duzlemine
// PARALEL olacak." Yani kesitin genislik ekseni YATAY secilir; baslik
// plakalari yatay (XY'ye paralel), govde dusey kalir:
//   width  = normalize(n x axis)   (n = referenceNormal, dunya Z iken {+-y, ax, 0})
//   height = axis x width          (n = dunya Z, yatay uyede tam olarak +Z)
//   axis   = normalize(axisDirection)
// GUNCELLEME (kullanici): "profil cizilirken ust baslik WORKPLANE ile paralel
// olacak." → cagiran, baslik plakalarinin paralel olmasini istedigi duzlemin
// NORMALINI verir (aktif is duzleminin normali; bkz. view_definition.hpp
// profileSectionReferenceNormal). referenceNormal = dunya Z iken sonuc eski
// davranisla BIREBIR aynidir.
// Not: OCC'nin iki argumanli gp_Ax3(P, N) kurucusu X eksenini N'in en kucuk
// koordinatina gore KEYFI secer; eksenel kirisleri 90° yatirir (genislik Z'ye,
// yukseklik Y'ye gider) ve plan caprazi uyelerde dogru sonuc verdigi icin
// tutarsiz gorunur. Bu yuzden cerceve elle kurulur.
struct ProfileSectionFrame {
    Vec3 width{1.0, 0.0, 0.0};   // kesit genisligi yonu (yerel X)
    Vec3 height{0.0, 1.0, 0.0};  // kesit yuksekligi yonu (yerel Y)
    Vec3 axis{0.0, 0.0, 1.0};    // uye ekseni (yerel Z)
};

// Sifir uzunlukta dunya cercevesi doner (axis = +Z, width = +X).
// referenceNormal: baslik plakalarinin PARALEL olacagi duzlemin normali
// (varsayilan dunya Z = XY duzlemi → eski davranis). n ∥ axis ise (baslik o
// duzleme paralel olamaz) dunya kuralina, o da dejenere ise +X'e dusulur.
ProfileSectionFrame profileSectionFrame(const Vec3& axisDirection,
                                       const Vec3& referenceNormal = Vec3{0.0, 0.0, 1.0}) noexcept;

struct WorkPlane {
    Vec3 origin{};
    Vec3 u{1.0, 0.0, 0.0};
    Vec3 v{0.0, 1.0, 0.0};
    Vec3 normal{0.0, 0.0, 1.0};

    static WorkPlane world() noexcept;
    static std::optional<WorkPlane> fromThreePoints(Vec3 first, Vec3 second, Vec3 third) noexcept;
    static WorkPlane fromAxisAndPoint(Vec3 origin, Vec3 zPoint) noexcept;
    static WorkPlane fromCurrentView(Vec3 origin, const Vec3& viewDir, const Vec3& viewUp) noexcept;
    static WorkPlane fromViewDirection(Vec3 origin, Vec3 viewDirection) noexcept;
    WorkPlane rotatedX(double angleDeg) const noexcept;
    WorkPlane rotatedY(double angleDeg) const noexcept;
    WorkPlane rotatedZ(double angleDeg) const noexcept;
    Vec3 fromPlane(Vec2 point) const noexcept;
    Vec2 toPlane(Vec3 point) const noexcept;
    // Noktayi duzleme iz dusur (normal bileseni atilir). Iki noktali gorunuste
    // secilen noktalari is duzlemine oturtmak icin (bkz. application.cpp).
    Vec3 projectPoint(Vec3 point) const noexcept;
};
// IS-DUZLEMI (UCS) DELTA DONUSUMU — tek kaynak.
// Dolgusal kopyala/tasima paneli ve "dx,dy,dz" girdisi IS DUZLEMI
// eksenlerindedir; geometri dunya koordinatindadir. Bu iki fonksiyon
// arasindaki donusumu TEK yerde tutar (round-trip birebir).
Vec3 workPlaneDeltaToWorld(const WorkPlane& plane, const Vec3& delta) noexcept;
Vec3 worldDeltaToWorkPlane(const WorkPlane& plane, const Vec3& delta) noexcept;



struct Edge {
    std::size_t from{};
    std::size_t to{};
    bool operator==(const Edge& other) const noexcept = default;
};

// KONSTRUKSIYON (yardimci) nesne turu (P2). WireframeModel uzerinde isaret
// olarak tasinir (kopya/tasima/undo ile kendiliginden gezer); geometri ise
// standart WireframeModel'dir (snap/secim/gumball zaten calisir).
enum class ConstructionKind { None, Point, Line, Plane };

// KESIT YERLESIM ANCHOR'I (Slice 2d-2c P3 / 2d-3). Uye ekseni kesitin neresinden
// gecer? DEFAULT Top ("top of section"): eksen kesitin EN USTUNDEN gecer, kesit
// altina sarkar (Tekla "position: top of section"). Bu, mevcut modellerin KALICI
// dogruluk kaynagi olan davranistir; var olan belgeler SESSIZCE baska bir
// anchor'a DONUSTURULMEZ. Center/Bottom kullanici secenekleridir.
enum class SectionAnchor {
    Top,     // kesit ust yuzeyi eksende (mevcut davranis)
    Center,  // kesit merkezi/orijini eksende
    Bottom,  // kesit alt yuzeyi eksende
};

using Face = std::vector<std::size_t>;

struct EntityProperties {
    std::string layer{"0"};
    std::string profileName;
    double profileRotation{};
    std::int64_t profileSourceLine{-1};
    // Katinin kendi ekseni (atama anindan) — yeniden uretim asla cizgi
    // aramaz; indeks kaymalarina bagli yanlis eksen sorunu kokten biter.
    double axisFromX{}, axisFromY{}, axisFromZ{};
    double axisToX{}, axisToY{}, axisToZ{};
    // Kesit REFERANS NORMALI: baslik plakalarinin PARALEL oldugu duzlemin
    // normali. Kati uretilirken saklanir; gumball yerel cercevesi ve yeniden
    // uretim AYNI degeri kullanir (yoksa tutamaclar katidan kayar).
    // Varsayilan (0,0,1) = dunya XY → eski davranis birebir korunur.
    double sectionNormalX{}, sectionNormalY{}, sectionNormalZ{1.0};
    // Kesit yerlesim ANCHOR'I (SectionAnchor): uye ekseninin kesitin neresinden
    // gectigi. DEFAULT Top ("top of section") — eski dosyalar sessizce baska
    // bir anchor'a donusturulmez. MMW8'de 'A' blogu ile (EntityId -> anchor)
    // kalicilastirilir; MMW1-7 yuklemede Top varsayilir.
    SectionAnchor sectionAnchor{SectionAnchor::Top};
    std::string lineType{"BYLAYER"};
    std::string material;
    int colorIndex{256};
    std::optional<std::uint32_t> trueColor;
    int lineWeight{-1};
    double thickness{};
    double lineTypeScale{1.0};
    int transparency{};
    bool visible{true};
    bool frozen{};
    bool locked{};
    bool plottable{true};
    std::string description;
    std::string name;      // parca adi (Tekla Plates: Name; bos = varsayilan)
    std::string finish;    // yuzey islemi (Tekla Plates: Finish; bos = varsayilan)
    std::uint32_t effectiveColor{0x68CAFF};
    int effectiveLineWeight{};
    std::string effectiveLineType{"CONTINUOUS"};
    bool operator==(const EntityProperties&) const = default;
};

class WireframeModel {
public:
    WireframeModel() = default;
    WireframeModel(std::vector<Vec3> vertices, std::vector<Edge> edges,
                   std::vector<Face> faces = {});

    static WireframeModel line(Vec3 from, Vec3 to);
    static WireframeModel polyline(const std::vector<Vec3>& vertices);
    static WireframeModel point(Vec3 position);
    static WireframeModel face3D(const std::array<Vec3, 4>& corners);
    static WireframeModel rectangle(Vec3 firstCorner, Vec3 oppositeCorner);
    static WireframeModel circle(Vec3 center, double radius, std::size_t segments = 64);
    static WireframeModel rectangleOnPlane(const WorkPlane& plane, Vec2 firstCorner, Vec2 oppositeCorner);
    static WireframeModel circleOnPlane(const WorkPlane& plane, Vec2 center, double radius,
                                        std::size_t segments = 64);
    static WireframeModel cube(double size);
    static WireframeModel pyramid(double baseSize, double height);

    const std::vector<Vec3>& vertices() const noexcept;
    const std::vector<Edge>& edges() const noexcept;
    const std::vector<Face>& faces() const noexcept;
    bool isPointEntity() const noexcept;
    bool isFace3D() const noexcept;
    std::optional<Vec3> insertionPoint() const noexcept;
    std::optional<Vec3> analyticCenter() const noexcept;
    std::optional<double> analyticRadius() const noexcept;
    const EntityProperties& properties() const noexcept;
    void setProperties(EntityProperties properties);
    // KONSTRUKSIYON turu (P2): varsayilan None; Point/Line/Plane isareti.
    ConstructionKind constructionKind() const noexcept { return constructionKind_; }
    void setConstructionKind(ConstructionKind kind) noexcept { constructionKind_ = kind; }
    void translate(const Vec3& offset) noexcept;
    void rotateAroundZ(const Vec3& center, double radians) noexcept;
    void rotateAroundAxis(const Vec3& center, const Vec3& axis, double radians) noexcept;
    // Genel afin donusum: v -> center + M*(v - center); M row-major 3x3.
    // Rotate/scale gumball'i bunu kullanir (tek transform yolu).
    void affineAbout(const Vec3& center, const std::array<double, 9>& linear) noexcept;

private:
    std::vector<Vec3> vertices_;
    std::vector<Edge> edges_;
    std::vector<Face> faces_;
    bool pointEntity_{};
    bool face3D_{};
    std::optional<Vec3> insertionPoint_;
    std::optional<Vec3> analyticCenter_;
    std::optional<double> analyticRadius_;
    EntityProperties properties_;
    ConstructionKind constructionKind_{ConstructionKind::None};
};

// KONSTRUKSIYON DUZLEM CERCEVESI: ConstructionPlane modelinin yerel
// koordinat sistemini 4 koseden yeniden kurar. Koseler duzlem uzayinda
// (-h,-h),(h,-h),(h,h),(-h,h) sirasindadir (Document::addConstructionPlane);
// bu yuzden u = normalize(v1-v0), v = normalize(v3-v0), normal = u x v,
// origin = kose centroid'i. WorkPlane ayri bir kalicilik alani tasimaz —
// koseler zaten MMW8'de sakli oldugu icin cerceve kayit/ac sonrasi da
// aynen geri kurulur (ayrica P1 Fitting kesme duzleminin cercevesini de
// buradan alir). Degenerese (4 kose yok, kenar ~0, cakisik/collinear)
// ise nullopt doner.
std::optional<WorkPlane> constructionPlaneFrame(const WireframeModel& model) noexcept;

struct ProfileDefinition {
    const wchar_t* label;
    const char* name;
    const char* type;
    double h; // height mm
    double b; // width mm
    double tf; // flange thickness mm
    double tw; // web thickness mm
    double area; // cm²
};
struct NodeConstraint {
    bool ux{}, uy{}, uz{}, rx{}, ry{}, rz{};
    bool isPinned() const noexcept { return ux && uy && uz && !rx && !ry && !rz; }
    bool isFixed() const noexcept { return ux && uy && uz && rx && ry && rz; }
    bool isFree() const noexcept { return !ux && !uy && !uz && !rx && !ry && !rz; }
    void setPinned() noexcept { ux = uy = uz = true; rx = ry = rz = false; }
    void setFixed() noexcept { ux = uy = uz = rx = ry = rz = true; }
    void setFree() noexcept { ux = uy = uz = rx = ry = rz = false; }
};
struct BeamLoad {
    double wY{}; // vertical distributed load (kN/m, positive = -Y direction)
    double wZ{}; // lateral distributed load (kN/m)
};

// MESH RAY-CAST SONUCU (3B yuz secimi icin; orn. bulon ekseni = yuz normali).
struct MeshRayHit {
    Vec3 point{};      // dunya koordinatinda isabet noktasi
    Vec3 normal{};     // yuz normali (birim uzunluk)
    double distance{}; // isin parametresi t >= 0 (rayOrigin + t*rayDirection)
    std::size_t faceIndex{}; // isabet edilen poligonun model.faces() indeksi
};

// Bir WireframeModel'in ucgen yuzlerine karsi en yakin isin kesisimi (Moller-
// Trumbore). faces() poligonlari fan ucleme ile ucgenlere ayrilir ({v0,vi,
// vi+1}). Isabet yoksa nullopt. rayDirection birim olmali (normalize edilir).
std::optional<MeshRayHit> raycastMesh(const WireframeModel& model, Vec3 rayOrigin,
                                      Vec3 rayDirection) noexcept;

// Ayni BAGLANTILI DUZLEMSEL yuzdeki (ayni ana B-Rep yuzunden gelen) TUM ucgen
// indekslerini dondurur. startTriangle'dan baslar, paylasilan kenar uzerinden
// es-duzlemsel (normal paralel, |dot|>0.999) komsulara tasarak yayilir. Bolt
// yuz secim vurgusunun TEK ucgen degil, tum duz yuzeyi vurgulamasi icin.
// Gecersiz startTriangle icin {startTriangle} doner.
std::vector<std::size_t> planarFaceTriangleGroup(const WireframeModel& model,
                                                 std::size_t startTriangle) noexcept;

} // namespace mm
