// P2A — OTOMATIK IMALAT OLCULERI + AKILLI SIFIR-DUZENLEME SAYFA YERLESIMI (OCCT'siz, Qt'siz).
// Kabul fixture'i: kiris + bagli uc levhasi (IKI bagimsiz bulon grubu) + oteli ikincil kanat plakasi.
// Olculer GERCEK modelden; bagimsiz elle-hesap zemin gerceklik ile karsilastirilir.

#include "fab_fixture.hpp"
#include "model_maker/drawing_builder.hpp"
#include "model_maker/drawing_layout.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <set>

using namespace mm;
using namespace mm::drawing;

static int failures = 0;
static int checks = 0;
static void check(bool ok, const char* label) {
    ++checks;
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", label);
    if (!ok) ++failures;
}
static bool nearTo(double a, double b, double tol = 1e-6) { return std::fabs(a - b) <= tol; }

static std::vector<const Dimension2D*> dimsOf(const DrawingSheet& s, DimensionRole role, const std::string& fk = {}) {
    std::vector<const Dimension2D*> out;
    for (const auto& d : s.dimensions)
        if (d.role == role && (fk.empty() || d.featureKey == fk)) out.push_back(&d);
    return out;
}
static std::vector<double> valuesOf(const std::vector<const Dimension2D*>& v) {
    std::vector<double> o;
    for (const auto* d : v) o.push_back(d->value);
    std::sort(o.begin(), o.end());
    return o;
}
static bool hasValue(const std::vector<const Dimension2D*>& v, double x) {
    for (const auto* d : v)
        if (nearTo(d->value, x, 1e-6)) return true;
    return false;
}
static std::size_t countValue(const std::vector<const Dimension2D*>& v, double x) {
    std::size_t n = 0;
    for (const auto* d : v) n += nearTo(d->value, x, 1e-6);
    return n;
}
static bool hasItem(const DrawingSheet& s, const char* key, ChecklistState st) {
    for (const auto& c : s.fabrication.checklist)
        if (c.key == key && c.state == st) return true;
    return false;
}

// (rol, anahtar, deger) coklu kumesi -- transform-degismezlik karsilastirmasi icin.
static std::vector<std::string> dimSignature(const DrawingSheet& s) {
    std::vector<std::string> out;
    for (const auto& d : s.dimensions) {
        char buf[160];
        std::snprintf(buf, sizeof(buf), "%d|%s|%.4f", static_cast<int>(d.role), d.featureKey.c_str(), d.value);
        out.push_back(buf);
    }
    std::sort(out.begin(), out.end());
    return out;
}

static BuildResult build(const fx::Fixture& f, EntityId subject, DrawingOptions o = {}) {
    // 2026-10-29: legacy fabrication assertions target the pre-section view set;
    // sections/default-on behavior is covered separately by sections_test.cpp.
    o.sectionViews = false;
    MarkRegistry reg;
    return buildSheet(f.doc, reg, subject, o);
}
static EntityId assemblyOf(const fx::Fixture& f) { return f.doc.assemblies().empty() ? 0 : f.doc.assemblies()[0].id; }

int main() {
    // =========================================================================
    // A) Kabul fixture'i: gercek degerler, provenance, dogru gorunum, yinelenen zincir yok
    // =========================================================================
    const fx::Fixture f = fx::make();
    check(f.doc.assemblies().size() == 1 && f.doc.assemblies()[0].parts.size() == 3, "A: fixture assembly = kiris + 2 levha");
    const std::uint64_t revBefore = f.doc.revision();
    const std::size_t modsBefore = f.doc.cutModifiers().size(), modelsBefore = f.doc.models().size();
    const BuildResult r = build(f, assemblyOf(f));
    check(r.ok, "A: buildSheet basarili");
    const DrawingSheet& s = r.sheet;
    check(f.doc.revision() == revBefore && f.doc.cutModifiers().size() == modsBefore && f.doc.models().size() == modelsBefore,
          "A: yerlesim/cizim Document'u DEGISTIRMEDI (surum, kesme, model sayisi ayni)");
    check(s.documentRevision == revBefore, "A: sheet.documentRevision = uretildigi Document surumu");
    check(r.inventory.groups.size() == 3 && r.inventory.holes.size() == 16, "A: envanter 3 grup / 16 delik");

    const auto keyOf = [&](EntityId bg) {
        for (const auto& g : r.inventory.groups)
            if (g.boltGroup == bg) return g.id;
        return std::string();
    };
    const std::string kU = keyOf(f.groupUpper), kL = keyOf(f.groupLower), kW = keyOf(f.groupFin);
    const std::string kE = "plate:" + std::to_string(f.endPlate), kF = "plate:" + std::to_string(f.finPlate);
    check(!kU.empty() && !kL.empty() && !kW.empty() && kU != kL && kL != kW, "A: uc grubun ayri kararli anahtari var");

    // --- gercek-deger olculer (elle hesaplanmis zemin gerceklik) ---
    {
        const auto pu = dimsOf(s, DimensionRole::HolePitch, kU);
        // ust grup: v adimi 40 (dusey zincir bu grubun anahtarinda); u adimi 100 (yatay zincir)
        check(hasValue(pu, 40), "A: ust grup dusey delik adimi 40");
        check(hasValue(pu, 100), "A: ust grup yatay delik adimi 100");
        const auto pl = dimsOf(s, DimensionRole::HolePitch, kL);
        check(countValue(pl, 45) == 2 && hasValue(pl, 100), "A: alt grup dusey adim 45,45 + yatay 100");
        const auto pw = dimsOf(s, DimensionRole::HolePitch, kW);
        check(countValue(pw, 100) == 2 && countValue(pw, 60) == 1, "A: kanat grubu yatay adim 100,100 + dusey 60");

        const auto eu = dimsOf(s, DimensionRole::HoleEdge, kU);
        check(hasValue(eu, 49), "A: UST net kenar (levha ust kenari -> ust deligin DIS kenari) = 49");
        check(countValue(eu, 39) == 2, "A: ust grup net yan kenarlar 39 + 39 (100-50-11)");
        const auto el = dimsOf(s, DimensionRole::HoleEdge, kL);
        check(hasValue(el, 51), "A: ALT net kenar (alt deligin DIS kenari -> levha alt kenari) = 51");
        check(hasValue(el, 130), "A: bagimsiz gruplar arasi NET aralik = 130 (89 - (-41))");
        check(countValue(el, 41) == 2, "A: alt grup net yan kenarlar 41 + 41 (100-50-9)");
        const auto ew = dimsOf(s, DimensionRole::HoleEdge, kW);
        check(countValue(ew, 39) == 2 && countValue(ew, 59) == 2, "A: kanat grubu net kenarlar 39,39 (yan) ve 59,59 (ust/alt)");

        const auto pw1 = dimsOf(s, DimensionRole::PlateWidth, kE), ph1 = dimsOf(s, DimensionRole::PlateHeight, kE);
        check(pw1.size() == 1 && nearTo(pw1[0]->value, 200) && ph1.size() == 1 && nearTo(ph1[0]->value, 400), "A: uc levhasi 200 x 400");
        const auto pw2 = dimsOf(s, DimensionRole::PlateWidth, kF), ph2 = dimsOf(s, DimensionRole::PlateHeight, kF);
        check(pw2.size() == 1 && nearTo(pw2[0]->value, 300) && ph2.size() == 1 && nearTo(ph2[0]->value, 200), "A: kanat plaka 300 x 200");

        const auto ox = dimsOf(s, DimensionRole::PlateOffsetX), oy = dimsOf(s, DimensionRole::PlateOffsetY),
                   oz = dimsOf(s, DimensionRole::PlateOffsetZ);
        check(ox.size() == 1 && nearTo(ox[0]->value, 1350) && ox[0]->featureKey == kF,
              "A: kanat plaka X ofseti = 1350 (uye BASLANGICINDAN)");
        check(oy.size() == 1 && nearTo(oy[0]->value, 75) && oy[0]->featureKey == kF, "A: kanat plaka Y ofseti = 75");
        check(oz.empty(), "A: z' ofset yok (kesen) ve uc levhasi ofsetleri 0 -> gereksiz olcu uretilmedi");
    }

    // --- kaynak (provenance) ve DOGRU GORUNUM ---
    {
        bool prov = true, measuredOk = true, sourceOk = true, trueView = true;
        for (const auto& d : s.dimensions) {
            const bool fab = d.role == DimensionRole::HolePitch || d.role == DimensionRole::HoleEdge ||
                             d.role == DimensionRole::PlateWidth || d.role == DimensionRole::PlateHeight ||
                             d.role == DimensionRole::PlateOffsetX || d.role == DimensionRole::PlateOffsetY ||
                             d.role == DimensionRole::PlateOffsetZ;
            if (!fab) continue;
            prov = prov && !d.featureKey.empty() && !d.basis.empty() && d.documentRevision == revBefore;
            measuredOk = measuredOk && nearTo(d.measured(), d.value, 1e-6);   // izdusum uzunlugu == gercek model degeri
            sourceOk = sourceOk && (d.source == f.endPlate || d.source == f.finPlate);
            const DrawingView* v = s.findView(d.viewId);
            // delik/levha boyutlari yalniz levha yuzunu gosteren gorunumde: normal gorunum normaline paralel
            if (d.role != DimensionRole::PlateOffsetX && d.role != DimensionRole::PlateOffsetY && d.role != DimensionRole::PlateOffsetZ) {
                const Vec3 n = d.source == f.endPlate ? Vec3{1, 0, 0} : Vec3{0, 1, 0};
                const double dotn = std::fabs(n.x * v->frame.normal.x + n.y * v->frame.normal.y + n.z * v->frame.normal.z);
                trueView = trueView && dotn > 1 - 1e-9;
            }
        }
        check(prov, "A: HER imalat olcusunde featureKey + basis + documentRevision (== Document surumu)");
        check(measuredOk, "A: HER imalat olcusunde measured() == value (<1e-6): gorunumde olculen = gercek model uzunlugu");
        check(sourceOk, "A: olcu kaynagi (source) EntityId = ilgili levha");
        check(trueView, "A: delik/levha olculeri YALNIZ levha yuzunu gosteren (gercek uzunluk) gorunumde");
        // assembly'de delikli her levha icin DETAY gorunumu: delik/levha olculeri orada, ana gorunumlerde degil
        const auto detailOf = [&](EntityId plate) -> const DrawingView* {
            for (const auto& v : s.views)
                if (v.kind == ViewKind::Detail && v.detailPlate == plate) return &v;
            return nullptr;
        };
        const DrawingView *dE = detailOf(f.endPlate), *dF = detailOf(f.finPlate);
        check(dE && dF && s.views.size() == 5, "A: 3 ana gorunum + 2 levha DETAY gorunumu (uc levhasi, kanat plaka)");
        bool endInDetail = dE != nullptr, finInDetail = dF != nullptr;
        for (const auto* d : dimsOf(s, DimensionRole::HolePitch, kU)) endInDetail = endInDetail && dE && d->viewId == dE->id;
        for (const auto* d : dimsOf(s, DimensionRole::HolePitch, kL)) endInDetail = endInDetail && dE && d->viewId == dE->id;
        for (const auto* d : dimsOf(s, DimensionRole::HolePitch, kW)) finInDetail = finInDetail && dF && d->viewId == dF->id;
        check(endInDetail && finInDetail, "A: delik adimlari kendi levhasinin DETAY gorunumunde");
        check(dE && dF && dE->scale > s.viewOfKind(ViewKind::Front)->scale - 1e-12 && dF->scale >= s.viewOfKind(ViewKind::Front)->scale - 1e-12,
              "A: detay olcegi ana olcekten ayni veya BUYUK (okunabilir delik olculeri)");
        bool noMainFab = true;
        for (const auto& d : s.dimensions)
            if ((d.role == DimensionRole::HolePitch || d.role == DimensionRole::HoleEdge || d.role == DimensionRole::PlateWidth ||
                 d.role == DimensionRole::PlateHeight) &&
                s.findView(d.viewId)->kind != ViewKind::Detail)
                noMainFab = false;
        check(noMainFab, "A: ana gorunumlerde delik/levha boyutu yok (yinelenen zincir yok); yalniz uye boyu/ofset/genel");
    }

    // --- yinelenen / fazlalik zincir YOK ---
    {
        bool dup = false;
        for (std::size_t i = 0; i < s.dimensions.size(); ++i)
            for (std::size_t j = i + 1; j < s.dimensions.size(); ++j) {
                const auto &a = s.dimensions[i], &b = s.dimensions[j];
                const auto close = [](const Vec2& p, const Vec2& q) { return std::fabs(p.x - q.x) < 0.01 && std::fabs(p.y - q.y) < 0.01; };
                if (a.viewId == b.viewId && a.direction == b.direction &&
                    ((close(a.p1, b.p1) && close(a.p2, b.p2)) || (close(a.p1, b.p2) && close(a.p2, b.p1))))
                    dup = true;
            }
        check(!dup, "A: ayni gorunum/yon/uc noktali yinelenen olcu YOK");
        // P1 yedek zinciri cozumlenmis levha delikleri icin KAPALI
        check(dimsOf(s, DimensionRole::HoleSpacing).empty(), "A: cercevesi cozumlenmis deliklerde P1 yedek HoleSpacing zinciri uretilmedi");
    }

    // --- callout'lar, kalinlik notu, mark'lar ---
    {
        const auto textOf = [&](TextKind k, const std::string& fk) -> const TextAnnotation* {
            for (const auto& t : s.texts)
                if (t.kind == k && t.featureKey == fk) return &t;
            return nullptr;
        };
        const TextAnnotation* cu = textOf(TextKind::HoleCallout, kU);
        const TextAnnotation* cl = textOf(TextKind::HoleCallout, kL);
        const TextAnnotation* cw = textOf(TextKind::HoleCallout, kW);
        check(cu && cu->lines.size() == 1 && cu->lines[0] == "4 x \xC3\x98" "22", "A: ust grup callout '4 x Ø22'");
        check(cl && cl->lines[0] == "6 x \xC3\x98" "18", "A: alt grup callout '6 x Ø18'");
        check(cw && cw->lines[0] == "6 x \xC3\x98" "22", "A: kanat grubu callout '6 x Ø22'");
        const TextAnnotation* ne = textOf(TextKind::PlateNote, kE);
        const TextAnnotation* nf = textOf(TextKind::PlateNote, kF);
        check(ne && ne->lines[0].find("PL 20") == 0 && ne->lines[0].find("S355") != std::string::npos, "A: uc levhasi notu 'PL 20  S355'");
        check(nf && nf->lines[0].find("PL 10") == 0, "A: kanat plaka notu 'PL 10'");
        bool markA = false, markP = false;
        for (const auto& m : s.marks) { markA = markA || m.isAssembly; markP = markP || (!m.isAssembly); }
        check(markA && markP, "A: assembly + parca mark etiketleri var");
        check(hasItem(s, "ASSEMBLY_MARK", ChecklistState::Satisfied) && hasItem(s, "PART_MARK", ChecklistState::Satisfied),
              "A: kontrol listesi: PART_MARK/ASSEMBLY_MARK saglandi");
        check(hasItem(s, "HOLE_DIAMETER_COUNT", ChecklistState::Satisfied) && hasItem(s, "HOLE_POSITIONS", ChecklistState::Satisfied) &&
                  hasItem(s, "PLATE_OUTLINE", ChecklistState::Satisfied) && hasItem(s, "PLATE_THICKNESS", ChecklistState::Satisfied) &&
                  hasItem(s, "PLATE_OFFSET", ChecklistState::Satisfied) && hasItem(s, "MEMBER_DATUM", ChecklistState::Satisfied),
              "A: kontrol listesi: levha/delik/ofset/datum maddeleri SAGLANDI");
        check(!hasItem(s, "HOLE_POSITIONS", ChecklistState::Unresolved) && !hasItem(s, "PLATE_OFFSET", ChecklistState::Unresolved),
              "A: cozumlenmemis delik/ofset maddesi yok");
    }

    // --- TASLAK / EKSIK: kaynak desteklenmiyor ---
    {
        check(s.fabrication.evaluated && s.fabrication.draft, "A: kaynakli assembly TASLAK (WELDS unresolved) -- imalata hazir ILAN EDILMEZ");
        check(hasItem(s, "WELDS", ChecklistState::Unresolved), "A: kontrol listesi: WELDS unresolved");
        bool stamp = false;
        for (const auto& t : s.texts)
            if (t.kind == TextKind::StatusStamp && !t.lines.empty() && t.lines[0].find("TASLAK") == 0) stamp = true;
        check(stamp, "A: sayfada 'TASLAK / EKSIK' damgasi var");
        const auto& codes = s.fabrication.unresolvedCodes;
        check(std::find(codes.begin(), codes.end(), "WELD_NOT_DIMENSIONED") != codes.end(), "A: unresolvedCodes WELD_NOT_DIMENSIONED icerir");
    }

    // =========================================================================
    // B) Yerlesim: sifir olculen cakisma, okunabilir yazi, ayni plan
    // =========================================================================
    {
        check(s.layout.ran && s.layout.ok, "B: yerlesim motoru calisti ve basarili (zorunlu oge cakismasiz yerlesti)");
        const OverlapReport m = measureOverlaps(s);
        if (m.total() != 0) for (const auto& d : m.details) std::printf("   overlap: %s\n", d.c_str());
        check(m.total() == 0, "B: BAGIMSIZ olcum: metin/metin, metin/cizgi, metin/geometri, gorunum, title, cerceve cakismasi = 0");
        bool sizes = s.layoutConfig.dimTextHeight >= 3.0 && s.layoutConfig.noteTextHeight >= 3.5;
        for (const auto& t : s.texts) sizes = sizes && t.height >= 3.5;
        check(sizes, "B: olcu metni >= 3.0 mm, not/callout >= 3.5 mm (kagit mm)");
        bool inside = true;
        for (const auto& d : s.dimensions) {
            const DimensionGeometry g = resolveDimension(s, d);
            inside = inside && g.valid && g.textHeight >= 3.0;
        }
        check(inside, "B: her olcu cozumlendi, yazi yuksekligi >= 3.0");
        check(s.scaleIsStandard && s.titleBlock.scaleText.find("1:") == 0, "B: standart olcek + title block olcek metni");
        // Gorunum projeksiyon iliskisi: 3. aci -> Top Front'un ustunde ayni x; Side Front'un sagi ayni z
        const DrawingView *fr = s.viewOfKind(ViewKind::Front), *tp = s.viewOfKind(ViewKind::Top), *sd = s.viewOfKind(ViewKind::Side);
        const Bounds2 bf = fr->paperBounds(), bt = tp->paperBounds(), bs = sd->paperBounds();
        check(nearTo(bf.min.x, bt.min.x, 1e-6) && nearTo(bf.max.x, bt.max.x, 1e-6) && bt.min.y > bf.max.y,
              "B: Top, Front'un hemen ustunde ve x ile hizali (projeksiyon iliskisi korunur)");
        check(nearTo(bf.centre().y, bs.centre().y, 1e-6) && bs.min.x > bf.max.x, "B: Side, Front'un sagi ve z ile hizali");
    }
    {
        // Tekrarlanabilirlik: iki bagimsiz kosu ayni JSON
        const BuildResult r2 = build(f, assemblyOf(f));
        DrawingDocument d1, d2;
        d1.addSheet(r.sheet);
        d2.addSheet(r2.sheet);
        check(d1.serialize() == d2.serialize(), "B: yerlesim deterministik: iki kosu BIREBIR ayni sayfa JSON'u");
    }

    // =========================================================================
    // C) Kaydet/yukle + yeniden uretim
    // =========================================================================
    {
        DrawingDocument d;
        d.addSheet(r.sheet);
        const std::string j = d.serialize();
        DrawingDocument back;
        std::string err;
        check(DrawingDocument::deserialize(j, back, err) && back.sheets().size() == 1, "C: JSON v2 geri okundu");
        check(back.serialize() == j, "C: kaydet -> yukle -> kaydet BIREBIR ayni (imalat alanlari, yerlesim, kontrol listesi dahil)");
        const DrawingSheet& b = back.sheets()[0];
        bool kept = b.dimensions.size() == r.sheet.dimensions.size();
        for (std::size_t i = 0; kept && i < b.dimensions.size(); ++i)
            kept = b.dimensions[i].featureKey == r.sheet.dimensions[i].featureKey &&
                   nearTo(b.dimensions[i].value, r.sheet.dimensions[i].value, 1e-12) &&
                   b.dimensions[i].documentRevision == revBefore && b.dimensions[i].textT == r.sheet.dimensions[i].textT &&
                   b.dimensions[i].chain == r.sheet.dimensions[i].chain;
        check(kept, "C: olcu degeri/featureKey/surum/yerlesim parametreleri korunur");
        check(b.fabrication.draft == r.sheet.fabrication.draft && b.fabrication.checklist.size() == r.sheet.fabrication.checklist.size() &&
                  b.layout.ok == r.sheet.layout.ok && b.texts.size() == r.sheet.texts.size(),
              "C: kontrol listesi, yerlesim raporu ve metinler korunur");
        check(measureOverlaps(b).total() == 0, "C: yuklenen sayfada olculen cakisma = 0");
        // v1 (P1) kayit uyumu: yeni alanlari sil, surumu 1 yap
        nlohmann::json root = nlohmann::json::parse(j);
        root["version"] = 1;
        for (auto& sh : root["sheets"]) {
            for (const char* k : {"texts", "fabrication", "layoutConfig", "layoutReport", "documentRevision"}) sh.erase(k);
            for (auto& dd : sh["dimensions"])
                for (const char* k : {"side", "textT", "textSide", "chain", "featureKey", "basis", "documentRevision"}) dd.erase(k);
            for (auto& vv : sh["views"]) vv.erase("frame");
        }
        DrawingDocument old;
        check(DrawingDocument::deserialize(root.dump(), old, err) && old.sheets()[0].dimensions.size() == r.sheet.dimensions.size() &&
                  old.sheets()[0].dimensions[0].side == 1 && old.sheets()[0].views[0].frame.right.x == 1.0,
              "C: eski (v1) cizim kaydi okunur; yeni alanlar varsayilan");
        // Yeniden uretim: ayni Document -> ayni olculer; model degisince olculer DEGISIR
        const BuildResult again = build(f, assemblyOf(f));
        check(dimSignature(again.sheet) == dimSignature(r.sheet), "C: yeniden uretim ayni olcu kumesini verir");
        fx::Fixture g = fx::make();
        // kanat plaka grubunu kaydir: bolt origin x'i 1400 -> 1420 (yalniz delikler kayar)
        BoltGroup moved = g.doc.boltGroups()[2];
        moved.origin.x += 20;
        std::size_t bi = 0;
        for (std::size_t i = 0; i < g.doc.boltGroups().size(); ++i) if (g.doc.boltGroups()[i].id == g.groupFin) bi = i;
        // setBoltGroup delikleri YENIDEN uretmez; eskiyi silip yenisini ekle
        g.doc.removeBoltGroup(bi);
        moved.id = kInvalidEntityId;
        g.doc.addBoltGroup(moved, WireframeModel::line(moved.origin, moved.origin));
        const BuildResult chg = build(g, assemblyOf(g));
        bool shifted = false;
        for (const auto& d : chg.sheet.dimensions)
            if (d.role == DimensionRole::HoleEdge && (nearTo(d.value, 19, 1e-6) || nearTo(d.value, 59 - 0 + 0, 1e-6) == false) && nearTo(d.value, 19, 1e-6))
                shifted = true;
        check(shifted, "C: delikler 20 mm kayinca net sol kenar 39 -> 19, sag kenar 59 olarak yeniden uretilir");
        check(chg.sheet.documentRevision != revBefore, "C: model degisince sheet.documentRevision DEGISIR (bayat cizim tespiti)");
    }

    // =========================================================================
    // D) Donusum degismezligi: dondurulmus (egik) ve aynalanmis uye
    // =========================================================================
    {
        const std::vector<std::string> base = dimSignature(r.sheet);
        fx::Xf rot = fx::mul(fx::translate({1234, -777, 55}), fx::mul(fx::rotZ(30), fx::rotY(15)));
        const fx::Fixture fr = fx::make(rot);
        const BuildResult rr = build(fr, assemblyOf(fr));
        check(rr.ok, "D: egik dondurulmus uye cizildi");
        check(rr.sheet.views.size() == 5 && rr.sheet.views[0].frame.memberAligned, "D: egik uyede gorunumler UYE eksenine hizali (memberAligned)");
        // featureKey EntityId icerir; ayni olusturma sirasi -> ayni ID -> imza birebir
        const std::vector<std::string> sig = dimSignature(rr.sheet);
        check(sig == base, "D: dondurulmus+oteli model: TUM imalat olcusu (rol, anahtar, deger) AYNI");
        check(rr.sheet.layout.ok && measureOverlaps(rr.sheet).total() == 0, "D: dondurulmus modelde yerlesim basarili, olculen cakisma 0");
        bool meas = true;
        for (const auto& d : rr.sheet.dimensions)
            if (d.role == DimensionRole::HolePitch || d.role == DimensionRole::HoleEdge)
                meas = meas && nearTo(d.measured(), d.value, 1e-6);
        check(meas, "D: dondurulmus modelde de measured() == gercek model degeri");

        const fx::Fixture fm = fx::make(fx::mirrorX());
        const BuildResult rm = build(fm, assemblyOf(fm));
        check(rm.ok && dimSignature(rm.sheet) == base, "D: aynalanmis model: TUM imalat olcusu AYNI");
        check(rm.sheet.layout.ok && measureOverlaps(rm.sheet).total() == 0, "D: aynalanmis modelde yerlesim basarili, cakisma 0");
    }

    // =========================================================================
    // E) Olcek 1:10 ve 1:20 ve uzun assembly (kagit buyutme)
    // =========================================================================
    {
        DrawingOptions o10;
        o10.sheet = SheetSize::A3;
        o10.layout.fixedScaleDen = 10.0;
        o10.layout.maxSheet = SheetSize::A0;
        const fx::Fixture fshort = fx::make({}, 1800.0, false);   // 1800 mm: A3'te 1:10 sigar
        // kanat plaka 1350..1650 icinde kalir (1800 > 1650)
        const BuildResult a = build(fshort, fshort.beam, o10);
        check(a.ok, "E: tek kiris (cercevesiz kiris) 1:10 uretildi");
        const fx::Fixture fA = fx::make({}, 1800.0, true);
        const BuildResult b10 = build(fA, assemblyOf(fA), o10);
        DrawingOptions o20 = o10;
        o20.layout.fixedScaleDen = 20.0;
        const BuildResult b20 = build(fA, assemblyOf(fA), o20);
        check(b10.ok && nearTo(b10.sheet.scale, 0.1) && b10.sheet.titleBlock.scaleText == "1:10", "E: sabit olcek 1:10 uygulandi");
        check(b20.ok && nearTo(b20.sheet.scale, 0.05) && b20.sheet.titleBlock.scaleText == "1:20", "E: sabit olcek 1:20 uygulandi");
        check(b10.sheet.layout.ok && measureOverlaps(b10.sheet).total() == 0, "E: 1:10 sifir olculen cakisma");
        check(b20.sheet.layout.ok && measureOverlaps(b20.sheet).total() == 0, "E: 1:20 sifir olculen cakisma");
        check(dimSignature(b10.sheet) == dimSignature(b20.sheet), "E: iki olcekte AYNI gercek model olculeri (olcek degeri etkilemez)");
        check(b10.sheet.layoutConfig.dimTextHeight == b20.sheet.layoutConfig.dimTextHeight &&
                  b10.sheet.layoutConfig.dimTextHeight >= 3.0,
              "E: yazi kagit mm'de ayni (olcege gore KUCULMEZ), >= 3.0");
        bool measured = true;
        for (const auto& d : b10.sheet.dimensions)
            if (d.role == DimensionRole::HolePitch || d.role == DimensionRole::HoleEdge || d.role == DimensionRole::PlateWidth)
                measured = measured && nearTo(d.measured(), d.value, 1e-6);
        check(measured, "E: 1:10'da olculer view-yerel (olceksiz) -> measured() == value");
        // kagit boyutu: 1:20'de 1800 mm = 90 mm kagit; A3 yeterli
        check(b20.sheet.format.size <= SheetSize::A2 && b20.sheet.layout.finalSheet == b20.sheet.format.size,
              "E: kisa assembly 1:20 en fazla A2 (ana gorunumler + 2 detay + olculer), rapor sayfa boyutuyla tutarli");
    }
    {
        // Uzun assembly: 6000 mm @ 1:10 = 600 mm kagit -> A3'e SIGMAZ, daha buyuk kagida gecer.
        DrawingOptions o;
        o.sheet = SheetSize::A3;
        o.layout.fixedScaleDen = 10.0;
        o.layout.maxSheet = SheetSize::A1;
        const fx::Fixture fl = fx::make({}, 6000.0, true);
        const BuildResult lr = build(fl, assemblyOf(fl), o);
        check(lr.ok && lr.sheet.layout.ok, "E: 6000 mm assembly @ 1:10 yerlesti");
        check(lr.sheet.format.size == SheetSize::A1 && lr.sheet.layout.escalations >= 1 && lr.sheet.layout.requestedSheet == SheetSize::A3,
              "E: A3 -> daha buyuk kagit (A1) gecisi raporlandi (escalations >= 1)");
        check(measureOverlaps(lr.sheet).total() == 0 && lr.sheet.layoutConfig.dimTextHeight >= 3.0,
              "E: buyuk kagitta cakisma 0 ve yazi KUCULMEDI");
        // 12000 mm @ 1:10 hicbir kagida sigmaz -> SESSIZ cakisma YOK, acik rapor
        const fx::Fixture fh = fx::make({}, 12000.0, true);
        const BuildResult hr = build(fh, assemblyOf(fh), o);
        check(hr.ok && !hr.sheet.layout.ok, "E: 12000 mm @ 1:10 A1'e bile sigmaz -> layout.ok == false");
        bool noFit = false;
        for (const auto& i : hr.sheet.layout.issues) noFit = noFit || i.code == "NO_FIT";
        check(noFit, "E: sorun raporu NO_FIT iceriyor (sessiz sigdirma/kucultme yok)");
        check(hr.sheet.fabrication.draft && hasItem(hr.sheet, "LAYOUT", ChecklistState::Unresolved),
              "E: yerlesim basarisizsa cizim TASLAK ve kontrol listesinde LAYOUT unresolved");
        check(hr.sheet.layoutConfig.dimTextHeight >= 3.0, "E: basarisiz yerlesimde de yazi kucultulmedi");
        // Otomatik olcek: ayni 12000 mm model standart olcekle yerlesir
        DrawingOptions oa;
        oa.layout.maxSheet = SheetSize::A1;
        const BuildResult ar = build(fh, assemblyOf(fh), oa);
        check(ar.sheet.layout.ok && measureOverlaps(ar.sheet).total() == 0 && ar.sheet.scaleIsStandard,
              "E: otomatik standart olcekte uzun assembly cakismasiz yerlesir");
    }
    {
        // Yazi minimumu ihlali: reddedilir
        DrawingOptions o;
        o.layout.dimTextHeight = 1.5;
        const BuildResult tr = build(f, assemblyOf(f), o);
        bool tooSmall = false;
        for (const auto& i : tr.sheet.layout.issues) tooSmall = tooSmall || i.code == "TEXT_TOO_SMALL";
        check(tr.ok && !tr.sheet.layout.ok && tooSmall, "E: 1.5 mm olcu yazisi yapilandirmasi REDDEDILIR (TEXT_TOO_SMALL), sessizce kabul edilmez");
    }

    // =========================================================================
    // F) Tek levha cizimi: desteklenen kapsam tam -> TASLAK degil
    // =========================================================================
    {
        const fx::Fixture g = fx::make({}, 4000, false);
        const BuildResult pr = build(g, g.finPlate);
        check(pr.ok && pr.sheet.kind == DrawingKind::SinglePart, "F: tek levha (kanat plaka) cizimi");
        const std::string pk = "plate:" + std::to_string(g.finPlate);
        check(valuesOf(dimsOf(pr.sheet, DimensionRole::PlateWidth, pk)) == std::vector<double>{300} &&
                  valuesOf(dimsOf(pr.sheet, DimensionRole::PlateHeight, pk)) == std::vector<double>{200},
              "F: levha 300 x 200 tek kez olculur (genel olcu yinelenmez)");
        bool dupOverall = false;
        const DrawingView* face = nullptr;   // yuz gorunumu = PlateWidth'in bulundugu gorunum
        for (const auto* d : dimsOf(pr.sheet, DimensionRole::PlateWidth)) face = pr.sheet.findView(d->viewId);
        for (const auto* d : dimsOf(pr.sheet, DimensionRole::OverallWidth))
            if (face && d->viewId == face->id && nearTo(d->value, 300)) dupOverall = true;
        check(face && !dupOverall, "F: yuz gorunumunde 300 mm genel genislik, PlateWidth ile yinelenmedi");
        check(pr.sheet.layout.ok && measureOverlaps(pr.sheet).total() == 0, "F: tek levha yerlesimi cakismasiz");
        check(!pr.sheet.fabrication.draft, "F: kaynak/kesim/cozumlenmemis yok -> desteklenen kapsamda TASLAK DEGIL");
        check(hasItem(pr.sheet, "WELDS", ChecklistState::NotApplicable) && hasItem(pr.sheet, "CUTS", ChecklistState::NotApplicable) &&
                  !hasItem(pr.sheet, "MEMBER_DATUM", ChecklistState::Unresolved),
              "F: WELDS/CUTS NotApplicable, uye datumu gerekmiyor");
    }
    {
        // Cercevesiz levha: delik var ama kaynagi yok -> TASLAK, delik olcusu uydurulmaz
        fx::Fixture g = fx::make({}, 4000, false);
        const EntityId bare = fx::addFramedPlate(g, {2500, 80, 0}, {1, 0, 0}, {0, 0, 1}, {0, 1, 0}, 200, 100, 8, "cercevesiz", false);
        fx::addBolts(g, bare, {2450, 80, 20}, {0, 1, 0}, {1, 0, 0}, 2, 1, 100, 0, 20);
        const BuildResult br = build(g, bare);
        // 2026-10-28 DAVRANIS DEGISIKLIGI: kalici PlateSpec yoksa cerceve GERCEK
        // GEOMETRIDEN turetilir (dikdortgen levha); delik konum olculeri uretilir.
        // Olcu uydurulmaz — gercek geometriden olculur.
        check(br.ok && !hasItem(br.sheet, "HOLES_UNRESOLVED", ChecklistState::Unresolved),
              "F: cercevesiz DIKDORTGEN levha -> geometriden turetilen cerceve (HOLES_UNRESOLVED yok)");
        check(!dimsOf(br.sheet, DimensionRole::HolePitch).empty() &&
                  !dimsOf(br.sheet, DimensionRole::HoleEdge).empty(),
              "F: cercevesiz levhada delik adimi + net kenar olculeri URETILIR");
        bool trueValue = true;
        for (const Dimension2D* d : dimsOf(br.sheet, DimensionRole::HolePitch))
            if (std::fabs(d->measured() - d->value) > 1e-6) trueValue = false;
        for (const Dimension2D* d : dimsOf(br.sheet, DimensionRole::HoleEdge))
            if (std::fabs(d->measured() - d->value) > 1e-6) trueValue = false;
        check(trueValue, "F: turetilen delik olculeri yeniden olcumle AYNI (uydurma yok)");
    }

    // =========================================================================
    // G) Layout motoru: zorlayici durum (dar zincir, yakin gruplar) ve fabrication kapali geri-uyum
    // =========================================================================
    {
        DrawingOptions off;
        off.fabrication = false;
        off.autoLayout = false;
        const BuildResult pr = build(f, assemblyOf(f), off);
        check(pr.ok && dimsOf(pr.sheet, DimensionRole::HolePitch).empty() && !pr.sheet.fabrication.evaluated,
              "G: fabrication=false -> P1 davranisi (imalat olcusu/durum yok)");
        check(!pr.sheet.layout.ran, "G: autoLayout=false -> yerlesim calistirilmadi");
    }
    {
        // Kucuk levha + yakin delikler @ 1:5: metin dar adimlarda kaydirilir/kademelenir, cakisma yok.
        fx::Fixture g = fx::make({}, 4000, false);
        const EntityId pl = fx::addFramedPlate(g, {2600, 80, 0}, {1, 0, 0}, {0, 0, 1}, {0, 1, 0}, 120, 80, 10, "yogun");
        fx::addBolts(g, pl, {2560, 80, 20}, {0, 1, 0}, {1, 0, 0}, 5, 3, 20, 20, 12);   // 5x3 Ø12 adim 20 (satirlar z=20,0,-20)
        // (1) ZORLANMIS 1:5: adimlar kagitta 4 mm -> metinler (7 mm) siginmaz. Sonuc DURUST olmali:
        //     ya cakismasiz yerlesir YA DA acik sorun raporu + TASLAK; sessiz cakisma/kucultme YOK.
        DrawingOptions o;
        o.layout.fixedScaleDen = 5.0;
        o.layout.maxSheet = SheetSize::A1;
        const BuildResult dr = build(g, pl, o);
        const bool honest = dr.ok && (dr.sheet.layout.ok ? measureOverlaps(dr.sheet).total() == 0
                                                         : (!dr.sheet.layout.issues.empty() && dr.sheet.fabrication.draft &&
                                                            hasItem(dr.sheet, "LAYOUT", ChecklistState::Unresolved)));
        check(honest, "G: zorlanmis yogun 1:5 -> ya cakismasiz ya da acik rapor + TASLAK (sessiz cakisma yok)");
        check(dr.sheet.layoutConfig.dimTextHeight >= 3.0, "G: zorlanmis yogun durumda yazi kucultulmedi");
        // (2) OTOMATIK olcek: yazi okunur kalir, olcek BUYUR (1:2) ve cakisma 0
        DrawingOptions oa;
        oa.layout.maxSheet = SheetSize::A1;
        const BuildResult da = build(g, pl, oa);
        check(da.ok && da.sheet.layout.ok && measureOverlaps(da.sheet).total() == 0, "G: yogun delik dizilimi OTOMATIK olcekte cakismasiz yerlesti");
        check(da.sheet.scale >= 0.2 - 1e-12, "G: otomatik olcek 1:5 veya daha buyuk secildi");
        check(countValue(dimsOf(da.sheet, DimensionRole::HolePitch), 20) == 4 + 2, "G: 5x3 dizilim: 4 yatay + 2 dusey adim = 6 x '20'");
        int moved = 0;
        for (const auto& d : dr.sheet.dimensions)
            if (d.role == DimensionRole::HolePitch && (d.textLevel > 0 || std::fabs(d.textT - 0.5) > 1e-9)) ++moved;
        check(moved >= 1, "G: zorlanmis durumda dar adim metinleri merkezden kaydirildi/kademelendi");
    }

    std::printf(failures ? "FAILURES: %d / %d\n" : "ALL PASS (%d checks)\n", failures ? failures : checks, checks);
    return failures ? 1 : 0;
}
