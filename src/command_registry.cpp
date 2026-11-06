#include "model_maker/command_registry.hpp"

// KOMUT KAYDI cekirdegi — Qt'siz/windows.h'siz. Linux ctest ile dogrulanir.
// Kural: burada UI cizimi YOK; yalniz tanim, dogrulama ve arama.

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <map>

namespace mm {
namespace {

std::string trimmed(std::string_view text) {
    std::size_t begin = 0;
    std::size_t end = text.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(text[begin]))) ++begin;
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1]))) --end;
    return std::string(text.substr(begin, end - begin));
}

// Arama icin katlama: Turkce karakterler ASCII'ye indirilir ve kucuk harfe
// cevrilir; boylece "kiris" sorgusu "Kiriş" komutunu da bulur (kullanici
// diyakritik yazmak zorunda kalmaz).
std::string folded(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        if ((c == 0xC3 || c == 0xC4 || c == 0xC5) && i + 1 < text.size()) {
            const unsigned char n = static_cast<unsigned char>(text[i + 1]);
            char mapped = 0;
            if (c == 0xC4 && (n == 0xB0 || n == 0xB1)) mapped = 'i';  // İ / ı
            else if (c == 0xC4 && (n == 0x9E || n == 0x9F)) mapped = 'g';  // Ğ / ğ
            else if (c == 0xC5 && (n == 0x9E || n == 0x9F)) mapped = 's';  // Ş / ş
            else if (c == 0xC3 && (n == 0x9C || n == 0xBC)) mapped = 'u';  // Ü / ü
            else if (c == 0xC3 && (n == 0x96 || n == 0xB6)) mapped = 'o';  // Ö / ö
            else if (c == 0xC3 && (n == 0x87 || n == 0xA7)) mapped = 'c';  // Ç / ç
            if (mapped != 0) {
                out += mapped;
                ++i;
                continue;
            }
        }
        out += static_cast<char>(std::tolower(c));
    }
    return out;
}

std::string lowered(std::string_view text) {
    std::string out(text);
    for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

bool isModifierToken(const std::string& token, KeyChord& chord) {
    const std::string t = folded(token);
    if (t == "ctrl" || t == "control" || t == "strg") { chord.ctrl = true; return true; }
    if (t == "alt" || t == "option") { chord.alt = true; return true; }
    if (t == "shift") { chord.shift = true; return true; }
    return false;
}

// Tus adi -> kanonik metin. Bilinmeyen tek karakter/rakam/F-tusu kabul edilir.
std::optional<std::string> normalizeKey(const std::string& token) {
    const std::string t = folded(token);
    if (t.empty()) return std::nullopt;
    if (t.size() == 1) {
        const unsigned char c = static_cast<unsigned char>(t[0]);
        if (std::isalnum(c)) return std::string(1, static_cast<char>(std::toupper(c)));
        return std::nullopt;
    }
    if (t.size() <= 3 && t[0] == 'f' && std::isdigit(static_cast<unsigned char>(t[1]))) {
        const int n = std::atoi(t.c_str() + 1);
        if (n >= 1 && n <= 24) return "F" + std::to_string(n);
        return std::nullopt;
    }
    static const std::map<std::string, std::string> named = {
        {"delete", "DELETE"}, {"del", "DELETE"}, {"esc", "ESC"}, {"escape", "ESC"},
        {"tab", "TAB"}, {"enter", "ENTER"}, {"return", "ENTER"}, {"space", "SPACE"},
        {"home", "HOME"}, {"end", "END"}, {"pgup", "PGUP"}, {"pageup", "PGUP"},
        {"pgdn", "PGDN"}, {"pagedown", "PGDN"}, {"insert", "INSERT"}, {"ins", "INSERT"},
        {"backspace", "BACKSPACE"}, {"back", "BACKSPACE"},
        {"left", "LEFT"}, {"right", "RIGHT"}, {"up", "UP"}, {"down", "DOWN"},
    };
    const auto it = named.find(t);
    if (it != named.end()) return it->second;
    return std::nullopt;
}

std::string displayKey(const std::string& key) {
    if (key.size() == 1) return key;
    if (key.size() >= 2 && key[0] == 'F' &&
        std::isdigit(static_cast<unsigned char>(key[1]))) {
        return key;
    }
    static const std::map<std::string, std::string> pretty = {
        {"DELETE", "Delete"}, {"ESC", "Esc"}, {"TAB", "Tab"}, {"ENTER", "Enter"},
        {"SPACE", "Space"}, {"HOME", "Home"}, {"END", "End"}, {"PGUP", "PgUp"},
        {"PGDN", "PgDn"}, {"INSERT", "Insert"}, {"BACKSPACE", "Backspace"},
        {"LEFT", "Left"}, {"RIGHT", "Right"}, {"UP", "Up"}, {"DOWN", "Down"},
    };
    const auto it = pretty.find(key);
    return it == pretty.end() ? key : it->second;
}

std::vector<std::string> tokenize(std::string_view text) {
    std::vector<std::string> out;
    std::string current;
    for (char c : text) {
        if (std::isspace(static_cast<unsigned char>(c))) {
            if (!current.empty()) { out.push_back(current); current.clear(); }
        } else {
            current += c;
        }
    }
    if (!current.empty()) out.push_back(current);
    return out;
}

// Komutun arama havuzu: ad + ipucu + anahtar sozcukler + id.
std::string haystack(const CommandDef& def) {
    return folded(def.title) + " | " + folded(def.tooltip) + " | " +
           folded(def.keywords) + " | " + folded(def.id);
}

} // namespace

// ---------------------------------------------------------------------------
// Kisayol
// ---------------------------------------------------------------------------

std::optional<KeyChord> parseShortcut(std::string_view text) {
    const std::string all = trimmed(text);
    if (all.empty()) return std::nullopt;

    KeyChord chord;
    std::size_t start = 0;
    std::vector<std::string> parts;
    while (start <= all.size()) {
        const std::size_t plus = all.find('+', start);
        const std::string part = trimmed(std::string_view(all).substr(
            start, plus == std::string::npos ? std::string::npos : plus - start));
        if (!part.empty()) parts.push_back(part);
        else if (plus == std::string::npos) break;
        else return std::nullopt;  // "Ctrl++" gibi bos parca
        if (plus == std::string::npos) break;
        start = plus + 1;
    }
    if (parts.empty()) return std::nullopt;

    for (std::size_t i = 0; i + 1 < parts.size(); ++i) {
        if (!isModifierToken(parts[i], chord)) return std::nullopt;  // "S+Ctrl" hatali
    }
    const auto key = normalizeKey(parts.back());
    if (!key.has_value()) return std::nullopt;
    chord.key = *key;
    return chord;
}

std::string formatShortcut(const KeyChord& chord) {
    std::string out;
    if (chord.ctrl) out += "Ctrl+";
    if (chord.alt) out += "Alt+";
    if (chord.shift) out += "Shift+";
    out += displayKey(chord.key);
    return out;
}

std::string shortcutKey(std::string_view text) {
    const auto chord = parseShortcut(text);
    if (!chord.has_value()) return {};
    std::string out;
    if (chord->ctrl) out += "ctrl+";
    if (chord->alt) out += "alt+";
    if (chord->shift) out += "shift+";
    out += lowered(chord->key);
    return out;
}

// ---------------------------------------------------------------------------
// ValidationIssue
// ---------------------------------------------------------------------------

std::string ValidationIssue::message() const {
    static const char* names[] = {"DuplicateId", "DuplicateShortcut", "InvalidShortcut",
                                  "EmptyTitle", "MissingShortcutId", "UnknownId",
                                  "DuplicateInPlan", "EmptyPlan", "SurfaceMismatch"};
    const char* name = "?";
    const auto idx = static_cast<std::size_t>(code);
    if (idx < sizeof(names) / sizeof(names[0])) name = names[idx];
    std::string out = std::string(name) + " [" + id + "]";
    if (!detail.empty()) out += ": " + detail;
    return out;
}

// ---------------------------------------------------------------------------
// CommandRegistry
// ---------------------------------------------------------------------------

void CommandRegistry::add(CommandDef def) {
    commands_.push_back(std::move(def));
}

const CommandDef* CommandRegistry::find(std::string_view id) const {
    for (const CommandDef& def : commands_) {
        if (def.id == id) return &def;
    }
    return nullptr;
}

std::string CommandRegistry::shortcutOf(std::string_view id) const {
    const CommandDef* def = find(id);
    return def ? def->shortcut : std::string();
}

const CommandDef* CommandRegistry::commandForShortcut(std::string_view shortcut) const {
    const std::string key = shortcutKey(shortcut);
    if (key.empty()) return nullptr;
    for (const CommandDef& def : commands_) {
        if (!def.shortcut.empty() && shortcutKey(def.shortcut) == key) return &def;
    }
    return nullptr;
}

std::vector<const CommandDef*> CommandRegistry::search(std::string_view query,
                                                       std::size_t limit) const {
    const std::vector<std::string> tokens = tokenize(folded(query));
    std::vector<std::pair<int, const CommandDef*>> scored;

    for (const CommandDef& def : commands_) {
        if (tokens.empty()) {
            scored.emplace_back(3, &def);
            continue;
        }
        const std::string hay = haystack(def);
        const std::string title = folded(def.title);
        bool all = true;
        for (const std::string& token : tokens) {
            if (hay.find(token) == std::string::npos) { all = false; break; }
        }
        if (!all) continue;

        const std::string joined = trimmed(folded(query));
        int score = 3;
        if (title == joined) score = 0;
        else if (title.rfind(joined, 0) == 0) score = 1;
        else if (title.find(joined) != std::string::npos) score = 2;
        scored.emplace_back(score, &def);
    }

    std::stable_sort(scored.begin(), scored.end(),
                     [](const auto& a, const auto& b) {
                         if (a.first != b.first) return a.first < b.first;
                         return a.second->title < b.second->title;
                     });

    std::vector<const CommandDef*> out;
    out.reserve(std::min(limit, scored.size()));
    for (std::size_t i = 0; i < scored.size() && i < limit; ++i) {
        out.push_back(scored[i].second);
    }
    return out;
}

std::vector<CommandRegistry::ShortcutConflict> CommandRegistry::shortcutConflicts() const {
    std::vector<ShortcutConflict> out;
    std::map<std::string, std::string> seen;  // chord -> ilk komut id
    for (const CommandDef& def : commands_) {
        if (def.shortcut.empty()) continue;
        const std::string key = shortcutKey(def.shortcut);
        if (key.empty()) continue;
        const auto it = seen.find(key);
        if (it == seen.end()) {
            seen.emplace(key, def.id);
        } else {
            out.push_back({it->second, def.id, def.shortcut});
        }
    }
    return out;
}

std::vector<ValidationIssue> CommandRegistry::validate() const {
    std::vector<ValidationIssue> issues;

    std::map<std::string, std::size_t> ids;
    for (const CommandDef& def : commands_) {
        if (def.id.empty()) {
            issues.push_back({ValidationIssue::Code::DuplicateId, def.id,
                              "bos komut kimligi"});
        }
        if (def.title.empty()) {
            issues.push_back({ValidationIssue::Code::EmptyTitle, def.id,
                              "gorunen ad bos"});
        }
        if (++ids[def.id] > 1) {
            issues.push_back({ValidationIssue::Code::DuplicateId, def.id,
                              "ayni kimlik birden fazla tanimlanmis"});
        }
        if (!def.shortcut.empty()) {
            if (!parseShortcut(def.shortcut).has_value()) {
                issues.push_back({ValidationIssue::Code::InvalidShortcut, def.id,
                                  "'" + def.shortcut + "' ayristirilamiyor"});
            } else if (def.delivery == ShortcutDelivery::None) {
                issues.push_back({ValidationIssue::Code::MissingShortcutId, def.id,
                                  "kisayol metni var ama teslim yolu None"});
            }
        }
        if (def.kind == CommandKind::Toggle && def.toggleKey.empty()) {
            issues.push_back({ValidationIssue::Code::EmptyTitle, def.id,
                              "ac-kapa komutunun durum anahtari bos"});
        }
    }

    for (const ShortcutConflict& conflict : shortcutConflicts()) {
        issues.push_back({ValidationIssue::Code::DuplicateShortcut, conflict.second,
                          "'" + conflict.chord + "' hem " + conflict.first +
                              " hem " + conflict.second + " tarafindan kullaniliyor"});
    }
    return issues;
}

// ---------------------------------------------------------------------------
// UiPlan
// ---------------------------------------------------------------------------

void UiPlan::addTab(PlanTab tab) { tabs_.push_back(std::move(tab)); }

namespace {
void collectIds(const std::vector<PlanItem>& items, std::vector<std::string>& out) {
    for (const PlanItem& item : items) {
        if (item.kind == PlanItem::Kind::Command) out.push_back(item.id);
        else if (item.kind == PlanItem::Kind::Menu) collectIds(item.children, out);
    }
}

void countId(const std::vector<PlanItem>& items, std::string_view id, std::size_t& count) {
    for (const PlanItem& item : items) {
        if (item.kind == PlanItem::Kind::Command) {
            if (item.id == id) ++count;
        } else if (item.kind == PlanItem::Kind::Menu) {
            countId(item.children, id, count);
        }
    }
}
} // namespace

std::vector<std::string> UiPlan::commandIds() const {
    std::vector<std::string> out;
    for (const PlanTab& tab : tabs_) {
        for (const PlanGroup& group : tab.groups) collectIds(group.items, out);
    }
    return out;
}

std::size_t UiPlan::occurrences(std::string_view id) const {
    std::size_t count = 0;
    for (const PlanTab& tab : tabs_) {
        for (const PlanGroup& group : tab.groups) countId(group.items, id, count);
    }
    return count;
}

std::vector<ValidationIssue> UiPlan::validate(const CommandRegistry& registry) const {
    std::vector<ValidationIssue> issues;

    std::map<std::string, std::size_t> occurrencesById;
    for (const PlanTab& tab : tabs_) {
        if (tab.groups.empty()) {
            issues.push_back({ValidationIssue::Code::EmptyPlan, tab.title,
                              "bos sekme/menu"});
        }
        for (const PlanGroup& group : tab.groups) {
            if (group.items.empty()) {
                issues.push_back({ValidationIssue::Code::EmptyPlan, group.title,
                                  "bos grup"});
            }
            for (const std::string& id : [&] {
                     std::vector<std::string> ids;
                     collectIds(group.items, ids);
                     return ids;
                 }()) {
                if (!registry.contains(id)) {
                    issues.push_back({ValidationIssue::Code::UnknownId, id,
                                      "planda gecen komut kayitta yok"});
                } else if (++occurrencesById[id] > 1) {
                    issues.push_back({ValidationIssue::Code::DuplicateInPlan, id,
                                      "komut planda birden fazla kez"});
                }
            }
        }
    }
    return issues;
}

std::vector<std::string> UiPlan::missingCommands(const CommandRegistry& registry) const {
    std::vector<std::string> out;
    for (const CommandDef& def : registry.commands()) {
        if (occurrences(def.id) == 0) out.push_back(def.id);
    }
    return out;
}

// ---------------------------------------------------------------------------
// Katalog
// ---------------------------------------------------------------------------

std::vector<ValidationIssue> validateCatalog(const CommandCatalog& catalog) {
    std::vector<ValidationIssue> issues = catalog.registry.validate();
    for (const ValidationIssue& issue : catalog.ribbon.validate(catalog.registry))
        issues.push_back(issue);
    for (const ValidationIssue& issue : catalog.menu.validate(catalog.registry))
        issues.push_back(issue);

    // YER KURALI: serit komutu seritte TAM BIR kez; menu komutu seritte hic
    // olmamali; alt cubuk komutu seritte gorunmemeli. Menude her komut en az
    // bir kez bulunmali (menu = tam envanter, serit = secilmis alt kume).
    for (const CommandDef& def : catalog.registry.commands()) {
        const std::size_t inRibbon = catalog.ribbon.occurrences(def.id);
        const std::size_t inMenu = catalog.menu.occurrences(def.id);
        switch (def.surface) {
        case CommandSurface::Ribbon:
            if (inRibbon != 1) {
                issues.push_back({ValidationIssue::Code::SurfaceMismatch, def.id,
                                  "serit komutu seritte tam bir kez olmali (bulunan: " +
                                      std::to_string(inRibbon) + ")"});
            }
            break;
        case CommandSurface::MenuOnly:
        case CommandSurface::StatusBar:
            if (inRibbon != 0) {
                issues.push_back({ValidationIssue::Code::SurfaceMismatch, def.id,
                                  "bu komut seritte gorunmemeli (bulunan: " +
                                      std::to_string(inRibbon) + ")"});
            }
            break;
        }
        if (def.surface != CommandSurface::StatusBar && inMenu == 0) {
            issues.push_back({ValidationIssue::Code::SurfaceMismatch, def.id,
                              "menu tam envanter olmali: komut menude yok"});
        }
    }
    return issues;
}

} // namespace mm
