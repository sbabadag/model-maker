#pragma once

// MM-2 — JSON CODEC (component sozlesme katmani).
//
// CompParamSpec / CompParam / ComponentType (+ ComponentCatalogMeta) /
// ComponentDialogSchema / GenerationPlan (PlateSpec + outline/chamfer,
// BoltGroup, CutModifier, WeldSpec, roleKey dizileri) icin KAYIPSIZ
// JSON gidis-donus + kanonik (sirali, 1e-6 yuvarlamali, sabit bicimli)
// JSON + SHA-256 digest.
//
// Kurallar:
//   - wstring <-> UTF-8 (portable; wchar_t 16/32 bit ayrimi kod icinde).
//   - enum'lar SAYI degil STRING olarak yazilir.
//   - bilinmeyen alanlar okunurken hata VERMEZ (yok sayilir).
//   - codec surumu her kok objeye "formatVersion" olarak yazilir.
//   - digest = SHA-256(kanonik JSON); Linux GCC == Windows mingw (deterministik).
//
// OCCT yok, Qt yok, windows.h yok. Linux'ta derlenir/test edilir.

#include "model_maker/component_dialog.hpp"
#include "model_maker/component_engine.hpp"
#include "model_maker/recipe.hpp"

#include <nlohmann/json.hpp>
#include <filesystem>
#include <string>
#include <vector>

namespace mm {

// Codec format surumu (her kok objeye "formatVersion" olarak yazilir).
inline constexpr const char* kCodecFormatVersion = "1.0.0";

// --- UTF-8 donusumleri -------------------------------------------------------
// wstring -> UTF-8 std::string. wchar_t 16-bit (Windows) ve 32-bit (Linux)
// ayrimini compile-time halleder; surrogate ciftleri dogru kodlanir.
std::string utf8FromWide(const std::wstring& w);
std::wstring wideFromUtf8(const std::string& s);

// --- CompParamSpec -----------------------------------------------------------
nlohmann::json compParamSpecToJson(const CompParamSpec& spec);
CompParamSpec compParamSpecFromJson(const nlohmann::json& j);

// --- CompParam (tipli deger) ------------------------------------------------
nlohmann::json compParamToJson(const CompParam& p);
CompParam compParamFromJson(const nlohmann::json& j);

// --- ComponentCatalogMeta + ComponentType ------------------------------------
nlohmann::json componentCatalogMetaToJson(const ComponentCatalogMeta& m);
ComponentCatalogMeta componentCatalogMetaFromJson(const nlohmann::json& j);

nlohmann::json componentTypeToJson(const ComponentType& t);
ComponentType componentTypeFromJson(const nlohmann::json& j);

// --- ComponentDialogSchema ---------------------------------------------------
nlohmann::json componentDialogSchemaToJson(const ComponentDialogSchema& s);
ComponentDialogSchema componentDialogSchemaFromJson(const nlohmann::json& j);

// --- GenerationPlan ----------------------------------------------------------
nlohmann::json generationPlanToJson(const GenerationPlan& p);
GenerationPlan generationPlanFromJson(const nlohmann::json& j);

// --- Recipe (MM-5 paket yukleyici) ------------------------------------------
// recipe.json <-> Recipe kayipsiz gidis-donus. Bilinmeyen alanlar yok sayilir;
// zorunlu alanlar (steps[].use|check, args) eksikse bos birakilir ve
// validateRecipe yakalar (fail-closed tek noktada).
nlohmann::json recipeToJson(const Recipe& r);
Recipe recipeFromJson(const nlohmann::json& j);

// --- Kanonik recipe/paket metin yazicisi (F) ---------------------------------
// recipeCanonicalText: recipe.json dosya bicimi. recipeToJson'dan farki:
//   - formatVersion YOK (recipe dosyasi codec surumu tasimaz);
//   - anahtar sirasi RECIPE.md'ye gore: kok = recipeVersion, let, steps;
//     adim = id, use, role, when, check, message, args;
//   - args ADA GORE ALFABETIK (RecipeStep::args std::map zaten sirali);
//   - dump(2) + sonda TEK '\n', YALNIZ LF (CR yok).
// Studio da AYNI fonksiyonu kullanir; iki taraf bayt-ayni dosya yazar.
std::string recipeCanonicalText(const Recipe& r);

// packageJsonText: paket JSON dosyalari (manifest/parameters/dialog) icin ortak
// metin bicimi — dump(2) + sonda TEK '\n', YALNIZ LF.
std::string packageJsonText(const nlohmann::json& j);

// CR bayti iceren metni REDDEDEN dosya yazici (LF-only garantisi). Windows'ta
// metin modunda '\n' -> '\r\n' cevirilmesin diye BINARY modda yazar. '\r' iceren
// metin yazilmaz (hata + false). Basariliysa true.
//
// 1.13.0 (contract istegi b): yol artik GENIS/UTF-8 guvenli.
//  - `std::filesystem::path` imzasi TERCIH EDILIR: Windows'ta yerel genis
//    yol kullanilir (ANSI kod sayfasi devrede degil) -> Turkce karakterli
//    klasor adi kirilmaz.
//  - `std::string` imzasi KORUNUR (geriye uyum): yol UTF-8 kabul edilir.
//  - Metin SONU TEK LF ile bitmek ZORUNDA; CR iceren, LF ile bitmeyen ya
//    da cift LF ile biten metin YAZILMAZ (false).
bool writePackageJsonFile(const std::filesystem::path& path, const std::string& text);
bool writePackageJsonFile(const std::string& utf8Path, const std::string& text);

// --- MemberGeometry / ComponentInput (paket altin test girdisi) --------------
// Paket icindeki tests/*.json "input" alanini tasir: uye geometrisi + parametre
// degerleri. Yukleyici bu girdiyle recipe'yi kosup planDigest'i karsilastirir
// (golden replay). Alan adlari ComponentInput/MemberGeometry alanlariyla ayni;
// eksik alanlar varsayilanina duser (fail-closed dogrulama recipe/validate'de).
nlohmann::json memberGeometryToJson(const MemberGeometry& m);
MemberGeometry memberGeometryFromJson(const nlohmann::json& j);

nlohmann::json componentInputToJson(const ComponentInput& in);
ComponentInput componentInputFromJson(const nlohmann::json& j);

// --- Kanonik JSON + digest ---------------------------------------------------
// Bir json agacini kanonik metne cevirir: nesne anahtarlari SIRALI, sayilar
// 1e-6'ya yuvarlanmis ve sabit bicimde (%.6f), -0 -> 0, UTF-8, LF yok (tek
// satir kompakt). Bu cikti platformdan bagimsizdir (Linux == Windows).
std::string canonicalJsonString(const nlohmann::json& j);

// GenerationPlan'in kanonik JSON metni (digest girdisi).
std::string generationPlanCanonicalJson(const GenerationPlan& p);

// GenerationPlan'in SHA-256 hex ozeti (planDigest).
std::string generationPlanDigest(const GenerationPlan& p);

} // namespace mm
