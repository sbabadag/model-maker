#pragma once

// KOMUT KATALOĞU (UI-2) — tum komutlarin ve gorunum planlarinin tek kaynagi.
//
// `buildCommandCatalog()` saf veri dondurur (Qt yok, windows.h yok). Qt kabugu
// bu katalogdan ≡ menusunu ve seridi cizer; Win32 tarafi ayni katalogdan
// klavye cozumlemesi yapar. Isleyici GOVDELERI platform katmanindadir.

#include "model_maker/command_registry.hpp"

namespace mm {

CommandCatalog buildCommandCatalog();

} // namespace mm
