#pragma once

// UNIVERSAL COMPONENT DIALOG + SCHEMATIC FRAMEWORK.
//
// Tekla-tarzi component dialogu: SOL taraf sematik resim (2B cizim + hotspot),
// SAG taraf parametre listesi (gruplara bolunmus alanlar). Bir hotspot sematik
// uzerindeki bir noktadir; tiklani nca ilgili parametre satirini vurgular ve
// tersi (parametre satiri secilince hotspot vurgulanir). Baglanti (End Plate,
// Fin Plate, Base Plate, ...) gelistiren programci YALNIZCA:
//
//   1) parametre semasini (CompParamSpec listesi),
//   2) sematik cizimi (ComponentDiagramItem + DiagramPrimitive),
//   3) hotspot -> parametre baglarini ve
//   4) parametre gruplarini (sekmeler/bolumler)
//
// tanimlar. Cizim/etkilesim/alan duzeni mantigi TEK KAYNAK buradadir.
//
// BU DOSYA SAF VERI MODELIDIR (OCCT'siz, windows.h'siz). Linux'ta derlenir ve
// birim testleriyle dogrulanir. Gercek cizim/hit-test Qt/Win32 katmaninda bu
// veriyi okur; "cizimle hit-test AYNI listeyi kullanir" kurali component_glyph
// ile ayni felsefededir.
//
// SOURCE OF TRUTH (yeniden yazilmaz):
//   - component_engine.hpp  : CompParamSpec / CompParam / ComponentType / Registry
//   - end_plate.hpp         : EndPlateParams (zengin yapi)
//
// BU KATMANIN SAGLADIGI YENI PARCALAR:
//   DiagramPrimitive/ComponentDiagramItem : normalizesmis 2B sematik cizim
//   DiagramHotspot                        : cizim noktasi <-> parametre baglantisi
//   ComponentDialogSchema/Group           : dialog semasi (cizim + gruplar)
//   validateDialogSchema                  : sema butunlugu (tekil id, gecerli bag)
//   hitTestDiagramHotspot                 : cizim noktasi -> hotspot secimi
//   endPlateToParams / endPlateFromParams : EndPlateParams <-> CompParam adaptor
//   ComponentDialogRegistry               : typeId -> dialog tanimi

#include "model_maker/component_engine.hpp"
#include "model_maker/end_plate.hpp"

#include <optional>
#include <string>
#include <vector>

namespace mm {

// --- 2B SEMATIK CIZIM --------------------------------------------------------
// Koordinat uzayi normalizesmistir: x saga, y yukari. Birimler soyuttur (resim
// birimleri); renderer bunu ekran dikdortgenine olcekler. Bu sayede sematik
// tanimi ekran boyutundan ve DPI'dan bagimsiz kalir.

struct DiagramPoint {
    double x{0.0};
    double y{0.0};
};

enum class DiagramPrimitiveKind {
    Rect,      // eksen-hizali dikdortgen (a=min kose, b=max kose)
    Line,      // dogru parcasi (a=bas, b=son)
    Circle,    // daire (a=merkez, r=yaricap)
    Text,      // etiket (a=sol-alt cipa, text=metin)
    Polyline,  // coklu cizgi (pts=koseler, closed=kapali)
};

// Tek bir cizim ilkel elemani. Hangi alanin gecerli oldugu `kind`e gore degisir
// (kullanilmayan alanlar yok sayilir).
struct DiagramPrimitive {
    DiagramPrimitiveKind kind{DiagramPrimitiveKind::Line};
    DiagramPoint a{};   // Rect min / Line bas / Circle merkez / Text cipa
    DiagramPoint b{};   // Rect max / Line son
    double r{0.0};      // Circle yaricap
    std::wstring text;  // Text metni
    std::vector<DiagramPoint> pts;  // Polyline koseleri
    bool closed{false}; // Polyline kapali mi
    bool dashed{false}; // kesikli cizgi (olcu cizgileri icin)
    bool filled{false}; // dolu gövde (parca yuzeyleri icin)
};

// Adlandirilmis cizim ogresi (orn. "column", "beam", "plate", "bolts"). id sema
// icinde TEKIL olmali. Ilkel elemanlari cizim sirasinda tasir.
struct ComponentDiagramItem {
    std::wstring id;
    std::vector<DiagramPrimitive> primitives;
};

// Sematik uzerinde tiklanabilir nokta. position + radius pick bolgesini tanimlar.
// paramName bos degilse bu hotspot O parametre alanina baglidir (tiklama ->
// satir vurgusu ve tersi). label istege bagli gorunen numara/etiket.
struct DiagramHotspot {
    std::wstring id;      // sema icinde tekil
    DiagramPoint position{};
    double radius{8.0};   // pick yaricapi (cizim birimi)
    std::optional<std::wstring> paramName;  // bagli parametre (bos = dekoratif)
    std::optional<std::wstring> label;      // gorunen etiket
};

// Parametre alanlarini bir sekme/bolum altinda siralar. paramNames sirasi ayrica
// goruntuleme sirasidir; her ad semada var olan bir parametreyi isaret etmeli.
struct ComponentDialogGroup {
    std::wstring id;      // sema icinde tekil
    std::wstring title;   // gorunen baslik
    std::vector<std::wstring> paramNames;  // bu gruptaki parametreler (sirali)
};

// Bir component tipinin TAM dialog semasi: cizim + hotspotlar + parametre
// gruplari. typeId ComponentRegistry'deki tipla eslesir.
struct ComponentDialogSchema {
    std::wstring typeId;
    std::vector<ComponentDiagramItem> items;
    std::vector<DiagramHotspot> hotspots;
    std::vector<ComponentDialogGroup> groups;
};

// --- DOGRULAMA ---------------------------------------------------------------

struct DialogSchemaValidation {
    bool ok{false};
    std::vector<std::wstring> problems;  // sorun aciklamalari (insan okunur)
};

// Sema butunlugunu denetler: (1) item/hotspot/grup id'leri bos ve tekil degil,
// (2) hotspot->paramName semada VAR olan bir parametreyi isaret ediyor,
// (3) grup->paramNames semada VAR olan parametreleri isaret ediyor. specs =
// dialog'un parametre semasi (ComponentDialogDefinition::specs). Dogrulama
// firlatmaz; tum sorunlari problems listesinde toplar.
DialogSchemaValidation validateDialogSchema(const ComponentDialogSchema& schema,
                                            const std::vector<CompParamSpec>& specs) noexcept;

// p noktasina en yakin hotspot'un id'sini dondurur (mesafe <= radius). Birden
// fazla aday varsa EN YAKIN olan kazanir (esitlikte onceki indeks). Hicbiri
// kapsamiyorsa nullopt.
std::optional<std::wstring> hitTestDiagramHotspot(const ComponentDialogSchema& schema,
                                                  DiagramPoint p) noexcept;

// id'li hotspot'u bulur (yoksa nullptr).
const DiagramHotspot* findHotspot(const ComponentDialogSchema& schema,
                                  const std::wstring& id) noexcept;

// --- PARAMETRE ADAPTORU (End Plate) -----------------------------------------
// Zengin EndPlateParams yapisini <-> tipli CompParam listesi arasinda cevirir.
// Kapsam: endPlateDialogSpecs()'te listelenen 27 alan. Adaptor YALNIZCA bu
// alanlari tasir; stiffener'lar, degisken bulon araliklari (boltSpacingXs/Ys),
// deletedBolts ve bulon standardi/yonu gibi alanlar SONRAKI dilime birakilmistir
// (round-trip onlari varsayilana birakir). Kapsam disi alanlari tasiyan bir
// yapi adaptorden gecince o alanlar varsayilana doner (bilinmeyen parametre
// kaybolmaz — endPlateFromParams yalnizca bilinen adlari okur).

// End Plate dialog'unun parametre semasi (superset: engine + Tekla 144
// alanlari). Varsayilanlar EndPlateParams yapisinin varsayilanlariyla aynidir.
std::vector<CompParamSpec> endPlateDialogSpecs() noexcept;

// EndPlateParams -> CompParam listesi (kapsanan alanlar).
std::vector<CompParam> endPlateToParams(const EndPlateParams& p) noexcept;

// CompParam listesi -> EndPlateParams (eksik/gecersiz degerlerde guvenli
// varsayilan; kapsanan alanlar okunur, digerleri varsayilan kalir).
EndPlateParams endPlateFromParams(const std::vector<CompParam>& params) noexcept;

// --- DIALOG REGISTRY ---------------------------------------------------------

// Bir component tipinin dialog tanimi: parametre semasi + sematik sema. Ayni
// typeId tekil kaydedilir.
struct ComponentDialogDefinition {
    std::wstring typeId;
    std::vector<CompParamSpec> specs;
    ComponentDialogSchema schema;
};

// Statik dialog registry/factory (ComponentRegistry ile ayni felsefe). Uygulama
// bir component acinca typeId ile buradan dialog tanimini alir.
class ComponentDialogRegistry {
public:
    ComponentDialogRegistry() = default;

    static ComponentDialogRegistry& instance() noexcept;

    // Ayni typeId varsa REDDEDER (false); yoksa kaydeder (true). Bos typeId
    // veya bos specs de reddedilir.
    bool registerDialog(ComponentDialogDefinition def) noexcept;
    const ComponentDialogDefinition* find(const std::wstring& typeId) const noexcept;
    std::size_t count() const noexcept { return defs_.size(); }
    void clear() noexcept { defs_.clear(); }

private:
    std::vector<ComponentDialogDefinition> defs_;
};

// --- YERLESIK DIALOG KATALOGU ------------------------------------------------

ComponentDialogDefinition endPlateDialogDefinition() noexcept;
ComponentDialogDefinition finPlateDialogDefinition() noexcept;

// Yerlesik dialog katalogunu bir registry'ye kaydeder (idempotent — ayni
// typeId zaten varsa atlanir).
void registerBuiltInDialogs(ComponentDialogRegistry& registry) noexcept;

} // namespace mm
