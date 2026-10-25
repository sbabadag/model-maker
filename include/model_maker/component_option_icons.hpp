#pragma once
// Tekla 146 dialog acilir listeleri: her secenegin yardim sayfasindaki resmi.
// Tablo tools/tekla146_canvas/mk_opt_icons.py ile uretilir (src/component_option_icons.cpp).
#include <map>
#include <string>
#include <vector>

namespace mm {

// param adinin secenek resimleri (Enum choices sirasina gore; bos dize = resim yok).
// Parametrenin resmi yoksa nullptr.
const std::vector<std::wstring>* optionIconsFor(const std::wstring& param);

}  // namespace mm
