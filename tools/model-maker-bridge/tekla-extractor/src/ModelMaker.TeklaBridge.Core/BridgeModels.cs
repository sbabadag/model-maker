using System.Collections.Generic;
using Newtonsoft.Json;

namespace ModelMaker.TeklaBridge.Core
{
    // Bridge paketinin tipli veri modeli (Tekla'dan bagimsiz; Linux'ta test edilir).
    // JSON alan adlari kucuk harftir (model-maker-bridge.schema.json ile birebir).

    public class Vec3
    {
        [JsonProperty("x")] public double X { get; set; }
        [JsonProperty("y")] public double Y { get; set; }
        [JsonProperty("z")] public double Z { get; set; }
    }

    public class Manifest
    {
        [JsonProperty("formatVersion")] public string FormatVersion { get; set; } = "1.0";
        [JsonProperty("sourceApplication")] public string SourceApplication { get; set; } = "Tekla Structures";
        [JsonProperty("sourceVersion")] public string SourceVersion { get; set; }
        [JsonProperty("sourceComponentType")] public string SourceComponentType { get; set; }
        [JsonProperty("sourceComponentNumber")] public int SourceComponentNumber { get; set; }
        [JsonProperty("sourceComponentName")] public string SourceComponentName { get; set; }
        [JsonProperty("sourceModelIdentifier")] public string SourceModelIdentifier { get; set; }
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

    public class MemberRef
    {
        [JsonProperty("guid")] public string Guid { get; set; }
        [JsonProperty("identifierId")] public int? IdentifierId { get; set; }
        [JsonProperty("profile")] public string Profile { get; set; }
        [JsonProperty("material")] public string Material { get; set; }
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

    public class ObjectRecord
    {
        [JsonProperty("sourceGUID")] public string SourceGUID { get; set; }
        [JsonProperty("identifierId")] public int? IdentifierId { get; set; }
        [JsonProperty("kind")] public string Kind { get; set; }
        [JsonProperty("name")] public string Name { get; set; }
        [JsonProperty("role")] public string Role { get; set; }
        [JsonProperty("profile")] public string Profile { get; set; }
        [JsonProperty("material")] public string Material { get; set; }
        [JsonProperty("parentSourceGUID")] public string ParentSourceGUID { get; set; }
        [JsonProperty("mapped")] public bool Mapped { get; set; }
        [JsonProperty("mappedKind")] public string MappedKind { get; set; }
        [JsonProperty("geometryFile")] public string GeometryFile { get; set; }
        [JsonProperty("parametric")] public Dictionary<string, object> Parametric { get; set; }
        [JsonProperty("unsupportedReason")] public string UnsupportedReason { get; set; }
    }

    public class Diagnostics
    {
        [JsonProperty("knownLimitations")] public List<string> KnownLimitations { get; set; } = new List<string>();
        [JsonProperty("inaccessibleObjects")] public List<string> InaccessibleObjects { get; set; } = new List<string>();
        [JsonProperty("unsupportedFeatures")] public List<string> UnsupportedFeatures { get; set; } = new List<string>();
        [JsonProperty("warnings")] public List<string> Warnings { get; set; } = new List<string>();
    }

    // geometry/<dosya>.json icerigi. Type: bbox | mesh | unsupported.
    public class GeometryFile
    {
        [JsonProperty("formatVersion")] public string FormatVersion { get; set; } = "1.0";
        [JsonProperty("type")] public string Type { get; set; }
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
        [JsonProperty("diagnostics")] public Diagnostics Diagnostics { get; set; }
    }
}
