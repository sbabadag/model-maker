using System;
using System.Collections.Generic;
using System.IO;
using Newtonsoft.Json;
using Xunit;

namespace ModelMaker.TeklaBridge.Core.Tests
{
    public class BridgePackageTests
    {
        private static BridgePackage SamplePackage()
        {
            return new BridgePackage
            {
                Manifest = new Manifest
                {
                    FormatVersion = "1.0",
                    SourceApplication = "Tekla Structures",
                    SourceVersion = "2025.0",
                    SourceComponentType = "Connection",
                    SourceComponentNumber = 146,
                    SourceComponentName = "Shear plate simple",
                    SourceModelIdentifier = "C:\\TeklaStructures\\Models\\Test",
                    ExportTimestamp = "2026-10-20T00:00:00Z",
                    CoordinateSystem = "TeklaGlobal",
                    LengthUnit = "mm",
                    ComponentIdentifier = "GUID-TEST-146",
                    ExportedObjectCount = 1,
                    ExporterVersion = "1.0.0"
                },
                Component = new BridgeComponentRecord
                {
                    SourceType = "Connection",
                    Name = "Shear plate simple",
                    Number = 146,
                    Guid = "GUID-TEST-146",
                    CoordinateSystem = new CoordinateSystem
                    {
                        Origin = new Vec3 { X = 0, Y = 0, Z = 0 },
                        XAxis = new Vec3 { X = 1, Y = 0, Z = 0 },
                        YAxis = new Vec3 { X = 0, Y = 1, Z = 0 },
                        ZAxis = new Vec3 { X = 0, Y = 0, Z = 1 }
                    },
                    PrimaryMember = new MemberRef { Guid = "GUID-COL", Profile = "HEA400", Material = "S235JR" },
                    SecondaryMembers = new List<MemberRef>
                    {
                        new MemberRef { Guid = "GUID-BEAM", Profile = "IPE300", Material = "S235JR" }
                    }
                },
                Parameters = new List<ParameterRecord>
                {
                    new ParameterRecord { Name = "plateThickness", Type = "Double", Value = 10.0, Source = "GetAttribute", Readable = "ok", Confidence = "observed" }
                },
                Objects = new List<ObjectRecord>
                {
                    new ObjectRecord { SourceGUID = "GUID-PLATE", Kind = "ContourPlate", Role = "plate" }
                },
                Diagnostics = new Diagnostics()
            };
        }

        [Fact]
        public void WriterRoundtrip_PreservesManifest()
        {
            var pkg = SamplePackage();
            var dir = Path.Combine(Path.GetTempPath(), "mmbridge-test-" + Guid.NewGuid().ToString("N"));
            try
            {
                BridgePackageWriter.Write(pkg, new Dictionary<string, GeometryFile>(), dir);

                var manifestPath = Path.Combine(dir, "manifest.json");
                Assert.True(File.Exists(manifestPath));
                var manifest = JsonConvert.DeserializeObject<Manifest>(File.ReadAllText(manifestPath));
                Assert.Equal("GUID-TEST-146", manifest.ComponentIdentifier);
                Assert.Equal(146, manifest.SourceComponentNumber);
                Assert.Equal("Connection", manifest.SourceComponentType);
                Assert.Equal(1, manifest.ExportedObjectCount);
            }
            finally
            {
                if (Directory.Exists(dir)) Directory.Delete(dir, true);
            }
        }

        [Fact]
        public void WriterWritesGeometryFiles()
        {
            var pkg = SamplePackage();
            pkg.Objects[0].GeometryFile = "geometry/0000.json";
            var geom = new Dictionary<string, GeometryFile>
            {
                { "geometry/0000.json", new GeometryFile
                    {
                        FormatVersion = "1.0",
                        Type = "bbox",
                        Bbox = new BBox { Min = new Vec3 { X = 0, Y = 0, Z = 0 }, Max = new Vec3 { X = 100, Y = 200, Z = 10 } }
                    }
                }
            };
            var dir = Path.Combine(Path.GetTempPath(), "mmbridge-test-" + Guid.NewGuid().ToString("N"));
            try
            {
                BridgePackageWriter.Write(pkg, geom, dir);
                Assert.True(File.Exists(Path.Combine(dir, "geometry", "0000.json")));
            }
            finally
            {
                if (Directory.Exists(dir)) Directory.Delete(dir, true);
            }
        }

        [Fact]
        public void ValidatorAcceptsValidPackage()
        {
            var errors = BridgePackageValidator.Validate(SamplePackage());
            Assert.Empty(errors);
        }

        [Fact]
        public void ValidatorRejectsBadComponentType()
        {
            var pkg = SamplePackage();
            pkg.Manifest.SourceComponentType = "NotAComponent";
            var errors = BridgePackageValidator.Validate(pkg);
            Assert.Contains(errors, e => e.Contains("sourceComponentType"));
        }

        [Fact]
        public void ValidatorRejectsMissingPrimaryGuid()
        {
            var pkg = SamplePackage();
            pkg.Component.PrimaryMember.Guid = "";
            var errors = BridgePackageValidator.Validate(pkg);
            Assert.Contains(errors, e => e.Contains("primaryMember"));
        }

        [Fact]
        public void ValidatorRejectsUnsafeGeometryPath()
        {
            var pkg = SamplePackage();
            pkg.Objects[0].GeometryFile = "../../evil.json";
            var errors = BridgePackageValidator.Validate(pkg);
            Assert.Contains(errors, e => e.Contains("geometryFile"));
        }

        [Theory]
        [InlineData("geometry/0000.json", true)]
        [InlineData("geometry/sub/0000.json", true)]
        [InlineData("../evil.json", false)]
        [InlineData("/abs/path.json", false)]
        [InlineData("C:\\evil.json", false)]
        [InlineData("", false)]
        public void SafeRelativePath(string path, bool expected)
        {
            Assert.Equal(expected, BridgePackageValidator.IsSafeRelativePath(path));
        }

        private static BridgePackage V3Package()
        {
            var pkg = SamplePackage();
            pkg.Manifest.FormatVersion = "3.0";
            pkg.Manifest.Producer = new Producer { Name = "ModelMaker.TeklaBridge", Version = "3.0.0" };
            pkg.Manifest.SourceModelName = "test.db1";
            pkg.Objects[0].RoleKey = "shear_tab";
            pkg.Objects[0].GeometryRepresentation = "reconstructed";
            pkg.Objects[0].Confidence = "observed";
            pkg.Relationships = new List<RelationshipRecord>
            {
                new RelationshipRecord { Id = "r1", Kind = "partOf", SourceGUID = "GUID-PLATE", TargetGUID = "GUID-TEST-146", Confidence = "observed" }
            };
            pkg.Operations = new List<OperationRecord>
            {
                new OperationRecord { Id = "op1", Type = "BoltHole", TargetPartGUID = "GUID-PLATE", OperatorGUID = "GUID-BOLT", Confidence = "observed" }
            };
            return pkg;
        }

        [Fact]
        public void V3_ValidatorAcceptsRelationshipsAndOperations()
        {
            var errors = BridgePackageValidator.Validate(V3Package());
            Assert.Empty(errors);
        }

        [Fact]
        public void V3_ValidatorRejectsBadRelationshipKind()
        {
            var pkg = V3Package();
            pkg.Relationships[0].Kind = "notARelation";
            var errors = BridgePackageValidator.Validate(pkg);
            Assert.Contains(errors, e => e.Contains("relationships"));
        }

        [Fact]
        public void V3_ValidatorRejectsBadOperationType()
        {
            var pkg = V3Package();
            pkg.Operations[0].Type = "Explode";
            var errors = BridgePackageValidator.Validate(pkg);
            Assert.Contains(errors, e => e.Contains("operations"));
        }

        [Fact]
        public void V3_WriterWritesRelationshipsOperationsAndChecksums()
        {
            var pkg = V3Package();
            pkg.Objects[0].GeometryFile = "geometry/0000.json";
            var geom = new Dictionary<string, GeometryFile>
            {
                { "geometry/0000.json", new GeometryFile { FormatVersion = "3.0", Type = "bbox", Representation = "reconstructed", Bbox = new BBox { Min = new Vec3 { X = 0, Y = 0, Z = 0 }, Max = new Vec3 { X = 100, Y = 200, Z = 10 } } } }
            };
            var dir = Path.Combine(Path.GetTempPath(), "mmbridge-v3-" + Guid.NewGuid().ToString("N"));
            try
            {
                BridgePackageWriter.Write(pkg, geom, dir);
                Assert.True(File.Exists(Path.Combine(dir, "relationships.json")));
                Assert.True(File.Exists(Path.Combine(dir, "operations.json")));
                Assert.True(File.Exists(Path.Combine(dir, "checksums.json")));
                var checksums = JsonConvert.DeserializeObject<Dictionary<string, string>>(File.ReadAllText(Path.Combine(dir, "checksums.json")));
                Assert.True(checksums.ContainsKey("manifest.json"));
                Assert.True(checksums.ContainsKey("geometry/0000.json"));
                Assert.Equal(64, checksums["manifest.json"].Length);
            }
            finally
            {
                if (Directory.Exists(dir)) Directory.Delete(dir, true);
            }
        }

        [Fact]
        public void DiscoveryFormatter_ProducesTableAndJson()
        {
            var report = new DiscoveryReport
            {
                ModelName = "test.db1",
                ModelPath = "C:\\models\\test",
                SourceVersion = "2025.0",
                Components = new List<ComponentSummary>
                {
                    new ComponentSummary { IdentifierId = 5245, Number = 146, Name = "Shear plate simple", Type = "Connection", PrimaryProfile = "HEA400", Exportable = true }
                }
            };
            var table = DiscoveryReportFormatter.ToTable(report);
            Assert.Contains("Shear plate simple", table);
            Assert.Contains("146", table);
            var json = DiscoveryReportFormatter.ToJson(report);
            Assert.Contains("\"number\": 146", json);
            Assert.Contains("\"components\"", json);
        }
    }
}
