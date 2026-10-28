// NUMBERING / MARKING CORE (P1) — cekirdek test (OCC'siz, Qt'siz).
//
// Dogrular: P1,P2.. / A1,A2.. bicimi; ayni imza = ayni mark; donusum (oteleme/dondurme)
// bagimsizligi; farkli uzunluk/profil/malzeme = farkli mark; EntityId ile karismama;
// emekli numara yeniden kullanilmaz; JSON gidis-donus (Turkce) ve bozuk kayit; assembly
// imzasi; ozel biçim.

#include "model_maker/marking.hpp"

#include <cmath>
#include <cstdio>

using namespace mm;

static int failures = 0;
static void check(bool ok, const char* label) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", label);
    if (!ok) ++failures;
}

static WireframeModel makeBox(Vec3 lo, Vec3 hi, const char* profile = "IPE300",
                              const char* material = "S355", bool withAxis = true) {
    std::vector<Vec3> v;
    for (int i = 0; i < 8; ++i)
        v.push_back({(i & 1) ? hi.x : lo.x, (i & 2) ? hi.y : lo.y, (i & 4) ? hi.z : lo.z});
    std::vector<Edge> e;
    for (std::size_t i = 0; i < 8; ++i)
        for (std::size_t b = 1; b < 8; b <<= 1)
            if (!(i & b)) e.push_back({i, i | b});
    std::vector<Face> f{{0, 2, 6, 4}, {1, 3, 7, 5}, {0, 1, 5, 4}, {2, 3, 7, 6}, {0, 1, 3, 2}, {4, 5, 7, 6}};
    WireframeModel m(std::move(v), std::move(e), std::move(f));
    EntityProperties p;
    p.profileName = profile;
    p.material = material;
    if (withAxis) {
        p.axisFromX = lo.x; p.axisFromY = (lo.y + hi.y) / 2; p.axisFromZ = (lo.z + hi.z) / 2;
        p.axisToX = hi.x;   p.axisToY = (lo.y + hi.y) / 2;   p.axisToZ = (lo.z + hi.z) / 2;
    }
    m.setProperties(p);
    return m;
}

int main() {
    // --- bicim + gruplama ---------------------------------------------------------
    MarkRegistry reg;
    const auto beamA = makeBox({0, 0, 0}, {6000, 150, 300});
    const auto beamB = makeBox({1000, 5000, 200}, {7000, 5150, 500}); // otelenmis kopya
    const auto longer = makeBox({0, 0, 0}, {6500, 150, 300});
    const auto otherProfile = makeBox({0, 0, 0}, {6000, 150, 300}, "HEA300");
    const auto otherMaterial = makeBox({0, 0, 0}, {6000, 150, 300}, "IPE300", "S235");

    const auto r1 = reg.markPart(EntityId{10}, partSignatureOf(beamA));
    check(r1.isNew && r1.mark.text() == L"P1" && r1.mark.kind == MarkKind::Part, "ilk parca P1");
    const auto r2 = reg.markPart(EntityId{11}, partSignatureOf(beamB));
    check(!r2.isNew && r2.mark.text() == L"P1", "otelenmis ayni parca AYNI mark (P1)");
    check(reg.markPart(EntityId{12}, partSignatureOf(longer)).mark.text() == L"P2",
          "farkli uzunluk -> yeni numara P2");
    check(reg.markPart(EntityId{13}, partSignatureOf(otherProfile)).mark.text() == L"P3",
          "farkli profil -> P3");
    check(reg.markPart(EntityId{14}, partSignatureOf(otherMaterial)).mark.text() == L"P4",
          "farkli malzeme -> P4");
    check(reg.partMarkCount() == 4 && reg.assignmentCount() == 5, "4 mark, 5 varlik atamasi");

    // Dondurme (Z 90 derece) + oteleme + AYNA: imza degismez (eksen yok -> en uzun kenar).
    {
        auto a = makeBox({0, 0, 0}, {6000, 150, 300}, "IPE300", "S355", false);
        auto b = a;
        b.rotateAroundZ({0, 0, 0}, 1.5707963267948966);
        b.translate({123.4, -987.6, 55.5});
        check(partSignatureOf(a) == partSignatureOf(b), "dondurme+oteleme imzayi degistirmez");
        auto c = a;
        c.affineAbout({0, 0, 0}, {-1, 0, 0, 0, 1, 0, 0, 0, 1}); // X aynasi
        check(partSignatureOf(a) == partSignatureOf(c), "ayna imzayi degistirmez");
    }

    // EntityId'den bagimsiz kullanici mark'i: id degeri mark numarasini belirlemez.
    {
        MarkRegistry r;
        check(r.markPart(EntityId{987654321}, partSignatureOf(beamA)).mark.text() == L"P1",
              "buyuk EntityId yine de P1 (mark != EntityId)");
        check(!r.markPart(kInvalidEntityId, partSignatureOf(beamA)).mark.valid(),
              "gecersiz EntityId mark almaz");
        check(r.markOf(EntityId{5}) == std::nullopt, "atanmamis varlik -> nullopt");
    }

    // Uyeler / imza erisimi.
    {
        const auto m = *reg.markOf(EntityId{10});
        const auto members = reg.membersOf(m);
        check(members.size() == 2 && members[0] == 10 && members[1] == 11, "P1 uyeleri: 10, 11");
        // 2026-10-28: uzunluk artik 1 mm biriminde (0.1 mm yuvarlamasi ayni
        // olculu parcalari farkli mark'a dusuruyordu — gercek modelde olculdu).
        check(reg.partSignatureFor(m) != nullptr && reg.partSignatureFor(m)->length == 6000,
              "P1 imzasi: uzunluk 6000 mm (1 mm biriminde 6000)");
        check(reg.assemblySignatureFor(m) == nullptr, "part mark'in assembly imzasi yok");
    }

    // Tasima (changed) ve forget: numara bir imzaya kalici baglidir.
    {
        const auto mv = reg.markPart(EntityId{11}, partSignatureOf(longer));
        check(mv.changed && !mv.isNew && mv.mark.text() == L"P2", "varlik baska imzaya tasindi (changed)");
        check(reg.membersOf(Mark{MarkKind::Part, 1, L"P"}).size() == 1, "P1'de tek uye kaldi");
        check(reg.forget(EntityId{10}) && !reg.markOf(EntityId{10}).has_value(), "forget baglantiyi siler");
        check(!reg.forget(EntityId{10}), "ikinci forget false");
        const auto back = reg.markPart(EntityId{20}, partSignatureOf(beamA));
        check(!back.isNew && back.mark.text() == L"P1", "ayni imza geri gelince AYNI P1 (emekli numara imzaya bagli)");
        const auto fresh = reg.markPart(EntityId{21}, partSignatureOf(makeBox({0, 0, 0}, {1234, 50, 50})));
        check(fresh.isNew && fresh.mark.text() == L"P5", "yeni imza P5 (numaralar yeniden kullanilmaz)");
    }

    // --- assembly imzasi ----------------------------------------------------------
    {
        MarkRegistry r;
        const int p1 = r.markPart(EntityId{1}, partSignatureOf(beamA)).mark.number;
        const int p2 = r.markPart(EntityId{2}, partSignatureOf(makeBox({0, 0, 0}, {200, 150, 12}, "PL12"))).mark.number;
        auto sig = [&](std::vector<Vec3> centres) {
            AssemblySignature s;
            s.mainPartMark = p1;
            s.memberPartMarks = {p2, p1, p2}; // sirasiz verilir, kanonik siralanir
            s.layoutHash = layoutHashOf(centres);
            return s;
        };
        const std::vector<Vec3> layout{{0, 0, 0}, {3000, 0, 0}, {6000, 0, 0}};
        std::vector<Vec3> moved;
        for (const Vec3& v : layout) moved.push_back({v.y + 77, v.x + 5, v.z - 9}); // x<->y + otele
        const auto a1 = r.markAssembly(EntityId{100}, sig(layout));
        check(a1.isNew && a1.mark.text() == L"A1" && a1.mark.kind == MarkKind::Assembly, "ilk assembly A1");
        check(r.markAssembly(EntityId{101}, sig(moved)).mark.text() == L"A1",
              "dondurulmus/otelenmis ayni yerlesim AYNI A1");
        const std::vector<Vec3> other{{0, 0, 0}, {1000, 0, 0}, {6000, 0, 0}};
        check(r.markAssembly(EntityId{102}, sig(other)).mark.text() == L"A2", "farkli yerlesim -> A2");
        check(r.assemblyMarkCount() == 2 && r.assemblySignatureFor(*r.markOf(EntityId{100})) != nullptr &&
                  r.assemblySignatureFor(*r.markOf(EntityId{100}))->memberPartMarks.size() == 3,
              "assembly imzasi saklanir (uyeler sirali, tekrarli)");
        // part ve assembly numara uzaylari ayri: A1 ve P1 birlikte var olabilir.
        check(r.markOf(EntityId{1})->text() == L"P1" && r.markOf(EntityId{100})->text() == L"A1",
              "P1 ve A1 bagimsiz sayaclar");
    }

    // --- ozel bicim ---------------------------------------------------------------
    {
        MarkFormat f;
        f.partPrefix = L"POZ-";
        f.partStart = 10;
        f.assemblyPrefix = L"MNT";
        f.assemblyStart = 100;
        MarkRegistry r(f);
        check(r.markPart(EntityId{1}, partSignatureOf(beamA)).mark.text() == L"POZ-10", "ozel on ek + baslangic");
        AssemblySignature s;
        s.mainPartMark = 10;
        s.memberPartMarks = {10};
        check(r.markAssembly(EntityId{2}, s).mark.text() == L"MNT100", "ozel assembly biçimi");
    }

    // --- JSON gidis-donus + bozuk kayit -------------------------------------------
    {
        MarkFormat f;
        f.partPrefix = L"P\u00d6Z"; // Turkce karakter
        MarkRegistry r(f);
        r.markPart(EntityId{1}, partSignatureOf(makeBox({0, 0, 0}, {10, 10, 10}, "K\u0130R\u0130\u015e")));
        r.markPart(EntityId{2}, partSignatureOf(beamA));
        r.markPart(EntityId{3}, partSignatureOf(beamB));
        AssemblySignature s;
        s.mainPartMark = 2;
        s.memberPartMarks = {1, 2};
        s.layoutHash = 0xFEDCBA9876543210ULL; // 64-bit tam deger korunmali
        r.markAssembly(EntityId{50}, s);

        const std::string json = r.serialize();
        MarkRegistry back;
        std::string err;
        check(MarkRegistry::deserialize(json, back, err) && err.empty(), "deserialize basarili");
        check(back.serialize() == json, "gidis-donus birebir (serialize==serialize)");
        check(back.markOf(EntityId{1})->text() == L"P\u00d6Z1" && back.markOf(EntityId{3})->text() == L"P\u00d6Z2",
              "Turkce on ek ve atamalar korunur");
        check(back.assemblySignatureFor(*back.markOf(EntityId{50}))->layoutHash == 0xFEDCBA9876543210ULL,
              "64-bit hash kaybi yok");
        // Yuklenen kayit numaralamaya KALDIGI yerden devam eder.
        const auto next = back.markPart(EntityId{4}, partSignatureOf(makeBox({0, 0, 0}, {77, 7, 7})));
        check(next.isNew && next.mark.number == 3, "yuklemeden sonra numara 3'ten devam");
        check(back.markPart(EntityId{5}, partSignatureOf(beamA)).mark.number == 2 &&
                  !back.markPart(EntityId{6}, partSignatureOf(beamA)).isNew,
              "yuklenen imzalar gruplamaya devam eder");

        MarkRegistry untouched;
        untouched.markPart(EntityId{9}, partSignatureOf(beamA));
        const std::string before = untouched.serialize();
        check(!MarkRegistry::deserialize("{ bozuk", untouched, err) && !err.empty() &&
                  untouched.serialize() == before,
              "bozuk JSON reddedilir, hedef DEGISMEZ");
        check(!MarkRegistry::deserialize("{\"version\":99}", untouched, err), "bilinmeyen surum reddedilir");
        std::string tampered = json;
        const auto pos = tampered.find("\"number\": 1");
        if (pos != std::string::npos) tampered.replace(pos, 11, "\"number\": 7");
        check(!MarkRegistry::deserialize(tampered, untouched, err) && untouched.serialize() == before,
              "tutarsiz numara (sayac disi / atama kopuk) reddedilir");
    }

    // --- POZ KIMLIGI REGRESYONU (2026-10-28, gercek modelde olculen iki hata) ---
    {
        // (a) AYNI olcu + kayan nokta gurultusu -> AYNI mark (eskiden 0.1 mm
        //     yuvarlamasi hash'i bozup ayni parcaya iki poz veriyordu: P8/P9).
        WireframeModel a = makeBox({0, 0, 0}, {6000, 150, 300});
        WireframeModel b = makeBox({0, 0, 0}, {6000.018, 150.021, 299.983});
        MarkRegistry r;
        const auto ma = r.markPart(1001, partSignatureOf(a));
        const auto mb = r.markPart(1002, partSignatureOf(b));
        check(ma.mark.number == mb.mark.number,
              "POZ: ayni olcu + 0.02 mm gurultu -> AYNI mark (gurultuye dayanikli)");

        // (b) 10 mm farkli olcu -> FARKLI mark (eskiden mozaik kenar hash'i
        //     yuzunden ayni poz veriliyordu: P19/P28).
        WireframeModel c = makeBox({0, 0, 0}, {6000, 160, 300});
        const auto mc = r.markPart(1003, partSignatureOf(c));
        check(mc.mark.number != ma.mark.number, "POZ: 10 mm farkli olcu -> FARKLI mark");

        // (c) Yogun mozaikli (cok kenarli) levhalar da olcuyle ayrilir.
        WireframeModel d = makeBox({0, 0, 0}, {58.8, 112.0, 152.0}, "");
        WireframeModel e = makeBox({0, 0, 0}, {58.8, 122.0, 152.0}, "");
        std::vector<Vec3> verts = d.vertices();
        std::vector<Edge> edges = d.edges();
        for (int k = 0; k < 200; ++k) {   // tessellation benzeri ek kenarlar
            const std::size_t i0 = static_cast<std::size_t>(k) % verts.size();
            const std::size_t i1 = (i0 + 3) % verts.size();
            edges.push_back({i0, i1});
        }
        WireframeModel d2(verts, edges);
        d2.setProperties(d.properties());
        std::vector<Vec3> verts2 = e.vertices();
        std::vector<Edge> edges2 = e.edges();
        for (int k = 0; k < 200; ++k) {
            const std::size_t i0 = static_cast<std::size_t>(k) % verts2.size();
            const std::size_t i1 = (i0 + 3) % verts2.size();
            edges2.push_back({i0, i1});
        }
        WireframeModel e2(verts2, edges2);
        e2.setProperties(e.properties());
        const auto md = r.markPart(1004, partSignatureOf(d2));
        const auto me = r.markPart(1005, partSignatureOf(e2));
        check(md.mark.number != me.mark.number,
              "POZ: mozaikli levhada 10 mm fark -> FARKLI mark (kenar hash'i cozumu degil)");
    }

    std::printf(failures ? "FAILURES: %d\n" : "ALL PASS\n", failures);
    return failures ? 1 : 0;
}
