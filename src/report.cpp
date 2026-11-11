#include "model_maker/report.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cwchar>
#include <functional>
#include <map>
#include <set>
#include <vector>

namespace mm {
namespace {

constexpr double kRound = 1.0;   // mm — rapor olculeri 1 mm'ye yuvarlanir

std::wstring widen(const std::string& s) {
    std::wstring w;
    w.reserve(s.size());
    for (const char c : s) w.push_back(static_cast<wchar_t>(static_cast<unsigned char>(c)));
    return w;
}

std::wstring num(double v) {
    const double r = std::round(v / kRound) * kRound;
    wchar_t buf[64];
    std::swprintf(buf, 64, L"%.0f", r);
    return buf;
}

// Gorsel/yardimci modeller (bulon montaji, kaynak katisi) malzeme listesine
// GIRMEZ: bunlar nesnenin kendisi degil, cizim yardimcisidir.
std::set<EntityId> displayOnlyModels(const Document& doc) {
    std::set<EntityId> out;
    for (const auto& b : doc.boltGroups())
        if (b.displayEntityId != kInvalidEntityId) out.insert(b.displayEntityId);
    for (const auto& w : doc.welds())
        if (w.displayEntityId != kInvalidEntityId) out.insert(w.displayEntityId);
    return out;
}

// Bulon/kaynak kimlikleri MODEL kimligi degildir; kendi tablolarinda aranir.
bool isLiveBolt(const Document& doc, EntityId id) { return doc.indexOfBoltGroup(id).has_value(); }
bool isLiveWeld(const Document& doc, EntityId id) { return doc.indexOfWeld(id).has_value(); }

} // namespace

MaterialList buildMaterialList(const Document& document) {
    MaterialList list;
    const std::set<EntityId> skip = displayOnlyModels(document);
    const std::vector<Bounds3>& bounds = document.modelBounds();

    // Gruplama: ayni gorunen satir tek satirda toplanir (adet artar).
    std::map<std::wstring, MaterialListRow> memberRows, plateRows;
    for (std::size_t i = 0; i < document.models().size(); ++i) {
        const EntityId id = document.modelId(i);
        if (skip.count(id)) continue;
        const EntityProperties& p = document.models()[i].properties();
        if (!p.visible) continue;
        double dx = 0.0, dy = 0.0, dz = 0.0;
        if (i < bounds.size()) {
            dx = std::abs(bounds[i].maximum.x - bounds[i].minimum.x);
            dy = std::abs(bounds[i].maximum.y - bounds[i].minimum.y);
            dz = std::abs(bounds[i].maximum.z - bounds[i].minimum.z);
        }
        if (!p.profileName.empty()) {
            // Uye: profil adi + boy. Boy = en buyuk bbox kenari.
            const double len = std::max({dx, dy, dz});
            std::wstring key = widen(p.profileName);
            std::wstring detail = L"L=" + num(len) + L" mm";
            if (!p.material.empty()) detail += L"  " + widen(p.material);
            const std::wstring sig = key + L"|" + detail;
            MaterialListRow& row = memberRows[sig];
            row.key = key;
            row.detail = detail;
            ++row.count;
            ++list.memberCount;
        } else {
            // Levha: olculer buyukten kucuge; kalinlik en kucuk kenar.
            std::vector<double> d{dx, dy, dz};
            std::sort(d.begin(), d.end(), std::greater<double>());
            std::wstring key = p.name.empty() ? std::wstring(L"levha") : widen(p.name);
            std::wstring detail = num(d[0]) + L" x " + num(d[1]) + L" x " + num(d[2]) + L" mm";
            if (!p.material.empty()) detail += L"  " + widen(p.material);
            if (!p.description.empty()) detail += L"  " + widen(p.description);
            const std::wstring sig = key + L"|" + detail;
            MaterialListRow& row = plateRows[sig];
            row.key = key;
            row.detail = detail;
            ++row.count;
            ++list.plateCount;
        }
    }
    for (auto& [sig, row] : memberRows) {
        (void)sig;
        list.members.push_back(row);
    }
    for (auto& [sig, row] : plateRows) {
        (void)sig;
        list.plates.push_back(row);
    }

    // BULON GRUPLARI: cap + sinif + grip ayni olan gruplar tek satir.
    std::map<std::wstring, MaterialListRow> boltRows;
    for (const auto& b : document.boltGroups()) {
        if (!isLiveBolt(document, b.id)) continue;
        const std::wstring key = L"M" + num(b.params.nominalDiameter) + L" " +
                                 widen(b.params.grade);
        std::wstring detail = L"grip=" + num(b.gripLength) + L" mm, delik=" +
                              num(b.params.holeDiameter) + L" mm";
        const int per = std::max(1, b.countX) * std::max(1, b.countY);
        detail += L", " + std::to_wstring(std::max(1, b.countX)) + L"x" +
                  std::to_wstring(std::max(1, b.countY)) + L" = " + std::to_wstring(per) +
                  L" bulon";
        const std::wstring sig = key + L"|" + detail;
        MaterialListRow& row = boltRows[sig];
        row.key = key;
        row.detail = detail;
        ++row.count;               // satir = grup sayisi
        list.boltCount += per;
    }
    for (auto& [sig, row] : boltRows) {
        (void)sig;
        list.boltGroups.push_back(row);
    }

    // KAYNAKLAR: tur + olcu + tek/cift tarafli ayni olan kaynaklar tek satir.
    std::map<std::wstring, MaterialListRow> weldRows;
    for (const auto& w : document.welds()) {
        if (!isLiveWeld(document, w.id)) continue;
        const std::wstring key = (w.type == WeldType::Butt) ? L"Kut kaynagi" : L"Kose kaynagi";
        std::wstring detail = L"a=" + num(w.size) +
                              (w.sizeIsThroat ? L" mm (bogaz)" : L" mm") +
                              (w.doubleSided ? L", cift tarafli" : L", tek tarafli");
        if (w.length > 0.0) detail += L", L=" + num(w.length) + L" mm";
        const std::wstring sig = key + L"|" + detail;
        MaterialListRow& row = weldRows[sig];
        row.key = key;
        row.detail = detail;
        ++row.count;
        ++list.weldCount;
    }
    for (auto& [sig, row] : weldRows) {
        (void)sig;
        list.welds.push_back(row);
    }
    return list;
}

std::wstring materialListReport(const Document& document) {
    const MaterialList list = buildMaterialList(document);
    const auto pad = [](const std::wstring& s, std::size_t n) {
        return s.size() >= n ? s : s + std::wstring(n - s.size(), L' ');
    };
    std::wstring out;
    const auto section = [&](const wchar_t* title, const std::vector<MaterialListRow>& rows,
                             int total, const wchar_t* unit) {
        out += title;
        out += L"  (" + std::to_wstring(total) + L" " + unit + L")\n";
        if (rows.empty()) {
            out += L"  -  yok\n";
        } else {
            for (const auto& r : rows) {
                out += L"  " + pad(std::to_wstring(r.count) + L" x", 7) + pad(r.key, 18) + r.detail +
                       L"\n";
            }
        }
        out += L"\n";
    };
    out += L"MALZEME LISTESI\n";
    out += L"===============\n\n";
    section(L"UYELER", list.members, list.memberCount, L"adet");
    section(L"LEVHALAR", list.plates, list.plateCount, L"adet");
    section(L"BULON GRUPLARI", list.boltGroups, list.boltCount, L"bulon");
    section(L"KAYNAKLAR", list.welds, list.weldCount, L"adet");
    out += L"TOPLAM: " + std::to_wstring(list.memberCount) + L" uye, " +
           std::to_wstring(list.plateCount) + L" levha, " + std::to_wstring(list.boltCount) +
           L" bulon (" + std::to_wstring(list.boltGroups.size()) + L" grup), " +
           std::to_wstring(list.weldCount) + L" kaynak\n";
    return out;
}

} // namespace mm
