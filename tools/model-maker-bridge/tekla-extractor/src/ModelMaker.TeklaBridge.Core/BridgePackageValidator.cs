using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;

namespace ModelMaker.TeklaBridge.Core
{
    // Bridge paketinin YAPISAL dogrulamasi (model-maker-bridge.schema.json ile esdeger).
    // Tam JSON Schema motoru DEGILDIR; zorunlu alanlar, tipler, enum degerleri,
    // guvenli path ve boyut sinirlarini denetler. Hatali paketi Model Maker'a kismen
    // aktarip "basarili" saymamak icin importer bu kurallari ayrica uygular.
    public static class BridgePackageValidator
    {
        public const long MaxFileBytes = 32L * 1024 * 1024;
        public const long MaxTotalBytes = 256L * 1024 * 1024;

        private static readonly HashSet<string> ComponentTypes =
            new HashSet<string> { "Connection", "Component", "Detail", "CustomPart", "Seam", "Unknown" };
        private static readonly HashSet<string> ObjectKinds =
            new HashSet<string> { "Beam", "ContourPlate", "PolyBeam", "BoltGroup", "Weld", "BooleanPart", "Fitting", "ConstructionPlane", "Unknown" };
        private static readonly HashSet<string> MappedKinds =
            new HashSet<string> { "Plate", "BoltGroup", "Cut", "Weld", "Beam", "ReferenceGeometry", "Unsupported" };
        private static readonly HashSet<string> ParamTypes =
            new HashSet<string> { "Length", "Angle", "Integer", "Boolean", "Enum", "String", "Profile", "Material", "EntityRef", "Double", "Unknown" };
        private static readonly HashSet<string> Readable = new HashSet<string> { "ok", "unsupported", "error" };
        private static readonly HashSet<string> Confidence = new HashSet<string> { "documented", "observed", "inferred", "unresolved" };
        private static readonly HashSet<string> GeomTypes = new HashSet<string> { "bbox", "mesh", "unsupported" };

        public static List<string> Validate(BridgePackage pkg)
        {
            var errors = new List<string>();
            if (pkg == null) { errors.Add("paket null"); return errors; }

            ValidateManifest(pkg.Manifest, errors);
            ValidateComponent(pkg.Component, errors);
            ValidateParameters(pkg.Parameters, errors);
            ValidateObjects(pkg.Objects, errors);
            ValidateDiagnostics(pkg.Diagnostics, errors);
            return errors;
        }

        public static bool IsSafeRelativePath(string p)
        {
            if (string.IsNullOrWhiteSpace(p)) return false;
            if (p.StartsWith("/") || p.StartsWith("\\")) return false;
            if (p.Length >= 2 && char.IsLetter(p[0]) && p[1] == ':') return false; // windows surucu
            var parts = p.Replace('\\', '/').Split('/');
            return parts.All(part => part.Length > 0 && part != "." && part != "..");
        }

        private static void ValidateManifest(Manifest m, List<string> errors)
        {
            if (m == null) { errors.Add("manifest eksik"); return; }
            if (m.FormatVersion != "1.0") errors.Add("manifest.formatVersion 1.0 olmali");
            if (m.SourceApplication != "Tekla Structures") errors.Add("manifest.sourceApplication 'Tekla Structures' olmali");
            if (!ComponentTypes.Contains(m.SourceComponentType)) errors.Add("manifest.sourceComponentType gecersiz: " + m.SourceComponentType);
            if (string.IsNullOrEmpty(m.ComponentIdentifier)) errors.Add("manifest.componentIdentifier bos olamaz");
            if (string.IsNullOrEmpty(m.ExportTimestamp)) errors.Add("manifest.exportTimestamp bos olamaz");
            if (m.CoordinateSystem != "TeklaGlobal") errors.Add("manifest.coordinateSystem 'TeklaGlobal' olmali");
            if (m.LengthUnit != "mm") errors.Add("manifest.lengthUnit 'mm' olmali");
        }

        private static void ValidateComponent(BridgeComponentRecord c, List<string> errors)
        {
            if (c == null) { errors.Add("component eksik"); return; }
            if (string.IsNullOrEmpty(c.Guid)) errors.Add("component.guid bos olamaz");
            if (c.PrimaryMember == null || string.IsNullOrEmpty(c.PrimaryMember.Guid)) errors.Add("component.primaryMember.guid eksik");
            if (c.CoordinateSystem == null || !IsVec3(c.CoordinateSystem.Origin) ||
                !IsVec3(c.CoordinateSystem.XAxis) || !IsVec3(c.CoordinateSystem.YAxis) || !IsVec3(c.CoordinateSystem.ZAxis))
                errors.Add("component.coordinateSystem eksik/gecersiz");
            if (c.SecondaryMembers == null) errors.Add("component.secondaryMembers liste olmali");
        }

        private static void ValidateParameters(List<ParameterRecord> ps, List<string> errors)
        {
            if (ps == null) { errors.Add("parameters eksik"); return; }
            var seen = new HashSet<string>();
            for (var i = 0; i < ps.Count; i++)
            {
                var p = ps[i];
                if (p == null) { errors.Add("parameters[" + i + "] null"); continue; }
                if (string.IsNullOrEmpty(p.Name)) errors.Add("parameters[" + i + "].name bos");
                if (!ParamTypes.Contains(p.Type)) errors.Add("parameters[" + i + "].type gecersiz: " + p.Type);
                if (!Readable.Contains(p.Readable)) errors.Add("parameters[" + i + "].readable gecersiz: " + p.Readable);
                if (!Confidence.Contains(p.Confidence)) errors.Add("parameters[" + i + "].confidence gecersiz: " + p.Confidence);
                if (!seen.Add(p.Name)) errors.Add("parameters[" + i + "].name tekrarli: " + p.Name);
            }
        }

        private static void ValidateObjects(List<ObjectRecord> os, List<string> errors)
        {
            if (os == null) { errors.Add("objects eksik"); return; }
            var guids = new HashSet<string>();
            for (var i = 0; i < os.Count; i++)
            {
                var o = os[i];
                if (o == null) { errors.Add("objects[" + i + "] null"); continue; }
                if (string.IsNullOrEmpty(o.SourceGUID)) errors.Add("objects[" + i + "].sourceGUID bos");
                if (!ObjectKinds.Contains(o.Kind)) errors.Add("objects[" + i + "].kind gecersiz: " + o.Kind);
                if (o.MappedKind != null && !MappedKinds.Contains(o.MappedKind)) errors.Add("objects[" + i + "].mappedKind gecersiz: " + o.MappedKind);
                if (o.GeometryFile != null && !IsSafeRelativePath(o.GeometryFile)) errors.Add("objects[" + i + "].geometryFile guvenli degil: " + o.GeometryFile);
                if (!guids.Add(o.SourceGUID)) errors.Add("objects[" + i + "].sourceGUID tekrarli: " + o.SourceGUID);
            }
        }

        private static void ValidateDiagnostics(Diagnostics d, List<string> errors)
        {
            if (d == null) { errors.Add("diagnostics eksik"); return; }
            if (d.KnownLimitations == null) errors.Add("diagnostics.knownLimitations eksik");
            if (d.InaccessibleObjects == null) errors.Add("diagnostics.inaccessibleObjects eksik");
            if (d.UnsupportedFeatures == null) errors.Add("diagnostics.unsupportedFeatures eksik");
            if (d.Warnings == null) errors.Add("diagnostics.warnings eksik");
        }

        private static bool IsVec3(Vec3 v)
        {
            return v != null;
        }

        // Paket KLASORUNU diskten dogrular (boyut sinirlari + geometry dosyalari dahil).
        public static List<string> ValidateDirectory(string dir)
        {
            var errors = new List<string>();
            if (!Directory.Exists(dir)) { errors.Add("paket klasoru yok: " + dir); return errors; }

            long total = 0;
            foreach (var f in Directory.EnumerateFiles(dir, "*", SearchOption.AllDirectories))
            {
                var len = new FileInfo(f).Length;
                total += len;
                if (len > MaxFileBytes) errors.Add("dosya cok buyuk: " + f);
            }
            if (total > MaxTotalBytes) errors.Add("paket toplam boyutu cok buyuk: " + total);

            foreach (var name in new[] { "manifest.json", "component.json", "parameters.json", "objects.json", "diagnostics.json" })
            {
                if (!File.Exists(Path.Combine(dir, name))) errors.Add(name + " yok");
            }
            return errors;
        }
    }
}
