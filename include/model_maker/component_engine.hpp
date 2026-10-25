#pragma once

// COMPONENT ENGINE (Phase C1/C2) — genisletilebilir parametrik birlesim motoru.
//
// Amac: yeni bir celik birlesim gelistiren programci, ortak elemanlari (levha,
// bulon grubu, kesme modifier, kaynak) ve parametre altyapisini YENIDEN YAZMADAN
// yeni component tipini sisteme ekleyebilsin. Bunun icin uc ayri katman:
//
//   1) TIPLI PARAMETRE SEMASI (CompParamSpec / CompParamValue):
//      Length, Angle, Integer, Boolean, Enum, String, Profile, Material,
//      EntityRef turleri. Her parametre varsayilan deger + izin verilen aralik
//      + birim + aciklama + enum secenekleri tasir. Dogrulama/migrasyon ortak
//      (validateParamValue / alignParamsToSpecs): eksik -> varsayilan, eski/
//      tip uyumsuz -> zorlanir, BILINMEYEN parametreler SESSIZCE KAYBEDILMEZ
//      (listeye eklenir ve korunur).
//
//   2) REGISTRY / FACTORY (ComponentRegistry):
//      Her component tipi kalici TypeId (string) + SchemaVersion tasir. Bir
//      tip, parametre semasi + bir GEOMETRI URETECI (ComponentGenerator)
//      ile kaydedilir. Yeni component = registerComponent(...) + kendi
//      generator fonksiyonu; Document/renderer/serializer icine switch/case
//      EKLEMEK GEREKMEZ (kayit bilgisi registry'den okunur).
//
//   3) URETIM PLANI (GenerationPlan):
//      Generator girdi olarak Main/Secondary parca geometrisi + yerel koordinat
//      sistemi + parametreler + referanslar alir; CIKTI olarak ortak eleman
//      TURLERINI kullanan bir URETIM PLANI uretir: PlateSpec (levha), BoltGroup
//      (bulon grubu), CutModifier (kesme), WeldSpec (kaynak). GERCEK OCCT
//      B-Rep islemleri bu katmanda YAPILMAZ — plan, Document/OCC katmaninda
//      maddelestirilir (EntityId atanir, B-Rep uretilir, undo/MMW8 yapilir).
//      Boylece "parametre uretimi" ile "gercek geometri" ayrilir.
//
// Sahiplik kurali (generator ciktisindaki BoltGroup.parts): generator yalnizca
// KULLANICI parcalarini bilir (kolon/ana parca id'si). Bağlantının SAHIP OLDUGU
// levha, materializasyon aninda Document tarafindan atanir ve bulon grubunun
// parts listesinin BASINA eklenir ({levha, kolon}) — bolonlar levha + ana
// parca deler. Kullanici parcalari (main/secondary) component tarafindan
// SAHIPLENILMEZ, yalnizca referanslanir.
//
// windows.h yok; Linux'ta derlenir/test edilir. Saf veri modeli + uretim plani.

#include "model_maker/bolt.hpp"
#include "model_maker/constraint_system.hpp"
#include "model_maker/cut_modifier.hpp"
#include "model_maker/entity_id.hpp"
#include "model_maker/geometry.hpp"
#include "model_maker/magnetic_frame.hpp"

#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace mm {

// --- COMPONENT OBJECT KIND (ortak nesne turleri) -----------------------------
// ComponentObjectSpec (component_framework.hpp) bu turu tasir; ComponentChildRecord
// ve ComponentInstanceRecord da tip-guvenli KALICILIK icin ayni turu kullanir.
// roleKey BIR KERE bir ture baglanir; ayni roleKey baska bir ture yeniden
// baglanamaz (ComponentChildTable bunu denetler).
enum class ComponentObjectKind {
    Plate,
    BoltGroup,
    Cut,
    Weld,
    ConstructionPoint,
    ConstructionLine,
    ConstructionPlane,
};

// --- TIPLI PARAMETRE SEMASI --------------------------------------------------

enum class CompParamType {
    Length,     // mm (double, aralikli)
    Angle,      // derece (double, aralikli)
    Integer,    // int (aralikli)
    Boolean,    // bool
    Enum,       // seceneklerden biri (wstring)
    String,     // serbest metin (wstring)
    Profile,    // profil adi (wstring; katalogdan)
    Material,   // malzeme sinifi (wstring)
    EntityRef,  // baska bir varliga referans (EntityId)
};

// Bir parametrenin sema tanimi: ad + tur + birim + aciklama + varsayilan +
// izin verilen aralik + enum secenekleri. hasMin/hasMax ilgili turde aralik
// kontrolu acar (Length/Angle/Integer).
struct CompParamSpec {
    std::wstring name;
    CompParamType type{CompParamType::Length};
    std::wstring unit;       // "mm", "deg", "" ...
    std::wstring description;

    double defDouble{0.0};
    double minDouble{0.0};
    double maxDouble{0.0};
    bool hasMinDouble{false};
    bool hasMaxDouble{false};

    int defInt{0};
    int minInt{0};
    int maxInt{0};
    bool hasMinInt{false};
    bool hasMaxInt{false};

    bool defBool{false};
    std::wstring defString;
    std::vector<std::wstring> choices;  // Enum
    // TEKLA "Default" ONIZLEME KUTUSU: true ise dialog satirin solunda bir
    // onay kutusu gosterir. Isaretli = kullanicinin degeri KULLANILIR; isaretsiz
    // = parametre sonuc listesine HIC yazilmaz, motor varsayilani uygular
    // (Tekla'daki "Default" davranisi). false = kutu yok, deger her zaman yazilir
    // (geriye uyumlu; mevcut parametrelerin davranisi degismez).
    bool optional{false};
    // TEKLA ALANI AMA GEOMETRISI YOK: true ise dialog alani Tekla duzeninde
    // GOSTERIR fakat devre disi (gri) birakir ve deger sonuca YAZILMAZ — motor
    // bu alani okumaz, yani gorunen deger hicbir geometriyi degistirmez. Amac
    // formu Tekla'ya benzetirken olmayan bir ozelligi varmis gibi gostermemek.
    bool documentedOnly{false};
};

// Tipik deger kumesi. Turler (double/int/bool/wstring/EntityId) birbirinden
// ayri oldugu icin variant net calisir. EntityId = uint64_t.
using CompParamValue = std::variant<double, int, bool, std::wstring, EntityId>;

// Bir instance'in parametre DEGERI (ad + tipli deger).
struct CompParam {
    std::wstring name;
    CompParamValue value;
};

// Varsayilan degeri ture dikkate alarak uretir.
CompParamValue paramDefaultValue(const CompParamSpec& spec) noexcept;

// Bir degeri semaya gore dogrular/zorlar (migrate): tip uyumsuzsa zorlanir,
// aralik disiysa kelepcelenir, Enum gecersizse varsayilana duser. Hicbir durumda
// firlatmaz — her zaman gecerli bir deger doner (guvenli migration).
CompParamValue validateParamValue(const CompParamSpec& spec,
                                  const CompParamValue& value) noexcept;

// Değerleri semaya hizalar: (1) semada olan ama degerde eksik -> varsayilan,
// (2) semada olan ve degerde olan -> validateParamValue, (3) semada OLMAYAN
// ama degerde olan -> KORUNUR (bilinmeyen parametre sessizce KAYBEDILMEZ;
// ileri uyumluluk icin listenin sonuna eklenir).
std::vector<CompParam> alignParamsToSpecs(const std::vector<CompParamSpec>& specs,
                                          const std::vector<CompParam>& values) noexcept;

const CompParamSpec* findParamSpec(const std::vector<CompParamSpec>& specs,
                                   const std::wstring& name) noexcept;
const CompParam* findParam(const std::vector<CompParam>& params,
                           const std::wstring& name) noexcept;

// Tipli deger okuyucular (fallback: bulunamazsa/tip uyusmazsa).
double paramAsDouble(const std::vector<CompParam>& params, const std::wstring& name,
                     double fallback) noexcept;
int paramAsInt(const std::vector<CompParam>& params, const std::wstring& name,
               int fallback) noexcept;

// Backward-compatible Haunch 40 edge migration. Old "boltEdge" denotes
// the structural allowance; measured centre-to-plate edges are eL/eR=E,
// eT/eB=E+20 because the outermost bolt rows sit 20 mm inward.
std::vector<CompParam> migrateHaunch40Edges(const std::vector<CompParam>& values);
bool paramAsBool(const std::vector<CompParam>& params, const std::wstring& name,
                 bool fallback) noexcept;
std::wstring paramAsString(const std::vector<CompParam>& params,
                           const std::wstring& name, std::wstring fallback) noexcept;
EntityId paramAsEntity(const std::vector<CompParam>& params, const std::wstring& name,
                       EntityId fallback) noexcept;

// --- REGISTRY / FACTORY ------------------------------------------------------

// Bir uyenin (kiris/kolon) generator icin gereken GEOMETRI baglami. Document/
// App katmani bu bilgiyi profilin/katinin kalici ozelliklerinden doldurur.
struct MemberGeometry {
    EntityId id{kInvalidEntityId};
    Vec3 axisFrom{};                 // eksen basi
    Vec3 axisTo{};                   // eksen ucu (baglanti noktasi = kiris ucu)
    Vec3 refNormal{0.0, 0.0, 1.0};   // kesit referans normali
    double secWidth{0.0};            // kesit bbox genisligi (mm)
    double secHeight{0.0};           // kesit bbox yuksekligi (mm)
    // Kesit PARCA kalinliklari (mm); profil katalogundan doldurulur. 0 = bilinmiyor.
    // Bulon grip (boy) hesabinda bbox YERINE bunlar kullanilir: bir bulon I-profil
    // govdesini delerse grip = web (secWebThickness), flansi delerse grip = flans
    // (secFlangeThickness). bbox kullanmak flans genisligini (ornegin 300 mm) grip'e
    // sokar ve civata boyunu gercek disi uzatir.
    double secWebThickness{0.0};
    double secFlangeThickness{0.0};
    // Kesit MERKEZININ uye eksenine gore ISARETLI kaymasi (mm), kesit YUKSEKLIK
    // yonu boyunca. SectionAnchor: Top -> -h/2 (eksen ust baslikta, govde
    // asagida), Center -> 0, Bottom -> +h/2. Levha/bulon GOVDE MERKEZINE
    // oturmali; yoksa ust basliga kayar ve delikler web'e denk gelmez.
    double sectionCenterOffset{0.0};
};

// Generator girdisi: main (kolon) + secondary (kiris listesi) + parametreler.
struct ComponentInput {
    MemberGeometry main;
    std::vector<MemberGeometry> secondary;
    std::vector<CompParam> params;
};

// Levha spec'i (plan asamasinda; EntityId yok — materializasyonda atanir).
struct PlateSpec {
    Vec3 center{};
    Vec3 u{1.0, 0.0, 0.0};    // genislik yonu
    Vec3 v{0.0, 1.0, 0.0};    // yukseklik yonu
    Vec3 axis{0.0, 0.0, 1.0}; // kalinlik (normal) yonu
    double width{0.0};
    double height{0.0};
    double thickness{0.0};
    std::wstring name;
    // Tekla parca ozellikleri (Material / Finish / Pos_No). Bos = varsayilan.
    // Pos_No modelde ayri alan olarak yok: "Pos_No: <onek><baslangic>" olarak
    // parcanin aciklamasina (EntityProperties::description) yazilir.
    std::wstring material;
    std::wstring finish;
    std::wstring posNo;
    // KOSE PAHLARI (Tekla "chamfers"). corner: 0 = (-u,-v), 1 = (+u,-v), 2 = (+u,+v),
    // 3 = (-u,+v) (levha merkezine gore). type: 1 = dogru pah, 2 = konveks yay,
    // 3 = konkav yay. h = u yonunde, v = v yonunde bacak (mm). Bos = dikdortgen.
    struct Chamfer {
        int corner{0};
        int type{1};
        double h{0.0};
        double v{0.0};
        bool operator==(const Chamfer&) const = default;
    };
    std::vector<Chamfer> chamfers;
};

// Kaynak spec'i (plan asamasinda; ileriki fazlarda Weld modeli).
struct WeldSpec {
    EntityId partA{kInvalidEntityId};
    EntityId partB{kInvalidEntityId};
    // roleKey referanslari (generator yolu): uretilen levha gibi EntityId'si
    // henuz atanmamis owned child'lar plan asamasinda roleKey ile isaret edilir;
    // commit/recompute'ta EntityId'ye cozulur. \"main\"/\"secondary_N\" rezerve rol.
    std::string partARole;
    std::string partBRole;
    double throat{0.0};        // legacy: bacak (leg) anlaminda tutulur
    double size{5.0};          // kaynak olcusu (bacak, mm) — generator bunu kullanir
    bool doubleSided{false};   // cift tarafli fillet kaynagi
    std::wstring name;
};

// Uretim plani: generatorun CIKTISI. Sahip olunacak alt nesnelerin specleri +
// kullanici parcalarina uygulanacak kesmeler. ok=false ise error dolu ve plan
// kullanilmamali (belge KISMEN degistirilmez).
//
// STABIL SEMANTIC ROLEKEY (C6 sonrasi): generator her child'a KALICI bir roleKey
// atayabilir (plateKeys[i] <-> plates[i]). Bu anahtar generator SIRASINDAN
// BAGIMSIZDIR — generator ayni tür cocuklarin sirasini degistirse bile ayni
// roleKey ayni EntityId'yi korur (recompute/MMW8 stabil kimlik). Anahtar bos
// veya vektor kisa ise componentPlanFromGenerationPlan POSITIONAL fallback
// ("plate_0", ...) kullanir. Anahtar [A-Za-z0-9_] olmali ve plan icinde TEKIL
// olmali (baski kopru reddeder).
struct GenerationPlan {
    std::vector<PlateSpec> plates;
    std::vector<BoltGroup> boltGroups;   // parts = {ana parca}; levha materializasyonda basata
    std::vector<CutModifier> cuts;
    std::vector<WeldSpec> welds;
    std::vector<std::string> plateKeys;      // plates[i] -> semantic roleKey (bos = positional)
    std::vector<std::string> boltGroupKeys;  // boltGroups[i] -> semantic roleKey
    std::vector<std::string> cutKeys;        // cuts[i] -> semantic roleKey
    std::vector<std::string> weldKeys;       // welds[i] -> semantic roleKey
    bool ok{true};
    std::wstring error;
};

// --- COMPONENT CATALOG METADATA (C7) -----------------------------------------
// Katalog/generic UI'nin bir component'i HERHANGI BIR switch/case OLMADAN
// sunmasi icin gereken meta. ComponentType'in bir parcasidir; registry'de
// saklanir, catalog view (component_catalog.hpp) buradan okur. Bos = ayarsiz
// (katalogda gorunmez). IKINCI bir registry DEGILDIR — ayni kaydin alanidir.
struct ComponentCatalogMeta {
    std::wstring sourceComponentNumber;   // Tekla/standart no (orn "146"); bos = yok
    std::wstring category;                // "Shear connection", "Beam to Column", ...
    std::wstring primaryMemberType;       // secim ipucu: "kolon" / "kiris" / ...
    std::wstring secondaryMemberType;     // secim ipucu
    std::vector<std::wstring> supportedFeatures; // uretilen nesneler (levha/bulon/...)
    std::wstring coverageStatus;          // DOCUMENTED_ONLY / PARTIALLY_IMPLEMENTED / ...
    int pickCount{2};                     // secilecek parca sayisi (generic komut)
    bool catalogVisible{false};           // generic katalogda gosterilir mi (acikca opt-in)
};

// Bir component tipinin kayit tanimi.
struct ComponentType {
    std::wstring typeId;     // kalici tekil id (string) — yeni tip icin switch yok
    int schemaVersion{1};    // sema surumu (migration icin)
    std::wstring displayName;
    std::vector<CompParamSpec> params;
    ComponentCatalogMeta catalog;  // C7: katalog/generic UI metadatasi (bos = gizli)
};

// Generator fonksiyon imzasi: girdi -> uretim plani.
using ComponentGenerator = GenerationPlan (*)(const ComponentInput&);

// Statik registry/factory. Varsayilan kurulabilir (testler yerel registry
// kullanir); instance() uygulama-genel tek ornegi verir. Yeni component
// registerComponent() ile kaydedilir; Document/renderer/serializer tipi
// registry'den okur (switch/case GEREKMEZ).
class ComponentRegistry {
public:
    ComponentRegistry() = default;

    static ComponentRegistry& instance() noexcept;

    // Ayni typeId varsa REDDEDER (false); yoksa kaydeder (true). Generator
    // null olamaz.
    bool registerType(ComponentType type, ComponentGenerator generator) noexcept;
    const ComponentType* findType(const std::wstring& typeId) const noexcept;
    const ComponentGenerator* generator(const std::wstring& typeId) const noexcept;
    std::vector<std::wstring> typeIds() const noexcept;
    std::size_t typeCount() const noexcept { return entries_.size(); }

    // Tipi bulur, parametreleri semaya hizalar, generatoru cagirir. Bilinmeyen
    // tip -> ok=false plan (belge degismez). Generator yoksa -> ok=false.
    GenerationPlan generate(const std::wstring& typeId,
                            const ComponentInput& input) const noexcept;

    void clear() noexcept { entries_.clear(); }

private:
    struct Entry {
        ComponentType type;
        ComponentGenerator gen;
    };
    std::vector<Entry> entries_;
};

// --- KALICI INSTANCE KAYDI (Document/MMW8 katmani sonra baglanir) ------------

// TIP-GUVENLI, KALICI CHILD IDENTITY (C1). Bir component'in SAHIP OLDUGU veya
// referansladigi alt nesnenin kararli kimligi. roleKey -> (kind, EntityId,
// bagimliliklar, uretim parametreleri) iliskisini KORUR. roleKey bir ture
// (kind) baglidir; ayni roleKey FARKLI bir ture yeniden baglanamaz
// (ComponentChildTable::bind denetler). entityId materialization'da atanir ve
// recompute/MMW8 sonrasi STABIL kalir (ayni roleKey ayni EntityId).
struct ComponentChildRecord {
    std::string roleKey;
    ComponentObjectKind kind{ComponentObjectKind::Plate};
    EntityId entityId{kInvalidEntityId};       // sahip olunan alt nesne (0 = henuz materialize degil)
    std::vector<std::string> dependencies;     // bagli oldugu roleKey'ler (AttachedTo/DependsOn -> to)
    // Uretim parametreleri (kind ile tutarli TEK payload dolu olmalidir):
    std::optional<PlateSpec> plate;
    std::optional<BoltGroup> boltGroup;
    std::optional<CutModifier> cut;
    std::optional<WeldSpec> weld;
};

// Bir child kaydinin TUTARLI oldugunu dogrular: gecerli roleKey + payload kind
// ile uyumlu (kind == Plate ise plate dolu ve digerleri bos, ...). Type-safe
// baglama bunu on kosul sayar.
bool isValidComponentChildRecord(const ComponentChildRecord& c) noexcept;

// Bir component INSTANCE'inin kalici kaydi. Kendi EntityId'si, TypeId +
// SchemaVersion, main/secondary referanslari, tipli parametreler, yerlesim
// (LCS), sahip olunan alt nesneler ve kirli/oksz durumu. Bu kayit Document
// icinde saklanir; MMW8 bu yapiyi (TypeId + SchemaVersion + parametreler +
// yerlesim + referanslar + sahip olunan alt nesneler) serilestirir.
struct ComponentInstanceRecord {
    EntityId id{kInvalidEntityId};
    std::wstring typeId;
    int schemaVersion{1};
    EntityId mainPart{kInvalidEntityId};
    std::vector<EntityId> secondaryParts;
    std::vector<CompParam> params;
    Vec3 origin{};
    Vec3 u{1.0, 0.0, 0.0};
    Vec3 v{0.0, 1.0, 0.0};
    Vec3 axis{0.0, 0.0, 1.0};
    std::vector<EntityId> ownedPlateIds;      // sahip olunan levhalar
    std::vector<EntityId> ownedBoltGroupIds;  // sahip olunan bulon gruplari
    // TIP-GUVENLI CHILD TABLOSU (C1): roleKey -> (kind, EntityId, bagimliliklar,
    // uretim parametreleri). roleKey -> EntityId eslemesinin KALICI, deterministik
    // kaynagidir; ownedPlateIds/ownedBoltGroupIds legacy listeleri korunur ama
    // yeni kod bu tabloyu kullanir. MMW8'de ileriye-uyumlu ayri bloga yazilir
    // (legacy dosyada yoksa bos — ownedPlateIds/ownedBoltGroupIds'tan turetilir).
    std::vector<ComponentChildRecord> children;
    bool dirty{true};                         // yeniden hesaplama gerekli
    bool orphan{false};                       // typeId registry'de yok
    bool enabled{true};

    // --- (Slice 2d-3) PARAMETRIK REFERANS/KISIT/CERCEVE DURUMU ------------
    // Bunlar component'in TAM parametrik durumunu olusturur; MMW8'de 'I' blogu
    // DEGIL, ileriye-uyumlu AYRI bloklar ('M' cerceve, 'J' yuz referanslari,
    // 'D' kisit+handle+bagimlilik) ile kalicilastirilir. Eski dosyalarda bu
    // bloklar yoksa frame origin/u/v/axis'tan turetilir, refs/kisitlar BOS
    // kalir (legacy — recompute yine calisir).
    ComponentLocalFrame frame;                  // magnetic frame + alt nesne yerlesimleri
    std::vector<FaceReference> faceRefs;        // semantik yuz referanslari
    std::vector<DistanceConstraint> constraints; // mesafe kisitlari
    std::vector<ConstraintHandleDef> handles;   // handle tanimlari
    std::vector<ConstraintDependency> dependencies;  // bagimlilik tanimlari
};

// --- YERLESIK BILESEN KATALOGU ----------------------------------------------

ComponentType endPlateComponentType() noexcept;
GenerationPlan generateEndPlate(const ComponentInput& input) noexcept;

ComponentType finPlateComponentType() noexcept;
GenerationPlan generateFinPlate(const ComponentInput& input) noexcept;

// Tekla 146 "Shear plate simple" — tekli ortogonal shear tab. Geometri olarak
// mevcut fin plate cekirdegiyle AYNI (kolon yuzune kaynakli, kiris govdesine
// civatali, t-eksantrisite + SectionAnchor + rol tespiti). 146'nin fazla
// parametreleri (beam gap, double tab, stiffener, haunch, seat angle, notch,
// beam end cut, ...) blueprint/coverage'ta documented_only/unsupported olarak
// tasinir; bu dilimde geometri onlari SESSIZCE UYDURMAZ.
ComponentType shearPlate146ComponentType() noexcept;
GenerationPlan generateShearPlateSimple146(const ComponentInput& input) noexcept;

// Tekla 40 - Haunch: gercek hedef uyelerin geometri/kesitlerinden tureyen
// parametrik alin levhasi + konik haunch web/flans + 2 bulon grubu.
// Stage H40 v1 = supported core, 40'in tum ozel opsiyonlari DEGIL.
ComponentType haunch40ComponentType() noexcept;
GenerationPlan generateHaunch40(const ComponentInput& input) noexcept;

// Yerlesik katalogu bir registry'ye kaydeder (idempotent — ayni typeId zaten
// varsa atlanir).
void registerBuiltInComponents(ComponentRegistry& registry) noexcept;

} // namespace mm
