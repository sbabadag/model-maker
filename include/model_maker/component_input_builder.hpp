#pragma once

// ============================================================================
// MODELDEN GIRDI KURUCU (Document -> ComponentInput) — TEK KAYNAK.
//
// Regresyon (42f61ee, Haunch 40 cap/alin levhasi): altin testler MemberGeometry'yi
// C++'ta ELDE kuruyordu; GERCEK GUI yolu (application.cpp memberGeometryFor ->
// kanonik memberGeometryFromProfile) altin testlerden GECMIYORDU. d8e10b6'nin
// kanonik fonksiyonu dusey kolonda (n ∥ axis) kesit cercevesini 90° cevirince
// model bozuldu ama 163 test yesil kaldi (kor nokta).
//
// Bu baslik Document'ten generator girdisini kurma mantigini TASINABILIR cekirdege
// cikarir; application.cpp VE testler AYNI fonksiyonu cagirir. Boylece "modelden
// kurulan girdiyle" altin testi, GUI'nin kullandigi yolun BIREBIR aynisini kosar.
// ============================================================================

#include "model_maker/component_engine.hpp"
#include "model_maker/geometry.hpp"
#include "model_maker/profile_database.hpp"

#include <optional>
#include <vector>

namespace mm {

class Document;

// Bir EntityProperties (kalici uye kaydi) + profil katalogundan generator icin
// MemberGeometry kurar. TEK KAYNAK: kanonik memberGeometryFromProfile (uçtan
// bagimsiz + kesit cercevesi dogru). application.cpp'deki memberGeometryFor
// mantigiyla BIREBIR ayni; id alani burada DOKUNULMAZ (kInvalidEntityId kalir),
// cagiran Document uzerinden atar.
MemberGeometry memberGeometryFromEntityProperties(
    const EntityProperties& p, const std::vector<SteelProfile>& catalog) noexcept;

// Document'ten ComponentInput kurar: mainPart + secondaryParts (EntityId).
// - Katalogda profili OLMAYAN parcalar atlanir; mainPart'in profili yoksa
//   nullopt (f3bc942: uydurma 200x300 kesit gosterilmez).
// - Ikincil parca ZORUNLU DEGIL (taban levhasi gibi tek parcali tipler icin).
// - params dogrudan tasinir (rec.params vb.).
std::optional<ComponentInput> componentInputFromDocument(
    const Document& doc, const std::vector<SteelProfile>& catalog,
    EntityId mainPart, const std::vector<EntityId>& secondaryParts,
    const std::vector<CompParam>& params = {});

} // namespace mm
