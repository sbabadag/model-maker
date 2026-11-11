#pragma once

// MM-5 — COMPONENT PAKET YUKLEYICI (.mmcpkg)
//
// Studio'nun urettigi komponent paketini (klasor) dogrular ve Model Maker'in
// MEVCUT altyapisina kaydeder: tip semasi -> ComponentRegistry, sematik ->
// ComponentDialogRegistry, recipe -> generic generator (recipe yorumlayici).
// Yeni komponent icin Model Maker'in yeniden derlenmesi GEREKMEZ.
//
// Tasarim kurallari (COMPONENT-PACKAGE-01 §4.4):
//   - FAIL-CLOSED: checksums -> manifest -> contract surumu -> parameters ->
//     dialog (validateDialogSchema) -> recipe (validateRecipe) -> altin testler.
//     Tek bir adim duserse TIP KAYDEDILMEZ (kismi kayit yok).
//   - Yerlesik typeId ile cakisma -> RED (yerlesik tip her zaman kazanir).
//   - Ayni typeId iki kok dizinde varsa PROJE klasoru kullanici klasorunu
//     GOLGELER (proje once yuklenir, kullanici kopyasi atlanir).
//   - Bilinmeyen tip yuklemede Document tarafinda `orphan` kalir (mevcut
//     davranis); yukleyici var olan kayitlari SILMEZ.
//
// OCCT yok, Qt yok, windows.h yok: saf std + nlohmann/json. Linux'ta test edilir.

#include "model_maker/component_dialog.hpp"
#include "model_maker/component_engine.hpp"
#include "model_maker/recipe.hpp"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <string>
#include <vector>

namespace mm {

// Paket dosya adlari (sozlesme sabitleri).
inline constexpr const char* kPackageManifestFile = "manifest.json";
inline constexpr const char* kPackageParametersFile = "parameters.json";
inline constexpr const char* kPackageDialogFile = "dialog.json";
inline constexpr const char* kPackageRecipeFile = "recipe.json";
inline constexpr const char* kPackageChecksumsFile = "checksums.json";
inline constexpr const char* kPackageTestsDir = "tests";

// Tek paket yukleme sonucu. ok=false ise `message` Turkce teshis DOLU olur ve
// `type`/`dialog`/`recipe` KULLANILMAZ.
struct ComponentPackageLoad {
    bool ok{false};
    std::wstring message;
    std::wstring typeId;
    std::wstring displayName;
    ComponentType type;                  // registry'e kaydedilecek tip
    ComponentDialogDefinition dialog;    // dialog registry'ye
    Recipe recipe;                       // generic generatorun tarifi
    std::filesystem::path dir;
    std::vector<std::wstring> warnings;  // fatal olmayan notlar
};

// recipe paketini CAPTURE eden generic generator. `registry.generate()` zaten
// parametreleri semaya hizalayip verir; burada yalniz runRecipe cagrilir.
ComponentGenerator makeRecipeGenerator(ComponentDialogDefinition dialog, Recipe recipe);

// Tek klasoru dogrular (registry kaydi YAPMAZ). Cakisma kontrolu icin mevcut
// registry okunur (yerlesik tipler + daha once yuklenmis paketler).
ComponentPackageLoad loadComponentPackage(const std::filesystem::path& dir,
                                          const ComponentRegistry& registry);

// 1.14.0 (contract istegi c): recipe.json BASLIK + arguman TURU dogrulayicisi
// PUBLIC. Studio ayni fonksiyonu kullanarak disa aktarmadan ONCE dogrular
// (fail-closed: recipeVersion semver + major 1, argumanlar metin/sayi; JSON
// boolean ve bilinmeyen alanlar REDDEDILIR).
bool validateRecipeJsonHeader(const nlohmann::json& recipeJson, std::wstring& error);

// Kok dizinleri tarar (sirayla: ILK kok oncelikli = proje klasoru) ve basarili
// paketleri kaydeder. `roots` icinde olmayan/erisilemeyen dizin sessizce atlanir
// (kullanici klasoru hic olmayabilir).
struct ComponentPackageScan {
    std::vector<std::wstring> registered;  // kaydedilen typeId'ler
    std::vector<std::wstring> shadowed;    // ayni typeId daha once yuklendi
    std::vector<std::wstring> errors;      // "<dizin>: <mesaj>"
    std::vector<std::wstring> warnings;    // fatal olmayan notlar
};

ComponentPackageScan loadComponentPackages(ComponentRegistry& registry,
                                           ComponentDialogRegistry& dialogs,
                                           const std::vector<std::filesystem::path>& roots);

// --- P-1/1..3: ATOMIK KURULUM (ice aktarma) ---------------------------------
//
// Paketi `targetRoot` altina `<typeId>.mmcpkg/` olarak kurar: once gecici
// klasore kopyalar, dogrular, sonra yeniden adlandirmayla YERINE KOYAR. Yarim
// yazilmis klasor asla hedef adiyla gorunmez; hata halinde hedefteki eski paket
// DOKUNULMAZ kalir (fail-closed).
//
// Surum kurali (P-1/3): ayni typeId
//   - yerlesik bir tiple cakisiyorsa          -> RED (yerlesik tip ezilmez),
//   - paketten gelmisse ve yeni surum BUYUKSE -> KABUL (upgrade),
//   - paketten gelmisse ve surum AYNI/DUSUKSE -> RED (hedef paket korunur).
// Kabul edilen paket registry'ye HEMEN kaydolur (yeniden baslatma gerekmez);
// onceki kayit (varsa) kaldirilip yenisi yazilir — kismi kayit birakilmaz.
struct ComponentPackageInstallOptions {
    // Ayni surum + AYNI icerik (checksum'lar ayni) yeniden kurulabilir mi?
    // Varsayilan false: ayni surum farkli icerikle gelirse RED.
    bool allowIdenticalReinstall{false};
};

struct ComponentPackageInstallResult {
    bool ok{false};
    bool replaced{false};              // ayni typeId onceki PAKET kaydi vardi
    bool upgraded{false};              // schemaVersion arttirildi
    std::wstring typeId;
    std::wstring displayName;
    std::wstring message;              // Turkce; ok=false ise ret nedeni
    std::filesystem::path targetDir;   // kurulan klasor (<root>/<typeId>.mmcpkg)
    int previousSchemaVersion{0};
    int schemaVersion{0};
    ComponentPackageLoad package;      // dogrulanmis paket (yeniden uretim icin)
    std::vector<std::wstring> warnings;
};

ComponentPackageInstallResult installComponentPackage(
    const std::filesystem::path& sourceDir,
    const std::filesystem::path& targetRoot,
    ComponentRegistry& registry,
    ComponentDialogRegistry& dialogs,
    const ComponentPackageInstallOptions& options = {});

// Paket yukleyicinin "bu typeId DAHA ONCE paketten yuklendi" bilgisi.
// Kaba serilestirme (state dosyasi) icin: kayitli tip listesinden hesaplanir.
bool isInstalledPackageType(const ComponentRegistry& registry, const std::wstring& typeId);


// Kullanici paket klasoru: %APPDATA%\ModelMaker\components (Windows).
// APPDATA yoksa (Linux/test) bos path doner; cagiran yalniz proje klasorunu tarar.
std::filesystem::path defaultUserComponentPackagesDir();

// Proje klasoru: proje dosyasinin (veya klasorunun) yanindaki `components`.
std::filesystem::path projectComponentPackagesDir(const std::filesystem::path& projectFileOrDir);

}  // namespace mm
