using System;
using System.Collections.Generic;
using System.IO;
using System.Security.Cryptography;
using System.Text;
using Newtonsoft.Json;

namespace ModelMaker.TeklaBridge.Core
{
    // Bridge paketini disk'e yazar:
    //   manifest.json / component.json / parameters.json / objects.json /
    //   relationships.json / operations.json / diagnostics.json /
    //   checksums.json (V3: goreli yol -> SHA-256) / geometry/<goreli yol>.json
    // Dosya yollari paket kokune gore GORELIDIR. Giris paketindeki geometryFiles
    // anahtarlari da goreli yol olmalidir (yazici bunu denetlemez; Validator denetler).
    public static class BridgePackageWriter
    {
        private static readonly JsonSerializerSettings Settings = new JsonSerializerSettings
        {
            Formatting = Formatting.Indented,
            NullValueHandling = NullValueHandling.Ignore,
            // NOT: DefaultValueHandling.Ignore kullanma — 0.0 mesru bir koordinattir
            // (ornegin bolt X=0.0); atlanirsa JSON'da koordinat kaybolur.
        };

        public static void Write(BridgePackage package,
                                 IReadOnlyDictionary<string, GeometryFile> geometryFiles,
                                 string outputDir)
        {
            if (package == null) throw new ArgumentNullException(nameof(package));
            Directory.CreateDirectory(outputDir);

            WriteJson(Path.Combine(outputDir, "manifest.json"), package.Manifest);
            WriteJson(Path.Combine(outputDir, "component.json"), package.Component);
            WriteJson(Path.Combine(outputDir, "parameters.json"), package.Parameters);
            WriteJson(Path.Combine(outputDir, "objects.json"), package.Objects);
            WriteJson(Path.Combine(outputDir, "diagnostics.json"), package.Diagnostics);

            // V3: relationships/operations (varsa yazilir; V1/V2 bos -> atlanir).
            if (package.Relationships != null && package.Relationships.Count > 0)
                WriteJson(Path.Combine(outputDir, "relationships.json"), package.Relationships);
            if (package.Operations != null && package.Operations.Count > 0)
                WriteJson(Path.Combine(outputDir, "operations.json"), package.Operations);

            if (geometryFiles != null)
            {
                foreach (var kv in geometryFiles)
                {
                    var rel = kv.Key.Replace('\\', '/');
                    var target = Path.Combine(outputDir, rel);
                    var dir = Path.GetDirectoryName(target);
                    if (!string.IsNullOrEmpty(dir)) Directory.CreateDirectory(dir);
                    WriteJson(target, kv.Value);
                }
            }

            // V3: checksums.json (paket icindeki tum dosyalarin SHA-256'si).
            if (package.Manifest != null && package.Manifest.FormatVersion == "3.0")
                WriteChecksums(outputDir);
        }

        private static void WriteChecksums(string outputDir)
        {
            var checksums = new SortedDictionary<string, string>(StringComparer.Ordinal);
            foreach (var f in Directory.EnumerateFiles(outputDir, "*", SearchOption.AllDirectories))
            {
                var rel = f.Substring(outputDir.Length).TrimStart('\\', '/').Replace('\\', '/');
                if (rel == "checksums.json") continue;
                using (var sha = SHA256.Create())
                using (var fs = File.OpenRead(f))
                {
                    var hash = sha.ComputeHash(fs);
                    checksums[rel] = ToHex(hash);
                }
            }
            WriteJson(Path.Combine(outputDir, "checksums.json"), checksums);
        }

        private static string ToHex(byte[] bytes)
        {
            var sb = new StringBuilder(bytes.Length * 2);
            foreach (var b in bytes) sb.Append(b.ToString("x2"));
            return sb.ToString();
        }

        private static void WriteJson(string path, object value)
        {
            var json = JsonConvert.SerializeObject(value, Settings);
            File.WriteAllText(path, json + "\n", new UTF8Encoding(false));
        }
    }
}
