#pragma once

// MM-3c C2 — Haunch 40 taşları (recipe blokları için paylaşımlı yüzey).
//
// KRİTİK KURAL (Selahattin): taşlar parametrelerini input.params'tan GİZLİCE
// okumaz; tüm parametreler HaunchParams üzerinden AÇIKÇA gelir. Böylece
// recipe argümanları parametreleri gösterir ve taşlar yeniden kullanılır.
//
// Köprü tasarımı: runHaunch40Block, HaunchParams'ı geçici bir ComponentInput'a
// enjekte eder ve C1'deki adlandırılmış fonksiyonları çağırır. Bu yüzden
// generator aritmetiği BİREBİR korunur; planDigest değişmez.
//
// Durum (Haunch40Ctx) OPAK'tır: std::shared_ptr<void> ile adımlar arasında
// taşınır; dış dünya iç tipi görmez.

#include "model_maker/component_engine.hpp"

#include <memory>
#include <string>

namespace mm {

// Tek bulon grubunun ham parametreleri (upper/lower için ayrı).
struct Haunch40Group {
    int nx{2}, ny{2};
    double pitchX{120.0}, pitchY{80.0};
    double diameter{20.0}, hole{22.0}, clear{44.0};
    double spanX{120.0}, spanY{80.0};
};

// generateHaunch40'ın input.params'tan okuduğu TÜM parametreler.
// generateHaunch40 bunları input.params'tan doldurur (davranış birebir);
// recipe blokları recipe argümanlarından doldurur (gizli okuma yok).
struct HaunchParams {
    double haunchLength{450.0};
    double haunchDepth{240.0};
    double haunchTipDepth{25.0};
    double haunchWebThickness{10.0};
    double haunchFlangeThickness{10.0};
    double endPlateThickness{20.0};
    double gap{0.0};
    double boltEdgeLeft{35.0};
    double boltEdgeRight{35.0};
    Haunch40Group upper;
    Haunch40Group lower;
    double boltEdge{35.0};
    double endPlateWidth{0.0};
    double endPlateHeight{0.0};
    double haunchFlangeWidth{0.0};
    std::wstring plateMaterial{L"S235JR"};
    double topPlateThickness{10.0};
    // Kolon ustu ile cap levhasi arasindaki bosluk (mm). 0 = levha kolon
    // tepesine oturur; >0 = levha bosluk kadar yukari kalkar (Tekla "cap
    // plate gap": tesviye/shim payi). Levha merkezi = kolon ustu + gap + t/2.
    double topPlateGap{0.0};
    double stiffenerClipSize{15.0};
    bool createBeamStiffener{false};
    double stiffenerThickness{10.0};
    bool createAddStiffener{false};
    double addStiffenerThickness{10.0};
    bool createRafterBottomStiffener{false};
    double rafterBottomStiffenerThickness{10.0};
    bool createComprStiffener{false};
    double comprStiffenerThickness{10.0};
    bool createMorrisStiffener{false};
    double morrisStiffenerThickness{10.0};
    bool createDiagStiffener{false};
    double diagStiffenerThickness{10.0};
    double diagStiffenerWidth{80.0};
    std::wstring grade{L"8.8"};
    bool createWelds{true};
    double weldSize{6.0};
};

// HaunchParams'ı input.params'tan doldurur (generateHaunch40 + uyum için;
// davranış birebir).
HaunchParams haunchParamsFromInput(const ComponentInput& input);

// Desteklenen blok adları (MM-3d — frame ADIYLA, parametreler per-taş):
//   "frame.contact"        — temas + eğik çerçeve geometrisi (PARAMETRESİZ)
//   "frame.plumb_face"     — düşey yüz başlangıcı (gap, endPlateThickness)
//   "haunch.end_plate"     — alın levhası + boyutlandırma
//   "haunch.web"           — konik gövde (L,D,tip,webT)
//   "haunch.flange"        — basınç flanşı (L,D,tip,flangeT,flangeWidth)
//   "haunch.stiffeners"    — kapak + kiriş/kolon berkitmeleri
//   "bolts.haunch_groups"  — iki bulon grubu
//   "haunch.welds"         — kaynaklar
// inFrame: recipe'de adı geçen önceki taşın çıktısı (opak; frame.contact hariç
// zorunlu). outFrame: bu taşın opak çıktısı (sonraki taşlar "frame" ile okur).
// role: tek-çıktılı taşlarda üretilen roleKey; boşsa sabit anahtar korunur.
bool runHaunch40Tas(const std::string& block, const HaunchParams& prm,
                    const ComponentInput& input,
                    const std::shared_ptr<void>& inFrame,
                    std::shared_ptr<void>& outFrame,
                    const std::string& role, GenerationPlan& plan);

} // namespace mm
