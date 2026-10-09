#pragma once

// Derleme/durum damgasi: "hangi ikili calisiyor" ve "OCC acik mi" sorularini
// tek bakista bitirir. Baslik tasinabilirdir — windows.h veya Qt icermez,
// bu yuzden cekirdek, testler ve Qt kabugu ayni metni kullanabilir.
//
// OCC durumu DERLEME ZAMANI bilgisidir: MM_HAS_OCC yalnizca CMake
// OpenCASCADE'i buldugunda tanimlanir (bkz. CMakeLists.txt). Calisma aninda
// degistirilemez; bu yuzden exe'nin kendisi dogru kaynaktir.

namespace mm {

// Derleme zamani damgasi. Baslikta gosterildiginde kullanicinin actigi
// pencerenin hangi derlemeye ait oldugu tartismasiz olur.
inline constexpr const char* buildStamp() noexcept { return __DATE__ " " __TIME__; }

#ifdef MM_HAS_OCC
#  ifdef MM_OCC_VERSION
inline constexpr const char* occStatus() noexcept { return "OCC acik (OCCT " MM_OCC_VERSION ")"; }
#  else
inline constexpr const char* occStatus() noexcept { return "OCC acik"; }
#  endif
#else
inline constexpr const char* occStatus() noexcept { return "OCC kapali (vcpkg/OCCT yok)"; }
#endif

} // namespace mm
