#pragma once

// KOMUT KAYDI (COMMAND REGISTRY) — UI-2'nin TEK KAYNAGI.
//
// Sorun: bugun ayni komut uc yerde tanimliydi —
//   1) QtMainWindow::commandCategories() (menu + serit + hizli arama),
//   2) createToolbar/createDockPanels icinde ELLE eklenen kalici denetimler,
//   3) Application::handleCanvasMessage icindeki WM_KEYDOWN if/else zinciri
//      (+ eski native serit CommandId enum'u).
// Bu yuzden bir komut menude vardi ama seritte yoktu, ya da iki komut ayni
// tusu paylasip biri OLU kaliyordu ('G' = Kolon + Fillet hatasi).
//
// Cozum: komut TANIMI (id, ad, ipucu, ikon, kisayol, tur, etkinlik kosulu,
// isleyici kimligi) tek yerde — bu dosyada; GORUNUM (menu agaci, serit
// sekmeleri/gruplari) de saf veri olarak ayni katalogda. Isleyici GOVDELERI
// platform katmaninda kalir (Qt: qt_main_window.cpp, Win32: application.cpp)
// cunku onlar Application'a (windows.h) baglidir.
//
// Bu dosya Qt'siz ve windows.h'siz derlenir: Linux'ta ctest ile dogrulanir,
// ayni tablo Windows'ta da kullanilir (tek kaynak, iki platform).
//
// KAPILAR (validate()):
//   - ayni id iki kez tanimlanamaz,
//   - ayni kisayol (ayni degistirici kumesi + ayni tus) iki komuta baglanamaz,
//   - kisayol metni ayristirilabilir olmali,
//   - serit planindaki her id kayitta bulunmali ve seritte TAM BIR kez gecmeli,
//   - menu planinda bilinmeyen id bulunmamali.

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace mm {

// ---------------------------------------------------------------------------
// Kisayol
// ---------------------------------------------------------------------------

// Bir tus bilesimi. `key` normalize edilmis buyuk harf metindir:
//   harf  -> "S", "F", "Q"
//   rakam -> "1".."9"
//   ozel  -> "F1".."F24", "DELETE", "ESC", "TAB", "ENTER", "SPACE",
//            "HOME", "END", "PGUP", "PGDN", "INSERT", "BACKSPACE",
//            "LEFT", "RIGHT", "UP", "DOWN"
struct KeyChord {
    bool ctrl{false};
    bool alt{false};
    bool shift{false};
    std::string key;

    bool operator==(const KeyChord& other) const noexcept {
        return ctrl == other.ctrl && alt == other.alt && shift == other.shift &&
               key == other.key;
    }
    bool operator!=(const KeyChord& other) const noexcept { return !(*this == other); }
};

// "Ctrl+Shift+V", "Alt+1", "F7", "S", "Delete", "Esc" gibi metinleri ayristirir.
// Bos/gecersiz metinde nullopt doner (sessiz yutma yok — validate() hata verir).
// Bastirici takma adlari: Control/Strg -> Ctrl, Option -> Alt, Del -> Delete.
std::optional<KeyChord> parseShortcut(std::string_view text);

// Gosterim metni: "Ctrl+Shift+V" (kanonik sira: Ctrl, Alt, Shift, tus).
std::string formatShortcut(const KeyChord& chord);

// Iki farkli yazimin ayni bilesimi gostermesi icin karsilastirma anahtari
// ("ctrl+s", "f7", "s"). Gecersiz metinde bos string.
std::string shortcutKey(std::string_view text);

// ---------------------------------------------------------------------------
// Komut tanimi
// ---------------------------------------------------------------------------

enum class CommandKind {
    Button,    // tek tiklamada calisir
    Dropdown,  // alt secenek listesi acar (seritte ▾)
    Toggle     // ac/kapa durumu vardir (durum `toggleKey` ile okunur)
};

// Komutun YERI. Serit testi "her komut seritte tam bir kez" kuralini ancak
// menuye ozgu komutlari ayirarak uygulayabilir (Dosya/Pencere karari geregi
// sekme acmaz; snap anahtarlari UI-6a'da alt cubuga tasinir).
enum class CommandSurface {
    Ribbon,     // seritte TAM BIR kez gorunmeli
    MenuOnly,   // yalniz ≡ menusunde (Dosya, Pencere)
    StatusBar   // alt cubukta/panelde (UI-6a); seritte gorunmez
};

// Kisayolun KIM tarafindan yakalanacagi. Iki platform da ayni kayittan
// cozer; bu alan yalnizca cift baglamayi onler.
enum class ShortcutDelivery {
    None,    // kisayol yok
    Canvas,  // tuval odaktayken anahtar yonlendiricisi calistirir (harf, F-tusu)
    Host     // pencere kisayolu: odak panelde/duzenleyicide olsa da calisir (Ctrl+S)
};

struct CommandDef {
    std::string id;         // kararli kimlik: "file.save", "edit.fillet"
    std::string title;      // gorunen ad: "Kaydet", "Fillet"
    std::string tooltip;    // ara ipucu; bos ise title kullanilir
    std::string keywords;   // boslukla ayrilmis ek arama sozcukleri (takma ad)
    std::string icon;       // ikon kimligi ("save", "line", "bolt", ...)
    std::string shortcut;   // gosterim metni; bos = kisayol yok
    ShortcutDelivery delivery{ShortcutDelivery::None};
    CommandKind kind{CommandKind::Button};
    CommandSurface surface{CommandSurface::Ribbon};
    std::string toggleKey;  // Toggle: durum okuyucu anahtari ("snap", "ortho", ...)
    std::string enabledKey; // bos = her zaman etkin; doluysa host'a sorulur
    std::string handler;    // isleyici kimligi; bos = id

    const std::string& handlerId() const noexcept {
        return handler.empty() ? id : handler;
    }
};

// ---------------------------------------------------------------------------
// Dogrulama
// ---------------------------------------------------------------------------

struct ValidationIssue {
    enum class Code {
        DuplicateId,
        DuplicateShortcut,
        InvalidShortcut,
        EmptyTitle,
        MissingShortcutId,
        UnknownId,       // planda gecen id kayitta yok
        DuplicateInPlan, // ayni komut seritte birden fazla kez
        EmptyPlan,       // bos sekme / bos menu
        SurfaceMismatch  // komutun yeri (serit/menu/alt cubuk) planla uyusmuyor
    };
    Code code{Code::EmptyTitle};
    std::string id;      // ilgili komut
    std::string detail;  // insan okur aciklama (Turkce)

    std::string message() const;  // "DuplicateShortcut [edit.fillet]: ..."
};

// ---------------------------------------------------------------------------
// Kayit
// ---------------------------------------------------------------------------

class CommandRegistry {
public:
    void add(CommandDef def);

    const CommandDef* find(std::string_view id) const;
    bool contains(std::string_view id) const { return find(id) != nullptr; }

    const std::vector<CommandDef>& commands() const noexcept { return commands_; }
    std::size_t size() const noexcept { return commands_.size(); }

    // Bu komutun tanimli kisayolu (yoksa bos string).
    std::string shortcutOf(std::string_view id) const;

    // Kisayol metninden komut (kanonik anahtar karsilastirmasi; "ctrl+s" == "Ctrl+S").
    const CommandDef* commandForShortcut(std::string_view shortcut) const;

    // Ada/ipucuna/anahtar sozcuklere gore arama. Sorgu sozcukleri AND'lenir;
    // her sozcuk herhangi bir alanda (ad, ipucu, anahtar sozcuk, id) gecmeli.
    // Siralama: tam ad eslesmesi, ad onek eslesmesi, ad icinde, digerleri.
    std::vector<const CommandDef*> search(std::string_view query,
                                          std::size_t limit = 12) const;

    // Ayni bilesimi paylasan komut ciftleri (id1, id2, bilesim metni).
    struct ShortcutConflict {
        std::string first;
        std::string second;
        std::string chord;
    };
    std::vector<ShortcutConflict> shortcutConflicts() const;

    std::vector<ValidationIssue> validate() const;

private:
    std::vector<CommandDef> commands_;
};

// ---------------------------------------------------------------------------
// Gorunum plani (menu ve serit AYNI yapiyi paylasir)
// ---------------------------------------------------------------------------

struct PlanItem {
    enum class Kind { Command, Menu, Separator };
    Kind kind{Kind::Command};
    std::string id;                    // Command/Menu: komut ya da menu kimligi
    std::string label;                 // Menu: baslik; bos ise komutun adi
    std::string icon;                  // Menu: ikon; bos ise cocugun ilki
    bool large{true};                  // seritte buyuk dugme (ikon + alt etiket)
    std::vector<PlanItem> children;    // Menu icerigi
};

struct PlanGroup {
    std::string title;  // seritte grup basligi; menude alt menu basligi
    std::vector<PlanItem> items;
};

struct PlanTab {
    std::string title;  // seritte sekme; menude ust duzey menu
    std::vector<PlanGroup> groups;
};

class UiPlan {
public:
    void addTab(PlanTab tab);
    const std::vector<PlanTab>& tabs() const noexcept { return tabs_; }

    // Plana giren TUM komut id'leri (menu icleri dahil, tekrarsiz sirayla).
    std::vector<std::string> commandIds() const;
    // Bir komutun planda kac kez gectigi (seritte "tam bir kez" kurali icin).
    std::size_t occurrences(std::string_view id) const;

    // Bilinmeyen id, tekrar eden komut, bos menu/sekme.
    std::vector<ValidationIssue> validate(const CommandRegistry& registry) const;

    // Kayitta olup planda hic gecmeyen komutlar.
    std::vector<std::string> missingCommands(const CommandRegistry& registry) const;

private:
    std::vector<PlanTab> tabs_;
};

// ---------------------------------------------------------------------------
// Katalog: komut kaydi + serit plani + menu plani (tek paket)
// ---------------------------------------------------------------------------

struct CommandCatalog {
    CommandRegistry registry;
    UiPlan ribbon;  // serit: sekme -> grup -> dugme
    UiPlan menu;    // ≡ menusu: menu -> alt menu -> eylem
};

// Tum dogrulamalari kosar (kayit + serit + menu). Bos = katalog saglikli.
std::vector<ValidationIssue> validateCatalog(const CommandCatalog& catalog);

} // namespace mm
