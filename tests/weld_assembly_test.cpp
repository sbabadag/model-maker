// WELD + ASSEMBLY (Phase W1) — cekirdek kabul testleri (OCC'siz).
//
// Kabul kriterleri (gorev 10): A, B, C, D, E, I, J, K, L, O — Document duzeyinde.
// Secim modlari (F/G/H/P) Application/viewport katmaninda; burada assemblyMembers
// genisleme mantigi dogrulanir (GUI seciminin dayandigi cekirdek).

#include "model_maker/document.hpp"
#include "model_maker/weld.hpp"

#include <cstdio>
#include <filesystem>
#include <set>

using namespace mm;

static int failures = 0;
static void check(bool ok, const char* label) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", label);
    if (!ok) ++failures;
}

static EntityId addPart(Document& doc, double x) {
    doc.addLine(Vec3{x, 0.0, 0.0}, Vec3{x, 100.0, 0.0});
    return doc.modelId(doc.models().size() - 1);
}

static Weld makeWeld(EntityId mainP, EntityId secP, WeldClass cls = WeldClass::Shop) {
    Weld w;
    w.mainPart = mainP;
    w.secondaryPart = secP;
    w.weldClass = cls;
    w.type = WeldType::Fillet;
    w.size = 5.0;
    return w;
}

int main() {
    // A) Iki bagimsiz Part + Shop Weld -> tek Assembly.
    {
        Document doc;
        const EntityId p1 = addPart(doc, 0.0);
        const EntityId p2 = addPart(doc, 100.0);
        doc.addWeld(makeWeld(p1, p2));
        check(doc.welds().size() == 1, "A: weld olustu");
        check(doc.assemblies().size() == 1, "A: tek assembly");
        const auto& a = doc.assemblies()[0];
        check(a.parts.size() == 2, "A: assembly 2 parca");
        check(doc.assemblyIdContaining(p1) == a.id, "A: p1 assembly'de");
        check(doc.assemblyIdContaining(p2) == a.id, "A: p2 assembly'de");
    }

    // B) Assembly'deki ucuncu Part'a Shop Weld -> ayni Assembly'ye eklenir.
    {
        Document doc;
        const EntityId p1 = addPart(doc, 0.0);
        const EntityId p2 = addPart(doc, 100.0);
        const EntityId p3 = addPart(doc, 200.0);
        doc.addWeld(makeWeld(p1, p2));
        const EntityId a1 = doc.assemblies()[0].id;
        doc.addWeld(makeWeld(p2, p3));
        check(doc.assemblies().size() == 1, "B: hala tek assembly");
        check(doc.assemblies()[0].id == a1, "B: assembly kimligi korundu (merge yok)");
        check(doc.assemblies()[0].parts.size() == 3, "B: 3 parca");
        check(doc.assemblyIdContaining(p3) == a1, "B: p3 ayni assembly'de");
    }

    // C) Iki farkli Assembly arasinda Shop Weld -> guvenli merge.
    {
        Document doc;
        const EntityId p1 = addPart(doc, 0.0);
        const EntityId p2 = addPart(doc, 100.0);
        const EntityId p3 = addPart(doc, 200.0);
        const EntityId p4 = addPart(doc, 300.0);
        doc.addWeld(makeWeld(p1, p2));
        doc.addWeld(makeWeld(p3, p4));
        check(doc.assemblies().size() == 2, "C: baslangicta 2 assembly");
        doc.addWeld(makeWeld(p2, p3));  // kopru
        check(doc.assemblies().size() == 1, "C: merge sonrasi tek assembly");
        check(doc.assemblies()[0].parts.size() == 4, "C: 4 parca birlesti");
        check(doc.assemblyIdContaining(p1) == doc.assemblies()[0].id, "C: p1 birlesti");
        check(doc.assemblyIdContaining(p4) == doc.assemblies()[0].id, "C: p4 birlesti");
    }

    // D) Site Weld -> iki ayri Assembly korunur.
    {
        Document doc;
        const EntityId p1 = addPart(doc, 0.0);
        const EntityId p2 = addPart(doc, 100.0);
        const EntityId p3 = addPart(doc, 200.0);
        const EntityId p4 = addPart(doc, 300.0);
        doc.addWeld(makeWeld(p1, p2, WeldClass::Shop));
        doc.addWeld(makeWeld(p3, p4, WeldClass::Shop));
        check(doc.assemblies().size() == 2, "D: iki assembly");
        doc.addWeld(makeWeld(p2, p3, WeldClass::Site));  // site weld kopru
        check(doc.assemblies().size() == 2, "D: site weld birlestirmedi");
        check(doc.welds().size() == 3, "D: site weld kayitli");
        check(doc.assemblyIdContaining(p2) != doc.assemblyIdContaining(p3), "D: p2/p3 farkli assembly");
    }

    // E) Iki parcayi yalniz Bolt ile bagla -> ayri Assembly kalir.
    {
        Document doc;
        const EntityId p1 = addPart(doc, 0.0);
        const EntityId p2 = addPart(doc, 100.0);
        BoltGroup bolt;
        bolt.parts = {p1, p2};
        bolt.origin = Vec3{50.0, 0.0, 0.0};
        bolt.axis = Vec3{0.0, 0.0, 1.0};
        bolt.xDir = Vec3{1.0, 0.0, 0.0};
        WireframeModel display;
        doc.addBoltGroup(bolt, display);
        check(doc.boltGroups().size() == 1, "E: bolt olustu");
        check(doc.assemblies().empty(), "E: bolt assembly olusturmadi");
    }

    // I) Weld deletion -> iliskiler guncellenir, gerektiginde split.
    {
        Document doc;
        const EntityId p1 = addPart(doc, 0.0);
        const EntityId p2 = addPart(doc, 100.0);
        const EntityId p3 = addPart(doc, 200.0);
        doc.addWeld(makeWeld(p1, p2));  // A001: {p1,p2}
        doc.addWeld(makeWeld(p2, p3));  // A001: {p1,p2,p3}
        check(doc.assemblies().size() == 1, "I: tek assembly");
        // p2'yi koparan iki weld'i de sil -> p2 yalniz kalir; p1 ve p3 kopmus grup.
        // p1-p2 weld'ini sil: p2 hala p3'e bagli -> {p1} ayrilir, {p2,p3} kalir.
        doc.removeWeld(0);  // p1-p2
        check(doc.assemblies().size() == 1, "I: split -> {p2,p3} kalir (yalniz parca assembly degil)");
        check(!doc.assemblyIdContaining(p1).has_value(), "I: p1 yalniz kaldi (assembly yok)");
        check(doc.assemblyIdContaining(p2) == doc.assemblyIdContaining(p3), "I: p2/p3 birlikte");
        // Kalan weld (p2-p3) silinirse p2 ve p3 de ayrilir -> 3 ayri assembly yok:
        // parcalar yalniz oldugu icin assembly YOK (yalniz parca assembly degildir).
        doc.removeWeld(0);  // p2-p3 (indeks kaydi)
        check(doc.assemblies().empty(), "I: tum weld'ler silindi -> assembly yok");
    }

    // J) Undo/Redo -> Weld ve Assembly grafiği geri yuklenir.
    {
        Document doc;
        const EntityId p1 = addPart(doc, 0.0);
        const EntityId p2 = addPart(doc, 100.0);
        const EntityId p3 = addPart(doc, 200.0);
        doc.pushSnapshot();
        doc.addWeld(makeWeld(p1, p2));
        check(doc.assemblies().size() == 1, "J: assembly var");
        doc.pushSnapshot();
        doc.addWeld(makeWeld(p2, p3));
        check(doc.assemblies()[0].parts.size() == 3, "J: 3 parca");
        doc.undo();
        check(doc.welds().size() == 1, "J: undo weld silinmesi");
        check(doc.assemblies().size() == 1, "J: undo assembly");
        check(doc.assemblies()[0].parts.size() == 2, "J: undo 2 parca");
        doc.redo();
        check(doc.welds().size() == 2, "J: redo weld geri");
        check(doc.assemblies()[0].parts.size() == 3, "J: redo 3 parca");
    }

    // L) Main Part degisikligi -> geometri ve EntityId korunur.
    {
        Document doc;
        const EntityId p1 = addPart(doc, 0.0);
        const EntityId p2 = addPart(doc, 100.0);
        doc.addWeld(makeWeld(p1, p2));
        const EntityId asmId = doc.assemblies()[0].id;
        check(doc.setAssemblyMainPart(asmId, p2), "L: main part degisti");
        check(doc.assemblies()[0].mainPart == p2, "L: yeni main part");
        check(doc.assemblies()[0].id == asmId, "L: assembly EntityId korundu");
        check(doc.assemblies()[0].parts.size() == 2, "L: parcalar korundu");
        // Gecersiz main part (uye olmayan) reddedilir.
        const EntityId pX = addPart(doc, 500.0);
        check(!doc.setAssemblyMainPart(asmId, pX), "L: uye olmayan main part reddedildi");
    }

    // O) Gecersiz Part secimi -> islem guvenli iptal (kendine kaynak reddedilir,
    //    gecersiz id grafe girmez).
    {
        Document doc;
        const EntityId p1 = addPart(doc, 0.0);
        Weld bad = makeWeld(p1, p1);  // ayni parca iki kez
        doc.addWeld(bad);
        check(doc.welds().size() == 1, "O: kendine kaynak kaydi (kayit var)");
        check(doc.assemblies().empty(), "O: kendine kaynak assembly kurmadi");
        // Gecersiz EntityId uc -> grafe girmez.
        Weld bad2 = makeWeld(p1, kInvalidEntityId);
        doc.addWeld(bad2);
        check(doc.assemblies().empty(), "O: gecersiz uc assembly kurmadi");
    }

    // K) MMW8 save/load -> Assembly uyelikleri ayni kalir.
    {
        Document doc;
        const EntityId p1 = addPart(doc, 0.0);
        const EntityId p2 = addPart(doc, 100.0);
        const EntityId p3 = addPart(doc, 200.0);
        doc.addWeld(makeWeld(p1, p2));
        doc.addWeld(makeWeld(p2, p3));
        const auto before = doc.assemblies();
        const EntityId asmId = before[0].id;
        const std::filesystem::path tmp =
            std::filesystem::temp_directory_path() / "weld_asm_test.mmw8";
        doc.save(tmp);

        Document doc2;
        doc2.load(tmp);
        check(doc2.welds().size() == 2, "K: weld sayisi korundu");
        check(doc2.assemblies().size() == 1, "K: assembly sayisi korundu");
        check(doc2.assemblies()[0].id == asmId, "K: assembly EntityId korundu");
        check(doc2.assemblies()[0].parts.size() == 3, "K: 3 parca korundu");
        check(doc2.assemblyIdContaining(p1) == asmId, "K: uyelik p1");
        check(doc2.assemblyIdContaining(p3) == asmId, "K: uyelik p3");
        std::filesystem::remove(tmp);
    }

    // assemblyMembers genisleme (secim modunun dayandigi cekirdek).
    {
        Document doc;
        const EntityId p1 = addPart(doc, 0.0);
        const EntityId p2 = addPart(doc, 100.0);
        const EntityId p3 = addPart(doc, 200.0);
        doc.addWeld(makeWeld(p1, p2));
        doc.addWeld(makeWeld(p2, p3));
        const EntityId weldId = doc.welds()[0].id;
        const auto members = doc.assemblyMembers(p1);
        check(members.size() == 5, "members: 3 parca + 2 weld");
        std::set<EntityId> s(members.begin(), members.end());
        check(s.count(p1) && s.count(p2) && s.count(p3), "members: parcalar");
        check(s.count(weldId), "members: weld id dahil");
        // Assembly disi parca -> yalniz kendisi.
        const EntityId pLone = addPart(doc, 500.0);
        const auto lone = doc.assemblyMembers(pLone);
        check(lone.size() == 1 && lone[0] == pLone, "members: yalniz parca kendisi");
    }

    std::printf("%s\n", failures == 0 ? "ALL PASS" : "SOME FAILED");
    return failures == 0 ? 0 : 1;
}
