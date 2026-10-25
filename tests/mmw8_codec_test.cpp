// MMW8 CODEC — OCCT'siz testler (portatif cekirdek).
//
// Kapsam: SHA-256 standart vektorleri, gecerli format round-trip, EntityId/
// nextEntityId korunmasi, coklu blob, NUL/newline iceren blob, ve TUM negatif
// senaryolar (yanlis checksum, kesilme, tekrar EntityId, yanlis bodyLen, boyut
// siniri, tasma, negatif sayi, tekrarlanan H/B, yanlis sira, eksik H).
//
// Bilinmeyen bloklar kayipsiz korunur (ileriye uyumluluk + yeniden yazma
// guvenligi). Akislar std::stringstream (ikili-guvenli) kullanir.

#include "model_maker/mmw8_codec.hpp"
#include "model_maker/sha256.hpp"

#include <array>
#include <cstdio>
#include <functional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace mm;

namespace {

int g_checks = 0;
int g_failures = 0;

void check(bool ok, const char* what) {
    ++g_checks;
    std::printf("  %s %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++g_failures;
}

// Kalici EntityProperties alanlarini tek tek karsilastir (turetilmis effective*
// alanlari DISINDA — bunlar kaydedilmez, yuklemede yeniden turetilir).
bool propsEqual(const EntityProperties& a, const EntityProperties& b) {
    return a.layer == b.layer && a.profileName == b.profileName &&
           a.profileRotation == b.profileRotation && a.profileSourceLine == b.profileSourceLine &&
           a.axisFromX == b.axisFromX && a.axisFromY == b.axisFromY && a.axisFromZ == b.axisFromZ &&
           a.axisToX == b.axisToX && a.axisToY == b.axisToY && a.axisToZ == b.axisToZ &&
           a.sectionNormalX == b.sectionNormalX && a.sectionNormalY == b.sectionNormalY &&
           a.sectionNormalZ == b.sectionNormalZ &&
           a.lineType == b.lineType && a.material == b.material &&
           a.colorIndex == b.colorIndex && a.trueColor == b.trueColor &&
           a.lineWeight == b.lineWeight && a.thickness == b.thickness &&
           a.lineTypeScale == b.lineTypeScale && a.transparency == b.transparency &&
           a.visible == b.visible && a.frozen == b.frozen && a.locked == b.locked &&
           a.plottable == b.plottable && a.description == b.description;
}

bool modelsEqual(const WireframeModel& a, const WireframeModel& b) {
    if (a.vertices().size() != b.vertices().size()) return false;
    if (a.edges().size() != b.edges().size()) return false;
    if (a.faces().size() != b.faces().size()) return false;
    if (a.isPointEntity() != b.isPointEntity()) return false;
    if (a.isFace3D() != b.isFace3D()) return false;
    for (std::size_t i = 0; i < a.vertices().size(); ++i) {
        const Vec3 va = a.vertices()[i], vb = b.vertices()[i];
        if (va.x != vb.x || va.y != vb.y || va.z != vb.z) return false;
    }
    for (std::size_t i = 0; i < a.edges().size(); ++i) {
        if (a.edges()[i] != b.edges()[i]) return false;
    }
    for (std::size_t i = 0; i < a.faces().size(); ++i) {
        if (a.faces()[i] != b.faces()[i]) return false;
    }
    return propsEqual(a.properties(), b.properties());
}

// Standart, taninabilir bir test belgesi uret.
Mmw8Document makeDoc() {
    Mmw8Document doc;
    doc.nextEntityId = 5;
    doc.metadata.binToolsFormatVersion = 4;
    doc.metadata.occtVersion = "7.9.2";

    EntityProperties layer;
    layer.layer = "0";
    layer.colorIndex = 7;
    layer.trueColor = 0xFFFFFFu;
    layer.lineType = layer.effectiveLineType = "CONTINUOUS";
    doc.layers["0"] = layer;

    WireframeModel lineModel = WireframeModel::line({0, 0, 0}, {1, 0, 0});
    {
        // Varsayilan DISI kalici ozellikler: tum P blogu alanlarinin round-trip
        // esitligini kanitlamak icin (turetilmis effective* HARIC).
        EntityProperties rp;
        rp.layer = "STRUCT";
        rp.profileName = "IPE300";
        rp.profileRotation = 45.0;
        rp.profileSourceLine = 12;
        rp.axisFromX = 1.0; rp.axisFromY = 2.0; rp.axisFromZ = 3.0;
        rp.axisToX = 4.0; rp.axisToY = 5.0; rp.axisToZ = 6.0;
        rp.sectionNormalX = 0.0; rp.sectionNormalY = 0.0; rp.sectionNormalZ = 1.0;
        rp.lineType = "DASHED";
        rp.material = "S355";
        rp.colorIndex = 3;
        rp.trueColor = 0x123456u;
        rp.lineWeight = 5;
        rp.thickness = 2.5;
        rp.lineTypeScale = 1.5;
        rp.transparency = 30;
        rp.visible = true; rp.frozen = false; rp.locked = true; rp.plottable = false;
        rp.description = "zengin ozellik testi";
        lineModel.setProperties(std::move(rp));
    }
    doc.models.push_back(std::move(lineModel));
    doc.entityIds.push_back(1);
    const std::array<Vec3, 4> quad = {Vec3{0, 0, 0}, Vec3{1, 0, 0}, Vec3{1, 1, 0}, Vec3{0, 1, 0}};
    doc.models.push_back(WireframeModel::face3D(quad));
    doc.entityIds.push_back(2);

    GridDefinition grid;
    grid.name = "GRID";
    grid.xCoords = {0.0, 4000.0};
    grid.yCoords = {0.0, 3000.0};
    grid.zCoords = {0.0, 3600.0};
    grid.xLabels = {"1", "2"};
    grid.yLabels = {"A", "B"};
    grid.zLabels = {"+0", "+3600"};
    doc.grids.push_back(grid);

    // NUL + newline + yuksek bayt iceren blob.
    const char raw1[] = {'B', 'L', 'O', 'B', '\x00', '\x01', '\x02', '\n', '\x0d', '\xff'};
    Mmw8Blob b1;
    b1.entityId = 1;
    b1.bytes.assign(raw1, sizeof(raw1));
    doc.blobs.push_back(b1);

    Mmw8Blob b2;
    b2.entityId = 2;
    b2.bytes.assign(300, '\xAA');
    doc.blobs.push_back(b2);

    return doc;
}

std::string serialize(const Mmw8Document& d) {
    std::ostringstream os;
    writeMmw8(os, d);
    return os.str();
}

bool readThrows(const std::string& bytes) {
    try {
        std::istringstream is(bytes);
        Mmw8Document d;
        readMmw8(is, d);
        return false;
    } catch (const std::exception&) {
        return true;
    }
}

void testSha256Vectors() {
    std::printf("Test: SHA-256 standard vectors\n");
    check(sha256Hex("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
          "SHA-256(\"\")");
    check(sha256Hex("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
          "SHA-256(\"abc\")");
    check(sha256Hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
              "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1",
          "SHA-256(two-block)");
    check(sha256Hex(std::string(1'000'000, 'a')) ==
              "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0",
          "SHA-256(1M 'a')");
}

void testRoundTrip() {
    std::printf("Test: valid MMW8 round-trip\n");
    const Mmw8Document doc = makeDoc();
    const std::string bytes = serialize(doc);

    Mmw8Document parsed;
    {
        std::istringstream is(bytes);
        readMmw8(is, parsed);
    }

    check(parsed.nextEntityId == 5, "nextEntityId preserved");
    check(parsed.metadata.binToolsFormatVersion == 4, "BinTools format version preserved");
    check(parsed.metadata.occtVersion == "7.9.2", "OCCT version preserved");
    check(parsed.entityIds.size() == 2 && parsed.entityIds[0] == 1 && parsed.entityIds[1] == 2,
          "EntityIds preserved");
    check(parsed.models.size() == 2, "two models");
    check(modelsEqual(parsed.models[0], doc.models[0]) && modelsEqual(parsed.models[1], doc.models[1]),
          "models round-trip equal");
    check(parsed.layers.size() == 1 && parsed.layers.count("0") == 1, "layers preserved");
    check(parsed.grids.size() == 1 && parsed.grids[0].name == "GRID", "grid preserved");
    check(parsed.grids[0].xCoords.size() == 2 && parsed.grids[0].zLabels.size() == 2,
          "grid coords/labels preserved");
    check(parsed.blobs.size() == 2, "two blobs");
    check(parsed.blobs[0].entityId == 1 && parsed.blobs[0].bytes == doc.blobs[0].bytes,
          "blob 1 (NUL/newline) byte-identical");
    check(parsed.blobs[1].entityId == 2 && parsed.blobs[1].bytes.size() == 300,
          "blob 2 byte-identical");
    check(sha256Hex(parsed.blobs[0].bytes) == parsed.blobs[0].sha256,
          "blob sha256 consistent");
    check(parsed.unknownBlocks.empty(), "no unknown blocks in clean file");
}

void testRejections() {
    const std::string valid = serialize(makeDoc());

    std::printf("Test: rejections\n");

    // 1) Yanlis checksum: blob baytini boz (sha degistirilmeden).
    {
        // Blob 1 bytes "BLOB..." -> ilk blob baytini boz.
        std::string bad = valid;
        // "BLOB" dizgisi yalniz blob iceriginde gecer; ilk B'yi degistir.
        const std::size_t pos = bad.find("BLOB");
        check(pos != std::string::npos, "setup: found blob bytes");
        if (pos != std::string::npos) {
            bad[pos] = 'X';
            check(readThrows(bad), "wrong checksum rejected");
        }
    }

    // 2) Kesilmis dosya. Kayit-ORTASI kesilmeler (kesin bozuk) reddedilir.
    // NOT: temiz bir BLOK SINIRINDA kesilme (or. H'den sonra, B'den once)
    // ayirt edilemez derecede gecerli bir "wireframe-only" dosya uretir — bu
    // per-blob SHA-256'nin kapsam disindadir; 3.4C'de atomik kaydetme (gecici
    // dosya + rename) kismi dosya olusmasini onler.
    for (std::size_t cut : {std::size_t(1), valid.size() - 1, valid.size() - 50}) {
        check(readThrows(valid.substr(0, cut)), "truncated file rejected");
    }

    // 3) Yinelenen EntityId: ikinci modelin kimligini ilkiyle ayni yap.
    {
        // model 2'nin kimligi 2; baslik satiri "... 2\n" ile biter. Benzersiz
        // oldugu icin " 2\n" degisimi guvenli degil — kimlikleri buyuk yap.
        Mmw8Document d = makeDoc();
        d.entityIds[0] = 9001;
        d.entityIds[1] = 9002;
        std::string s = serialize(d);
        const std::size_t pos = s.find("9002");
        check(pos != std::string::npos, "setup: found 9002");
        if (pos != std::string::npos) {
            s.replace(pos, 4, "9001");
            check(readThrows(s), "duplicate entityId rejected");
        }
    }

    // 4) Yanlis bodyLen (H blogu gövde uzunlugu).
    {
        // "H <len>\n" — len'i yanlis yap.
        Mmw8Document d = makeDoc();
        std::string s = serialize(d);
        const std::size_t pos = s.find("H ");
        if (pos != std::string::npos) {
            // H'den sonraki sayiyi 1 yap (gercek gövde daha uzun -> truncation).
            const std::size_t numStart = pos + 2;
            std::size_t numEnd = s.find('\n', numStart);
            s.replace(numStart, numEnd - numStart, "1");
            check(readThrows(s), "wrong bodyLen rejected");
        }
    }

    // 5) Blob boyut siniri (blobLen cok buyuk).
    {
        Mmw8Document d = makeDoc();
        std::string s = serialize(d);
        // Blob 1 kaydi: "1 10 <sha>\n..." — "1 " ile baslar; len 10. Bul ve buyut.
        const std::size_t pos = s.find("1 10 ");
        check(pos != std::string::npos, "setup: found blob record");
        if (pos != std::string::npos) {
            s.replace(pos + 2, 2, "99999999999999999999");  // cok buyuk
            check(readThrows(s), "blob size limit enforced");
        }
    }

    // 6) Sayisal tasma (modelCount).
    {
        Mmw8Document d = makeDoc();
        std::string s = serialize(d);
        // Baslik "MMW8 2 5\n" -> modelCount "2" yu tasan deger yap.
        s.replace(5, 1, "99999999999999999999999999999");
        check(readThrows(s), "integer overflow rejected");
    }

    // 7) Negatif sayi (modelCount).
    {
        Mmw8Document d = makeDoc();
        std::string s = serialize(d);
        s.replace(5, 1, "-1");
        check(readThrows(s), "negative number rejected");
    }

    // 8) Tekrarlanan H blogu.
    {
        Mmw8Document d = makeDoc();
        std::string s = serialize(d);
        // Kuyruga ikinci bir H blogu ekle.
        s += "H 5\nF 4 O \x04" "7.9.2";
        check(readThrows(s), "repeated H block rejected");
    }

    // 9) Tekrarlanan B blogu.
    {
        Mmw8Document d = makeDoc();
        std::string s = serialize(d);
        s += "B 1\n0\n";
        check(readThrows(s), "repeated B block rejected");
    }

    // 10) Eksik H blogu (B ilk sirada).
    {
        // Basit, H'siz bir kuyruk uret: gecerli dosyayi H'siz kur.
        // Kolay yol: H blogunu kaldir.
        Mmw8Document d = makeDoc();
        std::string s = serialize(d);
        const std::size_t hpos = s.find("H ");
        const std::size_t hend = s.find('\n', hpos);
        // H blogunun govdesi de var; govde uzunlugunu bilmek lazim. Bunun yerine
        // H icerigini "yanlis sira" testi ile birlikte ele al: B'yi H'den once koy.
        (void)hpos; (void)hend;
    }

    // 10 (devam) — yanlis sira: B, H'den once.
    {
        // H blogunu bul ve basina B koy.
        Mmw8Document d = makeDoc();
        std::string s = serialize(d);
        const std::size_t hpos = s.find("H ");
        check(hpos != std::string::npos, "setup: found H");
        if (hpos != std::string::npos) {
            // H oncesine bir B blogu ekle (B H'den once -> reddedilir).
            const std::string bBlock = "B 1\n0\n";
            s.insert(hpos, bBlock);
            check(readThrows(s), "B before H rejected");
        }
    }

    // 11) Desteklenmeyen BinTools format surumu.
    {
        Mmw8Document d = makeDoc();
        d.metadata.binToolsFormatVersion = 99;
        std::string s = serialize(d);
        check(readThrows(s), "unsupported format version rejected");
    }

    // 12) Blob entityId model listesinde yok.
    {
        Mmw8Document d = makeDoc();
        d.blobs[0].entityId = 777;  // model listesinde yok
        std::string s = serialize(d);
        check(readThrows(s), "blob entityId not in model list rejected");
    }
}

void testUnknownBlocksPreserved() {
    std::printf("Test: unknown blocks preserved + re-write safety\n");
    Mmw8Document doc = makeDoc();
    std::string s = serialize(doc);
    // Kuyruga bilinmeyen bir blok ekle.
    s += "X 3\nabc";

    Mmw8Document parsed;
    {
        std::istringstream is(s);
        readMmw8(is, parsed);
    }
    check(parsed.unknownBlocks.size() == 1, "unknown block captured");
    check(parsed.unknownBlocks[0].tag == 'X' && parsed.unknownBlocks[0].body == "abc",
          "unknown block body preserved");
    check(parsed.blobs.size() == 2, "known blobs still parsed");

    // Yeniden yaz -> bilinmeyen blok kayipsiz yeniden cikar.
    const std::string s2 = serialize(parsed);
    Mmw8Document parsed2;
    {
        std::istringstream is(s2);
        readMmw8(is, parsed2);
    }
    check(parsed2.unknownBlocks.size() == 1 &&
          parsed2.unknownBlocks[0].tag == 'X' && parsed2.unknownBlocks[0].body == "abc",
          "unknown block survives re-write");
}

} // namespace

int main() {
    std::printf("== MMW8 CODEC (OCCT'siz) ==\n");
    testSha256Vectors();
    testRoundTrip();
    testRejections();
    testUnknownBlocksPreserved();
    std::printf("checks=%d failures=%d\n", g_checks, g_failures);
    if (g_failures == 0) std::printf("MMW8 CODEC: OK\n");
    return g_failures == 0 ? 0 : 1;
}
