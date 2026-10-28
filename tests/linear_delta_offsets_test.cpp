// DOGRUSAL KOPYA ADEDI: k*delta ofsetleri (cekirdek testi — Linux'ta kosar).
#include "model_maker/linear_delta.hpp"

#include <cmath>
#include <cstdio>

namespace {

int failures = 0;

void check(bool ok, const char* what) {
    std::printf("%s  %s\n", ok ? "GECTI" : "KALDI", what);
    if (!ok) ++failures;
}

bool near(double a, double b) { return std::fabs(a - b) < 1e-9; }

} // namespace

int main() {
    // 1) Adet 1 = eski davranis: tek kopya, delta kadar.
    {
        const auto offsets = mm::linearCopyOffsets({1000.0, -500.0, 250.0}, 1);
        check(offsets.size() == 1, "adet=1 -> tek ofset");
        check(offsets.size() == 1 && near(offsets[0].x, 1000.0) && near(offsets[0].y, -500.0) &&
              near(offsets[0].z, 250.0), "adet=1 -> ofset = delta");
    }
    // 2) Adet 3: delta, 2*delta, 3*delta (kopyalar ust uste binmez).
    {
        const auto offsets = mm::linearCopyOffsets({500.0, 0.0, -100.0}, 3);
        check(offsets.size() == 3, "adet=3 -> 3 ofset");
        check(offsets.size() == 3 && near(offsets[0].x, 500.0) && near(offsets[1].x, 1000.0) &&
              near(offsets[2].x, 1500.0), "X ofsetleri 500/1000/1500");
        check(offsets.size() == 3 && near(offsets[0].z, -100.0) && near(offsets[1].z, -200.0) &&
              near(offsets[2].z, -300.0), "Z ofsetleri -100/-200/-300");
    }
    // 3) Adet 0 / negatif -> bos (cagiran 1'e ceker, sessiz yigin olmaz).
    check(mm::linearCopyOffsets({10.0, 10.0, 10.0}, 0).empty(), "adet=0 -> bos");
    check(mm::linearCopyOffsets({10.0, 10.0, 10.0}, -5).empty(), "adet=-5 -> bos");
    // 4) Ust sinir kirpilir (panel spinbox araligi ile ayni).
    {
        const auto offsets = mm::linearCopyOffsets({1.0, 0.0, 0.0}, 5000);
        check(static_cast<int>(offsets.size()) == mm::kLinearCopyMaxCount,
              "adet ust sinirla kirpilir");
        check(static_cast<int>(offsets.size()) == mm::kLinearCopyMaxCount &&
              near(offsets.back().x, static_cast<double>(mm::kLinearCopyMaxCount)),
              "son ofset = sinir * delta");
    }
    // 5) Negatif delta (ters yon) de dogrusal.
    {
        const auto offsets = mm::linearCopyOffsets({-250.0, 0.0, 0.0}, 2);
        check(offsets.size() == 2 && near(offsets[0].x, -250.0) && near(offsets[1].x, -500.0),
              "negatif delta: -250 / -500");
    }
    std::printf(failures == 0 ? "TUM KONTROLLER GECTI\n" : "%d KONTROL KALDI\n", failures);
    return failures == 0 ? 0 : 1;
}
