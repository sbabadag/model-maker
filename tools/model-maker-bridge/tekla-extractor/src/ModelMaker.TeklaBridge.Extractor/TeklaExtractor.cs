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
    // Tekla Structures modelinden bilesenleri okuyup Model Maker Bridge paketine
    // donusturur. V3: model bilgisi (ad/yol/surum) + component discovery
    // (list-components) + genel geometry extractor (part/fitting/boolean/weld/
    // bolt + bbox + mesh + topology + relationships + operations).
    //
    // DURUSTLUK KURALLARI (spec):
    //  - GetConnectionStatus() false ise SAHTE veri uretilmez, acik hata doner.
    //  - API'nin desteklemedigi alan null/unsupported olarak kaydedilir, uydurulmaz.
    //  - Parametreler YALNIZCA bilinen ham Tekla attribute adlariyla GetAttribute
    //    ile okunur; bilinmeyen parametrelerin otomatik kesfi VARSAYILMAZ.
    //  - Tekla GUID'leri yalniz kaynak referansidir; Model Maker EntityId'si importer
    //    tarafindan atanir.
    //  - Mesh yalniz deliksiz (holeLoops==0) katilarda yuz fan triangulation ile
    //    uretilir (approximate); delikli katida mesh UYDURULMAZ (bbox + bulon
    //    verisi ile yeniden uretilir, reconstructed).

    public sealed class ExtractorOptions
    {
        public string OutputDir = "";
        public List<string> AttributeNames = new List<string>();
        public int IdentifierId = -1;   // --id
        public int Number = -1;         // --number
        public bool ListComponents = false;
        public bool Json = false;
    }

    public sealed class ExtractResult
    {
        public bool Ok;
        public string Error;
        public string OutputDir;
        public string Report;           // list-components raporu (tablo veya JSON)
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

            var info = Safe(() => model.GetInfo(), null);
            string modelPath = Safe(() => info != null ? info.ModelPath : "", "");
            string modelName = Safe(() => info != null ? info.ModelName : "", "");

            if (opts.ListComponents)
            {
                var report = BuildDiscoveryReport(model, modelName, modelPath);
                return new ExtractResult
                {
                    Ok = true,
                    Report = opts.Json ? DiscoveryReportFormatter.ToJson(report)
                                       : DiscoveryReportFormatter.ToTable(report)
                };
            }

            // Bileseni bul: --id / --number / secili.
            var component = FindComponent(model, opts.IdentifierId, opts.Number);
            if (component == null)
            {
                return new ExtractResult
                {
                    Ok = false,
                    Error = "Bilesen bulunamadi. Tekla'da bir baglanti/component secin veya " +
                            "--id/--number verin (once list-components ile ID'leri gorun)."
                };
            }

            var pkg = new BridgePackage();
            var diag = new Diagnostics();

            pkg.Manifest = BuildManifest(component, modelPath, modelName);
            pkg.Component = BuildComponent(component, diag);
            pkg.Parameters = BuildParameters(component, opts.AttributeNames, diag);

            var objects = BuildObjects(component, diag);
            pkg.Objects = objects.Objects;
            pkg.Relationships = BuildRelationships(component, pkg, diag);
            pkg.Operations = BuildOperations(component, pkg, diag);
            pkg.Diagnostics = diag;
            pkg.Manifest.ExportedObjectCount = pkg.Objects.Count;

            var outputDir = string.IsNullOrWhiteSpace(opts.OutputDir)
                ? Path.Combine(Directory.GetCurrentDirectory(), "model-maker-bridge-export")
                : opts.OutputDir;

            BridgePackageWriter.Write(pkg, objects.GeometryFiles, outputDir);

            return new ExtractResult { Ok = true, OutputDir = outputDir };
        }

        // ---------------------------------------------------------------------
        // PHASE A: component discovery

        private static DiscoveryReport BuildDiscoveryReport(Model model, string modelName, string modelPath)
        {
            var report = new DiscoveryReport
            {
                ModelName = modelName,
                ModelPath = modelPath,
                SourceVersion = ReadTeklaVersion()
            };

            try
            {
                ModelObjectEnumerator.AutoFetch = true;
                var selector = Safe(() => model.GetModelObjectSelector(), null);
                var en = Safe(() => selector != null ? selector.GetAllObjects() : null, null);
                while (en != null && en.MoveNext())
                {
                    var mo = en.Current;
                    if (!(mo is BaseComponent bc)) continue;
                    if (!(bc is Connection || bc is Component || bc is Detail || bc is CustomPart || bc is Seam))
                        continue;

                    var sum = new ComponentSummary
                    {
                        IdentifierId = Safe(() => bc.Identifier.ID, 0),
                        Guid = Safe(() => bc.Identifier.GUID.ToString(), ""),
                        Number = Safe(() => bc.Number, 0),
                        Name = Safe(() => bc.Name, ""),
                        Type = ComponentTypeOf(bc),
                        Exportable = true
                    };

                    if (bc is Connection conn)
                    {
                        var p = Safe(() => conn.GetPrimaryObject(), null);
                        if (p is Part pp) { sum.PrimaryProfile = Safe(() => pp.Profile.ProfileString, ""); sum.PrimaryMaterial = Safe(() => pp.Material.MaterialString, ""); }
                        var sec = Safe(() => conn.GetSecondaryObjects(), null);
                        if (sec != null)
                            foreach (var s in sec)
                                if (s is Part sp) sum.SecondaryProfiles.Add(Safe(() => sp.Profile.ProfileString, ""));
                    }
                    else if (bc is Component comp)
                    {
                        var ci = Safe(() => comp.GetComponentInput(), null);
                        if (ci != null)
                        {
                            int idx = 0;
                            foreach (var o in ci)
                            {
                                if (o is Part ip)
                                {
                                    if (idx == 0) { sum.PrimaryProfile = Safe(() => ip.Profile.ProfileString, ""); sum.PrimaryMaterial = Safe(() => ip.Material.MaterialString, ""); }
                                    else sum.SecondaryProfiles.Add(Safe(() => ip.Profile.ProfileString, ""));
                                }
                                idx++;
                            }
                        }
                    }

                    var children = Safe(() => bc.GetChildren(), null);
                    if (children != null)
                    {
                        while (children.MoveNext())
                        {
                            var ch = children.Current;
                            if (ch == null) continue;
                            var kind = ChildKindOf(ch);
                            if (!sum.ChildCounts.ContainsKey(kind)) sum.ChildCounts[kind] = 0;
                            sum.ChildCounts[kind]++;
                        }
                    }

                    report.Components.Add(sum);
                }
            }
            catch (Exception)
            {
                // tarama hatasi -> rapor yine dondur (bos liste).
                if (report.Components.Count == 0)
                    report.Components = new List<ComponentSummary>();
            }
            return report;
        }

        private static BaseComponent FindComponent(Model model, int id, int number)
        {
            // Once secili nesneler (--id/--number verilmediyse).
            if (id < 0 && number < 0)
            {
                try
                {
                    ModelObjectEnumerator.AutoFetch = true;
                    var selector = new TSMUI.ModelObjectSelector();
                    var e = selector.GetSelectedObjects();
                    while (e != null && e.MoveNext())
                        if (e.Current is BaseComponent bc) return bc;
                }
                catch { }
                return null;
            }

            // Modeli tara ve id/number eslestir.
            try
            {
                ModelObjectEnumerator.AutoFetch = true;
                var selector = Safe(() => model.GetModelObjectSelector(), null);
                var en = Safe(() => selector != null ? selector.GetAllObjects() : null, null);
                while (en != null && en.MoveNext())
                {
                    if (!(en.Current is BaseComponent bc)) continue;
                    if (!(bc is Connection || bc is Component || bc is Detail || bc is CustomPart || bc is Seam))
                        continue;
                    if (id >= 0 && Safe(() => bc.Identifier.ID, -1) == id) return bc;
                    if (number >= 0 && Safe(() => bc.Number, -1) == number) return bc;
                }
            }
            catch { }
            return null;
        }

        private static string ReadTeklaVersion()
        {
            try { return Tekla.Structures.TeklaStructuresInfo.GetCurrentProgramVersion(); }
            catch { return ""; }
        }

        // ---------------------------------------------------------------------
        // V3 manifest / component / parameters

        private static Manifest BuildManifest(BaseComponent component, string modelPath, string modelName)
        {
            string guid = Safe(() => component.Identifier.GUID.ToString(), "");
            string type = ComponentTypeOf(component);
            int number = Safe(() => component.Number, 0);

            return new Manifest
            {
                FormatVersion = "3.0",
                SourceApplication = "Tekla Structures",
                SourceVersion = ReadTeklaVersion(),
                Producer = new Producer { Name = "ModelMaker.TeklaBridge", Version = "3.1.0-stage3a" },
                SourceComponentType = type,
                SourceComponentNumber = number,
                SourceComponentName = Safe(() => component.Name, ""),
                SourceModelIdentifier = modelPath,
                SourceModelName = modelName,
                ExportTimestamp = DateTime.UtcNow.ToString("o", CultureInfo.InvariantCulture),
                CoordinateSystem = "TeklaGlobal",
                LengthUnit = "mm",
                ComponentIdentifier = guid,
                ExporterVersion = "3.1.0-stage3a",
                KnownLimitations = new List<string>
                {
                    "Mesh yalniz deliksiz (holeLoops==0) katilarda fan triangulation ile uretilir (approximate).",
                    "Eğrisel yuzeyler fan triangulation ile yaklasik temsil edilir (approximate).",
                    "Parametreler yalnizca --attributes / --attributes-file ile verilen ham Tekla attribute adlariyla okunur."
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

            try
            {
                var cs = component.GetCoordinateSystem();
                if (cs != null)
                {
                    rec.CoordinateSystem = BridgeVectorMath.OrthonormalFrame(
                        ToVec3(cs.Origin), ToVec3(cs.AxisX), ToVec3(cs.AxisY));
                }
            }
            catch (Exception ex) { diag.InaccessibleObjects.Add("koordinat sistemi: " + ex.Message); }

            if (component is Connection conn)
            {
                try { var p = conn.GetPrimaryObject(); if (p != null) rec.PrimaryMember = ToMemberRef(p); }
                catch (Exception ex) { diag.InaccessibleObjects.Add("primary: " + ex.Message); }
                try
                {
                    var sec = conn.GetSecondaryObjects();
                    if (sec != null)
                        foreach (var o in sec)
                            if (o is ModelObject mo) rec.SecondaryMembers.Add(ToMemberRef(mo));
                }
                catch (Exception ex) { diag.InaccessibleObjects.Add("secondary: " + ex.Message); }
            }
            else if (component is Component comp)
            {
                try
                {
                    var ci = comp.GetComponentInput();
                    int idx = 0;
                    foreach (var o in ci)
                    {
                        if (o is ModelObject mo) { if (idx == 0) rec.PrimaryMember = ToMemberRef(mo); else rec.SecondaryMembers.Add(ToMemberRef(mo)); idx++; }
                    }
                }
                catch (Exception ex) { diag.InaccessibleObjects.Add("component input: " + ex.Message); }
            }
            else
            {
                diag.InaccessibleObjects.Add("primary/secondary okuma bu component tipi icin uygulanmadi (" + rec.SourceType + ").");
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
            var seenNames = new HashSet<string>(StringComparer.Ordinal);
            if (attributeNames == null || attributeNames.Count == 0)
            {
                diag.Warnings.Add("Parametre listesi bos: --attributes veya --attributes-file ile ham Tekla attribute adlari verilmedikce parameters.json bos kalir.");
                return result;
            }
            foreach (var name in attributeNames)
            {
                if (string.IsNullOrWhiteSpace(name) || !seenNames.Add(name)) continue;
                double d = 0.0;
                if (Safe(() => component.GetAttribute(name, ref d), false))
                { result.Add(new ParameterRecord { Name = name, Type = "Double", Value = d, Source = "GetAttribute", Readable = "ok", Confidence = "observed" }); continue; }
                int i = 0;
                if (Safe(() => component.GetAttribute(name, ref i), false))
                { result.Add(new ParameterRecord { Name = name, Type = "Integer", Value = i, Source = "GetAttribute", Readable = "ok", Confidence = "observed" }); continue; }
                string s = "";
                if (Safe(() => component.GetAttribute(name, ref s), false))
                { result.Add(new ParameterRecord { Name = name, Type = "String", Value = s, Source = "GetAttribute", Readable = "ok", Confidence = "observed" }); continue; }
                result.Add(new ParameterRecord { Name = name, Type = "Unknown", Value = null, Source = "GetAttribute", Readable = "error", Confidence = "unresolved" });
            }
            return result;
        }

        // ---------------------------------------------------------------------
        // V3 objects + geometry

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
            catch (Exception ex) { diag.InaccessibleObjects.Add("GetChildren: " + ex.Message); return res; }

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
                        ParentSourceGUID = parentGuid,
                        Confidence = "observed"
                    };

                    // Tekla bazi child'lara (orn. ControlPoint) Guid.Empty verir. Kimligi
                    // olmayan nesne iliski/operasyon referansi olamaz; yazilirsa objects.json'da
                    // tekrarli sourceGUID olusur ve import ("sourceGUID tekrarli") reddedilir.
                    if (string.IsNullOrWhiteSpace(rec.SourceGUID) ||
                        rec.SourceGUID == System.Guid.Empty.ToString())
                    {
                        diag.UnsupportedFeatures.Add(
                            "GUID'si olmayan child atlandi: " + child.GetType().Name);
                        continue;
                    }

                    if (child is BoltGroup bg)
                    {
                        rec.Kind = "BoltGroup";
                        rec.Role = "bolts";
                        rec.RoleKey = "bolts";
                        rec.Mapped = true;
                        rec.MappedKind = "BoltGroup";
                        rec.BoltGroup = BuildBoltGroup(bg, diag);
                    }
                    else if (child is Weld w)
                    {
                        rec.Kind = "Weld";
                        rec.Role = "weld";
                        rec.RoleKey = "weld";
                        rec.Mapped = true;
                        rec.MappedKind = "Weld";
                        rec.Weld = BuildWeld(w, diag);
                    }
                    else if (child is Fitting f)
                    {
                        rec.Kind = "Fitting";
                        rec.Role = "fitting";
                        rec.RoleKey = "fitting";
                        rec.Mapped = true;
                        rec.MappedKind = "Fitting";
                        rec.Confidence = "inferred";
                        rec.Fitting = BuildFitting(f, diag);
                    }
                    else if (child is BooleanPart bp)
                    {
                        rec.Kind = "BooleanPart";
                        rec.Role = "cut";
                        rec.RoleKey = "cut";
                        rec.Mapped = true;
                        rec.MappedKind = "Cut";
                        rec.Confidence = "inferred";
                        rec.BooleanPart = BuildBooleanPart(bp, diag);
                    }
                    else if (child is ContourPlate cp)
                    {
                        rec.Kind = "ContourPlate";
                        rec.Role = "plate";
                        rec.RoleKey = "plate";
                        rec.Profile = Safe(() => cp.Profile.ProfileString, "");
                        rec.Material = Safe(() => cp.Material.MaterialString, "");
                        rec.PlateHoleProbe = BuildPlateHoleProbe(cp, diag);
                        AttachGeometry(cp, seq, rec, res, diag);
                    }
                    else if (child is Beam bm)
                    {
                        rec.Kind = "Beam";
                        rec.Role = "beam";
                        rec.RoleKey = "beam";
                        rec.Profile = Safe(() => bm.Profile.ProfileString, "");
                        rec.Material = Safe(() => bm.Material.MaterialString, "");
                        AttachGeometry(bm, seq, rec, res, diag);
                    }
                    else if (child is Part part)
                    {
                        rec.Kind = "Part";
                        rec.Role = "part";
                        rec.RoleKey = "part";
                        rec.Profile = Safe(() => part.Profile.ProfileString, "");
                        rec.Material = Safe(() => part.Material.MaterialString, "");
                        AttachGeometry(part, seq, rec, res, diag);
                    }
                    else
                    {
                        // Taninmayan child (ControlPoint, vs.) nesne listesine YAZILMAZ:
                        // geometrisi yok ve kimligi guvenilir degil; yalniz teshise yazilir.
                        diag.UnsupportedFeatures.Add("Aktarilamayan child atlandi: " + child.GetType().Name);
                        continue;
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

        private static BoltGroupInfo BuildBoltGroup(BoltGroup bg, Diagnostics diag)
        {
            var info = new BoltGroupInfo();
            try
            {
                info.Size = "M" + Safe(() => bg.BoltSize, 0.0).ToString(CultureInfo.InvariantCulture);
                info.StandardCode = 0;  // BoltStandard string'dir; sayisal kod dogrudan okunamaz
                info.Tolerance = Safe(() => bg.Tolerance, 0.0);
                info.PartToBeBolted = Safe(() => bg.PartToBeBolted.Identifier.GUID.ToString(), "");
                info.PartToBoltTo = Safe(() => bg.PartToBoltTo.Identifier.GUID.ToString(), "");
                info.BoltType = Safe(() => bg.BoltType.ToString(), "");
                info.BoltStandard = Safe(() => bg.BoltStandard, "");
                info.Axis = "";  // Stage 3D must not authorize uncertain drilling
                info.AxisSource = "";
                info.Direction = null;

                // Tekla 2025 documents BoltPositions in the BOLT GROUP XY plane,
                // relative to the transformation plane selected at model lookup.
                // The normal of bg.GetCoordinateSystem() is ONLY a candidate;
                // it is not yet a verified global hole axis or cutting origin.
                try
                {
                    var bgCS = bg.GetCoordinateSystem();
                    if (bgCS != null)
                    {
                        var frame = BridgeVectorMath.OrthonormalFrame(
                            ToVec3(bgCS.Origin), ToVec3(bgCS.AxisX), ToVec3(bgCS.AxisY));
                        info.FrameProbe = new BoltFrameProbe
                        {
                            CoordinateSystem = frame,
                            NormalCandidate = frame.ZAxis,
                            VerifiedForDrilling = false
                        };
                    }
                    else
                    {
                        diag.Warnings.Add("BoltGroup.GetCoordinateSystem bos: eksen probe kaydedilemedi.");
                    }
                }
                catch (Exception ex)
                {
                    diag.Warnings.Add("BoltGroup koordinat probe okunamadi: " + ex.Message);
                }

                // Read-only hole settings. Hole1..5 indicate per-part selection,
                // not hole dimensions or verified drill permission.
                info.HoleSettingsProbe = new BoltHoleSettingsProbe
                {
                    BoltPresent = Safe(() => (bool?)bg.Bolt, (bool?)null),
                    PlainHoleType = Safe(() => bg.PlainHoleType.ToString(), ""),
                    SpecialHoleType = Safe(() => bg.HoleType.ToString(), ""),
                    SlottedHoleX = Safe(() => bg.SlottedHoleX, 0.0),
                    SlottedHoleY = Safe(() => bg.SlottedHoleY, 0.0),
                    Hole1 = Safe(() => bg.Hole1, false),
                    Hole2 = Safe(() => bg.Hole2, false),
                    Hole3 = Safe(() => bg.Hole3, false),
                    Hole4 = Safe(() => bg.Hole4, false),
                    Hole5 = Safe(() => bg.Hole5, false),
                    VerifiedForDrilling = false
                };

                // The five Hole1..Hole5 flags SELECT SPECIAL HOLES in plies,
                // not whether normal circular bolt holes exist.
                // A false flag cannot establish a solid/uncut target.
                try
                {
                    var parts = bg.BoltHolesAttributes;
                    if (parts != null)
                    {
                        var perPly = new List<BoltPlyHoleProbe>();
                        for (int i = 0; i < parts.Count; ++i)
                        {
                            var item = parts[i];
                            if (item == null) continue;
                            perPly.Add(new BoltPlyHoleProbe
                            {
                                PlyIndex = i + 1,
                                SpecialHoleType = Safe(() => item.HoleType.ToString(), ""),
                                LongHoleX = Safe(() => item.LongHoleX, 0.0),
                                LongHoleY = Safe(() => item.LongHoleY, 0.0),
                                SlotOffsetX = Safe(() => item.SlotOffsetX, 0.0),
                                SlotOffsetY = Safe(() => item.SlotOffsetY, 0.0)
                            });
                        }
                        info.HoleSettingsProbe.PerPlySpecialHoles = perPly;
                    }
                }
                catch (Exception ex)
                {
                    diag.Warnings.Add("BoltHolesAttributes probe okunamadi: " + ex.Message);
                }

                // HOLE_* template report values may be unavailable via model
                // GetReportProperty; keep unsupported fields readable=false.
                // Numeric -1 has a real meaning (ordinary hole).
                foreach (var reportName in new[]
                {
                    "HOLE_1_TYPE", "HOLE_2_TYPE", "HOLE_3_TYPE",
                    "HOLE_4_TYPE", "HOLE_5_TYPE", "PLAIN_HOLE_TYPE",
                    "DIAMETER_X", "DIAMETER_Y"
                })
                    info.HoleSettingsProbe.ReportProbes.Add(ReadBoltReportProbe(bg, reportName));

                // Preserve source BoltPositions AS RETURNED. Do not silently
                // reinterpret active selection-plane positions as world points,
                // and do not move X=0 into a 4.25..14.25 plate.
                var positions = Safe(() => bg.BoltPositions, null);
                if (positions != null)
                {
                    var pts = positions as System.Collections.IEnumerable;
                    if (pts != null)
                        foreach (var o in pts)
                            if (o is Point p)
                                info.Bolts.Add(new BoltRecord { Position = ToVec3(p), NominalDiameter = Safe(() => bg.BoltSize, 0.0), HoleDiameterSource = "" });
                }
                if (info.Bolts.Count == 0)
                    diag.Warnings.Add("Bulon grubu " + Safe(() => bg.Identifier.GUID.ToString(), "") + ": BoltPositions okunamadi.");
            }
            catch (Exception ex) { diag.InaccessibleObjects.Add("bolt group: " + ex.Message); }
            return info;
        }

        private static BoltReportValueProbe ReadBoltReportProbe(
            BoltGroup bg, string reportName)
        {
            var field = new BoltReportValueProbe { Name = reportName, Readable = false };
            try
            {
                int vi = 0;
                if (bg.GetReportProperty(reportName, ref vi))
                {
                    field.Readable = true;
                    field.Value = vi.ToString(CultureInfo.InvariantCulture);
                    return field;
                }
                double vd = 0.0;
                if (bg.GetReportProperty(reportName, ref vd))
                {
                    field.Readable = true;
                    field.Value = vd.ToString(CultureInfo.InvariantCulture);
                    return field;
                }
                string vs = "";
                if (bg.GetReportProperty(reportName, ref vs))
                {
                    field.Readable = true;
                    field.Value = vs;
                }
            }
            catch (Exception)
            {
                // Missing report attribute is normal for this API/model object.
                // Probe is optional; no synthetic value is substituted.
            }
            return field;
        }

        private static WeldInfo BuildWeld(Weld w, Diagnostics diag)
        {
            var info = new WeldInfo();
            try
            {
                info.Type = Safe(() => w.TypeAbove.ToString(), "");
                info.Size = Safe(() => w.SizeAbove, 0.0);
                info.SizeAbove = Safe(() => w.SizeAbove, 0.0);
                info.SizeBelow = Safe(() => w.SizeBelow, 0.0);
                var main = Safe(() => w.MainObject, null);
                var sec = Safe(() => w.SecondaryObject, null);
                if (main != null) info.MainSourceGUID = Safe(() => main.Identifier.GUID.ToString(), "");
                if (sec != null) info.SecondarySourceGUID = Safe(() => sec.Identifier.GUID.ToString(), "");
                info.Position = "";
            }
            catch (Exception ex) { diag.InaccessibleObjects.Add("weld: " + ex.Message); }
            return info;
        }

        private static FittingInfo BuildFitting(Fitting f, Diagnostics diag)
        {
            var info = new FittingInfo { Inferred = true };
            try
            {
                var plane = Safe(() => f.Plane, null);
                if (plane != null)
                {
                    var origin = Safe(() => plane.Origin, null);
                    var normal = BridgeVectorMath.UnitCross(
                        ToVec3(Safe(() => plane.AxisX, null)),
                        ToVec3(Safe(() => plane.AxisY, null)));
                    info.Plane = new PlaneRecord { Origin = ToVec3(origin), Normal = normal };
                    info.Normal = normal;
                }
                var target = Safe(() => f.Father, null);
                if (target != null) info.TargetPartGUID = Safe(() => target.Identifier.GUID.ToString(), "");
                info.KeepSide = "";  // korunan taraf API'den dogrudan okunamadi -> INFERRED
            }
            catch (Exception ex) { diag.InaccessibleObjects.Add("fitting: " + ex.Message); }
            return info;
        }

        private static BooleanPartInfo BuildBooleanPart(BooleanPart bp, Diagnostics diag)
        {
            var info = new BooleanPartInfo { Inferred = true };
            try
            {
                info.OperationType = "Cut";  // BooleanPart default Cut; API acik vermez -> INFERRED
                var op = Safe(() => bp.OperativePart, null);
                if (op != null) info.OperandGUID = Safe(() => op.Identifier.GUID.ToString(), "");
            }
            catch (Exception ex) { diag.InaccessibleObjects.Add("booleanPart: " + ex.Message); }
            return info;
        }

        // Tekla 2025: HAS_HOLES is a part report attribute returning 1 when
        // the part contains bolt holes and 0 otherwise (ordinary cuts excluded).
        // RAW/NORMAL comparison is just another read-only observation.
        // Neither is sufficient for an authoritative per-hole drilling axis.
        private static PlateHoleReportProbe BuildPlateHoleProbe(ContourPlate plate, Diagnostics diag)
        {
            var probe = new PlateHoleReportProbe();
            try
            {
                int n = -1;
                if (plate.GetReportProperty("HAS_HOLES", ref n) && (n == 0 || n == 1))
                {
                    probe.ReportReadable = true;
                    probe.ReportValue = n;
                    probe.HasBoltHoles = n == 1;
                }
                else
                {
                    // Some environments expose report attributes as strings.
                    string raw = "";
                    if (plate.GetReportProperty("HAS_HOLES", ref raw) &&
                        int.TryParse(raw.Trim(), NumberStyles.Integer, CultureInfo.InvariantCulture, out n) &&
                        (n == 0 || n == 1))
                    {
                        probe.ReportReadable = true;
                        probe.ReportValue = n;
                        probe.HasBoltHoles = n == 1;
                    }
                }
                if (!probe.ReportReadable)
                    diag.Warnings.Add("ContourPlate HAS_HOLES rapor alani okunamadi: " +
                        Safe(() => plate.Identifier.GUID.ToString(), ""));
            }
            catch (Exception ex)
            {
                diag.Warnings.Add("ContourPlate HAS_HOLES hatasi: " + ex.Message);
            }
            try
            {
                probe.RawTopology = ReadSolidTopology(
                    plate.GetSolid(Solid.SolidCreationTypeEnum.RAW));
            }
            catch (Exception ex) { diag.Warnings.Add("RAW topology probe: " + ex.Message); }
            try
            {
                probe.NormalTopology = ReadSolidTopology(
                    plate.GetSolid(Solid.SolidCreationTypeEnum.NORMAL));
            }
            catch (Exception ex) { diag.Warnings.Add("NORMAL topology probe: " + ex.Message); }

            if (probe.RawTopology != null && probe.NormalTopology != null)
                probe.TopologyDiffers =
                    probe.RawTopology.FaceCount != probe.NormalTopology.FaceCount ||
                    probe.RawTopology.LoopCount != probe.NormalTopology.LoopCount ||
                    probe.RawTopology.HoleLoops != probe.NormalTopology.HoleLoops;
            // Delik SAYISI, ekseni ve her biri icin cap geometrisi dogrulanmadi.
            probe.VerifiedHoleCount = null;
            probe.VerifiedForDrilling = false;
            return probe;
        }

        private static SolidTopologyProbe ReadSolidTopology(Solid solid)
        {
            if (solid == null) return null;
            int faces = 0, loops = 0;
            var faceEnumerator = solid.GetFaceEnumerator();
            while (faceEnumerator != null && faceEnumerator.MoveNext())
            {
                if (!(faceEnumerator.Current is Face face)) continue;
                faces++;
                var loopEnumerator = face.GetLoopEnumerator();
                while (loopEnumerator != null && loopEnumerator.MoveNext())
                    if (loopEnumerator.Current is Loop) loops++;
            }
            return new SolidTopologyProbe
            {
                FaceCount = faces,
                LoopCount = loops,
                HoleLoops = Math.Max(0, loops - faces)
            };
        }

        // Parca solid'inden bbox + topoloji + (deliksiz ise) mesh cikarir.
        private static void AttachGeometry(Part part, int seq, ObjectRecord rec, ObjectsResult res, Diagnostics diag)
        {
            double minX = double.MaxValue, minY = double.MaxValue, minZ = double.MaxValue;
            double maxX = double.MinValue, maxY = double.MinValue, maxZ = double.MinValue;
            bool any = false;
            int faceCount = 0, loopCount = 0, vertexOcc = 0;
            var faceLoops = new List<List<Point>>();  // deliksiz mesh icin yuz dis loop'lari

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
                        faceCount++;
                        int loopsInFace = 0;
                        List<Point> firstLoop = null;
                        var le = face.GetLoopEnumerator();
                        while (le != null && le.MoveNext())
                        {
                            var loop = le.Current as Loop;
                            if (loop == null) continue;
                            loopsInFace++;
                            var pts = new List<Point>();
                            var ve = loop.GetVertexEnumerator() as VertexEnumerator;
                            while (ve != null && ve.MoveNext())
                            {
                                var p = ve.Current as Point;
                                if (p == null) continue;
                                pts.Add(p);
                                vertexOcc++;
                                minX = Math.Min(minX, p.X); minY = Math.Min(minY, p.Y); minZ = Math.Min(minZ, p.Z);
                                maxX = Math.Max(maxX, p.X); maxY = Math.Max(maxY, p.Y); maxZ = Math.Max(maxZ, p.Z);
                                any = true;
                            }
                            if (firstLoop == null) firstLoop = pts;
                        }
                        loopCount += loopsInFace;
                        if (loopsInFace == 1 && firstLoop != null && firstLoop.Count >= 3)
                            faceLoops.Add(firstLoop);
                    }
                }
            }
            catch { }

            if (!any) return;

            int holeLoops = Math.Max(0, loopCount - faceCount);

            var rel = "geometry/" + seq.ToString("D4", CultureInfo.InvariantCulture) + ".json";
            rec.GeometryFile = rel;
            rec.Topology = new TopologyInfo
            {
                FaceCount = faceCount,
                LoopCount = loopCount,
                FaceVertexOccurrences = vertexOcc,
                HoleLoops = holeLoops
            };

            var geom = new GeometryFile
            {
                FormatVersion = "3.0",
                Bbox = new BBox
                {
                    Min = new Vec3 { X = minX, Y = minY, Z = minZ },
                    Max = new Vec3 { X = maxX, Y = maxY, Z = maxZ }
                }
            };

            // Deliksiz kati -> fan triangulation mesh (approximate). Delikli -> bbox only.
            if (holeLoops == 0 && faceLoops.Count > 0)
            {
                var mesh = TriangulateFaceLoops(faceLoops);
                if (mesh != null && mesh.Vertices.Count >= 4 && mesh.Triangles.Count >= 4)
                {
                    geom.Type = "mesh";
                    geom.Representation = "approximate";
                    geom.Mesh = mesh;
                }
                else
                {
                    geom.Type = "bbox";
                    geom.Representation = "reconstructed";
                }
            }
            else
            {
                geom.Type = "bbox";
                geom.Representation = "reconstructed";
            }
            rec.GeometryRepresentation = geom.Representation;
            res.GeometryFiles[rel] = geom;
        }

        private static Mesh TriangulateFaceLoops(List<List<Point>> faceLoops)
        {
            var verts = new List<List<double>>();
            var tris = new List<List<int>>();
            foreach (var loop in faceLoops)
            {
                int baseIdx = verts.Count;
                foreach (var p in loop)
                    verts.Add(new List<double> { p.X, p.Y, p.Z });
                // Fan: (v0, vi, vi+1)
                for (int i = 1; i + 1 < loop.Count; i++)
                    tris.Add(new List<int> { baseIdx, baseIdx + i, baseIdx + i + 1 });
            }
            if (verts.Count < 4 || tris.Count < 4) return null;
            return new Mesh { Vertices = verts, Triangles = tris };
        }

        // ---------------------------------------------------------------------
        // V3 relationships + operations

        private static List<RelationshipRecord> BuildRelationships(BaseComponent component, BridgePackage pkg, Diagnostics diag)
        {
            var rels = new List<RelationshipRecord>();
            string compGuid = Safe(() => component.Identifier.GUID.ToString(), "");
            int seq = 0;
            foreach (var o in pkg.Objects)
            {
                rels.Add(new RelationshipRecord { Id = "r" + (seq++), Kind = "partOf", SourceGUID = o.SourceGUID, TargetGUID = compGuid, Confidence = "observed" });
                if (o.BoltGroup != null)
                {
                    if (!string.IsNullOrEmpty(o.BoltGroup.PartToBoltTo))
                        rels.Add(new RelationshipRecord { Id = "r" + (seq++), Kind = "boltedTo", SourceGUID = o.SourceGUID, TargetGUID = o.BoltGroup.PartToBoltTo, Confidence = "observed" });
                    if (!string.IsNullOrEmpty(o.BoltGroup.PartToBeBolted))
                        rels.Add(new RelationshipRecord { Id = "r" + (seq++), Kind = "boltedTo", SourceGUID = o.SourceGUID, TargetGUID = o.BoltGroup.PartToBeBolted, Confidence = "inferred" });
                }
                else if (o.Weld != null)
                {
                    if (!string.IsNullOrEmpty(o.Weld.SecondarySourceGUID))
                        rels.Add(new RelationshipRecord { Id = "r" + (seq++), Kind = "weldedTo", SourceGUID = o.SourceGUID, TargetGUID = o.Weld.SecondarySourceGUID, Confidence = "observed" });
                    if (!string.IsNullOrEmpty(o.Weld.MainSourceGUID))
                        rels.Add(new RelationshipRecord { Id = "r" + (seq++), Kind = "weldedTo", SourceGUID = o.SourceGUID, TargetGUID = o.Weld.MainSourceGUID, Confidence = "inferred" });
                }
                else if (o.Fitting != null && !string.IsNullOrEmpty(o.Fitting.TargetPartGUID))
                    rels.Add(new RelationshipRecord { Id = "r" + (seq++), Kind = "fittedTo", SourceGUID = o.SourceGUID, TargetGUID = o.Fitting.TargetPartGUID, Confidence = "inferred" });
                else if (o.BooleanPart != null && !string.IsNullOrEmpty(o.BooleanPart.TargetPartGUID))
                    rels.Add(new RelationshipRecord { Id = "r" + (seq++), Kind = "operatedBy", SourceGUID = o.SourceGUID, TargetGUID = o.BooleanPart.TargetPartGUID, Confidence = "inferred" });
            }
            // uye iliskileri.
            if (pkg.Component.PrimaryMember != null && !string.IsNullOrEmpty(pkg.Component.PrimaryMember.Guid))
                rels.Add(new RelationshipRecord { Id = "r" + (seq++), Kind = "memberOf", SourceGUID = pkg.Component.PrimaryMember.Guid, TargetGUID = compGuid, Confidence = "observed" });
            foreach (var sm in pkg.Component.SecondaryMembers)
                if (!string.IsNullOrEmpty(sm.Guid))
                    rels.Add(new RelationshipRecord { Id = "r" + (seq++), Kind = "secondaryOf", SourceGUID = sm.Guid, TargetGUID = compGuid, Confidence = "observed" });
            return rels;
        }

        private static List<OperationRecord> BuildOperations(BaseComponent component, BridgePackage pkg, Diagnostics diag)
        {
            var ops = new List<OperationRecord>();
            int seq = 0;
            foreach (var o in pkg.Objects)
            {
                if (o.BoltGroup != null && !string.IsNullOrEmpty(o.BoltGroup.PartToBoltTo))
                    ops.Add(new OperationRecord { Id = "op" + (seq++), Type = "BoltHole", TargetPartGUID = o.BoltGroup.PartToBoltTo, OperatorGUID = o.SourceGUID, Confidence = "observed" });
                else if (o.Fitting != null)
                    ops.Add(new OperationRecord
                    {
                        Id = "op" + (seq++), Type = "Fitting",
                        TargetPartGUID = o.Fitting.TargetPartGUID ?? "",
                        OperatorGUID = o.SourceGUID,
                        FittingPlane = o.Fitting.Plane,
                        KeepSide = o.Fitting.KeepSide,
                        Inferred = o.Fitting.Inferred,
                        Confidence = "inferred"
                    });
                else if (o.BooleanPart != null)
                    ops.Add(new OperationRecord
                    {
                        Id = "op" + (seq++), Type = "BooleanCut",
                        TargetPartGUID = o.BooleanPart.TargetPartGUID ?? "",
                        OperatorGUID = o.SourceGUID,
                        CuttingSolidGUID = o.BooleanPart.OperandGUID,
                        Inferred = o.BooleanPart.Inferred,
                        Confidence = "inferred"
                    });
            }
            return ops;
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

        private static string ChildKindOf(ModelObject mo)
        {
            if (mo is BoltGroup) return "BoltGroup";
            if (mo is Weld) return "Weld";
            if (mo is Fitting) return "Fitting";
            if (mo is BooleanPart) return "BooleanPart";
            if (mo is ContourPlate) return "ContourPlate";
            if (mo is Beam) return "Beam";
            if (mo is PolyBeam) return "PolyBeam";
            if (mo is Part) return "Part";
            return mo.GetType().Name;
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

        private static T Safe<T>(Func<T> fn, T fallback)
        {
            try { return fn(); }
            catch { return fallback; }
        }
    }
}
