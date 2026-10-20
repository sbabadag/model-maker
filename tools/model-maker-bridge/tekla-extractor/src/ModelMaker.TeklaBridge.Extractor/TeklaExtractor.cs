using System;
using System.Collections;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using ModelMaker.TeklaBridge.Core;
using Tekla.Structures.Geometry3d;
using Tekla.Structures.Model;
using Tekla.Structures.Solid;
using TSMUI = Tekla.Structures.Model.UI;

namespace ModelMaker.TeklaBridge.Extractor
{
    // Tekla Structures modelinden secili bir bileseni (Connection/Component/Detail/
    // CustomPart/Seam) okuyup Model Maker Bridge paketine donusturur.
    //
    // DURUSTLUK KURALLARI (spec):
    //  - GetConnectionStatus() false ise SAHTE veri uretilmez, acik hata doner.
    //  - API'nin desteklemedigi alan null/unsupported olarak kaydedilir, uydurulmaz.
    //  - Parametreler YALNIZCA bilinen ham Tekla attribute adlariyla GetAttribute
    //    ile okunur; bilinmeyen parametrelerin otomatik kesfi VARSAYILMAZ.
    //  - Tekla GUID'leri yalniz kaynak referansidir; Model Maker EntityId'si importer
    //    tarafindan atanir.

    public sealed class ExtractorOptions
    {
        public string OutputDir = "";
        public List<string> AttributeNames = new List<string>();
    }

    public sealed class ExtractResult
    {
        public bool Ok;
        public string Error;
        public string OutputDir;
    }

    public static class TeklaExtractor
    {
        public static ExtractResult Extract(ExtractorOptions opts)
        {
            var model = new Model();
            if (!model.GetConnectionStatus())
            {
                return new ExtractResult
                {
                    Ok = false,
                    Error = "Tekla Structures çalışmıyor veya açık bir model yok (GetConnectionStatus() == false)."
                };
            }

            string modelPath = "";
            try { var info = model.GetInfo(); if (info != null) modelPath = info.ModelPath ?? ""; }
            catch { }

            // Secili nesneleri topla.
            var selected = new List<ModelObject>();
            try
            {
                ModelObjectEnumerator.AutoFetch = true;
                var selector = new TSMUI.ModelObjectSelector();
                var e = selector.GetSelectedObjects();
                if (e != null)
                {
                    while (e.MoveNext())
                    {
                        var mo = e.Current;
                        if (mo != null) selected.Add(mo);
                    }
                }
            }
            catch { }

            // Ilk bileseni bul.
            BaseComponent component = null;
            foreach (var mo in selected)
            {
                if (mo is BaseComponent bc) { component = bc; break; }
            }

            if (component == null)
            {
                return new ExtractResult
                {
                    Ok = false,
                    Error = "Seçili nesne bir component değil. Tekla'da bir bağlantı/component seçin " +
                            "(Connection / Component / Detail / CustomPart / Seam)."
                };
            }

            var pkg = new BridgePackage();
            var diag = new Diagnostics();

            pkg.Manifest = BuildManifest(component, modelPath);
            pkg.Component = BuildComponent(component, diag);
            pkg.Parameters = BuildParameters(component, opts.AttributeNames, diag);

            var objects = BuildObjects(component, diag);
            pkg.Objects = objects.Objects;
            pkg.Diagnostics = diag;
            pkg.Manifest.ExportedObjectCount = pkg.Objects.Count;

            var outputDir = string.IsNullOrWhiteSpace(opts.OutputDir)
                ? Path.Combine(Directory.GetCurrentDirectory(), "model-maker-bridge-export")
                : opts.OutputDir;

            BridgePackageWriter.Write(pkg, objects.GeometryFiles, outputDir);

            return new ExtractResult { Ok = true, OutputDir = outputDir };
        }

        // ---------------------------------------------------------------------

        private static Manifest BuildManifest(BaseComponent component, string modelPath)
        {
            string guid = Safe(() => component.Identifier.GUID.ToString(), "");
            string type = ComponentTypeOf(component);
            int number = Safe(() => component.Number, 0);

            return new Manifest
            {
                FormatVersion = "1.0",
                SourceApplication = "Tekla Structures",
                SourceVersion = "", // dogrulanmis okuma API'si bu dilimde yok
                SourceComponentType = type,
                SourceComponentNumber = number,
                SourceComponentName = Safe(() => component.Name, ""),
                SourceModelIdentifier = modelPath,
                ExportTimestamp = DateTime.UtcNow.ToString("o", CultureInfo.InvariantCulture),
                CoordinateSystem = "TeklaGlobal",
                LengthUnit = "mm",
                ComponentIdentifier = guid,
                ExporterVersion = "1.0.0",
                KnownLimitations = new List<string>
                {
                    "sourceVersion okunmadi (dogrulanmis Tekla surum API'si bu dilimde kullanilmadi).",
                    "Parametreler yalnizca --attributes ile verilen ham Tekla attribute adlariyla okunur."
                }
            };
        }

        private static BridgeComponentRecord BuildComponent(BaseComponent component, Diagnostics diag)
        {
            var rec = new BridgeComponentRecord
            {
                SourceType = ComponentTypeOf(component),
                Name = Safe(() => component.Name, ""),
                Number = Safe(() => component.Number, 0),
                Guid = Safe(() => component.Identifier.GUID.ToString(), ""),
                IdentifierId = Safe(() => (int?)component.Identifier.ID, (int?)null)
            };

            // Lokal koordinat sistemi.
            try
            {
                var cs = component.GetCoordinateSystem();
                if (cs != null)
                {
                    rec.CoordinateSystem = new ModelMaker.TeklaBridge.Core.CoordinateSystem
                    {
                        Origin = ToVec3(cs.Origin),
                        XAxis = ToVec3(cs.AxisX),
                        YAxis = ToVec3(cs.AxisY),
                        ZAxis = Cross(cs.AxisX, cs.AxisY)
                    };
                }
            }
            catch (Exception ex)
            {
                diag.InaccessibleObjects.Add("koordinat sistemi: " + ex.Message);
            }

            // Birincil / ikincil uyeler.
            if (component is Connection conn)
            {
                try
                {
                    var p = conn.GetPrimaryObject();
                    if (p != null) rec.PrimaryMember = ToMemberRef(p);
                }
                catch (Exception ex) { diag.InaccessibleObjects.Add("primary: " + ex.Message); }

                try
                {
                    var sec = conn.GetSecondaryObjects();
                    if (sec != null)
                    {
                        foreach (var o in sec)
                        {
                            var mo = o as ModelObject;
                            if (mo != null) rec.SecondaryMembers.Add(ToMemberRef(mo));
                        }
                    }
                }
                catch (Exception ex) { diag.InaccessibleObjects.Add("secondary: " + ex.Message); }
            }
            else if (component is Component comp)
            {
                // ComponentInput IEnumerable'dir; sira = girilen sira. Ilk ModelObject
                // birincil, sonrakiler ikincil kabul edilir (giris sirasi temelli).
                try
                {
                    var ci = comp.GetComponentInput();
                    int idx = 0;
                    foreach (var o in ci)
                    {
                        var mo = o as ModelObject;
                        if (mo == null) continue; // Point gibi girdiler bu dilimde kaydedilmez
                        if (idx == 0) rec.PrimaryMember = ToMemberRef(mo);
                        else rec.SecondaryMembers.Add(ToMemberRef(mo));
                        idx++;
                    }
                }
                catch (Exception ex) { diag.InaccessibleObjects.Add("component input: " + ex.Message); }
            }
            else
            {
                diag.InaccessibleObjects.Add(
                    "primary/secondary okuma bu component tipi icin bu dilimde uygulanmadi (" +
                    rec.SourceType + ").");
            }

            if (rec.PrimaryMember == null)
            {
                rec.PrimaryMember = new MemberRef { Guid = "" };
                diag.Warnings.Add("Birincil uye okunamadi; primaryMember.guid bos birakildi.");
            }

            return rec;
        }

        private static List<ParameterRecord> BuildParameters(BaseComponent component,
                                                            List<string> attributeNames,
                                                            Diagnostics diag)
        {
            var result = new List<ParameterRecord>();
            if (attributeNames == null || attributeNames.Count == 0)
            {
                diag.Warnings.Add(
                    "Parametre listesi bos: --attributes ile ham Tekla attribute adlari verilmedikce " +
                    "parameters.json bos kalir. (Bilinmeyen parametrelerin otomatik kesfi varsayilmaz.)");
                return result;
            }

            foreach (var name in attributeNames)
            {
                if (string.IsNullOrWhiteSpace(name)) continue;

                double d = 0.0;
                if (Safe(() => component.GetAttribute(name, ref d), false))
                {
                    result.Add(new ParameterRecord
                    {
                        Name = name, Type = "Double", Value = d,
                        Source = "GetAttribute", Readable = "ok", Confidence = "observed"
                    });
                    continue;
                }

                int i = 0;
                if (Safe(() => component.GetAttribute(name, ref i), false))
                {
                    result.Add(new ParameterRecord
                    {
                        Name = name, Type = "Integer", Value = i,
                        Source = "GetAttribute", Readable = "ok", Confidence = "observed"
                    });
                    continue;
                }

                string s = "";
                if (Safe(() => component.GetAttribute(name, ref s), false))
                {
                    result.Add(new ParameterRecord
                    {
                        Name = name, Type = "String", Value = s,
                        Source = "GetAttribute", Readable = "ok", Confidence = "observed"
                    });
                    continue;
                }

                result.Add(new ParameterRecord
                {
                    Name = name, Type = "Unknown", Value = null,
                    Source = "GetAttribute", Readable = "error",
                    Confidence = "unresolved"
                });
            }
            return result;
        }

        private sealed class ObjectsResult
        {
            public List<ObjectRecord> Objects = new List<ObjectRecord>();
            public Dictionary<string, GeometryFile> GeometryFiles = new Dictionary<string, GeometryFile>();
        }

        private static ObjectsResult BuildObjects(BaseComponent component, Diagnostics diag)
        {
            var res = new ObjectsResult();
            string parentGuid = Safe(() => component.Identifier.GUID.ToString(), "");

            ModelObjectEnumerator children = null;
            try { children = component.GetChildren(); }
            catch (Exception ex)
            {
                diag.InaccessibleObjects.Add("GetChildren: " + ex.Message);
                return res;
            }

            int seq = 0;
            try
            {
                ModelObjectEnumerator.AutoFetch = true;
                while (children != null && children.MoveNext())
                {
                    var child = children.Current;
                    if (child == null) continue;

                    var rec = new ObjectRecord
                    {
                        SourceGUID = Safe(() => child.Identifier.GUID.ToString(), ""),
                        IdentifierId = Safe(() => (int?)child.Identifier.ID, (int?)null),
                        ParentSourceGUID = parentGuid
                    };

                    if (child is BoltGroup bg)
                    {
                        rec.Kind = "BoltGroup";
                        rec.Role = "bolts";
                        rec.Parametric = new Dictionary<string, object>
                        {
                            { "boltSize", Safe(() => bg.BoltSize, 0.0) },
                            { "boltStandard", Safe(() => bg.BoltStandard, "") },
                            { "boltType", Safe(() => bg.BoltType.ToString(), "") },
                            { "boltCount", Safe(() => (int?)bg.BoltPositions.Count ?? 0, 0) },
                            { "bolt", Safe(() => bg.Bolt, false) }
                        };
                    }
                    else if (child is Weld)
                    {
                        rec.Kind = "Weld";
                        rec.Role = "weld";
                    }
                    else if (child is Fitting)
                    {
                        rec.Kind = "Fitting";
                        rec.Role = "fitting";
                    }
                    else if (child is BooleanPart)
                    {
                        rec.Kind = "BooleanPart";
                        rec.Role = "cut";
                    }
                    else if (child is ContourPlate cp)
                    {
                        rec.Kind = "ContourPlate";
                        rec.Role = "plate";
                        rec.Profile = Safe(() => cp.Profile.ProfileString, "");
                        rec.Material = Safe(() => cp.Material.MaterialString, "");
                        AttachGeometry(cp, seq, rec, res);
                    }
                    else if (child is Beam bm)
                    {
                        rec.Kind = "Beam";
                        rec.Role = "beam";
                        rec.Profile = Safe(() => bm.Profile.ProfileString, "");
                        rec.Material = Safe(() => bm.Material.MaterialString, "");
                        AttachGeometry(bm, seq, rec, res);
                    }
                    else if (child is Part part)
                    {
                        rec.Kind = "Beam"; // generic Part -> Beam gibi kaydedilir (profil/plaka ayrımı importerda)
                        rec.Role = "part";
                        rec.Profile = Safe(() => part.Profile.ProfileString, "");
                        rec.Material = Safe(() => part.Material.MaterialString, "");
                        AttachGeometry(part, seq, rec, res);
                    }
                    else
                    {
                        rec.Kind = "Unknown";
                        rec.Role = "unknown";
                        diag.UnsupportedFeatures.Add("Bilinmeyen child tipi: " + child.GetType().Name);
                    }

                    res.Objects.Add(rec);
                    seq++;
                }
            }
            catch (Exception ex)
            {
                diag.InaccessibleObjects.Add("GetChildren iterasyonu: " + ex.Message);
            }

            return res;
        }

        // Parca solid'inden yalnizca bbox cikarir (dogrulanmis Solid -> Face -> Loop ->
        // Vertex zinciri). BRep/mesh bu dilimde DISARI AKTARILMAZ (unsupported olarak
        // isaretlenir, uydurulmaz).
        private static void AttachGeometry(Part part, int seq, ObjectRecord rec, ObjectsResult res)
        {
            double minX = double.MaxValue, minY = double.MaxValue, minZ = double.MaxValue;
            double maxX = double.MinValue, maxY = double.MinValue, maxZ = double.MinValue;
            bool any = false;

            try
            {
                var solid = part.GetSolid();
                if (solid != null)
                {
                    var fe = solid.GetFaceEnumerator();
                    while (fe != null && fe.MoveNext())
                    {
                        var face = fe.Current as Face;
                        if (face == null) continue;
                        var le = face.GetLoopEnumerator();
                        while (le != null && le.MoveNext())
                        {
                            var loop = le.Current as Loop;
                            if (loop == null) continue;
                            var ve = loop.GetVertexEnumerator() as VertexEnumerator;
                            while (ve != null && ve.MoveNext())
                            {
                                var p = ve.Current as Point;
                                if (p == null) continue;
                                minX = Math.Min(minX, p.X); minY = Math.Min(minY, p.Y); minZ = Math.Min(minZ, p.Z);
                                maxX = Math.Max(maxX, p.X); maxY = Math.Max(maxY, p.Y); maxZ = Math.Max(maxZ, p.Z);
                                any = true;
                            }
                        }
                    }
                }
            }
            catch { }

            if (!any) return;

            var rel = "geometry/" + seq.ToString("D4", CultureInfo.InvariantCulture) + ".json";
            rec.GeometryFile = rel;
            res.GeometryFiles[rel] = new GeometryFile
            {
                FormatVersion = "1.0",
                Type = "bbox",
                Bbox = new BBox
                {
                    Min = new Vec3 { X = minX, Y = minY, Z = minZ },
                    Max = new Vec3 { X = maxX, Y = maxY, Z = maxZ }
                }
            };
        }

        // ---------------------------------------------------------------------
        // yardimcilar

        private static string ComponentTypeOf(BaseComponent c)
        {
            if (c is Connection) return "Connection";
            if (c is Component) return "Component";
            if (c is Detail) return "Detail";
            if (c is CustomPart) return "CustomPart";
            if (c is Seam) return "Seam";
            return "Unknown";
        }

        private static MemberRef ToMemberRef(ModelObject mo)
        {
            var mr = new MemberRef
            {
                Guid = Safe(() => mo.Identifier.GUID.ToString(), ""),
                IdentifierId = Safe(() => (int?)mo.Identifier.ID, (int?)null)
            };

            if (mo is Beam beam)
            {
                mr.Profile = Safe(() => beam.Profile.ProfileString, "");
                mr.Material = Safe(() => beam.Material.MaterialString, "");
                mr.StartPoint = ToVec3(Safe(() => beam.StartPoint, null));
                mr.EndPoint = ToVec3(Safe(() => beam.EndPoint, null));
            }
            else if (mo is Part part)
            {
                mr.Profile = Safe(() => part.Profile.ProfileString, "");
                mr.Material = Safe(() => part.Material.MaterialString, "");
            }
            return mr;
        }

        private static Vec3 ToVec3(Point p)
        {
            if (p == null) return null;
            return new Vec3 { X = p.X, Y = p.Y, Z = p.Z };
        }

        private static Vec3 ToVec3(Vector v)
        {
            if (v == null) return null;
            return new Vec3 { X = v.X, Y = v.Y, Z = v.Z };
        }

        private static Vec3 Cross(Vector a, Vector b)
        {
            if (a == null || b == null) return new Vec3 { X = 0, Y = 0, Z = 1 };
            return new Vec3
            {
                X = a.Y * b.Z - a.Z * b.Y,
                Y = a.Z * b.X - a.X * b.Z,
                Z = a.X * b.Y - a.Y * b.X
            };
        }

        private static T Safe<T>(Func<T> fn, T fallback)
        {
            try { return fn(); }
            catch { return fallback; }
        }
    }
}
