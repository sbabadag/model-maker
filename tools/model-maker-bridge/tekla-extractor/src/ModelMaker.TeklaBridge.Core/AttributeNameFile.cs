using System;
using System.Collections.Generic;
using System.IO;

namespace ModelMaker.TeklaBridge.Core
{
    // Candidate raw attribute names only. This is NOT a parser for Tekla .inp.
    // Never invent values: TeklaExtractor still queries each candidate by
    // GetAttribute and marks unsupported/unreadable names explicitly.
    public static class AttributeNameFile
    {
        public static List<string> Read(string path)
        {
            if (string.IsNullOrWhiteSpace(path))
                throw new ArgumentException("Attribute file path is empty.", nameof(path));
            return Parse(File.ReadAllLines(path));
        }

        public static List<string> Parse(IEnumerable<string> lines)
        {
            if (lines == null) throw new ArgumentNullException(nameof(lines));
            var names = new List<string>();
            var seen = new HashSet<string>(StringComparer.Ordinal);
            int lineNumber = 0;
            foreach (string raw in lines)
            {
                ++lineNumber;
                if (string.IsNullOrWhiteSpace(raw)) continue;
                string line = raw.Trim();
                if (line.StartsWith("#", StringComparison.Ordinal) ||
                    line.StartsWith("//", StringComparison.Ordinal))
                    continue;

                // Only simple raw Tekla attribute identifiers are accepted.
                // Require an explicit list, not an .inp file; .inp is a dialog
                // definition and can contain arbitrary quoted UI labels.
                if (line.IndexOf('(') >= 0 || line.IndexOf(')') >= 0 ||
                    line.IndexOf('"') >= 0 || line.IndexOf('\'') >= 0)
                    throw new FormatException("Invalid attribute list on line " + lineNumber +
                        ". Provide raw names, not .inp syntax.");

                foreach (string token in line.Split(
                    new[] { ',', ';', ' ', '\t' }, StringSplitOptions.RemoveEmptyEntries))
                {
                    if (!IsValidName(token))
                        throw new FormatException("Invalid attribute '" + token +
                            "' on line " + lineNumber + ".");
                    if (seen.Add(token)) names.Add(token);
                }
            }
            return names;
        }

        private static bool IsValidName(string s)
        {
            if (string.IsNullOrEmpty(s) || s.Length > 128) return false;
            if (!(IsAlpha(s[0]) || s[0] == '_')) return false;
            for (int i = 1; i < s.Length; ++i)
                if (!(IsAlpha(s[i]) || char.IsDigit(s[i]) || s[i] == '_'))
                    return false;
            return true;
        }

        private static bool IsAlpha(char c)
        {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
        }
    }
}
