#pragma once
// MODEL MAKER BRIDGE — Tekla -> Model Maker paket okuyucu (veri modeli).
//
// Tekla extractor'unun urettigi sürümlü JSON paketini (manifest/component/
// parameters/objects/diagnostics + geometry/*) okur ve TIPLI bir C++ modeline
// cevirir. OCCT'ye bagli DEGILDIR (Linux OCC-off ctest'te kosar).
//
// V3 (formatVersion 3.0) ekleri:
//   * relationships.json  -> BridgeRelationship  (semantik iliskiler)
//   * operations.json     -> BridgeOperation     (cut/fitting/boolean)
//   * checksums.json      -> SHA-256 butunluk dogrulamasi (varsa)
//   * objects.roleKey / geometryRepresentation / confidence / localToGlobal
//   * objects.fitting / booleanPart (islem metadata)
//   * geometry dosyalarinda mesh (vertices/triangles) yuklenir
// V1/V2 paketleri bu yapilar BOS olarak okunur (geriye donuk uyum).
//
// Tekla GUID'leri yalniz KAYNAK referansidir; Model Maker EntityId'si importer
// tarafindan atanir. Okuma + dogrulama tek islemde yapilir; dogrulama gecmeyen
// paket "basarili" kabul EDILMEZ (ok=false -> out kullanilmamali).

#include <array>
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
    std::string roleKey;           // V3 semantik roleKey (bos olabilir)
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
    std::string confidence;   // documented / observed / inferred / unresolved / unsupported
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
    std::optional<BridgeVec3> direction;  // V3: gercek eksen vektoru (mm'de birim)
    std::string boltType;            // V3
    std::string boltStandard;        // V3
    std::vector<BridgeBolt> bolts;
};

struct BridgeWeldInfo {
    std::string type;                // "fillet" | ...
    double size = 0.0;               // mm (5)
    double sizeAbove = 0.0;          // V3
    double sizeBelow = 0.0;          // V3
    std::string mainSourceGUID;      // ana uye (kolon)
    std::string secondarySourceGUID; // ikincil (levha)
    std::string position;            // "above_below" | ...
};

// Kati topolojisinin OLCULEN sayimlari (yuz/loop/kose). Kose KOORDINATLARI
// tasinmaz; yalnizca sayim — geometri bbox/mesh'ten yeniden uretilir.
struct BridgeTopology {
    int faceCount = 0;               // 6
    int loopCount = 0;               // 6
    int faceVertexOccurrences = 0;   // 24
    int holeLoops = 0;               // 0
};

// --- V3: fitting / boolean / transform / mesh / iliskiler / islemler ---------

struct BridgePlane {
    BridgeVec3 origin;
    BridgeVec3 normal;
};

struct BridgeFittingInfo {
    std::optional<BridgePlane> plane;
    std::optional<BridgeVec3> normal;
    std::string targetPartGUID;
    std::string keepSide;            // "positive" | "negative" | ""
    bool inferred = false;
};

struct BridgeBooleanPartInfo {
    std::string operationType;       // "Cut" | "Union" | "Intersect"
    std::string targetPartGUID;
    std::string operandGUID;
    bool inferred = false;
};

// Nesne duzeyinde yerel->global donusum (origin + eksenler veya 4x4 matris).
struct BridgeTransform {
    BridgeVec3 origin;
    BridgeVec3 xAxis{1.0, 0.0, 0.0};
    BridgeVec3 yAxis{0.0, 1.0, 0.0};
    BridgeVec3 zAxis{0.0, 0.0, 1.0};
    bool hasMatrix = false;
    double matrix[16]{};             // 4x4 satir-oncelikli
    bool valid = false;
};

// Ucgen mesh (geometry dosyasi type=mesh/brep). Koseler mm cinsinden global
// (veya localToGlobal varsa yerel — importer donusturur).
struct BridgeMesh {
    std::vector<BridgeVec3> vertices;
    std::vector<std::array<int, 3>> triangles;
};

struct BridgeObject {
    std::string sourceGUID;
    int identifierId = -1;    // -1 = yok
    std::string kind;         // Beam/ContourPlate/PolyBeam/BoltGroup/Weld/BooleanPart/Fitting/ConstructionPlane/Unknown
    std::string name;
    std::string role;         // V2 rol
    std::string roleKey;      // V3 semantik roleKey (okuyucu: roleKey > role)
    std::string profile;
    std::string material;
    std::string parentSourceGUID;
    bool mapped = false;
    std::string mappedKind;
    std::string geometryFile;  // paket kokune goreli (guvenli) yol
    std::string geometryRepresentation;  // exact/reconstructed/approximate/reference/unsupported
    std::string confidence;    // documented/observed/inferred/unresolved/unsupported
    std::string unsupportedReason;
    std::optional<BridgeBBox> bbox;  // geometry dosyasindan yuklenir (varsa)
    std::string platePartMark;  // Stage 3H optional Tekla Part.GetPartMark
    std::optional<BridgeTransform> plateDstvFrame;  // Part.GetDSTVCoordinateSystem
    std::optional<BridgeBoltGroupInfo> boltGroup;  // kind == BoltGroup
    std::optional<BridgeWeldInfo> weld;            // kind == Weld
    std::optional<BridgeTopology> topology;        // olculen yuz/loop sayimlari
    std::optional<BridgeFittingInfo> fitting;      // kind == Fitting (V3)
    std::optional<BridgeBooleanPartInfo> booleanPart;  // kind == BooleanPart (V3)
    std::optional<BridgeTransform> localToGlobal;  // V3
    std::optional<BridgeMesh> mesh;                // geometry type=mesh (V3)
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
    std::string sourceModelName;     // V3
    std::string producerName;        // V3
    std::string producerVersion;     // V3
    std::string exportTimestamp;
    std::string coordinateSystem;
    std::string lengthUnit;
    std::string componentIdentifier;
    int exportedObjectCount = 0;
    std::vector<std::string> knownLimitations;
};

// V3: semantik iliski.
struct BridgeRelationship {
    std::string id;
    std::string kind;          // partOf/boltedTo/weldedTo/cutBy/fittedTo/attachedTo/memberOf/secondaryOf/operatedBy
    std::string sourceGUID;
    std::string targetGUID;
    std::string role;
    std::string confidence;
};

// V3: cut/fitting/boolean islemi.
struct BridgeOperation {
    std::string id;
    std::string type;          // Cut/Fitting/BooleanCut/BooleanUnion/BoltHole
    std::string targetPartGUID;
    std::string operatorGUID;
    std::string cuttingSolidGUID;
    std::optional<BridgePlane> fittingPlane;
    std::string keepSide;      // positive/negative/""
    bool inferred = false;
    std::string confidence;
    std::string finalSolidGeometryFile;
};

struct BridgePackage {
    BridgeManifest manifest;
    BridgeComponent component;
    std::vector<BridgeParameter> parameters;
    std::vector<BridgeObject> objects;
    std::vector<BridgeRelationship> relationships;   // V3 (V1/V2: bos)
    std::vector<BridgeOperation> operations;         // V3 (V1/V2: bos)
    std::vector<std::string> knownLimitations;
    std::vector<std::string> inaccessibleObjects;
    std::vector<std::string> unsupportedFeatures;
    std::vector<std::string> warnings;
    std::vector<std::string> errors;                 // V3 diagnostics.errors
    std::vector<std::string> unresolvedObjects;      // V3 diagnostics.unresolvedObjects
};

// Paket klasorunu okur + YAPISAL dogrular (zorunlu alanlar, enum degerleri,
// guvenli geometryFile yollari, dosya boyut sinirlari, V3 checksum butunlugu).
// Basarisizlikta false + errors dolu doner; out o zaman kullanilmamali.
bool readBridgePackage(const std::string& dir, BridgePackage& out,
                       std::vector<std::string>& errors);

// geometryFile icin guvenli goreli yol mu? (mutlak/.. /bos/surucu yasak).
bool isSafeBridgeRelativePath(const std::string& p) noexcept;

// V3 checksums.json (varsa) SHA-256 butunluk dogrulamasi. Dosya yoksa true
// (checksum opsiyonel); uyusmazlik varsa errors'a ekler ve false doner.
bool verifyBridgeChecksums(const std::string& dir, std::vector<std::string>& errors);

}  // namespace mm
