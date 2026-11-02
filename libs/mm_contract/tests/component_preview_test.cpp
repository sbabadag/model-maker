// MM-6 (a) — ANCHOR COZUCU + OLCU HESABI testi (OCC GEREKTIRMEZ).
//
// Onizleme olculeri veriyle tanimlanir: { param, from: "<rol>.<anchor>", to, axis }.
// Bu test anchor cozumunun GenerationPlan + MemberGeometry uzerinden DOGRU 3B
// noktalari ve dogru olculeri urettigini olcer (cizim Qt katmaninda).
#include "model_maker/component_codec.hpp"
#include "model_maker/component_engine.hpp"
#include "model_maker/component_preview.hpp"
#include "model_maker/geometry.hpp"

#include <cmath>
#include <cstdio>

using namespace mm;

static int checks = 0, failures = 0;
static void check(bool ok, const char* what) {
    ++checks;
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
}
static bool approx(double a, double b, double eps = 1e-6) { return std::fabs(a - b) < eps; }

static ComponentInput makeInput() {
    ComponentInput in;
    in.main.id = 1;
    in.main.axisFrom = {0, 0, 0};
    in.main.axisTo = {0, 0, 3000};
    in.main.refNormal = {0, 0, 1};
    in.main.secWidth = 300;
    in.main.secHeight = 300;
    in.main.secWebThickness = 11;
    in.main.secFlangeThickness = 19;

    MemberGeometry b;
    b.id = 2;
    b.axisFrom = {0, 0, 3000};
    b.axisTo = {2000, 0, 3000};
    b.refNormal = {0, 0, 1};
    b.secWidth = 200;
    b.secHeight = 400;
    b.secWebThickness = 8.6;
    b.secFlangeThickness = 13.5;
    in.secondary = {b};
    return in;
}

int main() {
    registerBuiltInComponents(ComponentRegistry::instance());
    const ComponentInput in = makeInput();
    const GenerationPlan plan = generateEndPlate(in);
    check(plan.ok, "0 plan ok");
    if (!plan.ok) return 1;
    check(!plan.plateKeys.empty() && plan.plateKeys[0] == "end_plate", "0 levha roleKey end_plate");
    const PlateSpec& plate = plan.plates.front();

    // 1) Levha anchor'lari: merkez, kenarlar, yuzler.
    {
        const PreviewAnchor c = resolvePreviewAnchor(plan, in, L"end_plate.center");
        check(c.ok && approx(c.point.x, plate.center.x) && approx(c.point.y, plate.center.y) &&
                  approx(c.point.z, plate.center.z),
              "1 plate.center levha merkezi");
        check(c.hasFrame, "1 levha cercevesi gelir (olcu ekseni icin)");

        const PreviewAnchor uMax = resolvePreviewAnchor(plan, in, L"end_plate.edge_u_max");
        const double du = (uMax.point.x - plate.center.x) * plate.u.x +
                          (uMax.point.y - plate.center.y) * plate.u.y +
                          (uMax.point.z - plate.center.z) * plate.u.z;
        check(uMax.ok && approx(du, plate.width * 0.5), "1 edge_u_max = +w/2");

        const PreviewAnchor vMin = resolvePreviewAnchor(plan, in, L"end_plate.edge_v_min");
        const double dv = (vMin.point.x - plate.center.x) * plate.v.x +
                          (vMin.point.y - plate.center.y) * plate.v.y +
                          (vMin.point.z - plate.center.z) * plate.v.z;
        check(vMin.ok && approx(dv, -plate.height * 0.5), "1 edge_v_min = -h/2");

        const PreviewAnchor near = resolvePreviewAnchor(plan, in, L"end_plate.face_near");
        const PreviewAnchor far = resolvePreviewAnchor(plan, in, L"end_plate.face_far");
        const double dt = (far.point.x - near.point.x) * plate.axis.x +
                          (far.point.y - near.point.y) * plate.axis.y +
                          (far.point.z - near.point.z) * plate.axis.z;
        check(near.ok && far.ok && approx(dt, plate.thickness), "1 face_far - face_near = t");
    }

    // 2) Uye anchor'lari (secondary_0 = kiris).
    {
        const PreviewAnchor start = resolvePreviewAnchor(plan, in, L"secondary_0.start");
        check(start.ok && approx(start.point.x, 0) && approx(start.point.z, 3000),
              "2 secondary_0.start kiris eksen basi");
        const PreviewAnchor top = resolvePreviewAnchor(plan, in, L"secondary_0.face_top");
        check(top.ok, "2 secondary_0.face_top cozuldu");
        if (top.ok) {
            // face_top: kesit cercevesinde +h/2 yonunde (dunya +Z'ye yakin).
            check(top.point.z > 3000.0, "2 face_top kiris ekseninin USTUNDE");
        }
        const PreviewAnchor end = resolvePreviewAnchor(plan, in, L"secondary_0.end");
        check(end.ok && approx(end.point.x, 2000), "2 secondary_0.end");
    }

    // 3) Bulon anchor'lari.
    {
        check(!plan.boltGroupKeys.empty(), "3 bulon grubu var");
        const std::string key = plan.boltGroupKeys.empty() ? "" : plan.boltGroupKeys[0];
        const std::wstring wkey(key.begin(), key.end());
        const PreviewAnchor c = resolvePreviewAnchor(plan, in, wkey + L".center");
        check(c.ok, "3 bulon merkezi cozuldu");
        const PreviewAnchor rowLast = resolvePreviewAnchor(plan, in, wkey + L".row_last");
        check(rowLast.ok, "3 bulon row_last cozuldu");
    }

    // 4) OLCU: levha kalinligi ve ust tasma.
    {
        PreviewDimension t;
        t.param = L"plateThickness";
        t.from = L"end_plate.face_near";
        t.to = L"end_plate.face_far";
        t.axis = L'w';
        const PreviewMeasurement m = measurePreviewDimension(plan, in, t);
        check(m.ok && approx(m.value, plate.thickness), "4 kalinlik olcusu = t");
        check(m.ok && approx(m.distance, plate.thickness), "4 mutlak mesafe = t");

        PreviewDimension extra;
        extra.param = L"plateExtra";
        extra.from = L"secondary_0.face_top";
        extra.to = L"end_plate.edge_v_max";
        extra.axis = L'v';
        const PreviewMeasurement me = measurePreviewDimension(plan, in, extra);
        check(approx(me.value, (plate.height - in.secondary[0].secHeight) * 0.5),
              "4 ust tasma = (levha h - kesit h)/2");
        const ProfileSectionFrame bsec = memberSectionFrame(in.secondary[0]);
        std::printf("   plate.v=(%.3f,%.3f,%.3f) beamSec.height=(%.3f,%.3f,%.3f)\n",
                    plate.v.x, plate.v.y, plate.v.z, bsec.height.x, bsec.height.y, bsec.height.z);
        std::printf("   ust tasma = %.3f mm (v ekseni), plate.h=%.1f beam.h=%.1f\n",
                    me.value, plate.height, in.secondary[0].secHeight);

        // 5) Hata yollari: bilinmeyen rol / anchor / bicim.
        check(!resolvePreviewAnchor(plan, in, L"yok_boyle_rol.center").ok,
              "5 bilinmeyen rol ok=false");
        check(!resolvePreviewAnchor(plan, in, L"end_plate.yok_anchor").ok,
              "5 bilinmeyen anchor ok=false");
        check(!resolvePreviewAnchor(plan, in, L"bicimsiz").ok, "5 bicimsiz ifade ok=false");
        const PreviewAnchor bad = resolvePreviewAnchor(plan, in, L"yok.center");
        check(!bad.error.empty(), "5 hata mesaji Turkce dolu");
    }

    // 6) TOGGLE: rolePrefix ile ghost rolleri + deger okuma.
    {
        PreviewToggle tg;
        tg.param = L"createBeamStiffener";
        tg.rolePrefix = L"haunch";
        const auto roles = previewToggleRoles(plan, tg);
        check(roles.empty(), "6 end plate planinda 'haunch' oneki yok");
        PreviewToggle tg2;
        tg2.param = L"plateExtra";
        tg2.rolePrefix = L"end_plate";
        const auto roles2 = previewToggleRoles(plan, tg2);
        check(roles2.size() == 1 && roles2[0] == "end_plate", "6 rolePrefix ile rol bulundu");

        std::vector<CompParamSpec> specs;
        CompParamSpec s;
        s.name = L"createWelds";
        s.type = CompParamType::Boolean;
        s.defBool = true;
        specs.push_back(s);
        std::vector<CompParam> params;
        check(previewToggleValue(tg2, params, specs) == false, "6 parametre yoksa sema varsayilani (false)");
        check(previewToggleValue(PreviewToggle{L"createWelds", L""}, params, specs) == true,
              "6 Boolean varsayilani true okunur");
        params.push_back(CompParam{L"createWelds", CompParamValue{false}});
        check(previewToggleValue(PreviewToggle{L"createWelds", L""}, params, specs) == false,
              "6 acik deger varsayilani ezer");
    }

    // 7) codec round-trip: preview dialog.json'a yazilip geri okunur.
    {
        ComponentDialogSchema s;
        s.typeId = L"mm_test";
        PreviewDimension d;
        d.param = L"plateExtra";
        d.from = L"end_plate.edge_v_max";
        d.to = L"secondary_0.face_top";
        d.axis = L'v';
        d.label = L"ust tasma";
        s.preview.dimensions.push_back(d);
        s.preview.toggles.push_back(PreviewToggle{L"createBeamStiffener", L"haunch_beam"});
        const nlohmann::json j = componentDialogSchemaToJson(s);
        const ComponentDialogSchema back = componentDialogSchemaFromJson(j);
        check(back.preview.dimensions.size() == 1, "7 dimension round-trip");
        check(back.preview.dimensions[0].param == L"plateExtra" &&
                  back.preview.dimensions[0].axis == L'v' &&
                  back.preview.dimensions[0].from == L"end_plate.edge_v_max",
              "7 dimension alanlari korunur");
        check(back.preview.toggles.size() == 1 &&
                  back.preview.toggles[0].rolePrefix == L"haunch_beam",
              "7 toggle round-trip");
        // Bos preview JSON'a YAZILMAZ (eski paketler degismesin).
        ComponentDialogSchema empty;
        empty.typeId = L"mm_empty";
        const nlohmann::json je = componentDialogSchemaToJson(empty);
        check(!je.contains("preview"), "7 bos preview yazilmaz (geriye uyum)");
    }

    std::printf("\n%d/%d checks gecti\n", checks - failures, checks);
    return failures == 0 ? 0 : 1;
}
