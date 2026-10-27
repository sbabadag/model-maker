#include "model_maker/drawing_set.hpp"

#include "model_maker/drawing_layout.hpp"

#include <algorithm>
#include <cstdio>
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
};

struct Assembly {
    std::vector<Part> parts;
    std::vector<std::string> issues;
};

// Tek pafta kaliteyi saglamadiginda: ana gorunusler ana paftada, delikli levha detaylari
// numarali detay paftalarinda. Hicbir yazi kucultulmez, olcu atilmaz.
Assembly splitSheet(const DrawingSheet& base, const DrawingPreferences& prefs) {
    Assembly as;
    std::vector<ElementId> mainIds, detailIds;
    for (const DrawingView& v : base.views) {
        if (v.kind == ViewKind::Detail) detailIds.push_back(v.id);
        else mainIds.push_back(v.id);
    }
    if (mainIds.empty()) {
        mainIds = detailIds;
        detailIds.clear();
    }

    DrawingSheet primary = subsetOf(base, mainIds, SheetRole::Primary);
    applyPreferences(primary, prefs);
    bool primaryOk = layoutOk(primary);
    std::vector<ElementId> moved;
    while (!primaryOk && mainIds.size() > 1) {
        moved.insert(moved.begin(), mainIds.back());
        mainIds.pop_back();
        primary = subsetOf(base, mainIds, SheetRole::Primary);
        applyPreferences(primary, prefs);
        primaryOk = layoutOk(primary);
    }
    as.parts.push_back({std::move(primary), "ANA PAFTA"});
    if (!primaryOk) as.issues.push_back("PRIMARY_SHEET_NOT_PLACED");

    if (!moved.empty()) {
        DrawingSheet cont = subsetOf(base, moved, SheetRole::Continuation);
        applyPreferences(cont, prefs);
        if (layoutOk(cont)) {
            as.parts.push_back({std::move(cont), "DEVAM"});
        } else {
            for (const ElementId id : moved) {
                DrawingSheet one = subsetOf(base, {id}, SheetRole::Continuation);
                applyPreferences(one, prefs);
                layoutOk(one);
                as.parts.push_back({std::move(one), "DEVAM"});
            }
        }
    }

    // Detay paftalari: her delikli levha detayi kendi paftasinda (deterministik, deneme yok).
    // Boylece tek-pafta aramasi basarisiz oldugunda maliyet dogrusal kalir; yazi/olcu kucultulmez.
    for (const ElementId id : detailIds) {
        DrawingSheet s = subsetOf(base, {id}, SheetRole::DetailSheet);
        applyPreferences(s, prefs);
        layoutOk(s);
        as.parts.push_back({std::move(s), detailTitle(base, {id})});
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
            one.push_back({std::move(sheet), "ANA PAFTA"});
            finish(res, std::move(one), preferences, std::move(issues));
            res.ok = false;
            res.layoutValid = false;
            res.ready = false;
            res.error = "tek pafta kalite kosullarini saglamadi (coklu pafta yedegi kapali)";
            return res;
        }
    }

    if (single) {
        std::vector<Part> one;
        one.push_back({std::move(sheet), "ANA PAFTA"});
        finish(res, std::move(one), preferences, std::move(issues));
        return res;
    }

    Assembly as = splitSheet(sheet, preferences);
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

} // namespace mm::drawing
