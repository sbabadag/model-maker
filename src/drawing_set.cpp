#include "model_maker/drawing_set.hpp"

#include "model_maker/drawing_layout.hpp"

#include <algorithm>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace mm::drawing {
namespace {

std::string pad2(int n) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%02d", n);
    return buf;
}

void applyPreferences(DrawingSheet& s, const DrawingPreferences& p) {
    const SheetSize first = p.autoSheetSize ? p.autoFirst : p.sheetSize;
    const SheetSize last = p.autoSheetSize ? p.autoMax : p.sheetSize;
    s.layoutConfig.minSheet = first;
    s.layoutConfig.maxSheet = (last < first) ? first : last;
    s.format = sheetFormat(first);
    s.format.margin = sheetFormat(first).margin;
}

bool layoutOk(DrawingSheet& s) {
    const bool placed = layoutSheet(s);
    return placed && measureOverlaps(s).total() == 0;
}

// Yerlesim sonucunu imalat kontrol listesine isler: LAYOUT maddesi ve TASLAK durumu.
// Diger maddeler (kaynak vb.) P2A degerlendirmesinden AYNEN korunur; burada UYDURULMAZ.
void statusForLayout(DrawingSheet& s, bool ok) {
    for (ChecklistItem& c : s.fabrication.checklist)
        if (c.key == "LAYOUT") {
            c.state = ok ? ChecklistState::Satisfied : ChecklistState::Unresolved;
            c.detail = ok ? std::string() : std::string("pafta yerlesimi tamamlanmadi (NO_FIT/cakisma)");
        }
    bool draft = false;
    for (const ChecklistItem& c : s.fabrication.checklist)
        if (c.state == ChecklistState::Unresolved) draft = true;
    if (!s.fabrication.checklist.empty()) s.fabrication.draft = draft;
    std::vector<std::string>& codes = s.fabrication.unresolvedCodes;
    codes.erase(std::remove(codes.begin(), codes.end(), std::string("LAYOUT")), codes.end());
    if (!ok) codes.push_back("LAYOUT");
}

// Paftanin yalniz secilen gorunuslerini + onlara ait olculeri/etiketleri/metinleri tasir.
// Sayfa duzeyi notlar (viewId == 0) her paftada kalir: revizyon/durum damgasi tutarli olsun.
DrawingSheet subsetOf(const DrawingSheet& src, const std::vector<ElementId>& viewIds, SheetRole role) {
    DrawingSheet out = src;
    out.role = role;
    out.references.clear();
    out.layout = LayoutReport{};
    const std::set<ElementId> keep(viewIds.begin(), viewIds.end());
    out.views.clear();
    for (const DrawingView& v : src.views)
        if (keep.count(v.id)) out.views.push_back(v);
    out.dimensions.clear();
    for (const Dimension2D& d : src.dimensions)
        if (keep.count(d.viewId)) out.dimensions.push_back(d);
    out.marks.clear();
    for (const MarkAnnotation& m : src.marks)
        if (keep.count(m.viewId)) out.marks.push_back(m);
    out.texts.clear();
    for (const TextAnnotation& t : src.texts)
        if (t.viewId == 0 || keep.count(t.viewId)) out.texts.push_back(t);
    return out;
}

ElementId addNote(DrawingSheet& s, const std::vector<std::string>& lines, bool bold = false) {
    TextAnnotation t;
    t.id = s.allocateId();
    t.viewId = 0;
    t.kind = TextKind::GeneralNote;
    t.lines = lines;
    t.height = s.layoutConfig.noteTextHeight;
    t.bold = bold;
    s.texts.push_back(std::move(t));
    return s.texts.back().id;
}

std::string detailTitle(const DrawingSheet& base, const std::vector<ElementId>& ids) {
    std::vector<std::string> marks;
    for (const ElementId id : ids)
        for (const DrawingView& v : base.views)
            if (v.id == id && !v.label.empty()) {
                std::string m = v.label;
                const std::size_t sp = m.find(' ');
                if (sp != std::string::npos) m = m.substr(sp + 1);
                marks.push_back(m);
            }
    std::string t = "DETAY";
    for (std::size_t i = 0; i < marks.size(); ++i) t += (i == 0 ? " " : " + ") + marks[i];
    return t;
}

struct Part {
    DrawingSheet sheet;
    std::string title;
    std::string reason;   // insan okur: neden bu kagit / neden bu pafta (deterministik)
};

struct Assembly {
    std::vector<Part> parts;
    std::vector<std::string> issues;
};

std::string paperReason(const DrawingSheet& s) {
    if (!s.layout.ran) return "yerlesim calistirilmadi";
    if (s.layout.finalSheet > s.layout.requestedSheet)
        return std::string("kagit buyutuldu ") + sheetSizeName(s.layout.requestedSheet) + "->" +
               sheetSizeName(s.layout.finalSheet) + " (yazi/olcu kucultulmedi)";
    return std::string("kagit ") + sheetSizeName(s.format.size) + " yeterli";
}

// Verilen gorunus kumesini tek paftada dener; `out` her durumda o kumenin paftasidir.
bool tryPack(const DrawingSheet& base, const std::vector<ElementId>& ids, SheetRole role,
             const DrawingPreferences& prefs, DrawingSheet& out) {
    out = subsetOf(base, ids, role);
    applyPreferences(out, prefs);
    return layoutOk(out);
}

// P1/P2D: BELIRLEYICI EN-IYI SIGDIRMA. Kural:
//   * once ana gorunusler (+ sigdigi kadar detay) TEK paftada; sigmayan ana gorunus devam paftasina,
//   * kalan gorunusler "once-sigdir" ile paftalara doldurulur (her paftada sigdigi kadar gorunus),
//     boylece sayfa sayisi en azdir; ayni levhanin detaylari yan yana gruplanir,
//   * hicbir gorunus sessizce DUSMEZ: sigmayan gorunus kendi paftasinda NO_FIT olarak raporlanir.
// Metin/olcu kucultme yok; yerlesim motorunun kalite kapisi (cakisma = 0) korunur.
Assembly packSheets(const DrawingSheet& base, const DrawingPreferences& prefs) {
    Assembly as;
    std::vector<ElementId> mains, details;
    std::map<ElementId, EntityId> plateOfView;
    for (const DrawingView& v : base.views) {
        if (v.kind == ViewKind::Detail) {
            details.push_back(v.id);
            plateOfView[v.id] = v.detailPlate;
        } else {
            mains.push_back(v.id);
        }
    }
    if (mains.empty()) {
        mains = details;
        details.clear();
    }
    // Ayni levhanin detaylari ardisik gelsin: (levha kimligi, gorunus kimligi) — BELIRLEYICI.
    std::stable_sort(details.begin(), details.end(), [&](ElementId a, ElementId b) {
        const EntityId pa = plateOfView.count(a) ? plateOfView[a] : kInvalidEntityId;
        const EntityId pb = plateOfView.count(b) ? plateOfView[b] : kInvalidEntityId;
        return pa != pb ? pa < pb : a < b;
    });

    // (1) Ana pafta: ana gorunusler; sigmazsa sondan devam paftasina tasi (deterministik).
    std::vector<ElementId> primaryIds = mains;
    std::vector<ElementId> moved;
    DrawingSheet primary;
    bool primaryOk = tryPack(base, primaryIds, SheetRole::Primary, prefs, primary);
    while (!primaryOk && primaryIds.size() > 1) {
        moved.insert(moved.begin(), primaryIds.back());
        primaryIds.pop_back();
        primaryOk = tryPack(base, primaryIds, SheetRole::Primary, prefs, primary);
    }

    // (2) Kalan tum gorunusler (tasinan ana + detaylar): ana paftaya sigdigi kadar ekle.
    std::vector<ElementId> remaining = moved;
    remaining.insert(remaining.end(), details.begin(), details.end());
    std::size_t addedToPrimary = 0;
    if (primaryOk) {
        std::vector<ElementId> cand = primaryIds;
        for (auto it = remaining.begin(); it != remaining.end();) {
            cand.push_back(*it);
            DrawingSheet trial;
            if (tryPack(base, cand, SheetRole::Primary, prefs, trial)) {
                primary = std::move(trial);
                ++addedToPrimary;
                it = remaining.erase(it);
            } else {
                cand.pop_back();
                ++it;
            }
        }
    }
    std::string primaryReason = paperReason(primary);
    if (addedToPrimary > 0)
        primaryReason += ", " + std::to_string(addedToPrimary) + " ek gorunus ayni paftada";
    as.parts.push_back({std::move(primary), "ANA PAFTA", primaryReason});
    if (!primaryOk) as.issues.push_back("PRIMARY_SHEET_NOT_PLACED");

    // (3) Kalanlar: her yeni pafta icin once-sigdir (mumkun oldugunca cok gorunus, en az sayfa).
    const std::set<ElementId> detailSet(details.begin(), details.end());
    while (!remaining.empty()) {
        std::vector<ElementId> ids{remaining.front()};
        remaining.erase(remaining.begin());
        const auto roleOf = [&]() {
            for (const ElementId id : ids)
                if (!detailSet.count(id)) return SheetRole::Continuation;
            return SheetRole::DetailSheet;
        };
        SheetRole role = roleOf();
        DrawingSheet sheet;
        bool ok = tryPack(base, ids, role, prefs, sheet);
        for (auto it = remaining.begin(); it != remaining.end();) {
            ids.push_back(*it);
            const SheetRole r2 = roleOf();
            DrawingSheet trial;
            if (tryPack(base, ids, r2, prefs, trial)) {
                sheet = std::move(trial);
                role = r2;
                it = remaining.erase(it);
            } else {
                ids.pop_back();
                ++it;
            }
        }
        std::string title = (role == SheetRole::DetailSheet) ? detailTitle(base, ids) : "DEVAM";
        std::string reason = std::to_string(ids.size()) + " gorunus tek paftada (en az sayfa)";
        if (!ok) reason = "NO_FIT: bu gorunusler secilen kagida/olcege sigmadi (olcu kucultulmedi)";
        as.parts.push_back({std::move(sheet), std::move(title), std::move(reason)});
        if (!ok) as.issues.push_back("DETAIL_SHEET_NOT_PLACED");
    }
    return as;
}

// Numaralandirma, indeks, cift yonlu referans, son yerlesim, set bilgisi, belge.
void finish(DrawingSetResult& res, std::vector<Part> parts, const DrawingPreferences& prefs,
            std::vector<std::string> issues) {
    if (parts.empty()) return;
    const DrawingSheet& prim = parts.front().sheet;
    const std::string baseNumber = prim.titleBlock.drawingNumber.empty() ? prim.name : prim.titleBlock.drawingNumber;
    const std::string baseTitle = prim.titleBlock.title;
    const std::string baseProject = prim.titleBlock.project;
    const std::string baseRevision = prim.titleBlock.revision;
    const std::uint64_t baseDocRev = prim.documentRevision;

    for (std::size_t i = 0; i < parts.size(); ++i) {
        DrawingSheet& s = parts[i].sheet;
        s.sheetIndex = static_cast<int>(i) + 1;
        if (i == 0) {
            s.role = SheetRole::Primary;
            s.sheetNumber = baseNumber;
            s.parentSheetId = 0;
        } else {
            if (s.role == SheetRole::Primary) s.role = SheetRole::Continuation;
            s.sheetNumber = baseNumber + "-" + pad2(static_cast<int>(i));
            s.titleBlock.drawingNumber = s.sheetNumber;
            s.titleBlock.title = baseTitle + " - " + parts[i].title;
            s.titleBlock.project = baseProject;
            s.titleBlock.revision = baseRevision;
        }
    }

    if (parts.size() > 1) {
        std::vector<std::string> idx{"PAFTA LISTESI"};
        for (std::size_t i = 1; i < parts.size(); ++i)
            idx.push_back(parts[i].sheet.sheetNumber + "  " + parts[i].title);
        addNote(parts.front().sheet, idx, true);
        for (std::size_t i = 1; i < parts.size(); ++i) addNote(parts[i].sheet, {"ANA PAFTA " + baseNumber});
    }

    bool allLayout = true;
    std::vector<std::string> reasons;
    for (const Part& p : parts) reasons.push_back(p.reason);
    for (Part& p : parts) {
        applyPreferences(p.sheet, prefs);
        const bool ok = layoutOk(p.sheet);
        statusForLayout(p.sheet, ok);
        allLayout = allLayout && ok;
        if (!ok) {
            std::string code = "NO_FIT";
            for (const LayoutIssue& i : p.sheet.layout.issues) {
                code = i.code;
                break;
            }
            issues.push_back("SHEET " + p.sheet.sheetNumber + ": " + code);
        }
    }

    DrawingDocument doc;
    doc.setPreferences(prefs);
    for (Part& p : parts) doc.addSheet(std::move(p.sheet));
    const std::size_t sheetCount = doc.sheets().size();
    const ElementId primaryId = doc.sheets().front().id;

    bool ready = allLayout;
    for (const DrawingSheet& s : doc.sheets())
        if (s.fabrication.draft) ready = false;

    if (sheetCount > 1) {
        DrawingSheet& primary = doc.sheets().front();
        for (std::size_t i = 1; i < sheetCount; ++i) {
            DrawingSheet& link = doc.sheets()[i];
            link.parentSheetId = primaryId;
            ElementId viewId = 0;
            EntityId source = kInvalidEntityId;
            if (link.role == SheetRole::DetailSheet && !link.views.empty()) {
                viewId = link.views.front().id;
                source = link.views.front().detailPlate;
            }
            SheetReference back;
            back.targetSheetId = primaryId;
            back.targetNumber = primary.sheetNumber;
            back.label = "ANA PAFTA " + primary.sheetNumber;
            back.sourceEntity = source;
            link.references.push_back(back);

            SheetReference fwd;
            fwd.targetSheetId = link.id;
            fwd.targetViewId = viewId;
            fwd.targetNumber = link.sheetNumber;
            fwd.label = "PAFTA " + link.sheetNumber + " - " + link.titleBlock.title;
            fwd.sourceEntity = source;
            primary.references.push_back(fwd);
        }
    }

    DrawingSetInfo& si = doc.setInfo();
    si.present = true;
    si.id = "DS-" + baseNumber + "-R" + std::to_string(baseDocRev);
    si.primarySheetId = primaryId;
    si.multiSheet = sheetCount > 1;
    si.layoutValid = allLayout;
    si.ready = ready;
    si.issues = issues;
    for (std::size_t i = 1; i < sheetCount; ++i) {
        const DrawingSheet& s = doc.sheets()[i];
        SheetIndexEntry e;
        e.sheetId = s.id;
        e.number = s.sheetNumber;
        e.title = s.titleBlock.title;
        e.role = s.role;
        si.index.push_back(std::move(e));
    }

    res.document = std::move(doc);
    res.singleSheet = sheetCount == 1;
    res.layoutValid = allLayout;
    res.ready = ready;
    res.issues = std::move(issues);
    res.plan.clear();
    for (std::size_t i = 0; i < res.document.sheets().size(); ++i) {
        const DrawingSheet& s = res.document.sheets()[i];
        SheetPlanEntry e;
        e.number = s.sheetNumber;
        e.size = s.format.size;
        e.scale = s.titleBlock.scaleText;
        e.views = s.views.size();
        e.reason = i < reasons.size() ? reasons[i] : std::string();
        res.plan.push_back(std::move(e));
    }
    res.ok = true;
}

} // namespace

DrawingSetResult buildDrawingSet(const Document& document, MarkRegistry& registry, EntityId subject,
                                 const DrawingOptions& options, const DrawingPreferences& preferences) {
    DrawingSetResult res;
    res.preferences = preferences;

    DrawingOptions opt = options;
    opt.autoLayout = true;   // akilli yerlesim bu modulun varlik nedenidir
    const SheetSize first = preferences.autoSheetSize ? preferences.autoFirst : preferences.sheetSize;
    const SheetSize last = preferences.autoSheetSize ? preferences.autoMax : preferences.sheetSize;
    opt.sheet = first;
    opt.layout.minSheet = first;
    opt.layout.maxSheet = (last < first) ? first : last;

    BuildResult base = buildSheet(document, registry, subject, opt);
    if (!base.ok) {
        res.error = base.error.empty() ? std::string("cizim uretilemedi") : base.error;
        return res;
    }
    res.inventory = base.inventory;
    DrawingSheet sheet = std::move(base.sheet);
    const bool single = sheet.layout.ok && measureOverlaps(sheet).total() == 0;

    std::vector<std::string> issues;
    if (!single) {
        issues.push_back("SINGLE_SHEET_NOT_VALID");
        bool reported = false;
        for (const LayoutIssue& i : sheet.layout.issues) {
            issues.push_back("SINGLE " + i.code + (i.detail.empty() ? std::string() : ": " + i.detail));
            reported = true;
        }
        if (!reported) issues.push_back("SINGLE NO_FIT: tek pafta kalite kosullarini saglamadi");
        if (!preferences.autoMultiSheetFallback) {
            issues.push_back("MULTI_SHEET_DISABLED");
            std::vector<Part> one;
            one.push_back({std::move(sheet), "ANA PAFTA", "tek pafta kaliteyi saglamadi (coklu pafta yedegi kapali)"});
            finish(res, std::move(one), preferences, std::move(issues));
            res.ok = false;
            res.layoutValid = false;
            res.ready = false;
            res.error = "tek pafta kalite kosullarini saglamadi (coklu pafta yedegi kapali)";
            return res;
        }
    }

    if (single) {
        std::string reason = paperReason(sheet) + ", tek pafta";
        std::vector<Part> one;
        one.push_back({std::move(sheet), "ANA PAFTA", std::move(reason)});
        finish(res, std::move(one), preferences, std::move(issues));
        return res;
    }

    Assembly as = packSheets(sheet, preferences);
    for (std::string& i : as.issues) issues.push_back("MULTI " + i);
    finish(res, std::move(as.parts), preferences, std::move(issues));
    return res;
}

bool validateDrawingSet(const DrawingDocument& document, std::vector<std::string>& issues) {
    issues.clear();
    const std::vector<DrawingSheet>& sheets = document.sheets();
    if (sheets.empty()) {
        issues.push_back("EMPTY_SET");
        return false;
    }
    const DrawingSetInfo& si = document.setInfo();
    const DrawingSheet* prim = document.primarySheet();
    if (!prim) {
        issues.push_back("NO_PRIMARY_SHEET");
        return false;
    }
    if (si.present) {
        if (si.primarySheetId != prim->id) issues.push_back("SET_PRIMARY_ID_MISMATCH");
        if (si.multiSheet != (sheets.size() > 1)) issues.push_back("SET_MULTI_FLAG_MISMATCH");
    }
    if (sheets.size() > 1) {
        int primaries = 0;
        for (const DrawingSheet& s : sheets) primaries += s.role == SheetRole::Primary ? 1 : 0;
        if (primaries != 1) issues.push_back("PRIMARY_SHEET_COUNT");
        if (sheets.front().role != SheetRole::Primary) issues.push_back("PRIMARY_NOT_FIRST");
    }

    const std::string baseNumber = prim->sheetNumber.empty() ? prim->titleBlock.drawingNumber : prim->sheetNumber;
    if (baseNumber.empty()) issues.push_back("SHEET_NUMBER_EMPTY");
    std::set<std::string> numbers;
    for (std::size_t i = 0; i < sheets.size(); ++i) {
        const DrawingSheet& s = sheets[i];
        if (s.sheetNumber.empty()) issues.push_back("SHEET_NUMBER_EMPTY");
        else if (!numbers.insert(s.sheetNumber).second) issues.push_back("SHEET_NUMBER_DUPLICATE: " + s.sheetNumber);
        if (s.sheetIndex != static_cast<int>(i) + 1) issues.push_back("SHEET_INDEX_BAD: " + s.sheetNumber);
        if (s.titleBlock.project != prim->titleBlock.project) issues.push_back("PROJECT_MISMATCH: " + s.sheetNumber);
        if (s.titleBlock.revision != prim->titleBlock.revision) issues.push_back("REVISION_MISMATCH: " + s.sheetNumber);
        if (s.titleBlock.revisions.size() != prim->titleBlock.revisions.size())
            issues.push_back("REVISION_TABLE_MISMATCH: " + s.sheetNumber);
        if (i > 0) {
            if (s.sheetNumber != baseNumber + "-" + pad2(static_cast<int>(i))) issues.push_back("SHEET_NUMBER_SEQUENCE: " + s.sheetNumber);
            if (s.titleBlock.drawingNumber != s.sheetNumber) issues.push_back("TITLEBLOCK_NUMBER_MISMATCH: " + s.sheetNumber);
            if (s.parentSheetId != prim->id) issues.push_back("PARENT_SHEET_MISMATCH: " + s.sheetNumber);
            const std::string roleName = s.role == SheetRole::DetailSheet ? "detail" : "continuation";
            if (s.role == SheetRole::Primary) issues.push_back("EXTRA_PRIMARY: " + s.sheetNumber);
            (void)roleName;
        }
        const bool placed = s.layout.ok && measureOverlaps(s).total() == 0;
        if (!placed) issues.push_back("SHEET_LAYOUT_INVALID: " + s.sheetNumber);
        bool layoutItemOk = true;
        bool hasLayoutItem = false;
        for (const ChecklistItem& c : s.fabrication.checklist)
            if (c.key == "LAYOUT") {
                layoutItemOk = c.state == ChecklistState::Satisfied;
                hasLayoutItem = true;
            }
        if (hasLayoutItem && layoutItemOk != placed) issues.push_back("LAYOUT_STATUS_MISMATCH: " + s.sheetNumber);
    }

    // Cift yonlu referans butunlugu.
    if (sheets.size() > 1) {
        for (std::size_t i = 1; i < sheets.size(); ++i) {
            const DrawingSheet& link = sheets[i];
            bool back = false;
            for (const SheetReference& r : link.references)
                if (r.targetSheetId == prim->id) back = true;
            if (!back) issues.push_back("BACKREF_MISSING: " + link.sheetNumber);
        }
        for (const SheetReference& r : prim->references) {
            const DrawingSheet* target = nullptr;
            for (const DrawingSheet& s : sheets)
                if (s.id == r.targetSheetId) target = &s;
            if (!target) {
                issues.push_back("REF_TARGET_MISSING: " + r.targetNumber);
                continue;
            }
            if (r.targetViewId != 0 && !target->findView(r.targetViewId))
                issues.push_back("REF_TARGET_VIEW_MISSING: " + r.targetNumber);
            if (r.targetNumber != target->sheetNumber) issues.push_back("REF_TARGET_NUMBER_MISMATCH: " + r.targetNumber);
            bool back = false;
            for (const SheetReference& b : target->references)
                if (b.targetSheetId == prim->id) back = true;
            if (!back) issues.push_back("REF_NOT_BIDIRECTIONAL: " + r.targetNumber);
        }
        if (si.present) {
            if (si.index.size() != sheets.size() - 1) issues.push_back("SET_INDEX_INCOMPLETE");
            for (const SheetIndexEntry& e : si.index) {
                const DrawingSheet* target = nullptr;
                for (const DrawingSheet& s : sheets)
                    if (s.id == e.sheetId) target = &s;
                if (!target) issues.push_back("SET_INDEX_TARGET_MISSING: " + e.number);
                else if (target->sheetNumber != e.number) issues.push_back("SET_INDEX_NUMBER_MISMATCH: " + e.number);
            }
        }
    }
    return issues.empty();
}

bool refreshSheetQuality(DrawingSheet& sheet) {
    const OverlapReport m = measureOverlaps(sheet);
    const bool ok = sheet.layout.ok && m.total() == 0;
    if (!ok) {
        bool reported = false;
        for (const LayoutIssue& i : sheet.layout.issues)
            if (i.code == "USER_EDIT_OVERLAP") reported = true;
        if (!reported)
            sheet.layout.issues.push_back(
                {"USER_EDIT_OVERLAP", "kagit duzenlemesi sonrasi cakisma/okunamaz yazi (olcu kucultulmedi)", 0});
        sheet.layout.ok = false;
    }
    statusForLayout(sheet, ok);
    return ok;
}

std::string statusText(const DrawingSetResult& r) {
    if (!r.ok) return "CIZIM YOK: " + (r.error.empty() ? std::string("bilinmeyen hata") : r.error);
    std::string t = r.ready ? "HAZIR" : (r.layoutValid ? "TASLAK / EKSIK IMALAT VERISI" : "NO_FIT");
    t += " - " + std::to_string(r.document.sheets().size()) + " pafta (" +
         (r.singleSheet ? "tek pafta" : "bagli coklu pafta") + ")";
    for (const SheetPlanEntry& p : r.plan) {
        t += " | " + p.number + ": " + sheetSizeName(p.size) + " " + (p.scale.empty() ? "-" : p.scale);
        if (!p.reason.empty()) t += " (" + p.reason + ")";
    }
    if (!r.issues.empty()) t += " | " + std::to_string(r.issues.size()) + " sorun";
    return t;
}

} // namespace mm::drawing
