// SANAL PROFIL MERKEZI SNAP NOKTASI — OCC GEREKTIRMEZ, platformdan bagimsiz.
//
// KULLANICI SARTI: "profilin tam merkezinde bir sanal snap noktasi olustur".
//
// Profil elemani (katisi) icin, sinir kutusunun TAM MERKEZINDE sanal bir snap
// noktasi uretilir (SnapType::ProfileCenter). Gercek bir kose/kenar DEGILDIR:
// hicbir vertex/edge o noktada olmasa bile imlec oraya oturur. Merkez
// `Document::modelBounds()` (onbellekli AABB) uzerinden alinir — ikinci bir
// sinir kutusu hesabi yoktur.
//
// Bu dosya GERCEK `SnapEngine::snap` / `SnapEngine::snap3D`'yi cagirir —
// kopya mantik yok.
#include "model_maker/camera.hpp"
#include "model_maker/document.hpp"
#include "model_maker/drafting.hpp"
#include "model_maker/geometry.hpp"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace mm;

static int checks = 0, failures = 0;
static void check(bool ok, const char* what) {
    ++checks;
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
}
static bool near(double a, double b, double eps = 1e-6) { return std::abs(a - b) < eps; }
static bool nearPoint(const Vec3& p, const Vec3& q, double eps = 1e-6) {
    return near(p.x, q.x, eps) && near(p.y, q.y, eps) && near(p.z, q.z, eps);
}

// Yatay profil elemani (kati): X boyunca L, genislik w (Y'de ortalanmis),
// govde ust yuzden (z=0) asagi h. Gercek OCC katilari `shapeToWireframeWithFaces`
// ile ayni sekle sahiptir: ust/alt dortgen + dusey kenarlar + dortgen yuzler.
static WireframeModel profileBeam(double L, double w, double h, const std::string& profile) {
    const double y0 = -w / 2.0, y1 = w / 2.0, zt = 0.0, zb = -h;
    std::vector<Vec3> v{{0, y0, zt}, {L, y0, zt}, {L, y1, zt}, {0, y1, zt},
                        {0, y0, zb}, {L, y0, zb}, {L, y1, zb}, {0, y1, zb}};
    const int quads[6][4] = {{0, 1, 2, 3}, {4, 7, 6, 5}, {0, 4, 5, 1},
                             {1, 5, 6, 2}, {0, 3, 7, 4}, {1, 2, 6, 5}};
    std::vector<Face> faces;
    for (const auto& q : quads) {
        const std::size_t a = q[0], b = q[1], c = q[2], d = q[3];
        faces.push_back({a, b, c});
        faces.push_back({a, c, d});
    }
    WireframeModel model{std::move(v),
        {{0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6}, {6, 7}, {7, 4},
         {0, 4}, {1, 5}, {2, 6}, {3, 7}}, std::move(faces)};
    EntityProperties props;
    props.profileName = profile;
    model.setProperties(props);
    return model;
}

// Dusey profil elemani (kolon): kesit XY'de b x h, boy Z boyunca 0..H.
static WireframeModel profileColumn(double b, double h, double H, const std::string& profile) {
    const double x0 = -b / 2.0, x1 = b / 2.0, y0 = 0.0, y1 = -h;
    std::vector<Vec3> v{{x0, y0, 0}, {x1, y0, 0}, {x1, y1, 0}, {x0, y1, 0},
                        {x0, y0, H}, {x1, y0, H}, {x1, y1, H}, {x0, y1, H}};
    WireframeModel model{std::move(v),
        {{0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6}, {6, 7}, {7, 4},
         {0, 4}, {1, 5}, {2, 6}, {3, 7}}};
    EntityProperties props;
    props.profileName = profile;
    model.setProperties(props);
    return model;
}

static SnapTypeMask maskExcept(std::initializer_list<SnapType> off) {
    SnapTypeMask m{};
    m.fill(true);
    for (const auto type : off) m[static_cast<std::size_t>(type)] = false;
    return m;
}

int main() {
    const double L = 3000.0, w = 300.0, h = 400.0;
    const Vec3 center{L / 2.0, 0.0, -h / 2.0};   // (1500, 0, -200)
    const double tol = 100.0;                     // 2B: dunya birimi
    const SnapTypeMask allOn = maskExcept({});

    // --- 1) 2B: imlec merkezde -> SANAL PROFIL MERKEZI ---
    {
        Document doc; doc.addModel(profileBeam(L, w, h, "IPE300"));
        const SnapResult r = SnapEngine::snap({L / 2.0, 0.0, 0.0}, doc, tol, true, false,
                                              std::nullopt, &allOn);
        check(r.type == SnapType::ProfileCenter, "1a 2B: profil merkezi SANAL snap noktasi uretilir");
        check(nearPoint(r.point, center), "1b 2B: nokta sinir kutusunun TAM merkezi (1500,0,-200)");
    }

    // --- 2) PROFIL DEGILSE uretilmez (duz kutu/cizgi etkilenmez) ---
    {
        Document doc; doc.addModel(profileBeam(L, w, h, ""));   // profileName bos
        const SnapResult r = SnapEngine::snap({L / 2.0, 0.0, 0.0}, doc, tol, true, false,
                                              std::nullopt, &allOn);
        check(r.type != SnapType::ProfileCenter, "2a profilsiz eleman: profil merkezi uretilmez");
        check(r.type == SnapType::None, "2b profilsiz eleman: merkezde baska snap da yok -> None");
    }

    // --- 3) MASKE KAPISI: tur kapaliysa aday uretilmez ---
    {
        Document doc; doc.addModel(profileBeam(L, w, h, "IPE300"));
        const SnapTypeMask noProfileCenter = maskExcept({SnapType::ProfileCenter});
        const SnapResult r = SnapEngine::snap({L / 2.0, 0.0, 0.0}, doc, tol, true, false,
                                              std::nullopt, &noProfileCenter);
        check(r.type != SnapType::ProfileCenter, "3a maske kapali: profil merkezi devre disi");
        check(r.type == SnapType::None, "3b maske kapali: merkezde None");
    }

    // --- 4) UC ONCELIGI KORUNUR: kosede Endpoint kazanir ---
    {
        Document doc; doc.addModel(profileBeam(L, w, h, "IPE300"));
        const SnapResult r = SnapEngine::snap({0.0, -w / 2.0, 0.0}, doc, tol, true, false,
                                              std::nullopt, &allOn);
        check(r.type == SnapType::Endpoint, "4a kosede: Endpoint onceligi korunur");
    }

    // --- 5) MERKEZ AABB'DIR, VERTEX ORTALAMASI DEGIL ---
    // Fazladan bir vertex (kosede tekrar) degistirir: vertex ortalamasi kayar,
    // AABB merkezi SABIT kalir. Kod ortalamayi kullansa nokta kayardi.
    {
        WireframeModel model = profileBeam(L, w, h, "IPE300");
        std::vector<Vec3> v = model.vertices();
        v.push_back({0.0, -w / 2.0, 0.0});          // kosede kopya
        WireframeModel biased{std::move(v), model.edges(), {}};
        EntityProperties props; props.profileName = "IPE300";
        biased.setProperties(props);
        Document doc; doc.addModel(biased);
        const SnapResult r = SnapEngine::snap({L / 2.0, 0.0, 0.0}, doc, 1.0, true, false,
                                              std::nullopt, &allOn);
        check(r.type == SnapType::ProfileCenter, "5a vertex dagilimi bozuk: merkez yine uretilir");
        check(nearPoint(r.point, center, 1e-9), "5b merkez AABB merkezi (vertex ortalamasi kaydi degil)");
        // Kanit: vertex ortalamasi gercekten FARKLI (test anlamli).
        Vec3 mean{};
        for (const auto& p : biased.vertices()) mean = mean + p;
        mean = mean * (1.0 / static_cast<double>(biased.vertices().size()));
        check(!nearPoint(mean, center, 1.0), "5c vertex ortalamasi merkezden farkli (test anlamli)");
    }

    // --- 6) TOLERANS: merkez yakinda snap, uzakta degil ---
    {
        Document doc; doc.addModel(profileBeam(L, w, h, "IPE300"));
        const SnapResult inside = SnapEngine::snap({L / 2.0 + 60.0, 0.0, 0.0}, doc, tol, true, false,
                                                   std::nullopt, &allOn);
        check(inside.type == SnapType::ProfileCenter, "6a tolerans icinde: merkeze snap");
        const SnapResult outside = SnapEngine::snap({L / 2.0 + 600.0, 0.0, 0.0}, doc, tol, true, false,
                                                    std::nullopt, &allOn);
        check(outside.type == SnapType::None, "6b tolerans disinda: merkeze snap yok");
    }

    // --- 7) COKLU PROFIL: EN YAKIN MERKEZ kazanir ---
    {
        Document doc;
        doc.addModel(profileBeam(L, w, h, "IPE300"));                 // merkez (1500,0,-200)
        WireframeModel second = profileBeam(L, w, h, "HEA200");
        second.translate({0.0, 4000.0, 0.0});                          // merkez (1500,4000,-200)
        doc.addModel(second);
        const SnapResult r = SnapEngine::snap({L / 2.0, 4000.0, 0.0}, doc, tol, true, false,
                                              std::nullopt, &allOn);
        check(r.type == SnapType::ProfileCenter, "7a coklu profil: merkez snap'i");
        check(near(r.point.y, 4000.0), "7b coklu profil: EN YAKIN merkez secilir (y=4000)");
    }

    // --- 8) 3B (snap3D, plan gorunus): SANAL nokta ekranda dogru yere oturur ---
    const int W = 1200, H = 800;
    Camera topCam;
    topCam.setViewBasis(Vec3{1, 0, 0}, Vec3{0, 1, 0}, Vec3{L / 2.0, 0.0, 0.0});
    {
        Document doc; doc.addModel(profileBeam(L, w, h, "IPE300"));
        const Vec2 s = topCam.project(center, W, H);
        const SnapResult r = SnapEngine::snap3D(s, doc, topCam, W, H, 10.0, WorkPlane::world(),
                                                true, false, std::nullopt, nullptr, false);
        check(r.type == SnapType::ProfileCenter, "8a 3B plan: profil merkezine snap");
        check(nearPoint(r.point, center), "8b 3B plan: nokta merkez (1500,0,-200)");
    }

    // --- 9) SOLID gorunurluk filtresi SANAL noktayi ETKILEMEZ ---
    // (Merkez gercek bir kenar degildir; kenar filtresi onu eleyemez.)
    {
        Document doc; doc.addModel(profileBeam(L, w, h, "IPE300"));
        const Vec2 s = topCam.project(center, W, H);
        const SnapResult wire = SnapEngine::snap3D(s, doc, topCam, W, H, 10.0, WorkPlane::world(),
                                                   true, false, std::nullopt, nullptr, false);
        const SnapResult solid = SnapEngine::snap3D(s, doc, topCam, W, H, 10.0, WorkPlane::world(),
                                                    true, false, std::nullopt, nullptr, true);
        check(wire.type == SnapType::ProfileCenter && solid.type == SnapType::ProfileCenter,
              "9a Wireframe ve Solid: profil merkezi AYNI (stilden bagimsiz)");
        check(nearPoint(wire.point, solid.point), "9b iki stilde ayni nokta");
    }

    // --- 10) DUSEY PROFIL (kolon), ELEVASYON gorunusu: merkez govdenin ortasi ---
    {
        const double b = 300.0, hh = 400.0, HH = 3000.0;
        const Vec3 colCenter{0.0, -hh / 2.0, HH / 2.0};   // (0,-200,1500)
        Document doc; doc.addModel(profileColumn(b, hh, HH, "HEA300"));
        Camera frontCam;
        frontCam.setViewBasis(Vec3{1, 0, 0}, Vec3{0, 0, 1}, Vec3{0.0, -hh / 2.0, HH / 2.0});
        const WorkPlane frontPlane =
            WorkPlane::fromViewDirection({0.0, -hh / 2.0, HH / 2.0}, Vec3{0.0, -1.0, 0.0});
        const Vec2 s = frontCam.project(colCenter, W, H);
        const SnapResult r = SnapEngine::snap3D(s, doc, frontCam, W, H, 10.0, frontPlane,
                                                true, false, std::nullopt, nullptr, false);
        check(r.type == SnapType::ProfileCenter, "10a kolon/elevasyon: profil merkezine snap");
        check(nearPoint(r.point, colCenter), "10b kolon merkezi govdenin ortasi (0,-200,1500)");
    }

    // --- 11) TUR TABLOLARI (etiket/isaret/renk) TANIMLI ---
    {
        check(std::wstring(snapTypeLabel(SnapType::ProfileCenter)) == L"PROF CENTER",
              "11a etiket: PROF CENTER");
        check(snapMarkerSymbol(SnapType::ProfileCenter) != SnapMarkerSymbol::None,
              "11b marker sembolu tanimli");
        check(snapMarkerColorRgb(SnapType::ProfileCenter) != 0, "11c marker rengi tanimli");
        check(snapMarkerColorRgb(SnapType::ProfileCenter) != snapMarkerColorRgb(SnapType::GeometricCenter),
              "11d rengi GeometricCenter'dan FARKLI (ayirt edilebilir)");
    }

    // --- 12) MASKE BOYUTU: yeni tur kapsanir, mevcut indeksler kaymadi ---
    {
        SnapTypeMask m{}; m.fill(false);
        m[static_cast<std::size_t>(SnapType::ProfileCenter)] = true;   // tasma yok
        check(m.size() == static_cast<std::size_t>(SnapType::ProfileCenter) + 1,
              "12a maske boyutu en son turu kapsar");
        Document doc; doc.addModel(profileBeam(L, w, h, "IPE300"));
        const SnapResult r = SnapEngine::snap({L / 2.0, 0.0, 0.0}, doc, tol, true, false,
                                              std::nullopt, &m);
        check(r.type == SnapType::ProfileCenter, "12b yalniz ProfileCenter acik: merkez snap'i");
    }

    std::printf("\nPROFIL MERKEZI SNAP: %d kontrol, %d hata\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
