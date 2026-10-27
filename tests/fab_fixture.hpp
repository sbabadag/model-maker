#pragma once
// P2A TEST FIXTURE — kiris + bagli uc levhasi (IKI bagimsiz bulon grubu) + oteli ikincil levha (kanat plaka).
// Tum geometri bir rijit donusumden (dondurme/ayna/oteleme) gecirilebilir: imalat degerleri DEGISMEMELI.
//
// Kimlik (donusumsuz) gercek degerler (BAGIMSIZ, elle):
//   Kiris : x 0..4000, y -75..75, z -150..150 ; eksen (0,0,0)->(4000,0,0)
//   Uc levhasi E : merkez (10,0,0) u=Y(200) v=Z(400) n=X t=20  (x 0..20 -> uye baslangicina DEGER)
//       Ust grup U : 2x2, Ø22, y=-50,+50  z=100,140   (adim 100 / 40)
//       Alt grup L : 2x3, Ø18, y=-50,+50  z=-140,-95,-50 (adim 100 / 45,45)
//   Kanat plaka F : merkez (1500,80,0) u=X(300) v=Z(200) n=Y t=10  (y 75..85, x 1350..1650)
//       Grup W : 3x2, Ø22, x=1400,1500,1600 z=+30,-30 (adim 100,100 / 60)

#include "model_maker/bolt.hpp"
#include "model_maker/component_engine.hpp"
#include "model_maker/document.hpp"
#include "model_maker/weld.hpp"

#include <cmath>
#include <vector>

namespace fx {

using namespace mm;

struct Xf {
    double m[3][3]{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    Vec3 t{0, 0, 0};
    Vec3 vec(const Vec3& v) const {
        return {m[0][0] * v.x + m[0][1] * v.y + m[0][2] * v.z, m[1][0] * v.x + m[1][1] * v.y + m[1][2] * v.z,
                m[2][0] * v.x + m[2][1] * v.y + m[2][2] * v.z};
    }
    double det() const {
        return m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) - m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) +
               m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
    }
    Vec3 pt(const Vec3& p) const {
        const Vec3 r = vec(p);
        return {r.x + t.x, r.y + t.y, r.z + t.z};
    }
};
inline Xf mul(const Xf& a, const Xf& b) {  // a*b
    Xf r;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) {
            r.m[i][j] = 0;
            for (int k = 0; k < 3; ++k) r.m[i][j] += a.m[i][k] * b.m[k][j];
        }
    r.t = a.pt(b.t);
    return r;
}
inline Xf rotZ(double deg) {
    Xf x;
    const double a = deg * 3.14159265358979323846 / 180.0, c = std::cos(a), s = std::sin(a);
    x.m[0][0] = c; x.m[0][1] = -s; x.m[1][0] = s; x.m[1][1] = c;
    return x;
}
inline Xf rotY(double deg) {
    Xf x;
    const double a = deg * 3.14159265358979323846 / 180.0, c = std::cos(a), s = std::sin(a);
    x.m[0][0] = c; x.m[0][2] = s; x.m[2][0] = -s; x.m[2][2] = c;
    return x;
}
inline Xf mirrorX() {
    Xf x;
    x.m[0][0] = -1;
    return x;
}
inline Xf translate(Vec3 t) {
    Xf x;
    x.t = t;
    return x;
}

struct Fixture {
    Document doc;
    EntityId beam{0}, endPlate{0}, finPlate{0};
    EntityId groupUpper{0}, groupLower{0}, groupFin{0};
    Xf xf;
};

inline EntityId addModel(Document& doc, WireframeModel m) {
    doc.addModel(std::move(m));
    return doc.modelId(doc.models().size() - 1);
}

inline WireframeModel boxModel(const Xf& xf, Vec3 lo, Vec3 hi, const char* profile, const char* name) {
    std::vector<Vec3> v;
    for (int i = 0; i < 8; ++i)
        v.push_back(xf.pt({(i & 1) ? hi.x : lo.x, (i & 2) ? hi.y : lo.y, (i & 4) ? hi.z : lo.z}));
    std::vector<Edge> e;
    for (std::size_t i = 0; i < 8; ++i)
        for (std::size_t b = 1; b < 8; b <<= 1)
            if (!(i & b)) e.push_back({i, i | b});
    std::vector<Face> f{{0, 2, 6, 4}, {1, 3, 7, 5}, {0, 1, 5, 4}, {2, 3, 7, 6}, {0, 1, 3, 2}, {4, 5, 7, 6}};
    WireframeModel m(std::move(v), std::move(e), std::move(f));
    EntityProperties p;
    p.profileName = profile;
    p.name = name;
    m.setProperties(p);
    return m;
}

inline EntityId addBeam(Fixture& fx, double length) {
    WireframeModel m = boxModel(fx.xf, {0, -75, -150}, {length, 75, 150}, "IPE300", "Kiris");
    EntityProperties p = m.properties();
    const Vec3 a = fx.xf.pt({0, 0, 0}), b = fx.xf.pt({length, 0, 0});
    p.axisFromX = a.x; p.axisFromY = a.y; p.axisFromZ = a.z;
    p.axisToX = b.x; p.axisToY = b.y; p.axisToZ = b.z;
    m.setProperties(p);
    return addModel(fx.doc, std::move(m));
}

// Kayitli PlateSpec cerceveli levha (ComponentInstanceRecord.children) + kati kutu modeli.
inline EntityId addFramedPlate(Fixture& fx, Vec3 centre, Vec3 u, Vec3 v, Vec3 n, double w, double h, double t,
                               const char* name, bool registerFrame = true) {
    Vec3 lo{-w / 2, -h / 2, -t / 2}, hi{w / 2, h / 2, t / 2};
    std::vector<Vec3> verts;
    for (int i = 0; i < 8; ++i) {
        const double a = (i & 1) ? hi.x : lo.x, b = (i & 2) ? hi.y : lo.y, c = (i & 4) ? hi.z : lo.z;
        verts.push_back(fx.xf.pt({centre.x + u.x * a + v.x * b + n.x * c, centre.y + u.y * a + v.y * b + n.y * c,
                                  centre.z + u.z * a + v.z * b + n.z * c}));
    }
    std::vector<Edge> e;
    for (std::size_t i = 0; i < 8; ++i)
        for (std::size_t b = 1; b < 8; b <<= 1)
            if (!(i & b)) e.push_back({i, i | b});
    std::vector<Face> f{{0, 2, 6, 4}, {1, 3, 7, 5}, {0, 1, 5, 4}, {2, 3, 7, 6}, {0, 1, 3, 2}, {4, 5, 7, 6}};
    WireframeModel m(std::move(verts), std::move(e), std::move(f));
    EntityProperties p;
    p.profileName = "PL";
    p.name = name;
    p.material = "S355";
    m.setProperties(p);
    const EntityId id = addModel(fx.doc, std::move(m));
    if (registerFrame) {
        ComponentInstanceRecord rec;
        rec.typeId = L"fixture_plate";
        rec.mainPart = fx.beam;
        rec.ownedPlateIds = {id};
        ComponentChildRecord ch;
        ch.roleKey = name;
        ch.kind = ComponentObjectKind::Plate;
        ch.entityId = id;
        PlateSpec ps;
        ps.center = fx.xf.pt(centre);
        ps.u = fx.xf.vec(u);
        ps.v = fx.xf.vec(v);
        ps.axis = fx.xf.vec(n);
        ps.width = w;
        ps.height = h;
        ps.thickness = t;
        ps.material = L"S355";
        ch.plate = ps;
        rec.children.push_back(ch);
        fx.doc.addComponentInstance(rec);
    }
    return id;
}

inline EntityId addBolts(Fixture& fx, EntityId plate, Vec3 origin, Vec3 axis, Vec3 xDir, int nx, int ny, double sx,
                         double sy, double dia) {
    BoltGroup bg;
    bg.parts = {plate};
    bg.origin = fx.xf.pt(origin);
    // Ayna (det<0): yDir = axis x xDir isaret cevirir; delik ekseninin isareti geometriyi degistirmez -> eksen negatiflenir.
    const Vec3 ax = fx.xf.vec(axis);
    bg.axis = fx.xf.det() < 0 ? Vec3{-ax.x, -ax.y, -ax.z} : ax;
    bg.xDir = fx.xf.vec(xDir);
    bg.countX = nx;
    bg.countY = ny;
    bg.spacingX = sx;
    bg.spacingY = sy;
    bg.params.holeDiameter = dia;
    const std::size_t idx = fx.doc.addBoltGroup(bg, WireframeModel::line(bg.origin, bg.origin));
    return fx.doc.boltGroups()[idx].id;
}

// Tam fixture. withWelds: shop kaynagi (assembly olusturur; kaynak cizimde desteklenmedigi icin TASLAK).
inline Fixture make(const Xf& xf = Xf{}, double beamLength = 4000.0, bool withWelds = true) {
    Fixture fx;
    fx.xf = xf;
    fx.beam = addBeam(fx, beamLength);
    fx.endPlate = addFramedPlate(fx, {10, 0, 0}, {0, 1, 0}, {0, 0, 1}, {1, 0, 0}, 200, 400, 20, "uc_levhasi");
    fx.groupUpper = addBolts(fx, fx.endPlate, {10, -50, 100}, {1, 0, 0}, {0, 1, 0}, 2, 2, 100, 40, 22);
    fx.groupLower = addBolts(fx, fx.endPlate, {10, -50, -140}, {1, 0, 0}, {0, 1, 0}, 2, 3, 100, 45, 18);
    fx.finPlate = addFramedPlate(fx, {1500, 80, 0}, {1, 0, 0}, {0, 0, 1}, {0, 1, 0}, 300, 200, 10, "kanat_plaka");
    fx.groupFin = addBolts(fx, fx.finPlate, {1400, 80, 30}, {0, 1, 0}, {1, 0, 0}, 3, 2, 100, 60, 22);
    if (withWelds) {
        for (const EntityId p : {fx.endPlate, fx.finPlate}) {
            Weld w;
            w.mainPart = fx.beam;
            w.secondaryPart = p;
            w.weldClass = WeldClass::Shop;
            fx.doc.addWeld(w);
        }
    }
    return fx;
}

} // namespace fx
