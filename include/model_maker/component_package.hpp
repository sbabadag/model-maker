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

// Kullanici paket klasoru: %APPDATA%\ModelMaker\components (Windows).
// APPDATA yoksa (Linux/test) bos path doner; cagiran yalniz proje klasorunu tarar.
std::filesystem::path defaultUserComponentPackagesDir();

// Proje klasoru: proje dosyasinin (veya klasorunun) yanindaki `components`.
std::filesystem::path projectComponentPackagesDir(const std::filesystem::path& projectFileOrDir);

}  // namespace mm
