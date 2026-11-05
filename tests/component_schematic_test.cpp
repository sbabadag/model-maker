// MM-8b/3 — OTOMATIK SEMATIK (ortografik izdusum, gercek mm, tek olcek).
//
// Dogrulama: (1) end plate varsayilan girdide sematik uretiliyor; (2) bulon
// basi ISO 4014 tablo genisligiyle (s) ciziliyor; (3) levha kalinligi GERCEK
// mm olarak izdusuyor; (4) bas genisligi / levha kalinligi orani gercege %2
// icinde; (5) ayni girdi -> ayni cizim (deterministik). Qt/OCCT yok.

#include "model_maker/component_engine.hpp"
#include "model_maker/component_schematic.hpp"
#include "model_maker/bolt.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

using namespace mm;

static int failures = 0;
static void check(bool ok, const char* label) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", label);
    if (!ok) ++failures;
}
static bool approx(double a, double b, double tol = 1e-9) {
    return std::abs(a - b) < tol;
}

static ComponentInput defaultEndPlateInput() {
    ComponentInput in;
    in.main.id = EntityId{20};
    in.main.axisFrom = {0, 0, 0};
    in.main.axisTo = {0, 0, 3000};
    in.main.refNormal = {0, 0, 1};
    in.main.secWidth = 300.0;
    in.main.secHeight = 300.0;
    in.main.secWebThickness = 11.0;
    in.main.secFlangeThickness = 19.0;
    MemberGeometry beam;
    beam.id = EntityId{21};
    beam.axisFrom = {0, 0, 3000};
    beam.axisTo = {0, 4000, 3000};
    beam.refNormal = {0, 0, 1};
    beam.secWidth = 200.0;
    beam.secHeight = 300.0;
    beam.secWebThickness = 9.0;
    beam.secFlangeThickness = 15.0;
    in.secondary.push_back(beam);
    return in;
}

// Bir prim icin 2B sinir kutusu kapsamı (mm).
static void extent(const SchematicPrim& p, double& minx, double& miny,
                   double& maxx, double& maxy) {
    minx = miny = std::numeric_limits<double>::max();
    maxx = maxy = std::numeric_limits<double>::lowest();
    const auto incl = [&](double x, double y) {
        minx = std::min(minx, x);
        miny = std::min(miny, y);
        maxx = std::max(maxx, x);
        maxy = std::max(maxy, y);
    };
    if (p.kind == SchematicPrimKind::Circle || p.kind == SchematicPrimKind::Annulus) {
        incl(p.cx - p.r, p.cy - p.r);
        incl(p.cx + p.r, p.cy + p.r);
    } else {
        for (const auto& q : p.pts) incl(q.x, q.y);
    }
}

int main() {
    registerBuiltInComponents(ComponentRegistry::instance());

    // --- End Plate (varsayilan) ----------------------------------------------
    const ComponentInput in = defaultEndPlateInput();
    GenerationPlan plan = ComponentRegistry::instance().generate(
        L"beam_to_column_end_plate", in);
    check(plan.ok && !plan.plates.empty(), "end plate plan uretildi");
    check(!plan.boltGroups.empty(), "end plate bulon grubu var");
    if (!plan.ok) return failures ? 1 : 0;

    const SchematicDrawing dr = buildComponentSchematic(plan, in, SchematicViewDir::Front);
    check(dr.ok, "sematik (Front) uretildi");

    // 1) Bulon basi: ISO 4014 tablo genisligi (M20 -> s=30).
    const BoltDimensions iso = boltDimensions(plan.boltGroups[0].params.nominalDiameter);
    double headDia = -1.0;
    int headCount = 0;
    for (const auto& p : dr.prims) {
        if (p.role == "bolt" && p.detail == "head") {
            headDia = p.r * 2.0;
            ++headCount;
        }
    }
    check(headCount > 0, "bulon basi cizildi");
    check(headDia > 0.0 && approx(headDia, iso.headAcrossFlats, 1e-6),
          "bulon basi genisligi ISO tablosu (s)");

    // 2) Levha kalinligi: levha cokgeninin DAR ekseni gercek kalinlik.
    const double plateT = plan.plates[0].thickness;
    double plateMinX = 0, plateMinY = 0, plateMaxX = 0, plateMaxY = 0;
    bool plateFound = false;
    for (const auto& p : dr.prims) {
        if (!p.isPlate || p.kind != SchematicPrimKind::Polygon) continue;
        double x0, y0, x1, y1;
        extent(p, x0, y0, x1, y1);
        plateMinX = x0; plateMinY = y0; plateMaxX = x1; plateMaxY = y1;
        plateFound = true;
        break;  // end plate: tek levha
    }
    check(plateFound, "levha cokgeni var");
    if (plateFound) {
        const double extX = plateMaxX - plateMinX;
        const double extY = plateMaxY - plateMinY;
        const double thin = std::min(extX, extY);
        // Levha kalinligi GERCEK mm olarak izdusuyor (Front: kalinlik +Y=screenX).
        check(approx(thin, plateT, 1e-6), "levha kalinligi gercek mm");
    }

    // 3) Bas genisligi / levha kalinligi orani gercege %2 icinde.
    if (headDia > 0.0 && plateT > 0.0) {
        const double realRatio = iso.headAcrossFlats / plateT;
        const double drawnRatio = headDia / plateT;
        const double errPct = std::abs(drawnRatio - realRatio) / realRatio * 100.0;
        check(errPct < 2.0, "bas/kalinlik orani %2 icinde");
    }

    // 4) Deterministik: ayni girdi -> ayni cizim.
    const SchematicDrawing dr2 = buildComponentSchematic(plan, in, SchematicViewDir::Front);
    bool same = dr2.prims.size() == dr.prims.size();
    for (std::size_t i = 0; same && i < dr.prims.size(); ++i) {
        const auto& a = dr.prims[i];
        const auto& b = dr2.prims[i];
        if (a.kind != b.kind || a.pts.size() != b.pts.size() || a.role != b.role ||
            a.detail != b.detail || !approx(a.cx, b.cx) || !approx(a.cy, b.cy) ||
            !approx(a.r, b.r) || !approx(a.rInner, b.rInner))
            same = false;
        for (std::size_t k = 0; same && k < a.pts.size(); ++k)
            same = approx(a.pts[k].x, b.pts[k].x) && approx(a.pts[k].y, b.pts[k].y);
    }
    check(same, "deterministik (ayni girdi -> ayni cizim)");

    // --- Haunch 40 (varsayilan) ----------------------------------------------
    ComponentInput hIn;
    hIn.main.id = EntityId{45};
    hIn.main.axisFrom = {0, 0, 0};
    hIn.main.axisTo = {0, 0, 5000};
    hIn.main.refNormal = {0, 0, 1};
    hIn.main.secWidth = 240.0;
    hIn.main.secHeight = 230.0;
    hIn.main.secWebThickness = 7.5;
    hIn.main.secFlangeThickness = 12.0;
    MemberGeometry hBeam;
    hBeam.id = EntityId{46};
    hBeam.axisFrom = {0, 1200, 5000};
    hBeam.axisTo = {0, 4000, 5000};
    hBeam.refNormal = {0, 0, 1};
    hBeam.secWidth = 220.0;
    hBeam.secHeight = 450.0;
    hBeam.secWebThickness = 9.4;
    hBeam.secFlangeThickness = 14.0;
    hIn.secondary.push_back(hBeam);
    GenerationPlan hPlan = ComponentRegistry::instance().generate(
        L"tekla_haunch_40", hIn);
    if (hPlan.ok) {
        const SchematicDrawing hDr = buildComponentSchematic(hPlan, hIn, SchematicViewDir::Front);
        check(hDr.ok, "haunch 40 sematigi (Front) uretildi");
        bool hasPlate = false, hasBolt = false;
        for (const auto& p : hDr.prims) {
            if (p.isPlate) hasPlate = true;
            if (p.role == "bolt") hasBolt = true;
        }
        check(hasPlate && hasBolt, "haunch 40 sematikte levha + bulon var");
    } else {
        std::printf("SKIP haunch 40 (plan reddedildi: %ls)\n", hPlan.error.c_str());
    }

    if (failures) {
        std::printf("%d FAIL\n", failures);
        return 1;
    }
    std::printf("ALL PASS\n");
    return 0;
}
