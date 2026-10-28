// TESHIS ARACI: kaydedilmis bir modeli (.mmw) acip cizim setini headless uretir ve
// HER assembly icin imalat (delik) olculerinin neden uretilmedigini raporlar.
//
// Kullanim (OCC-acik build):
//   cmake --build build-occ-hermes --target model_maker_drawing_fab_probe
//   LD_LIBRARY_PATH=/usr/lib/x86_64-linux-gnu ./build-occ-hermes/model_maker_drawing_fab_probe <model.mmw>
//
// Cikti: assembly basina levhalar, delik gruplari (duzenli mi, satir/sutun, cap),
// imalat 'unresolved' kodlari, cizim setindeki paftalar, olcu ROLU sayilari ve
// cozulmemis kontrol-listesi maddeleri. Hicbir sey UYDURULMAZ; yalniz gercek
// envanter/cizim sonucu yazdirilir.
#include "model_maker/document.hpp"
#include "model_maker/drawing_set.hpp"
#include "model_maker/marking.hpp"
#include "model_maker/occ_solid_store.hpp"

#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <map>
#include <set>
#include <string>

using namespace mm;
using namespace mm::drawing;

static const char* roleName(DimensionRole r) {
    switch (r) {
    case DimensionRole::MemberLength: return "MemberLength";
    case DimensionRole::OverallWidth: return "OverallWidth";
    case DimensionRole::OverallHeight: return "OverallHeight";
    case DimensionRole::HoleSpacing: return "HoleSpacing(P1 yedek)";
    case DimensionRole::PartPosition: return "PartPosition";
    case DimensionRole::PlateWidth: return "PlateWidth";
    case DimensionRole::PlateHeight: return "PlateHeight";
    case DimensionRole::HolePitch: return "HolePitch";
    case DimensionRole::HoleEdge: return "HoleEdge";
    case DimensionRole::HoleOrdinate: return "HoleOrdinate";
    case DimensionRole::PlateOffsetX: return "PlateOffsetX";
    case DimensionRole::PlateOffsetY: return "PlateOffsetY";
    case DimensionRole::PlateOffsetZ: return "PlateOffsetZ";
    }
    return "?";
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("kullanim: %s <model.mmw>\n", argv[0]);
        return 2;
    }
    Document doc;
    doc.setOccStore(makeOccSolidStore());
    try {
        doc.load(argv[1]);
    } catch (const std::exception& e) {
        std::printf("YUKLEME HATASI: %s\n", e.what());
        return 1;
    }
    std::printf("model: %s\n", argv[1]);
    std::printf("  modeller=%zu  component=%zu  bulon grubu=%zu  kaynak=%zu  assembly=%zu  kesme=%zu\n",
                doc.models().size(), doc.componentInstances().size(), doc.boltGroups().size(),
                doc.welds().size(), doc.assemblies().size(), doc.cutModifiers().size());

    // Component child dagilimi (levha cercevesi nereden geliyor?)
    {
        std::map<std::string, int> kinds;
        std::size_t ownedPlates = 0;
        for (const ComponentInstanceRecord& r : doc.componentInstances()) {
            ownedPlates += r.ownedPlateIds.size();
            for (const ComponentChildRecord& c : r.children) {
                const char* k = c.kind == ComponentObjectKind::Plate ? "Plate"
                                : c.kind == ComponentObjectKind::BoltGroup ? "BoltGroup"
                                : c.kind == ComponentObjectKind::Cut ? "Cut"
                                : c.kind == ComponentObjectKind::Weld ? "Weld" : "?";
                ++kinds[k];
            }
        }
        std::printf("  component child: ");
        for (const auto& [k, v] : kinds) std::printf("%s=%d  ", k.c_str(), v);
        std::printf(" | ownedPlateIds=%zu\n", ownedPlates);
        std::size_t plateChildren = 0, platePayload = 0, plateLive = 0, plateInParts = 0;
        std::set<EntityId> allPartIds;
        for (const AssemblyRecord& a : doc.assemblies())
            for (const EntityId p : a.parts) allPartIds.insert(p);
        for (const ComponentInstanceRecord& r : doc.componentInstances())
            for (const ComponentChildRecord& c : r.children) {
                if (c.kind != ComponentObjectKind::Plate) continue;
                ++plateChildren;
                if (c.plate) ++platePayload;
                if (c.entityId != kInvalidEntityId && doc.indexOfEntity(c.entityId)) ++plateLive;
                if (c.entityId != kInvalidEntityId && allPartIds.count(c.entityId)) ++plateInParts;
            }
        std::printf("  levha child: toplam=%zu  PlateSpec dolu=%zu  canli=%zu  assembly uyesi=%zu\n",
                    plateChildren, platePayload, plateLive, plateInParts);
    }

    // Kaynakli birlesimler (assembly) uzerinde dolas.
    std::size_t asmNo = 0;
    for (const AssemblyRecord& a : doc.assemblies()) {
        ++asmNo;
        std::printf("\n=== ASSEMBLY #%zu  id=%llu  uye=%zu  main=%llu\n", asmNo,
                    (unsigned long long)a.id, a.parts.size(), (unsigned long long)a.mainPart);
        const fab::FabricationInventory inv = fab::extractFabrication(doc, a.parts, a.mainPart);
        std::printf("  levha=%zu  delik grubu=%zu  delik=%zu\n", inv.plates.size(), inv.groups.size(),
                    inv.holes.size());
        for (const fab::PlateFeature& p : inv.plates)
            std::printf("    levha %llu: %.1f x %.1f x %.1f\n", (unsigned long long)p.plate, p.width,
                        p.height, p.thickness);
        for (const fab::HoleGroupFeature& g : inv.groups)
            std::printf("    grup: levha=%llu  %dx%d  Ø%.1f  %s%s  (holes=%zu)\n",
                        (unsigned long long)g.plate, g.cols, g.rows, g.diameter,
                        g.regular ? "duzenli" : "DUZENSIZ",
                        g.shape == fab::HoleShape::Slot ? " YUVA" : "", g.holes.size());
        {
            int shown = 0;
            for (const CutModifier& m : doc.cutModifiers()) {
                if (m.kind != CutKind::Hole || !m.enabled) continue;
                if (!std::count(a.parts.begin(), a.parts.end(), m.target)) continue;
                if (shown++ >= 6) break;
                std::printf("    delik-kesme: target=%llu Ø%.2f slotAlong=%.2f slotAcross=%.2f owner=%llu\n",
                            (unsigned long long)m.target, m.diameter, m.slotAlong, m.slotAcross,
                            (unsigned long long)m.owner);
            }
        }
        for (const fab::Unresolved& u : inv.unresolved)
            std::printf("    [UNRESOLVED] %s: %s\n", u.code.c_str(), u.detail.c_str());

        // Cizim seti: uygulamanin kullandigi varsayilanlarla.
        MarkRegistry reg;
        drawing::DrawingOptions opt;
        drawing::DrawingPreferences pref;
        // MM_UNIFORM_SCALE=1: tek olcek modu (tum gorunusler pafta olceginde,
        // detay gorunumu yok) — kullanici istegi.
        if (std::getenv("MM_UNIFORM_SCALE") != nullptr) pref.uniformViewScale = true;
        const drawing::DrawingSetResult res = drawing::buildDrawingSet(doc, reg, a.id, opt, pref);
        std::printf("  cizim: ok=%d  hata='%s'  layoutValid=%d  ready=%d  pafta=%zu\n", res.ok ? 1 : 0,
                    res.error.c_str(), res.layoutValid ? 1 : 0, res.ready ? 1 : 0,
                    res.document.sheets().size());
        for (const std::string& is : res.issues) std::printf("    [SET ISSUE] %s\n", is.c_str());
        for (const DrawingSheet& s : res.document.sheets()) {
            std::map<int, int> byRole;
            for (const Dimension2D& d : s.dimensions) ++byRole[static_cast<int>(d.role)];
            std::printf("    pafta %s  ad='%s'  olcek=1:%.1f  gorunus=%zu  olcu=%zu  TASLAK=%d\n",
                        s.sheetNumber.c_str(), s.name.c_str(), 1.0 / s.scale, s.views.size(),
                        s.dimensions.size(), s.fabrication.draft ? 1 : 0);
            for (const DrawingView& v : s.views)
                std::printf("       gorunus id=%llu kind=%d levha=%llu label='%s' segment=%zu daire=%zu olcek=1:%.1f\n",
                            (unsigned long long)v.id, static_cast<int>(v.kind),
                            (unsigned long long)v.detailPlate, v.label.c_str(), v.segments.size(),
                            v.circles.size(), 1.0 / v.scale);
            for (const auto& [role, count] : byRole)
                std::printf("       %-22s %d\n", roleName(static_cast<DimensionRole>(role)), count);
            for (const ChecklistItem& c : s.fabrication.checklist)
                if (c.state == ChecklistState::Unresolved)
                    std::printf("       [CHECKLIST] %s — %s%s%s\n", c.key.c_str(), c.label.c_str(),
                                c.detail.empty() ? "" : ": ", c.detail.c_str());
            for (const std::string& code : s.fabrication.unresolvedCodes)
                std::printf("       [KOD] %s\n", code.c_str());
        }
    }
    return 0;
}
