using System.Collections.Generic;
using Newtonsoft.Json;

namespace ModelMaker.TeklaBridge.Core
{
    // Bridge paketinin tipli veri modeli (Tekla'dan bagimsiz; Linux'ta test edilir).
    // JSON alan adlari kucuk harftir (model-maker-bridge.schema.json ile birebir).
    // V3 ekleri: producer/sourceModelName (manifest), roleKey/geometryRepresentation/
    // confidence/localToGlobal/fitting/booleanPart/topology/boltGroup/weld (objects),
    // relationships/operations/checksums (paket), errors/unresolvedObjects (diagnostics),
    // representation (geometry dosyasi).

    public class Vec3
    {
        [JsonProperty("x")] public double X { get; set; }
        [JsonProperty("y")] public double Y { get; set; }
        [JsonProperty("z")] public double Z { get; set; }
    }

    public class Producer
    {
        [JsonProperty("name")] public string Name { get; set; }
        [JsonProperty("version")] public string Version { get; set; }
    }

    public class Manifest
    {
        [JsonProperty("formatVersion")] public string FormatVersion { get; set; } = "3.0";
        [JsonProperty("sourceApplication")] public string SourceApplication { get; set; } = "Tekla Structures";
        [JsonProperty("sourceVersion")] public string SourceVersion { get; set; }
        [JsonProperty("producer")] public Producer Producer { get; set; }
        [JsonProperty("sourceComponentType")] public string SourceComponentType { get; set; }
        [JsonProperty("sourceComponentNumber")] public int SourceComponentNumber { get; set; }
        [JsonProperty("sourceComponentName")] public string SourceComponentName { get; set; }
        [JsonProperty("sourceModelIdentifier")] public string SourceModelIdentifier { get; set; }
        [JsonProperty("sourceModelName")] public string SourceModelName { get; set; }
        [JsonProperty("exportTimestamp")] public string ExportTimestamp { get; set; }
        [JsonProperty("coordinateSystem")] public string CoordinateSystem { get; set; } = "TeklaGlobal";
        [JsonProperty("lengthUnit")] public string LengthUnit { get; set; } = "mm";
        [JsonProperty("componentIdentifier")] public string ComponentIdentifier { get; set; }
        [JsonProperty("exportedObjectCount")] public int ExportedObjectCount { get; set; }
        [JsonProperty("knownLimitations")] public List<string> KnownLimitations { get; set; } = new List<string>();
        [JsonProperty("exporterVersion")] public string ExporterVersion { get; set; }
    }

    public class CoordinateSystem
    {
        [JsonProperty("origin")] public Vec3 Origin { get; set; }
        [JsonProperty("xAxis")] public Vec3 XAxis { get; set; }
        [JsonProperty("yAxis")] public Vec3 YAxis { get; set; }
        [JsonProperty("zAxis")] public Vec3 ZAxis { get; set; }
    }

    public class Transform
    {
        [JsonProperty("origin")] public Vec3 Origin { get; set; }
        [JsonProperty("xAxis")] public Vec3 XAxis { get; set; }
        [JsonProperty("yAxis")] public Vec3 YAxis { get; set; }
        [JsonProperty("zAxis")] public Vec3 ZAxis { get; set; }
        [JsonProperty("matrix")] public List<List<double>> Matrix { get; set; }
    }

    public class MemberRef
    {
        [JsonProperty("guid")] public string Guid { get; set; }
        [JsonProperty("identifierId")] public int? IdentifierId { get; set; }
        [JsonProperty("profile")] public string Profile { get; set; }
        [JsonProperty("material")] public string Material { get; set; }
        [JsonProperty("role")] public string Role { get; set; }
        [JsonProperty("roleKey")] public string RoleKey { get; set; }
        [JsonProperty("startPoint")] public Vec3 StartPoint { get; set; }
        [JsonProperty("endPoint")] public Vec3 EndPoint { get; set; }
        [JsonProperty("class")] public string Class { get; set; }
    }

    public class BridgeComponentRecord
    {
        [JsonProperty("sourceType")] public string SourceType { get; set; }
        [JsonProperty("name")] public string Name { get; set; }
        [JsonProperty("number")] public int Number { get; set; }
        [JsonProperty("guid")] public string Guid { get; set; }
        [JsonProperty("identifierId")] public int? IdentifierId { get; set; }
        [JsonProperty("positionType")] public string PositionType { get; set; }
        [JsonProperty("upVector")] public Vec3 UpVector { get; set; }
        [JsonProperty("selectionOrder")] public List<string> SelectionOrder { get; set; } = new List<string>();
        [JsonProperty("coordinateSystem")] public CoordinateSystem CoordinateSystem { get; set; }
        [JsonProperty("primaryMember")] public MemberRef PrimaryMember { get; set; }
        [JsonProperty("secondaryMembers")] public List<MemberRef> SecondaryMembers { get; set; } = new List<MemberRef>();
        [JsonProperty("inputReferences")] public List<MemberRef> InputReferences { get; set; } = new List<MemberRef>();
    }

    public class ParameterRecord
    {
        [JsonProperty("name")] public string Name { get; set; }
        [JsonProperty("type")] public string Type { get; set; }
        [JsonProperty("value")] public object Value { get; set; }
        [JsonProperty("units")] public string Units { get; set; }
        [JsonProperty("source")] public string Source { get; set; }
        [JsonProperty("readable")] public string Readable { get; set; }
        [JsonProperty("confidence")] public string Confidence { get; set; }
    }

    public class PlaneRecord
    {
        [JsonProperty("origin")] public Vec3 Origin { get; set; }
        [JsonProperty("normal")] public Vec3 Normal { get; set; }
    }

    public class FittingInfo
    {
        [JsonProperty("plane")] public PlaneRecord Plane { get; set; }
        [JsonProperty("normal")] public Vec3 Normal { get; set; }
        [JsonProperty("targetPartGUID")] public string TargetPartGUID { get; set; }
        [JsonProperty("keepSide")] public string KeepSide { get; set; }
        [JsonProperty("inferred")] public bool Inferred { get; set; }
    }

    public class BooleanPartInfo
    {
        [JsonProperty("operationType")] public string OperationType { get; set; }
        [JsonProperty("targetPartGUID")] public string TargetPartGUID { get; set; }
        [JsonProperty("operandGUID")] public string OperandGUID { get; set; }
        [JsonProperty("inferred")] public bool Inferred { get; set; }
    }

    public class BoltRecord
    {
        [JsonProperty("position")] public Vec3 Position { get; set; }
        [JsonProperty("nominalDiameter")] public double NominalDiameter { get; set; }
        [JsonProperty("holeDiameter")] public double HoleDiameter { get; set; }
        [JsonProperty("holeDiameterSource")] public string HoleDiameterSource { get; set; }
        [JsonProperty("axis")] public string Axis { get; set; }
        [JsonProperty("axisSource")] public string AxisSource { get; set; }
    }

    // Stage 3D: read-only geometry diagnostics from Tekla, NOT drilling authorization.
    // The BoltPositions coordinate plane may not match the global model frame.
    public class BoltFrameProbe
    {
        [JsonProperty("coordinateSystem")] public CoordinateSystem CoordinateSystem { get; set; }
        [JsonProperty("normalCandidate")] public Vec3 NormalCandidate { get; set; }
        [JsonProperty("source")] public string Source { get; set; } = "BoltGroup.GetCoordinateSystem";
        [JsonProperty("positionReference")] public string PositionReference { get; set; } =
            "BoltPositions in group XY plane relative to selection transformation plane";
        [JsonProperty("verifiedForDrilling")] public bool VerifiedForDrilling { get; set; } = false;
    }

    // Tekla 2025 BoltHolesAttributes describes special holes by connected ply.
    // This is metadata ONLY; it is not proof that the target part contains cuts.
    public class BoltPlyHoleProbe
    {
        [JsonProperty("plyIndex")] public int PlyIndex { get; set; }
        [JsonProperty("specialHoleType")] public string SpecialHoleType { get; set; }
        [JsonProperty("longHoleX")] public double LongHoleX { get; set; }
        [JsonProperty("longHoleY")] public double LongHoleY { get; set; }
        [JsonProperty("slotOffsetX")] public double SlotOffsetX { get; set; }
        [JsonProperty("slotOffsetY")] public double SlotOffsetY { get; set; }
    }

    public class BoltHoleSettingsProbe
    {
        // BoltGroup.Bolt true=bolt with hole, false=hole only (not "no hole").
        [JsonProperty("boltPresent")] public bool? BoltPresent { get; set; }
        [JsonProperty("perPlySpecialHoles")] public List<BoltPlyHoleProbe> PerPlySpecialHoles { get; set; }
        [JsonProperty("plainHoleType")] public string PlainHoleType { get; set; }
        [JsonProperty("specialHoleType")] public string SpecialHoleType { get; set; }
        [JsonProperty("slottedHoleX")] public double SlottedHoleX { get; set; }
        [JsonProperty("slottedHoleY")] public double SlottedHoleY { get; set; }
        [JsonProperty("hole1")] public bool Hole1 { get; set; }
        [JsonProperty("hole2")] public bool Hole2 { get; set; }
        [JsonProperty("hole3")] public bool Hole3 { get; set; }
        [JsonProperty("hole4")] public bool Hole4 { get; set; }
        [JsonProperty("hole5")] public bool Hole5 { get; set; }
        [JsonProperty("verifiedForDrilling")] public bool VerifiedForDrilling { get; set; } = false;
    }

    public class BoltGroupInfo
    {
        [JsonProperty("frameProbe", NullValueHandling = NullValueHandling.Ignore)]
        public BoltFrameProbe FrameProbe { get; set; }
        [JsonProperty("holeSettingsProbe", NullValueHandling = NullValueHandling.Ignore)]
        public BoltHoleSettingsProbe HoleSettingsProbe { get; set; }
        [JsonProperty("size")] public string Size { get; set; }
        [JsonProperty("standardCode")] public int StandardCode { get; set; }
        [JsonProperty("tolerance")] public double Tolerance { get; set; }
        [JsonProperty("slottedHoleX")] public double SlottedHoleX { get; set; }
        [JsonProperty("slottedHoleY")] public double SlottedHoleY { get; set; }
        [JsonProperty("partToBeBolted")] public string PartToBeBolted { get; set; }
        [JsonProperty("partToBoltTo")] public string PartToBoltTo { get; set; }
        [JsonProperty("axis")] public string Axis { get; set; }
        [JsonProperty("axisSource")] public string AxisSource { get; set; }
        [JsonProperty("direction")] public Vec3 Direction { get; set; }
        [JsonProperty("boltType")] public string BoltType { get; set; }
        [JsonProperty("boltStandard")] public string BoltStandard { get; set; }
        [JsonProperty("bolts")] public List<BoltRecord> Bolts { get; set; } = new List<BoltRecord>();
    }

    public class WeldInfo
    {
        [JsonProperty("type")] public string Type { get; set; }
        [JsonProperty("size")] public double Size { get; set; }
        [JsonProperty("sizeAbove")] public double SizeAbove { get; set; }
        [JsonProperty("sizeBelow")] public double SizeBelow { get; set; }
        [JsonProperty("mainSourceGUID")] public string MainSourceGUID { get; set; }
        [JsonProperty("secondarySourceGUID")] public string SecondarySourceGUID { get; set; }
        [JsonProperty("position")] public string Position { get; set; }
    }

    public class TopologyInfo
    {
        [JsonProperty("faceCount")] public int FaceCount { get; set; }
        [JsonProperty("loopCount")] public int LoopCount { get; set; }
        [JsonProperty("faceVertexOccurrences")] public int FaceVertexOccurrences { get; set; }
        [JsonProperty("holeLoops")] public int HoleLoops { get; set; }
    }

    public class ObjectRecord
    {
        [JsonProperty("sourceGUID")] public string SourceGUID { get; set; }
        [JsonProperty("identifierId")] public int? IdentifierId { get; set; }
        [JsonProperty("kind")] public string Kind { get; set; }
        [JsonProperty("name")] public string Name { get; set; }
        [JsonProperty("role")] public string Role { get; set; }
        [JsonProperty("roleKey")] public string RoleKey { get; set; }
        [JsonProperty("profile")] public string Profile { get; set; }
        [JsonProperty("material")] public string Material { get; set; }
        [JsonProperty("parentSourceGUID")] public string ParentSourceGUID { get; set; }
        [JsonProperty("mapped")] public bool Mapped { get; set; }
        [JsonProperty("mappedKind")] public string MappedKind { get; set; }
        [JsonProperty("geometryFile")] public string GeometryFile { get; set; }
        [JsonProperty("geometryRepresentation")] public string GeometryRepresentation { get; set; }
        [JsonProperty("confidence")] public string Confidence { get; set; }
        [JsonProperty("localToGlobal")] public Transform LocalToGlobal { get; set; }
        [JsonProperty("parametric")] public Dictionary<string, object> Parametric { get; set; }
        [JsonProperty("unsupportedReason")] public string UnsupportedReason { get; set; }
        [JsonProperty("boltGroup")] public BoltGroupInfo BoltGroup { get; set; }
        [JsonProperty("weld")] public WeldInfo Weld { get; set; }
        [JsonProperty("fitting")] public FittingInfo Fitting { get; set; }
        [JsonProperty("booleanPart")] public BooleanPartInfo BooleanPart { get; set; }
        [JsonProperty("topology")] public TopologyInfo Topology { get; set; }
    }

    public class RelationshipRecord
    {
        [JsonProperty("id")] public string Id { get; set; }
        [JsonProperty("kind")] public string Kind { get; set; }
        [JsonProperty("sourceGUID")] public string SourceGUID { get; set; }
        [JsonProperty("targetGUID")] public string TargetGUID { get; set; }
        [JsonProperty("role")] public string Role { get; set; }
        [JsonProperty("confidence")] public string Confidence { get; set; }
    }

    public class OperationRecord
    {
        [JsonProperty("id")] public string Id { get; set; }
        [JsonProperty("type")] public string Type { get; set; }
        [JsonProperty("targetPartGUID")] public string TargetPartGUID { get; set; }
        [JsonProperty("operatorGUID")] public string OperatorGUID { get; set; }
        [JsonProperty("cuttingSolidGUID")] public string CuttingSolidGUID { get; set; }
        [JsonProperty("fittingPlane")] public PlaneRecord FittingPlane { get; set; }
        [JsonProperty("keepSide")] public string KeepSide { get; set; }
        [JsonProperty("inferred")] public bool Inferred { get; set; }
        [JsonProperty("confidence")] public string Confidence { get; set; }
        [JsonProperty("finalSolidGeometryFile")] public string FinalSolidGeometryFile { get; set; }
    }

    public class Diagnostics
    {
        [JsonProperty("knownLimitations")] public List<string> KnownLimitations { get; set; } = new List<string>();
        [JsonProperty("inaccessibleObjects")] public List<string> InaccessibleObjects { get; set; } = new List<string>();
        [JsonProperty("unsupportedFeatures")] public List<string> UnsupportedFeatures { get; set; } = new List<string>();
        [JsonProperty("warnings")] public List<string> Warnings { get; set; } = new List<string>();
        [JsonProperty("errors")] public List<string> Errors { get; set; } = new List<string>();
        [JsonProperty("unresolvedObjects")] public List<string> UnresolvedObjects { get; set; } = new List<string>();
    }

    // geometry/<dosya>.json icerigi. Type: bbox | mesh | brep | unsupported.
    public class GeometryFile
    {
        [JsonProperty("formatVersion")] public string FormatVersion { get; set; } = "3.0";
        [JsonProperty("type")] public string Type { get; set; }
        [JsonProperty("representation")] public string Representation { get; set; }
        [JsonProperty("bbox")] public BBox Bbox { get; set; }
        [JsonProperty("mesh")] public Mesh Mesh { get; set; }
        [JsonProperty("note")] public string Note { get; set; }
    }

    public class BBox
    {
        [JsonProperty("min")] public Vec3 Min { get; set; }
        [JsonProperty("max")] public Vec3 Max { get; set; }
    }

    public class Mesh
    {
        [JsonProperty("vertices")] public List<List<double>> Vertices { get; set; } = new List<List<double>>();
        [JsonProperty("triangles")] public List<List<int>> Triangles { get; set; } = new List<List<int>>();
    }

    public class BridgePackage
    {
        [JsonProperty("manifest")] public Manifest Manifest { get; set; }
        [JsonProperty("component")] public BridgeComponentRecord Component { get; set; }
        [JsonProperty("parameters")] public List<ParameterRecord> Parameters { get; set; } = new List<ParameterRecord>();
        [JsonProperty("objects")] public List<ObjectRecord> Objects { get; set; } = new List<ObjectRecord>();
        [JsonProperty("relationships")] public List<RelationshipRecord> Relationships { get; set; } = new List<RelationshipRecord>();
        [JsonProperty("operations")] public List<OperationRecord> Operations { get; set; } = new List<OperationRecord>();
        [JsonProperty("diagnostics")] public Diagnostics Diagnostics { get; set; }
        // checksums: goreli yol -> SHA-256 hex. Yazici/okuyucu bunu ayri checksums.json yazar.
        [JsonProperty("checksums")] public Dictionary<string, string> Checksums { get; set; } = new Dictionary<string, string>();
    }
}
