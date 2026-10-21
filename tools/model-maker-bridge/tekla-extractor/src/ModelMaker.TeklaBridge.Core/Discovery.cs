using System.Collections.Generic;
using System.Linq;
using System.Text;
using Newtonsoft.Json;

namespace ModelMaker.TeklaBridge.Core
{
    // COMPONENT DISCOVERY (Phase A) — Tekla'dan bagimsiz veri modeli.
    // Extractor modeldeki Connection/Detail/Component/CustomPart nesnelerini tarar,
    // bu ozeti doldurur; konsol tablosu ve JSON uretimi burada (test edilebilir).

    public class ComponentSummary
    {
        [JsonProperty("identifierId")] public int IdentifierId { get; set; }
        [JsonProperty("guid")] public string Guid { get; set; }
        [JsonProperty("number")] public int Number { get; set; }
        [JsonProperty("name")] public string Name { get; set; }
        [JsonProperty("type")] public string Type { get; set; }
        [JsonProperty("primaryProfile")] public string PrimaryProfile { get; set; }
        [JsonProperty("primaryMaterial")] public string PrimaryMaterial { get; set; }
        [JsonProperty("secondaryProfiles")] public List<string> SecondaryProfiles { get; set; } = new List<string>();
        [JsonProperty("childCounts")] public Dictionary<string, int> ChildCounts { get; set; } = new Dictionary<string, int>();
        [JsonProperty("exportable")] public bool Exportable { get; set; } = true;
        [JsonProperty("note")] public string Note { get; set; }
    }

    public class DiscoveryReport
    {
        [JsonProperty("modelName")] public string ModelName { get; set; }
        [JsonProperty("modelPath")] public string ModelPath { get; set; }
        [JsonProperty("sourceVersion")] public string SourceVersion { get; set; }
        [JsonProperty("components")] public List<ComponentSummary> Components { get; set; } = new List<ComponentSummary>();
    }

    public static class DiscoveryReportFormatter
    {
        // Okunabilir konsol tablosu.
        public static string ToTable(DiscoveryReport report)
        {
            var sb = new StringBuilder();
            sb.AppendLine("Model: " + (string.IsNullOrEmpty(report.ModelName) ? "(bilinmiyor)" : report.ModelName));
            sb.AppendLine("Path : " + (string.IsNullOrEmpty(report.ModelPath) ? "(bilinmiyor)" : report.ModelPath));
            sb.AppendLine("Tekla: " + (string.IsNullOrEmpty(report.SourceVersion) ? "(bilinmiyor)" : report.SourceVersion));
            sb.AppendLine();
            sb.AppendLine("ID      Num  Tur            Ad                          Primary         Uygunluk");
            sb.AppendLine("------  ---- -------------  --------------------------  --------------  --------");
            foreach (var c in report.Components)
            {
                var prim = string.IsNullOrEmpty(c.PrimaryProfile) ? "-" : c.PrimaryProfile;
                var ok = c.Exportable ? "export" : "SKIP(" + (c.Note ?? "?") + ")";
                sb.AppendLine(string.Format("{0,-6}  {1,-4} {2,-13}  {3,-26}  {4,-14}  {5}",
                    c.IdentifierId, c.Number, c.Type, Truncate(c.Name, 26), Truncate(prim, 14), ok));
            }
            return sb.ToString();
        }

        public static string ToJson(DiscoveryReport report)
        {
            return JsonConvert.SerializeObject(report, Formatting.Indented);
        }

        private static string Truncate(string s, int max)
        {
            if (string.IsNullOrEmpty(s)) return "-";
            return s.Length <= max ? s : s.Substring(0, max - 3) + "...";
        }
    }
}
