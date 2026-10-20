#pragma once
// MODEL MAKER BRIDGE — Tekla -> Model Maker paket okuyucu (veri modeli).
//
// Tekla extractor'unun urettigi sürümlü JSON paketini (manifest/component/
// parameters/objects/diagnostics + geometry/*) okur ve TIPLI bir C++ modeline
// cevirir. OCCT'ye bagli DEGILDIR (Linux OCC-off ctest'te kosar).
//
// Tekla GUID'leri yalniz KAYNAK referansidir; Model Maker EntityId'si importer
// tarafindan atanir. Okuma + dogrulama tek islemde yapilir; dogrulama gecmeyen
// paket "basarili" kabul EDILMEZ (ok=false -> out kullanilmamali).

#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace mm {

struct BridgeVec3 {
    double x = 0.0, y = 0.0, z = 0.0;
};

struct BridgeBBox {
    BridgeVec3 min;
    BridgeVec3 max;
};

struct BridgeMemberRef {
    std::string guid;
    std::string profile;
    std::string material;
    std::string role;              // "COLUMN" / "BEAM" / ... (uye rolu; V2)
    std::optional<BridgeVec3> startPoint;
    std::optional<BridgeVec3> endPoint;
};

// Bir parametrenin degeri. JSON tipine gore variant'a yerlestirilir;
// monostate = "value yok / null".
using BridgeParamValue = std::variant<std::monostate, double, int, bool, std::string>;

struct BridgeParameter {
    std::string name;
    std::string type;         // Length/Angle/Integer/Boolean/Enum/String/Profile/Material/EntityRef/Double/Unknown
    std::string units;
    std::string source;
    std::string readable;     // ok / unsupported / error
    std::string confidence;   // documented / observed / inferred / unresolved
    BridgeParamValue value;   // monostate = yok
};

// --- V2: bulon / kaynak / topoloji detaylari --------------------------------
// Bu alanlar yalnizca ilgili kind icin anlamlidir; diger kind'larda bos kalir.
// DEGERLER KAYNAK (Tekla) tarafindan bildirildigi gibi tasinir; importer bunlari
// DOGRULAR (projeksiyon/eksen/cap) ve dogrulanamayani INFERRED/UNRESOLVED isaretler.

// Tek bulon konumu + cap bilgisi (BoltGroup child'i).
struct BridgeBolt {
    BridgeVec3 position;             // global (Tekla is duzlemi)
    double nominalDiameter = 0.0;    // mm (M20 -> 20)
    double holeDiameter = 0.0;       // mm (22); 0 = standarddan turet
    std::string holeDiameterSource;  // "standard" | "measured" | "" (belirsiz)
    std::string axis;                // "x"|"y"|"z"|"" (bos = cikarilacak)
    std::string axisSource;          // "measured" | "inferred" | ""
};

struct BridgeBoltGroupInfo {
    std::string size;                // "M20"
    int standardCode = 0;            // 7990
    double tolerance = 0.0;          // mm (2)
    double slottedHoleX = 0.0;
    double slottedHoleY = 0.0;
    std::string partToBeBolted;      // kaynak GUID (ikincil uye)
    std::string partToBoltTo;        // kaynak GUID (levha)
    std::string axis;                // "x"|"y"|"z"|"" 
    std::string axisSource;          // "measured" | "inferred" | ""
    std::vector<BridgeBolt> bolts;
};

struct BridgeWeldInfo {
    std::string type;                // "fillet" | ...
    double size = 0.0;               // mm (5)
    std::string mainSourceGUID;      // ana uye (kolon)
    std::string secondarySourceGUID; // ikincil (levha)
    std::string position;            // "above_below" | ...
};

// Kati topolojisinin OLCULEN sayimlari (yuz/loop/kose). Kose KOORDINATLARI
// tasinmaz; yalnizca sayim — geometri bbox'tan yeniden uretilir.
struct BridgeTopology {
    int faceCount = 0;               // 6
    int loopCount = 0;               // 6
    int faceVertexOccurrences = 0;   // 24
    int holeLoops = 0;               // 0 (delik topolojisi yok -> "delinmis" IDDIA EDILMEZ)
};

struct BridgeObject {
    std::string sourceGUID;
    int identifierId = -1;    // -1 = yok
    std::string kind;         // Beam/ContourPlate/PolyBeam/BoltGroup/Weld/BooleanPart/Fitting/ConstructionPlane/Unknown
    std::string name;
    std::string role;
    std::string profile;
    std::string material;
    std::string parentSourceGUID;
    bool mapped = false;
    std::string mappedKind;
    std::string geometryFile;  // paket kokune goreli (guvenli) yol
    std::string unsupportedReason;
    std::optional<BridgeBBox> bbox;  // geometry dosyasindan yuklenir (varsa)
    std::optional<BridgeBoltGroupInfo> boltGroup;  // kind == BoltGroup
    std::optional<BridgeWeldInfo> weld;            // kind == Weld
    std::optional<BridgeTopology> topology;        // olculen yuz/loop sayimlari
};

struct BridgeCoordinateSystem {
    BridgeVec3 origin;
    BridgeVec3 xAxis;
    BridgeVec3 yAxis;
    BridgeVec3 zAxis;
};

struct BridgeComponent {
    std::string sourceType;
    std::string name;
    int number = 0;
    std::string guid;
    BridgeCoordinateSystem coordinateSystem;
    BridgeMemberRef primaryMember;
    std::vector<BridgeMemberRef> secondaryMembers;
};

struct BridgeManifest {
    std::string formatVersion;
    std::string sourceVersion;
    std::string sourceComponentType;
    int sourceComponentNumber = 0;
    std::string sourceComponentName;
    std::string sourceModelIdentifier;
    std::string exportTimestamp;
    std::string coordinateSystem;
    std::string lengthUnit;
    std::string componentIdentifier;
    int exportedObjectCount = 0;
    std::vector<std::string> knownLimitations;
};

struct BridgePackage {
    BridgeManifest manifest;
    BridgeComponent component;
    std::vector<BridgeParameter> parameters;
    std::vector<BridgeObject> objects;
    std::vector<std::string> knownLimitations;
    std::vector<std::string> inaccessibleObjects;
    std::vector<std::string> unsupportedFeatures;
    std::vector<std::string> warnings;
};

// Paket klasorunu okur + YAPISAL dogrular (zorunlu alanlar, enum degerleri,
// guvenli geometryFile yollari, dosya boyut sinirlari). Basarisizlikta false +
// errors dolu doner; out o zaman kullanilmamali.
bool readBridgePackage(const std::string& dir, BridgePackage& out,
                       std::vector<std::string>& errors);

// geometryFile icin guvenli goreli yol mu? (mutlak/.. /bos/surucu yasak).
bool isSafeBridgeRelativePath(const std::string& p) noexcept;

}  // namespace mm
