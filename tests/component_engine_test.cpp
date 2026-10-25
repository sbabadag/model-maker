// COMPONENT ENGINE (Phase C2) — cekirdek test.
//
// Tip parametre semasi (Length/Angle/Integer/Boolean/Enum/String/Profile/
// Material/EntityRef), dogrulama/migrasyon, registry/factory, uretim plani ve
// IKI yerlesik component (End Plate + Fin Plate) ayni altyapiyi yeniden
// kullaniyor. OCC'siz, Linux'ta kosar; saf veri + uretim plani.

#include "model_maker/component_engine.hpp"
#include "model_maker/bolt.hpp"

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
        check(reg.typeCount() == 4, "4 yerlesik component kaydedildi");

        const ComponentType* ep = reg.findType(L"beam_to_column_end_plate");
        check(ep != nullptr, "end plate tipi bulundu");
        check(ep != nullptr && ep->schemaVersion == 1, "end plate schemaVersion 1");
        check(reg.findType(L"fin_plate") != nullptr, "fin plate tipi bulundu");
        check(reg.findType(L"olmayan") == nullptr, "bilinmeyen tip -> null");
        check(reg.generator(L"fin_plate") != nullptr, "fin plate generator bulundu");
        const ComponentType* h40=reg.findType(L"tekla_haunch_40");
        check(h40 && h40->catalog.catalogVisible, "Haunch 40 katalogda gorunur");
        check(h40 && h40->catalog.sourceComponentNumber==L"40", "Haunch 40 kaynak numarasi");

        auto ids = reg.typeIds();
        check(ids.size() == 4, "typeIds 4 adet");

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

    // --- Haunch 40: selected Model Maker sections, NOT source Tekla labels ---
    {
        ComponentRegistry reg;
        registerBuiltInComponents(reg);
        ComponentInput in;
        in.main.id=EntityId{40};
        in.main.axisFrom={0,0,0};
        in.main.axisTo={0,0,7200};
        in.main.refNormal={0,1,0};
        in.main.secWidth=300;in.main.secHeight=400;
        in.main.secWebThickness=10;in.main.secFlangeThickness=18;
        MemberGeometry beam;
        beam.id=EntityId{41};
        beam.axisFrom={0,2200,6000};
        beam.axisTo={0,100,6000};
        beam.refNormal={0,0,1};
        beam.secWidth=200;beam.secHeight=300;
        beam.secWebThickness=8;beam.secFlangeThickness=12;
        in.secondary={beam};

        GenerationPlan p=reg.generate(L"tekla_haunch_40",in);
        check(p.ok,"Haunch 40: independent native generator builds");
        check(p.plates.size()==4,"Haunch 40: end+web+bottom+top 4 plates");
        check(p.boltGroups.size()==2,"Haunch 40: two bolt groups");
        check(p.welds.size()==4,"Haunch 40: four weld relations");
        check(p.cuts.size()==1 && p.cutKeys.size()==1 &&
              p.cutKeys[0]=="haunch_beam_end_cut",
              "Haunch 40: one persistent beam end cut");
        if(p.cuts.size()==1 && p.plates.size()>=1) {
            const CutModifier& cut=p.cuts[0];
            const PlateSpec& ep=p.plates[0];
            const Vec3 outerFace=ep.center+ep.axis*(ep.thickness*0.5);
            const Vec3 d=cut.planeOrigin-outerFace;
            check(cut.target==EntityId{41} && cut.kind==CutKind::PlaneCut &&
                  cut.keepPositive && cut.hidden,
                  "Haunch 40: hidden square cut ONLY targets actual beam");
            // Plane coincidence, NOT point coincidence: the end plate
            // centre is shifted vertically by half the haunch depth,
            // while the beam cut's origin is on the beam centreline.
            // A tangent-plane shift is expected and must not fail.
            const double signedDistance =
                d.x*ep.axis.x+d.y*ep.axis.y+d.z*ep.axis.z;
            check(std::fabs(signedDistance)<1e-6,
                  "Haunch 40: cut on END PLATE beam-side face");
            check(approx(cut.planeNormal.x,ep.axis.x) &&
                  approx(cut.planeNormal.y,ep.axis.y) &&
                  approx(cut.planeNormal.z,ep.axis.z),
                  "Haunch 40: keeps steel away from column");
        }
        check(p.plateKeys.size()==p.plates.size(),"Haunch 40: stable plate role keys");
        if(p.plates.size()>=3) {
            check(approx(p.plates[0].thickness,20.0),
                  "Haunch 40: PL20 end plate");
            check(p.plates[1].chamfers.size()==1,
                  "Haunch 40: actual tapered web chamfer");
            check(approx(p.plates[1].width,450.0) &&
                  approx(p.plates[1].height,240.0),
                  "Haunch 40: 450x240 taper");
            check(p.plates[2].height>450.0 &&
                  p.plates[2].height<540.0,
                  "Haunch 40: inclined flange real length");
            // The materializer's chamfer-1 joins x=-L/2+25 at the
            // LOWER web edge to x=+L/2 at the (D-tip) height. Both
            // flange INNER-face endpoints must land on this SAME line.
            const PlateSpec& web=p.plates[1];
            const PlateSpec& flange=p.plates[2];
            const double chamferU=web.chamfers.front().h;
            const double chamferV=web.chamfers.front().v;
            const Vec3 near=web.center+
                web.u*(-web.width*0.5+web.width-chamferU)+
                web.v*(-web.height*0.5);
            const Vec3 far=web.center+
                web.u*(web.width*0.5)+
                web.v*(-web.height*0.5+chamferV);
            const Vec3 innerMid=flange.center-
                flange.axis*(flange.thickness*0.5);
            const Vec3 flangeNear=innerMid-
                flange.v*(flange.height*0.5);
            const Vec3 flangeFar=innerMid+
                flange.v*(flange.height*0.5);
            const auto gap3=[](Vec3 a,Vec3 b) {
                const Vec3 d=a-b;
                return std::sqrt(d.x*d.x+d.y*d.y+d.z*d.z);
            };
            check(gap3(flangeNear,near)<1e-5 &&
                  gap3(flangeFar,far)<1e-5,
                  "Haunch 40: inclined flange inner face exactly follows web chamfer");
            check(approx(flange.height,gap3(near,far),1e-5),
                  "Haunch 40: exact web chamfer angle and flange span");
            // Column's actual top is 7200mm, beam top is ~6150mm.
            // Cap must be horizontal and on COLUMN, never on beam.
            const PlateSpec& cap=p.plates[3];
            check(approx(cap.center.z,7200.0+cap.thickness*0.5,1e-6),
                  "Haunch 40: cap LOWER face rests at real column top Z=7200");
            check(approx(cap.axis.x,0) && approx(cap.axis.y,0) &&
                  approx(cap.axis.z,1),
                  "Haunch 40: cap plate horizontal with +Z thickness");
            check(approx(cap.width,300.0) &&
                  approx(cap.height,400.0),
                  "Haunch 40: cap EXACT 300x400 column profile footprint");
            check(p.welds.size()==4 &&
                  p.welds[3].partA==EntityId{40} &&
                  p.welds[3].partBRole=="haunch_top_plate",
                  "Haunch 40: cap is welded to column, not beam");
        }
        if(p.boltGroups.size()==2) {
            for(const auto& b:p.boltGroups) {
                check(b.countX==2 && b.countY==2,"Haunch 40: 2x2 pattern");
                check(b.parts.size()==1 && b.parts[0]==EntityId{40},
                      "Haunch 40: bolt through actual support");
                check(approx(b.params.holeDiameter,22),"Haunch 40: round Ø22");
            }
        }
        // HOTFIX: The 8 physical bolt centres MUST lie on the same end-plate
        // local frame, with a realistic end plate + column-flange stack.
        if (p.plates.size() >= 1 && p.boltGroups.size() == 2) {
            const PlateSpec& ep=p.plates[0];
            int boltCount=0;
            for (const auto& bg:p.boltGroups) {
                check(bg.targetOwnedPlateRole=="haunch_end_plate",
                      "Haunch 40: drills only owned end plate");
                check(approx(bg.gripLength, ep.thickness+in.main.secFlangeThickness),
                      "Haunch 40: short physical flange grip, not column width");
                check(approx(bg.holeDepth,bg.gripLength+2.0),
                      "Haunch 40: drill depth limits cuts to actual stack");
                check(bg.params.direction==2,
                      "Haunch 40: bolt heads on accessible beam face");
                check(approx(bg.axis.x,ep.axis.x) &&
                      approx(bg.axis.y,ep.axis.y) &&
                      approx(bg.axis.z,ep.axis.z),
                      "Haunch 40: bolt axis equals end-plate normal");
                const auto pts=boltGroupBoltOrigins(bg);
                boltCount+=static_cast<int>(pts.size());
                for(const Vec3& c:pts) {
                    const Vec3 d=c-ep.center;
                    const double axial=d.x*ep.axis.x+d.y*ep.axis.y+d.z*ep.axis.z;
                    const double x=d.x*ep.u.x+d.y*ep.u.y+d.z*ep.u.z;
                    const double y=d.x*ep.v.x+d.y*ep.v.y+d.z*ep.v.z;
                    check(approx(axial,-0.5*in.main.secFlangeThickness,0.001),
                          "Haunch 40: correct bolt grip midplane");
                    check(std::abs(x)+11.0<ep.width*0.5 &&
                          std::abs(y)+11.0<ep.height*0.5,
                          "Haunch 40: bolt hole fully inside end plate");
                }
            }
            check(boltCount==8,"Haunch 40: exactly 8 local-frame bolt centres");
        }
        // Section adaptation and editable param changes.
        in.secondary[0].secWidth=120;
        in.secondary[0].secHeight=240; // e.g. IPE240, source type irrelevant
        in.params={{L"haunchLength",CompParamValue{600.0}},
                   {L"haunchDepth",CompParamValue{300.0}},
                   {L"createBeamStiffener",CompParamValue{true}}};
        GenerationPlan adapt=reg.generate(L"tekla_haunch_40",in);
        check(adapt.ok,"Haunch 40: different target section accepted");
        check(adapt.plates.size()==5,"Haunch 40: optional stiffener created");
        if(adapt.plates.size()>=4) {
            check(approx(adapt.plates[1].width,600.0) &&
                  approx(adapt.plates[1].height,300.0),
                  "Haunch 40: editable length/depth recomputed");
            check(approx(adapt.plates[3].width,300.0) &&
                  approx(adapt.plates[3].height,400.0),
                  "Haunch 40: cap follows EXACT column section, not beam flange");
        }
        // Reversed member axis direction must not move the cap to the
        // physically LOWER endpoint of the column.
        {
            ComponentInput axisReversed=in;
            axisReversed.main.axisFrom={0,0,7200};
            axisReversed.main.axisTo={0,0,0};
            axisReversed.params.clear();
            const GenerationPlan revCol=reg.generate(L"tekla_haunch_40",axisReversed);
            check(revCol.ok,"Haunch 40: reversed column axis accepted");
            if(revCol.ok && revCol.plates.size()>=4) {
                check(approx(revCol.plates[3].center.z,7205.0,1e-6),
                      "Haunch 40: reversed column still gets top cap");
            }
        }
        // Slight roof slope: cap must remain horizontal to the actual
        // column end face rather than tilting with the I-beam flange.
        {
            ComponentInput sloped=in;
            sloped.params.clear();
            sloped.secondary[0].axisFrom={0,2200,6080};
            sloped.secondary[0].axisTo={0,100,6000};
            const GenerationPlan sp=reg.generate(L"tekla_haunch_40",sloped);
            check(sp.ok,"Haunch 40: sloped beam supported");
            if(sp.ok && sp.plates.size()>=4) {
                check(approx(sp.plates[3].axis.z,1.0) &&
                      approx(sp.plates[3].axis.x,0) &&
                      approx(sp.plates[3].axis.y,0),
                      "Haunch 40: cap stays horizontal for sloped beam");
                check(approx(sp.plates[3].center.z,7205.0,1e-5),
                      "Haunch 40: cap tracks column top instead of beam slope");
            }
        }
        // Changing plate thickness or gap MUST reposition the beam end
        // cut along the SAME column face (not a stale global plane).
        {
            ComponentInput changed=in;
            changed.params={
                {L"endPlateThickness",CompParamValue{30.0}},
                {L"gap",CompParamValue{12.0}}
            };
            GenerationPlan remap=reg.generate(L"tekla_haunch_40",changed);
            check(remap.ok && remap.cuts.size()==1,
                  "Haunch 40: edited plate thickness/gap recomputes beam end cut");
            if(remap.ok && remap.cuts.size()==1 && p.cuts.size()==1) {
                const Vec3 delta=remap.cuts[0].planeOrigin-p.cuts[0].planeOrigin;
                const Vec3 normal=p.cuts[0].planeNormal;
                const double advance=delta.x*normal.x+
                                     delta.y*normal.y+delta.z*normal.z;
                check(approx(advance,22.0,1e-6),
                      "Haunch 40: beam cut moves 10mm PL + 12mm gap");
            }
        }
        // Two members selected reversed: physical roles still detected.
        ComponentInput reversed;
        reversed.main=beam;reversed.secondary={in.main};
        GenerationPlan rp=reg.generate(L"tekla_haunch_40",reversed);
        check(rp.ok,"Haunch 40: reversed selection accepted");
        if(rp.boltGroups.size()==2)
            check(rp.boltGroups[0].parts[0]==EntityId{40},
                  "Haunch 40: real column chosen for bolt target");

        // Both values are individually within their declared param limits,
        // but the resulting 60mm depth cannot accommodate a 50mm tapered tip
        // plus the minimum 25mm web wedge clearance. Before this fix the
        // test used depth=20, silently clamped by the registry to 60, which
        // made its originally paired tip=25 geometry valid.
        in.params={{L"haunchDepth",CompParamValue{60.0}},
                   {L"haunchTipDepth",CompParamValue{50.0}}};
        check(!reg.generate(L"tekla_haunch_40",in).ok,
              "Haunch 40: invalid taper geometry rejected");
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
