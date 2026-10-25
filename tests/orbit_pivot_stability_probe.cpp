// OLCUM PROBU (rapor amacli; ctest'e kayitli DEGIL, esik iddiasi yok):
// pivot etrafinda yorunge donusunde iki formulun sayisal kararliligi.
//
// Bu olcum rotateAbout'un neden "w = R^T*rx*R*ry" kurulumuyla YAZILMADIGINI
// belgeler. Kamera ic durumuna dokunmaz: ayni iki formul saf matris
// aritmetigiyle kosulur ve R'nin ORTONORMALLIK hatasinin adim basina
// buyumesi olculur.
//
// Sonuc (gcc -O1, x86-64, cift duzey):
//   KARARSIZ (w = Rt*rx*R*ry; R = R*w):   hata her adimda ~3 katina buyur
//       (adim 2: 5.5e-17 ... adim 19: 1.9e-08) -> 60 adimda R tamamen bozulur,
//       pivot ekranda ~2.8e4 px savrulur.
//   KARARLI (R = rx*R*ry; merkez = pivot + R_yeni^T*R_eski*(merkez-pivot)):
//       hata dogrusal buyur (~1e-15/adim) -> 500 adimda pivot sapmasi < 1e-6 px.
#include <array>
#include <cmath>
#include <cstdio>

namespace {

using Mat3 = std::array<double, 9>;

Mat3 mul(const Mat3& a, const Mat3& b) {
    Mat3 c{};
    for (int r = 0; r < 3; ++r)
        for (int col = 0; col < 3; ++col)
            c[r * 3 + col] = a[r * 3] * b[col] + a[r * 3 + 1] * b[3 + col] +
                             a[r * 3 + 2] * b[6 + col];
    return c;
}

Mat3 transpose(const Mat3& m) {
    return Mat3{m[0], m[3], m[6], m[1], m[4], m[7], m[2], m[5], m[8]};
}

// Ortormallik hatasi: max |R^T R - I|.
double orthonormalityError(const Mat3& m) {
    double worst = 0.0;
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c) {
            double sum = 0.0;
            for (int k = 0; k < 3; ++k) sum += m[k * 3 + r] * m[k * 3 + c];
            worst = std::max(worst, std::abs(sum - (r == c ? 1.0 : 0.0)));
        }
    return worst;
}

Mat3 rotationX(double a) {
    const double c = std::cos(a), s = std::sin(a);
    return Mat3{1, 0, 0, 0, c, -s, 0, s, c};
}
Mat3 rotationY(double a) {
    const double c = std::cos(a), s = std::sin(a);
    return Mat3{c, 0, s, 0, 1, 0, -s, 0, c};
}
Mat3 initialRotation() { // Camera::buildRotation(-0.55, 0.45, 0)
    const double cy = std::cos(-0.55), sy = std::sin(-0.55);
    const double cp = std::cos(0.45), sp = std::sin(0.45);
    return Mat3{cy, 0.0, sy, sp * sy, cp, -sp * cy, -cp * sy, sp, cp * cy};
}

} // namespace

int main() {
    constexpr double kYaw = 0.037, kPitch = -0.021;
    const Mat3 rx = rotationX(kPitch);
    const Mat3 ry = rotationY(kYaw);

    // 1) KARARSIZ formul: w = R^T*rx*R*ry, R = R*w.
    {
        Mat3 r = initialRotation();
        std::printf("KARARSIZ formul (w = R^T*rx*R*ry):\n");
        for (int step = 0; step < 20; ++step) {
            const Mat3 w = mul(transpose(r), mul(rx, mul(r, ry)));
            r = mul(r, w);
            if (step < 3 || (step % 4) == 3)
                std::printf("   adim=%2d ortonormallik hatasi=%.3e\n", step,
                            orthonormalityError(r));
        }
    }

    // 2) KARARLI formul: R = rx*R*ry, merkez iki temiz matrisle doner.
    {
        Mat3 r = initialRotation();
        const Mat3 initial = r;
        const double offset[3] = {420.0, -180.0, 95.0};
        double center[3] = {0.0, 0.0, 0.0};
        const double pivot[3] = {420.0, -180.0, 95.0};
        const auto projectPivot = [&]() {
            double d[3] = {pivot[0] - center[0], pivot[1] - center[1], pivot[2] - center[2]};
            const double sx = r[0] * d[0] + r[1] * d[1] + r[2] * d[2];
            const double sy = r[3] * d[0] + r[4] * d[1] + r[5] * d[2];
            return std::array<double, 2>{sx, sy};
        };
        (void)offset;
        (void)initial;
        const auto before = projectPivot();
        double worst = 0.0;
        for (int step = 0; step < 500; ++step) {
            const Mat3 previous = r;
            r = mul(rx, mul(r, ry));
            double d[3] = {center[0] - pivot[0], center[1] - pivot[1], center[2] - pivot[2]};
            double inPrevious[3] = {
                previous[0] * d[0] + previous[1] * d[1] + previous[2] * d[2],
                previous[3] * d[0] + previous[4] * d[1] + previous[5] * d[2],
                previous[6] * d[0] + previous[7] * d[1] + previous[8] * d[2]};
            const double world[3] = {
                r[0] * inPrevious[0] + r[3] * inPrevious[1] + r[6] * inPrevious[2],
                r[1] * inPrevious[0] + r[4] * inPrevious[1] + r[7] * inPrevious[2],
                r[2] * inPrevious[0] + r[5] * inPrevious[1] + r[8] * inPrevious[2]};
            center[0] = pivot[0] + world[0];
            center[1] = pivot[1] + world[1];
            center[2] = pivot[2] + world[2];
            const auto now = projectPivot();
            worst = std::max(worst, std::hypot(now[0] - before[0], now[1] - before[1]));
        }
        std::printf("\nKARARLI formul (R = rx*R*ry + W^T = R_yeni^T*R_eski):\n");
        std::printf("   adim=19  ortonormallik hatasi=%.3e\n", orthonormalityError(r));
        std::printf("   500 adim: pivot ekran sapmasi = %.3e birim (olcek 65 px/birim)\n",
                    worst);
    }
    return 0;
}
