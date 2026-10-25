#pragma once

// COMPONENT SEMBOLU (GLYPH) — TEK KAYNAK (tek render + tek hit-test).
//
// Renderer'in cizdigi component isaretleri ile application'in tiklama
// isabet-testi AYNI listeyi kullanir. Liste iki uretim yolunu birlestirir:
//
//   1) ALIN LEVHASI (EndPlateConnection) — mevcut production yolu.
//   2) UNIVERSAL PARAMETRIK COMPONENT (ComponentInstanceRecord) — Fin Plate vb.
//
// Boylece Fin Plate baglantisi da End Plate ile ayni kullanici deneyimine
// sahip olur; paralel bir glyph sistemi YOKTUR.
//
// windows.h yok; Linux'ta derlenir/test edilir.

#include "model_maker/document.hpp"
#include "model_maker/geometry.hpp"

#include <vector>

namespace mm {

// Bir component sembolunun dunya-uzayi ankraj noktasi ve tiklanacak model
// EntityId'si. Renderer ekran-olcekli elmas cizer; hit-test ayni noktayi
// kullanir (projeksiyon farki olusmaz).
struct ComponentGlyph {
    Vec3 anchor{};                          // sembolun cizildigi dunya noktasi
    EntityId entityId{kInvalidEntityId};    // tiklanacak model (levha/bulon display)
    EntityId componentId{kInvalidEntityId}; // parent component / baglanti kimligi
    bool universal{false};                  // true: ComponentInstanceRecord
};

// Belgedeki TUM component sembollerini toplar.
//   - Devre disi (enabled=false) ve orphan (typeId registry'de yok) kayitlar atlanir.
//   - Gecersiz/fiilen bulunmayan EntityId'ler atlanir.
//   - Ayni levha iki yoldan da gelirse ILK gelen kazanir (End Plate oncelikli);
//     entityId bazinda tekillesir — cift sembol cizilmez.
//   - Levhasi olmayan universal component'te bulon display merkezi kullanilir.
std::vector<ComponentGlyph> collectComponentGlyphs(const Document& document);

} // namespace mm
