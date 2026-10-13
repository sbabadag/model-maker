#pragma once
// SHA-256 (FIPS 180-4) — kendine-yeterli, harici bagimlilik YOK.
//
// MMW8 binary B-Rep blob'larinin butunluk dogrulamasi icin kullanilir. Repoda
// veya mevcut bagimliliklarda hazir bir SHA-256 yoktu (OpenSSL/libcrypto bu
// host'ta mevcut ama Windows/MinGW-MSVC derlemesine ek bir DIS bagimlilik
// getirirdi; kapsam "gerekmedikce yeni bagimlilik ekleme" oldugundan bu
// kendine-yeterli uygulama tercih edildi). Standart NIST vektorleriyle
// dogrulanir (bkz. tests/mmw8_codec_test.cpp).

#include <cstddef>
#include <cstdint>
#include <string>

namespace mm {

// 32 bayt ham SHA-256 ozeti.
struct Sha256Digest {
    std::uint8_t bytes[32]{};
};

// `size` baytlik `data`nin SHA-256 ozeti.
Sha256Digest sha256(const void* data, std::size_t size);

// 64 kucuk-hex karakterlik SHA-256 dizgisi (kolaylik).
std::string sha256Hex(const void* data, std::size_t size);
std::string sha256Hex(const std::string& data);

} // namespace mm
