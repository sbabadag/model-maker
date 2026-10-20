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
            Console.WriteLine("  TeklaBridge.Extractor.exe [--output <klasor>] [--attributes <ad1,ad2,...>]");
            Console.WriteLine("");
            Console.WriteLine("  Tekla acik ve model yukluyken, baglanti/component secili iken calistirin.");
            Console.WriteLine("  --attributes: GetAttribute ile okunacak ham Tekla attribute adlari");
            Console.WriteLine("                (virgul/space ile). Verilmezse parameters.json bos kalir.");
            Console.WriteLine("  --output:     paket klasoru (varsayilan: .\\model-maker-bridge-export)");
        }
    }
}
