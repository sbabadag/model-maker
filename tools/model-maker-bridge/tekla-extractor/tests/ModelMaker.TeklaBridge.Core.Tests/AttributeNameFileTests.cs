using System;
using System.IO;
using System.Linq;
using Xunit;

namespace ModelMaker.TeklaBridge.Core.Tests
{
    public class AttributeNameFileTests
    {
        [Fact]
        public void Parse_DeduplicatesAndPreservesOrder()
        {
            var result = AttributeNameFile.Parse(new[]
            {
                "# plain raw names only",
                "tpl1, e1;bolt_d",
                "// comment",
                "e1 t2",
                "   ",
                "_special"
            });
            Assert.Equal(new[] { "tpl1", "e1", "bolt_d", "t2", "_special" }, result);
        }

        [Theory]
        [InlineData("attribute(\"tpl1\", \"label\", distance)")]
        [InlineData("name-with-hyphen")]
        [InlineData("../unsafe")]
        [InlineData("invalid.name")]
        [InlineData("'not_a_name'")]
        public void Parse_RejectsInvalidOrInpSyntax(string line)
        {
            Assert.Throws<FormatException>(() => AttributeNameFile.Parse(new[] { line }));
        }

        [Fact]
        public void Parse_DoesNotInventNamesFromComments()
        {
            Assert.Empty(AttributeNameFile.Parse(new[] { "# a", "// b", " " }));
        }

        [Fact]
        public void Read_UsesActualUtf8File()
        {
            string path = Path.Combine(Path.GetTempPath(),
                "bridge-attributes-" + Guid.NewGuid().ToString("N") + ".txt");
            try
            {
                File.WriteAllText(path, "# raw Tekla names\nplate_x\nbolt_d,plate_x\n");
                Assert.Equal(new[] { "plate_x", "bolt_d" }, AttributeNameFile.Read(path));
            }
            finally
            {
                if (File.Exists(path)) File.Delete(path);
            }
        }

        [Fact]
        public void Read_MissingFileMustFail()
        {
            Assert.Throws<FileNotFoundException>(() =>
                AttributeNameFile.Read(Path.Combine(Path.GetTempPath(),
                    "no-such-bridge-attribute-file-" + Guid.NewGuid().ToString("N"))));
        }
    }
}
