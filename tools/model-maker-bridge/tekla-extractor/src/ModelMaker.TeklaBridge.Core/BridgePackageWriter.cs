using System;
using System.Collections.Generic;
using System.IO;
using System.Text;
using Newtonsoft.Json;

namespace ModelMaker.TeklaBridge.Core
{
    // Bridge paketini disk'e yazar:
    //   manifest.json / component.json / parameters.json / objects.json /
    //   diagnostics.json / geometry/<goreli yol>.json
    // Dosya yollari paket kokune gore GORELIDIR. Giris paketindeki geometryFiles
    // anahtarlari da goreli yol olmalidir (yazici bunu denetlemez; Validator denetler).
    public static class BridgePackageWriter
    {
        private static readonly JsonSerializerSettings Settings = new JsonSerializerSettings
        {
            Formatting = Formatting.Indented,
            NullValueHandling = NullValueHandling.Ignore,
            DefaultValueHandling = DefaultValueHandling.Ignore,
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
        }

        private static void WriteJson(string path, object value)
        {
            var json = JsonConvert.SerializeObject(value, Settings);
            File.WriteAllText(path, json + "\n", new UTF8Encoding(false));
        }
    }
}
