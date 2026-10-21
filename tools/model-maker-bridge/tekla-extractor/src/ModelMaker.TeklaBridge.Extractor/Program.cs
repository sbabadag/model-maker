using System;
using System.Collections.Generic;
using System.Globalization;

namespace ModelMaker.TeklaBridge.Extractor
{
    internal static class Program
    {
        private static int Main(string[] args)
        {
            var opts = new ExtractorOptions();
            if (!ParseArgs(args, opts)) return 2;

            var res = TeklaExtractor.Extract(opts);
            if (!res.Ok)
            {
                Console.Error.WriteLine("HATA: " + res.Error);
                return 1;
            }

            if (opts.ListComponents)
            {
                Console.WriteLine(res.Report);
                return 0;
            }

            Console.WriteLine("TAMAM: " + res.OutputDir);
            return 0;
        }

        private static bool ParseArgs(string[] args, ExtractorOptions opts)
        {
            for (int i = 0; i < args.Length; i++)
            {
                var a = args[i];
                switch (a)
                {
                    case "-h":
                    case "--help":
                        PrintHelp();
                        return false;
                    case "list-components":
                        opts.ListComponents = true;
                        break;
                    case "--json":
                        opts.Json = true;
                        break;
                    case "--id":
                        if (i + 1 >= args.Length) { Console.Error.WriteLine("--id icin deger gerekli."); return false; }
                        if (!int.TryParse(args[++i], NumberStyles.Integer, CultureInfo.InvariantCulture, out int id)) { Console.Error.WriteLine("--id tamsayi olmali."); return false; }
                        opts.IdentifierId = id;
                        break;
                    case "--number":
                        if (i + 1 >= args.Length) { Console.Error.WriteLine("--number icin deger gerekli."); return false; }
                        if (!int.TryParse(args[++i], NumberStyles.Integer, CultureInfo.InvariantCulture, out int num)) { Console.Error.WriteLine("--number tamsayi olmali."); return false; }
                        opts.Number = num;
                        break;
                    case "-o":
                    case "--output":
                        if (i + 1 >= args.Length) { Console.Error.WriteLine("--output icin deger gerekli."); return false; }
                        opts.OutputDir = args[++i];
                        break;
                    case "-a":
                    case "--attributes":
                        if (i + 1 >= args.Length) { Console.Error.WriteLine("--attributes icin deger gerekli."); return false; }
                        foreach (var name in args[++i].Split(new[] { ',', ';', ' ' }, StringSplitOptions.RemoveEmptyEntries))
                            opts.AttributeNames.Add(name.Trim());
                        break;
                    default:
                        Console.Error.WriteLine("Bilinmeyen arguman: " + a);
                        return false;
                }
            }
            return true;
        }

        private static void PrintHelp()
        {
            Console.WriteLine("Model Maker Tekla Bridge — Extractor (salt okunur)");
            Console.WriteLine("");
            Console.WriteLine("Kullanim:");
            Console.WriteLine("  TeklaBridge.Extractor.exe list-components [--json]");
            Console.WriteLine("  TeklaBridge.Extractor.exe [--id <ID> | --number <N>] [--output <klasor>] [--attributes <ad1,ad2,...>]");
            Console.WriteLine("");
            Console.WriteLine("  Tekla acik ve model yukluyken calistirin.");
            Console.WriteLine("  list-components: modeldeki Connection/Detail/Component/CustomPart nesnelerini");
            Console.WriteLine("                   tarar; okunabilir tablo veya --json ile JSON uretir.");
            Console.WriteLine("  --id / --number: export edilecek bileseni Tekla ID veya component numarasiyla sec.");
            Console.WriteLine("                   (verilmezse secili bilesen kullanilir; ayni numarada birden");
            Console.WriteLine("                   fazla bilesen varsa --id ile ayristirin).");
            Console.WriteLine("  --attributes:    GetAttribute ile okunacak ham Tekla attribute adlari");
            Console.WriteLine("                   (virgul/space ile). Verilmezse parameters.json bos kalir.");
            Console.WriteLine("  --output:        paket klasoru (varsayilan: .\\model-maker-bridge-export)");
        }
    }
}
