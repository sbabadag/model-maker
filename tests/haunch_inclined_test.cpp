// EĞİK KİRİŞTE HAUNCH 40 — alın levhası kolon yüzüne OTURMALI.
//
// Kullanıcı isteği (Selahattin 2026-10-29): "haunch eğik kirişe uyumlu
// olmalı". Eski sürüm tüm bağlantıyı kiriş eksenine DİK kuruyordu; 10° eğik
// kirişte alın levhasının alt kenarı kolonun ~16 mm içine giriyor, üst kenarı
// ~72 mm açıkta kalıyor ve haunch gövdesinin ön kenarı levhadan 48 mm geride
// duruyordu. Bu test düşey alın levhasını, düşey kesimi ve düşey ön kenarlı
// haunch gövdesini doğrular. Yatay kiriş davranışı DEĞİŞMEZ.
#include "model_maker/component_engine.hpp"
#include <cmath>
#include <cstdio>

using namespace mm;

static int bad = 0;
static void check(bool p, const char* what) {
    std::printf("%s %s\n", p ? "PASS" : "FAIL", what);
    bad += !p;
}

static double dotv(Vec3 a, Vec3 b) { return a.x*b.x + a.y*b.y + a.z*b.z; }
static Vec3 addv(Vec3 a, Vec3 b) { return {a.x+b.x, a.y+b.y, a.z+b.z}; }
static Vec3 mulv(Vec3 a, double k) { return {a.x*k, a.y*k, a.z*k}; }

static ComponentInput makeInput(double slopeDeg)
{
    ComponentInput in;
    in.main.id = 1;
    in.main.axisFrom = {0,0,0};
    in.main.axisTo = {0,0,3000};
    in.main.refNormal = {0,1,0};
    in.main.secHeight = 300;
    in.main.secWidth = 300;
    in.main.secWebThickness = 11;
    in.main.secFlangeThickness = 19;
    in.main.sectionCenterOffset = 0.0;

    MemberGeometry beam;
    beam.id = 2;
    const double t = std::tan(slopeDeg * M_PI / 180.0);
    beam.axisFrom = {0, 1200, 1500 + 1140*t};
    beam.axisTo   = {0, 60, 1500};
    beam.refNormal = {0,0,1};
    beam.secHeight = 200;
    beam.secWidth = 100;
    beam.secWebThickness = 5.6;
    beam.secFlangeThickness = 8.5;
    beam.sectionCenterOffset = 0.0;
    in.secondary = {beam};
    return in;
}

static const PlateSpec* findPlate(const GenerationPlan& p, const char* key, std::size_t* idx = nullptr)
{
    for (std::size_t i = 0; i < p.plates.size(); ++i) {
        if (p.plateKeys.size() > i && p.plateKeys[i] == key) {
            if (idx) *idx = i;
            return &p.plates[i];
        }
    }
    return nullptr;
}

// Kolon +Y yüzü: y = 150 düzlemi (300x300 merkezli kolon, ax = +Y).
static double faceDistance(Vec3 q) { return q.y - 150.0; }

static void checkInclined(double slopeDeg, bool expectInclined)
{
    ComponentInput in = makeInput(slopeDeg);
    GenerationPlan gp = ComponentRegistry::instance().generate(L"tekla_haunch_40", in);
    check(gp.ok, "egik kiris: plan uretildi");
    if (!gp.ok) {
        std::printf("  hata: %ls\n", gp.error.c_str());
        return;
    }
    check(gp.plates.size() == 4, "egik kiris: 4 levha");
    check(gp.boltGroups.size() == 2, "egik kiris: 2 bulon grubu");
    check(gp.cuts.size() == 1, "egik kiris: 1 kesim");

    const PlateSpec* end = findPlate(gp, "haunch_end_plate");
    const PlateSpec* web = findPlate(gp, "haunch_web");
    const PlateSpec* fl  = findPlate(gp, "haunch_lower_flange");
    check(end && web && fl, "egik kiris: levha rolleri mevcut");
    if (!end || !web || !fl) return;

    // 1) Alın levhası DÜŞEY (normal yatay) ve kolona OTURMUŞ.
    check(std::fabs(end->axis.z) < 1e-9, "alin levhasi: normal YATAY (dusey levha)");
    check(std::fabs(dotv(end->axis, Vec3{0,0,1})) < 1e-9,
          "alin levhasi: eksen world up'a dik");
    double minFace = 1e9, maxFace = -1e9;
    for (int au = -1; au <= 1; au += 2)
        for (int av = -1; av <= 1; av += 2)
            for (int at = -1; at <= 1; at += 2) {
                Vec3 q = addv(addv(end->center, mulv(end->u, au*end->width*0.5)),
                              addv(mulv(end->v, av*end->height*0.5),
                                   mulv(end->axis, at*end->thickness*0.5)));
                const double d = faceDistance(q);
                if (d < minFace) minFace = d;
                if (d > maxFace) maxFace = d;
            }
    check(minFace > -0.01, "alin levhasi: kolonun ICINE girmiyor");
    check(std::fabs(minFace) < 0.5, "alin levhasi: EN YAKIN yuz kolon yuzunde (gap=0)");
    check(maxFace < 40.0, "alin levhasi: en uzak kose makul mesafede");

    // 2) Kesim DÜŞEY düzlem.
    check(std::fabs(gp.cuts[0].planeNormal.z) < 1e-9,
          "kesim: normal YATAY (dusey/plumb kesim)");

    // 3) Haunch gövdesi: düşey ön kenar + kiriş alt başlığına paralel üst kenar.
    check(web->outline.size() == 4, "govde: 4 noktali serbest kontur");
    if (web->outline.size() == 4) {
        check(std::fabs(web->outline[0].first - web->outline[3].first) < 1e-9,
              "govde: ON kenar dusey (ayni u)");
        check(std::fabs(web->outline[1].first - web->outline[2].first) < 1e-9,
              "govde: arka kenar dusey (ayni u)");
        // Üst kenar = kiriş alt başlığı eğimi (tanT).
        const double tanT = std::tan(slopeDeg * M_PI / 180.0);
        const double dz = web->outline[2].second - web->outline[3].second;
        const double du = web->outline[2].first  - web->outline[3].first;
        check(std::fabs(du) > 1.0 && std::fabs(dz/du - tanT) < 1e-6,
              "govde: ust kenar kiris egimine PARALEL");
        // Ön kenar levhanın kiriş tarafı yüzünde.
        const Vec3 nearBottom = addv(web->center,
            addv(mulv(web->u, web->outline[0].first),
                 mulv(web->v, web->outline[0].second)));
        check(std::fabs(faceDistance(nearBottom) - end->thickness) < 0.5,
              "govde: on kenar alin levhasinin yuzunde");
    }

    // 4) Basınç flanşı gövdenin alt kenarını izler.
    const Vec3 flNear = addv(fl->center,
        mulv(fl->v, -fl->height*0.5));
    check(std::fabs(faceDistance(flNear) - end->thickness) < 6.0,
          "basinc flansi: kolon tarafinda alin levhasina yakin");
    check(std::fabs(dotv(fl->v, Vec3{0,0,1})) < 1.0,
          "basinc flansi: egimli (dusey degil)");

    // 5) Bulonlar levha çerçevesinde (eksen yatay).
    for (const auto& bg : gp.boltGroups)
        check(std::fabs(bg.axis.z) < 1e-9, "bulon: eksen YATAY (alin levhasina dik)");

    (void)expectInclined;
}

int main()
{
    registerBuiltInComponents(ComponentRegistry::instance());

    std::printf("=== 10 derece YUKARI egik kiris ===\n");
    checkInclined(10.0, true);
    std::printf("=== 10 derece ASAGI egik kiris ===\n");
    checkInclined(-10.0, true);

    std::printf("=== YATAY kiris (REGRESYON: degismemeli) ===\n");
    {
        ComponentInput in = makeInput(0.0);
        GenerationPlan gp = ComponentRegistry::instance().generate(L"tekla_haunch_40", in);
        check(gp.ok, "yatay: plan uretildi");
        check(gp.plates.size() == 4, "yatay: 4 levha");
        const PlateSpec* end = findPlate(gp, "haunch_end_plate");
        const PlateSpec* web = findPlate(gp, "haunch_web");
        check(end && web, "yatay: roller mevcut");
        if (end) {
            check(std::fabs(end->axis.y - 1.0) < 1e-9,
                  "yatay: alin levhasi normali kiris ekseni (+Y)");
            check(std::fabs(end->center.y - 160.0) < 0.01,
                  "yatay: alin levhasi X/Y konumu degismedi (y=160)");
            check(std::fabs(end->center.z - 1380.0) < 0.01,
                  "yatay: alin levhasi Z konumu degismedi (z=1380)");
        }
        if (web) {
            check(web->outline.empty(), "yatay: govde eski dikdortgen+pah yolunda");
            check(web->chamfers.size() == 1, "yatay: govde tek pah korunuyor");
            check(std::fabs(web->center.y - 395.0) < 0.01 &&
                  std::fabs(web->center.z - 1280.0) < 0.01,
                  "yatay: govde konumu degismedi");
        }
    }

    std::printf("=== EAVES: kolon ustu = kiris ekseni (3000) — alin levhasi TASMAMALI ===\n");
    {
        ComponentInput in = makeInput(0.0);
        // Kiriş kolon tepesinde (katlı_yapı2 deseni).
        in.secondary[0].axisFrom = {0, 1200, 3000};
        in.secondary[0].axisTo   = {0, 60, 3000};
        GenerationPlan gp = ComponentRegistry::instance().generate(L"tekla_haunch_40", in);
        check(gp.ok, "eaves: plan uretildi");
        const PlateSpec* end = findPlate(gp, "haunch_end_plate");
        const PlateSpec* cap = findPlate(gp, "haunch_top_plate");
        check(end && cap, "eaves: alin levhasi ve kapak var");
        if (end) {
            const double top = end->center.z + end->height * 0.5;
            check(top <= 3000.01, "eaves: alin levhasi kolon tepesini ASMIYOR");
            check(end->height > 100.0, "eaves: levha hala gecerli yukseklikte");
        }
        if (cap) {
            check(std::fabs(cap->center.z - 3005.0) < 0.01,
                  "eaves: kapak kolon tepesinde (z=3605 degil 3005)");
            check(std::fabs(cap->axis.z - 1.0) < 1e-9,
                  "eaves: kapak normali yukari");
        }
        for (const auto& bg : gp.boltGroups) {
            // En üst bulon sırası kolon tepesinin altında kalmalı.
            const double topBoltZ = bg.origin.z + bg.spacingY * (bg.countY - 1);
            check(topBoltZ <= 3000.01, "eaves: bulonlar kolon tepesinin altinda");
        }
    }

    std::printf("=== 5 derece EGIK KOLON: kapak kolon eksenine DIK ===\n");
    {
        ComponentInput in = makeInput(10.0);
        const double a = 5.0 * M_PI / 180.0;
        in.main.axisFrom = {0, 0, 0};
        in.main.axisTo   = {0, 3000.0*std::sin(a), 3000.0*std::cos(a)};
        GenerationPlan gp = ComponentRegistry::instance().generate(L"tekla_haunch_40", in);
        check(gp.ok, "egik kolon: plan uretildi");
        const PlateSpec* cap = findPlate(gp, "haunch_top_plate");
        check(cap != nullptr, "egik kolon: kapak var");
        if (cap) {
            const double len = std::sqrt(3000.0*std::sin(a)*3000.0*std::sin(a) +
                                         3000.0*std::cos(a)*3000.0*std::cos(a));
            const Vec3 colDir{0, 3000.0*std::sin(a)/len, 3000.0*std::cos(a)/len};
            check(std::fabs(dotv(cap->axis, colDir) - 1.0) < 1e-6,
                  "egik kolon: kapak eksene DIK (yamuk oturmuyor)");
        }
    }

    std::printf("=== 90 DERECE DONUK KOLON + EGIK KIRIS (portal_1.mmw deseni) ===\n");
    {
        // Gercek model: kolon HEA240 90° donuk (genislik 240 → Y, yukseklik 230
        // → X), eksen kesitin SOL kenarinda (Top anchor → offset -h/2), kiris
        // IPE400 egik yukari, kolon tepesinde (eaves). Eski kod donusuyu yok
        // sayip alin levhasini kolon yuzunden ~110 mm iceri atiyordu.
        ComponentInput in;
        in.main.id = 45;
        in.main.axisFrom = {-115.0, 12000.0, 0.0};
        in.main.axisTo   = {-115.0, 12000.0, 5000.0};
        in.main.refNormal = {0, 0, 1};
        in.main.secWidth = 240.0;
        in.main.secHeight = 230.0;
        in.main.secWebThickness = 7.5;
        in.main.secFlangeThickness = 12.0;
        in.main.sectionCenterOffset = -115.0;   // Top anchor
        in.main.profileRotation = 90.0;
        MemberGeometry beam;
        beam.id = 47;
        beam.axisFrom = {0.0, 12000.0, 5000.0};
        beam.axisTo   = {5997.780613671378, 12000.0, 5986.683682028266};
        beam.refNormal = {0, 0, 1};
        beam.secWidth = 180.0;
        beam.secHeight = 400.0;
        beam.secWebThickness = 8.6;
        beam.secFlangeThickness = 13.5;
        beam.sectionCenterOffset = 0.0;
        beam.profileRotation = 0.0;
        in.secondary = {beam};

        GenerationPlan gp = ComponentRegistry::instance().generate(L"tekla_haunch_40", in);
        check(gp.ok, "donuk kolon: plan uretildi");
        if (!gp.ok) { std::printf("  hata: %ls\n", gp.error.c_str()); }
        const PlateSpec* end = findPlate(gp, "haunch_end_plate");
        const PlateSpec* cap = findPlate(gp, "haunch_top_plate");
        if (end) {
            // Alin levhasinin kolona YAKIN yuzu kolon yuzunde (x=+115) olmali;
            // donus goz ardi edilirse x≈5'e (110 mm iceri) duser.
            double minX = 1e9, maxX = -1e9, minZ = 1e9, maxZ = -1e9;
            for (int au=-1; au<=1; au+=2)
                for (int av=-1; av<=1; av+=2)
                    for (int at=-1; at<=1; at+=2) {
                        Vec3 q = addv(addv(end->center, mulv(end->u, au*end->width*0.5)),
                                      addv(mulv(end->v, av*end->height*0.5),
                                           mulv(end->axis, at*end->thickness*0.5)));
                        minX = std::min(minX, q.x); maxX = std::max(maxX, q.x);
                        minZ = std::min(minZ, q.z); maxZ = std::max(maxZ, q.z);
                    }
            check(std::fabs(minX - 115.0) < 0.5,
                  "donuk kolon: alin levhasi kolon yuzune OTURUYOR (x=115)");
            check(maxX < 140.0,
                  "donuk kolon: alin levhasi kolon yuzunden makul disari tasiyor");
            check(std::fabs(maxZ - 5000.0) < 0.5,
                  "donuk kolon: alin levhasi kolon tepesini ASMIYOR (z=5000)");
        }
        if (cap) {
            // Kapak kolon KESITINI birebir ortmeli (x=-115..115, y=11880..12120).
            double minX = 1e9, maxX = -1e9, minY = 1e9, maxY = -1e9;
            for (int au=-1; au<=1; au+=2)
                for (int av=-1; av<=1; av+=2) {
                    Vec3 q = addv(cap->center, addv(mulv(cap->u, au*cap->width*0.5),
                                                     mulv(cap->v, av*cap->height*0.5)));
                    minX = std::min(minX, q.x); maxX = std::max(maxX, q.x);
                    minY = std::min(minY, q.y); maxY = std::max(maxY, q.y);
                }
            check(std::fabs(minX - (-115.0)) < 0.5 && std::fabs(maxX - 115.0) < 0.5,
                  "donuk kolon: kapak kesit X kapsamini orter (-115..115)");
            check(std::fabs(minY - 11880.0) < 0.5 && std::fabs(maxY - 12120.0) < 0.5,
                  "donuk kolon: kapak kesit Y kapsamini orter (11880..12120)");
        }
    }

    std::printf("\n%s (%d hata)\n", bad ? "BASARISIZ" : "TUM TESTLER GECTI", bad);
    return bad ? 1 : 0;
}