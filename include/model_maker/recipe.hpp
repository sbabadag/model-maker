#pragma once

// MM-3 — RECIPE YORUMLAYICI (component paket uyumlulugu).
//
// Bir component paketindeki recipe.json (bildirimsel uretim tarifi) bu
// katmanda yorumlanir ve ortak GenerationPlan'a donusur. Studio ve Model
// Maker AYNI yorumlayiciyi AYNI commit'ten derler; ayni girdi + ayni recipe
// icin BIREBIR AYNI GenerationPlan (dolayisiyla ayni planDigest) uretilir.
//
// Tasarim kurallari (COMPONENT-PACKAGE-01 §5):
//   - Bildirimsel, deterministik, yan etkisiz. Dosya/ag/rastgelelik/saat YOK.
//   - Her islem bir roleKey uretir ([A-Za-z0-9_], paket icinde tekil).
//   - Koordinatlar her zaman bir CERCEVEYE (frame) gore yereldir; dunya
//     koordinati yazilamaz.
//   - Hata -> GenerationPlan.ok = false + Turkce mesaj; belge degismez.
//   - Dongu yok. Sinirli tekrar yalniz sabit-sinirli grid bloklariyla.
//
// Ifade dili: + - * / ^, min max abs sqrt sin cos tan atan2 clamp, if(),
// karsilastirma (< <= > >= == !=), and/or/not; param.<ad>, main.<alan>,
// secondary_N.<alan> (h, b, tw, tf, length). Trig fonksiyonlari DERECE alir
// (Angle parametreleriyle tutarli); atan2 DERECE dondurur.
//
// OCCT yok, Qt yok, windows.h yok. Linux'ta derlenir/test edilir; recipe.cpp
// mm_component_contract kutuphanesinde, runRecipe de bu baslikta tanimlanir.

#include "model_maker/component_engine.hpp"

#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace mm {

// --- RECIPE VERI MODELI ------------------------------------------------------

// Bir step argumani: ya skaler (ifade veya sabit metin) ya da liste
// (ornegin bolts.grid'in "through": ["main", "secondary_0"]).
struct RecipeArg {
    std::string text;                  // skaler: ifade metni veya sabit metin
    std::vector<RecipeArg> items;      // liste formu
    bool isList{false};
};

// Bir adim. Iki turu vardir:
//   - BLOK adimi: use bos degil; args + opsiyonel role + opsiyonel when.
//   - CHECK adimi: check bos degil; message Turkce hata metni.
struct RecipeStep {
    std::string id;                    // blok ciktisi baglama adi (frame/plate/bolts)
    std::string use;                   // blok adi ("frame.member_end", ...)
    std::map<std::string, RecipeArg> args;
    std::string role;                  // uretilen nesnenin roleKey'i (bos = positional)
    std::string when;                  // kosullu calistirma ifadesi (bos = hep)
    std::string check;                 // check adimi: kosul ifadesi
    std::string message;               // check adimi: Turkce hata metni
};

// Recipe: siralanmis let baglamalari (ad -> ifade) + adimlar.
struct Recipe {
    std::string recipeVersion{"1.0.0"};
    std::vector<std::pair<std::string, std::string>> lets;  // ad -> ifade (sirali)
    std::vector<RecipeStep> steps;
};

// --- DOGRULAMA (statik analiz, fail-closed) ----------------------------------

// Recipe'nin YAPISAL dogrulugunu denetler: bilinmeyen blok, bilinmeyen
// parametre, tanimsiz let, dongusel let, roleKey tekilligi + tur tutarliligi,
// §5.5 ust sinirlari (adim <= 256). Hata -> ok=false + Turkce mesaj.
// Blok argumanlarindaki ifadeler de (blok tanimina gore) taranir.
struct RecipeValidation {
    bool ok{true};
    std::wstring message;              // Turkce hata (ok=false iken)
};

RecipeValidation validateRecipe(const Recipe& recipe,
                                const std::vector<CompParamSpec>& specs) noexcept;

// --- TAS BILDIRIMLERI (L, salt okunur API) -----------------------------------
// Studio'nun blockRegistry aynasi yerine kullanacagi tek kaynak. Her tasin
// arguman adi + turu + sekli (liste/ifade-liste) disa acilir. J (bilinmeyen
// arguman) ve M (bool-param / uye rolu / liste-ifade) denetimleri de bu
// tablodan beslenir.
enum class RecipeArgKind {
    Expression,   // sayisal ifade (numArg)
    Text,         // sabit metin (strArg)
    TextParam,    // param.<ad> kabul eden metin (strParamArg)
    Logical,      // mantiksal (boolArg; param.* Boolean olmali)
    List,         // liste (strListArg)
    MemberRole,   // uye rolu (main / secondary_N)
};

struct BlockArgDecl {
    std::string name;
    RecipeArgKind kind{RecipeArgKind::Text};
    bool isList{false};        // liste formu (through/chamfers/outline)
    bool exprItems{false};     // liste ogeleri ifade (chamfers ':' / outline ',')
};

struct BlockDeclInfo {
    std::string use;                       // tas adi ("plate.rect", ...)
    std::vector<BlockArgDecl> args;        // ada gore alfabetik sirali
};

std::vector<BlockDeclInfo> blockDeclarations();

// --- YORUMCULAR --------------------------------------------------------------

// N — salt okunur cerceve API'si: bir recipe'yi yorumlayip GENERIC cerceve ureten
// her adimin (frame.member_end/offset/rotate/member_face, sonra frame.contact)
// cercevesini adim id'sine esler. Studio'nun cerceveyi okumak icin 1x1x1
// plate.rect "hilesini" kullanmasina gerek kalmaz. Plan/digest DEGISMEZ:
// runRecipe ile AYNI cekirdegi calistirir, plan disari atilir. Haunch opak
// cerceveleri (frame.plumb_face, haunch.*) disarida VERILMEZ — yalniz generic
// ad uzayi (ctx.frames).
struct RecipeFrame {
    Vec3 origin{};
    Vec3 u{1.0, 0.0, 0.0};
    Vec3 v{0.0, 1.0, 0.0};
    Vec3 w{0.0, 0.0, 1.0};
};

// Basarisiz recipe'de bos harita doner (ok=false davranisiyla tutarli);
// yorumlama hatasini ayri bildirmez (bunun icin runRecipe kullanin).
std::map<std::string, RecipeFrame> runRecipeFrames(
    const Recipe& recipe,
    const std::vector<CompParamSpec>& specs,
    const ComponentInput& input) noexcept;

// recipe -> GenerationPlan. specs = component tipinin parametre semasi (param.*
// erisimi ve hizalama icin); input = main/secondary geometri + parametre
// degerleri. Basarisizlikta GenerationPlan.ok=false ve error Turkce doludur;
// cagiran belgeyi DEGISTIRMEZ.
GenerationPlan runRecipe(const Recipe& recipe,
                         const std::vector<CompParamSpec>& specs,
                         const ComponentInput& input) noexcept;

} // namespace mm
