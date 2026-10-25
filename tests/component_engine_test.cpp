// COMPONENT ENGINE (Phase C2) — cekirdek test.
//
// Tip parametre semasi (Length/Angle/Integer/Boolean/Enum/String/Profile/
// Material/EntityRef), dogrulama/migrasyon, registry/factory, uretim plani ve
// IKI yerlesik component (End Plate + Fin Plate) ayni altyapiyi yeniden
// kullaniyor. OCC'siz, Linux'ta kosar; saf veri + uretim plani.

#include "model_maker/component_engine.hpp"

#include <cmath>
#include <cstdio>

using namespace mm;

static int failures = 0;
static void check(bool ok, const char* label) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", label);
    if (!ok) ++failures;
}

static bool approx(double a, double b, double tol = 1e-9) {
    return std::abs(a - b) < tol;
}

int main() {
    // --- 1) Parametre varsayilan degerleri ----------------------------------
    {
        CompParamSpec len;
        len.name = L"t";
        len.type = CompParamType::Length;
        len.defDouble = 12.0;
        check(std::get<double>(paramDefaultValue(len)) == 12.0, "Length default");

        CompParamSpec n;
        n.name = L"n";
        n.type = CompParamType::Integer;
        n.defInt = 4;
        check(std::get<int>(paramDefaultValue(n)) == 4, "Integer default");

        CompParamSpec b;
        b.name = L"b";
        b.type = CompParamType::Boolean;
        b.defBool = true;
        check(std::get<bool>(paramDefaultValue(b)) == true, "Boolean default");

        CompParamSpec e;
        e.name = L"g";
        e.type = CompParamType::Enum;
        e.defString = L"8.8";
        check(std::get<std::wstring>(paramDefaultValue(e)) == L"8.8", "Enum default");

        CompParamSpec r;
        r.name = L"ref";
        r.type = CompParamType::EntityRef;
        check(std::get<EntityId>(paramDefaultValue(r)) == kInvalidEntityId,
              "EntityRef default = kInvalidEntityId");
    }

    // --- 2) validateParamValue: kelepceleme + zorlama + enum gecersiz ---------
    {
        CompParamSpec len;
        len.name = L"t";
        len.type = CompParamType::Length;
        len.defDouble = 12.0;
        len.hasMinDouble = true;
        len.hasMaxDouble = true;
        len.minDouble = 4.0;
        len.maxDouble = 100.0;
        check(std::get<double>(validateParamValue(len, CompParamValue{5.0})) == 5.0,
              "Length gecerli korunur");
        check(std::get<double>(validateParamValue(len, CompParamValue{999.0})) == 100.0,
              "Length ust kelepce");
        check(std::get<double>(validateParamValue(len, CompParamValue{0.0})) == 4.0,
              "Length alt kelepce");
        check(std::get<double>(validateParamValue(len, CompParamValue{7})) == 7.0,
              "Length int -> double zorlanir");

        CompParamSpec n;
        n.name = L"n";
        n.type = CompParamType::Integer;
        n.defInt = 2;
        n.hasMinInt = true;
        n.hasMaxInt = true;
        n.minInt = 1;
        n.maxInt = 8;
        check(std::get<int>(validateParamValue(n, CompParamValue{99})) == 8,
              "Integer ust kelepce");
        check(std::get<int>(validateParamValue(n, CompParamValue{0})) == 1,
              "Integer alt kelepce");
        check(std::get<int>(validateParamValue(n, CompParamValue{3.7})) == 4,
              "Integer double yuvarlanir");

        CompParamSpec en;
        en.name = L"g";
        en.type = CompParamType::Enum;
        en.defString = L"8.8";
        en.choices = {L"8.8", L"10.9"};
        check(std::get<std::wstring>(validateParamValue(en, CompParamValue{L"10.9"})) == L"10.9",
              "Enum gecerli korunur");
        check(std::get<std::wstring>(validateParamValue(en, CompParamValue{L"12.9"})) == L"8.8",
              "Enum gecersiz -> varsayilan");

        CompParamSpec er;
        er.name = L"ref";
        er.type = CompParamType::EntityRef;
        check(std::get<EntityId>(validateParamValue(er, CompParamValue{EntityId{42}})) == EntityId{42},
              "EntityRef korunur");
        check(std::get<EntityId>(validateParamValue(er, CompParamValue{5})) == kInvalidEntityId,
              "EntityRef tip uyusmaz -> gecersiz");
    }

    // --- 3) alignParamsToSpecs: eksik + bilinmeyen + zorlama -----------------
    {
        std::vector<CompParamSpec> specs;
        CompParamSpec a;
        a.name = L"a";
        a.type = CompParamType::Length;
        a.defDouble = 1.0;
        specs.push_back(a);
        CompParamSpec b;
        b.name = L"b";
        b.type = CompParamType::Integer;
        b.defInt = 2;
        specs.push_back(b);

        std::vector<CompParam> vals = {{L"b", CompParamValue{5}}, {L"unknown", CompParamValue{9.9}}};
        auto aligned = alignParamsToSpecs(specs, vals);

        // sirala: a (default), b (5), unknown (korunur).
        check(aligned.size() == 3, "align: 2 spec + 1 bilinmeyen");
        check(aligned[0].name == L"a" && std::get<double>(aligned[0].value) == 1.0,
              "align: eksik -> default");
        check(aligned[1].name == L"b" && std::get<int>(aligned[1].value) == 5,
              "align: gecerli korunur");
        check(aligned[2].name == L"unknown" && std::get<double>(aligned[2].value) == 9.9,
              "align: bilinmeyen KAYBEDILMEZ");
    }

    // --- 4) Registry / factory ------------------------------------------------
    {
        ComponentRegistry reg;
        check(reg.typeCount() == 0, "registry baslangic bos");

        registerBuiltInComponents(reg);
        check(reg.typeCount() == 3, "3 yerlesik component kaydedildi");

        const ComponentType* ep = reg.findType(L"beam_to_column_end_plate");
        check(ep != nullptr, "end plate tipi bulundu");
        check(ep != nullptr && ep->schemaVersion == 1, "end plate schemaVersion 1");
        check(reg.findType(L"fin_plate") != nullptr, "fin plate tipi bulundu");
        check(reg.findType(L"olmayan") == nullptr, "bilinmeyen tip -> null");
        check(reg.generator(L"fin_plate") != nullptr, "fin plate generator bulundu");

        auto ids = reg.typeIds();
        check(ids.size() == 3, "typeIds 3 adet");

        // tekil typeId reddi
        ComponentType dup = endPlateComponentType();
        check(!reg.registerType(dup, &generateEndPlate), "ayni typeId reddedilir");

        // gecersiz kayit
        ComponentType empty;
        check(!reg.registerType(empty, &generateEndPlate), "bos typeId reddedilir");
        check(!reg.registerType(endPlateComponentType(), nullptr), "null generator reddedilir");
    }

    // --- 5) End Plate uretimi (ortak cerceve yeniden kullanilir) -------------
    {
        ComponentRegistry reg;
        registerBuiltInComponents(reg);

        ComponentInput in;
        in.main.id = EntityId{10};  // kolon
        in.main.axisFrom = {0, 0, 0};
        in.main.axisTo = {0, 0, 3000};
        MemberGeometry beam;
        beam.id = EntityId{11};  // kiris
        beam.axisFrom = {0, 0, 3000};
        beam.axisTo = {0, 4000, 3000};  // +Y yonunde
        beam.refNormal = {0, 0, 1};
        beam.secWidth = 200.0;
        beam.secHeight = 300.0;
        in.secondary.push_back(beam);

        GenerationPlan plan = reg.generate(L"beam_to_column_end_plate", in);
        check(plan.ok, "end plate uretildi");
        check(plan.plates.size() == 1, "1 levha");
        check(plan.boltGroups.size() == 1, "1 bulon grubu");
        check(plan.cuts.empty(), "end plate kesme yok (bu dilim)");

        if (plan.plates.size() == 1) {
            const PlateSpec& p = plan.plates[0];
            check(approx(p.width, 200.0 + 40.0), "levha genislik = kesit + 2*pay");
            check(approx(p.height, 300.0 + 40.0), "levha yukseklik = kesit + 2*pay");
            check(approx(p.thickness, 12.0), "levha kalinlik default");
        }
        if (plan.boltGroups.size() == 1) {
            const BoltGroup& b = plan.boltGroups[0];
            check(b.countX == 2 && b.countY == 4, "end plate 2x4 bulon (default)");
            check(b.parts.size() == 1 && b.parts[0] == EntityId{10},
                  "bolonlar ana parca (kolon) referansli");
            check(approx(b.params.nominalDiameter, 20.0), "M20");
            check(approx(b.params.holeDiameter, 22.0), "delik 22");
        }
    }

    // --- 6) Fin Plate uretimi (ikinci component, AYNI altyapi) ---------------
    {
        ComponentRegistry reg;
        registerBuiltInComponents(reg);

        ComponentInput in;
        in.main.id = EntityId{20};
        in.main.axisFrom = {0, 0, 0};
        in.main.axisTo = {0, 0, 3000};
        MemberGeometry beam;
        beam.id = EntityId{21};
        beam.axisFrom = {0, 0, 3000};
        beam.axisTo = {0, 4000, 3000};
        beam.refNormal = {0, 0, 1};
        beam.secWidth = 200.0;
        beam.secHeight = 300.0;
        in.secondary.push_back(beam);
        // Parametre degistir: 3 bulon, 15 mm levha.
        in.params = {{L"plateThickness", CompParamValue{15.0}},
                     {L"boltCount", CompParamValue{3}}};

        GenerationPlan plan = reg.generate(L"fin_plate", in);
        check(plan.ok, "fin plate uretildi");
        check(plan.plates.size() == 1, "fin plate 1 levha");
        check(plan.boltGroups.size() == 1, "fin plate 1 bulon grubu");
        if (plan.plates.size() == 1) {
            check(approx(plan.plates[0].thickness, 15.0), "fin plate kalinlik parametreden");
        }
        if (plan.boltGroups.size() == 1) {
            const BoltGroup& b = plan.boltGroups[0];
            check(b.countX == 1, "fin plate tek sira");
            check(b.countY == 3, "fin plate 3 bulon (parametreden)");
            check(b.parts.size() == 1 && b.parts[0] == EntityId{21},
                  "fin plate bulonlari KIRISE referansli (kolona degil)");
        }
    }

    // --- 6b) Fin Plate: secim sirasi TERS (kiris once) -> roller duzeltilir ----
    {
        ComponentRegistry reg;
        registerBuiltInComponents(reg);

        // main = YATAY kiris (yanlis sirada secilmis), secondary = DUSEY kolon.
        ComponentInput in;
        in.main.id = EntityId{30};
        in.main.axisFrom = {0, 0, 1500};
        in.main.axisTo = {0, 4000, 1500};
        in.main.refNormal = {1, 0, 0};  // cizim duzleminden gelebilir
        in.main.secWidth = 100.0;
        in.main.secHeight = 200.0;
        MemberGeometry col;
        col.id = EntityId{31};
        col.axisFrom = {0, 0, 0};
        col.axisTo = {0, 0, 3000};
        col.refNormal = {0, 0, 1};
        col.secWidth = 300.0;
        col.secHeight = 300.0;
        in.secondary.push_back(col);

        GenerationPlan plan = reg.generate(L"fin_plate", in);
        check(plan.ok, "ters sira: fin plate uretildi");
        if (plan.ok && !plan.plates.empty()) {
            check(std::fabs(plan.plates[0].axis.z) < 1e-6,
                  "ters sira: levha yine DUSEY (roller duzeltildi)");
        }
        if (plan.ok && !plan.boltGroups.empty()) {
            const BoltGroup& b = plan.boltGroups[0];
            check(b.parts.size() == 1 && b.parts[0] == EntityId{30},
                  "ters sira: bulonlar YATAY uyeye (kiris) referansli");
        }
    }

    // --- 7) Bilinmeyen tip / eksik ikincil parca -----------------------------
    {
        ComponentRegistry reg;
        registerBuiltInComponents(reg);

        ComponentInput empty;
        GenerationPlan plan = reg.generate(L"olmayan_tip", empty);
        check(!plan.ok, "bilinmeyen tip -> ok=false");

        ComponentInput noSec;
        noSec.main.id = EntityId{30};
        GenerationPlan plan2 = reg.generate(L"beam_to_column_end_plate", noSec);
        check(!plan2.ok, "ikincil parca yok -> ok=false");
        check(!plan2.error.empty(), "hata mesaji dolu");
    }

    // --- 8) ComponentInstanceRecord kalici kayit -----------------------------
    {
        ComponentInstanceRecord rec;
        check(rec.id == kInvalidEntityId, "kayit id baslangic gecersiz");
        check(rec.schemaVersion == 1, "kayit schemaVersion 1");
        check(rec.dirty, "kayit baslangic kirli");
        check(!rec.orphan, "kayit baslangic oksuz degil");
        check(rec.mainPart == kInvalidEntityId, "kayit main baslangic gecersiz");
    }

    std::printf("FAILURES: %d\n", failures);
    return failures == 0 ? 0 : 1;
}
