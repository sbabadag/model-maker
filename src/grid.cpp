#include "model_maker/grid.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace mm {
namespace {

bool isSpaceChar(char c) noexcept {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

std::vector<std::string> splitTokens(std::string_view text) {
    std::vector<std::string> out;
    std::size_t i = 0;
    while (i < text.size()) {
        while (i < text.size() && isSpaceChar(text[i])) ++i;
        if (i >= text.size()) break;
        const std::size_t start = i;
        while (i < text.size() && !isSpaceChar(text[i])) ++i;
        out.emplace_back(text.substr(start, i - start));
    }
    return out;
}

// "12" / "12.5" / "1,5" -> sayi. Basarisizsa false.
bool parseDoubleToken(const std::string& tok, double& out) {
    if (tok.empty()) return false;
    std::string s = tok;
    for (char& c : s)
        if (c == ',') c = '.';
    char* end = nullptr;
    const double v = std::strtod(s.c_str(), &end);
    if (end == s.c_str() || (end != nullptr && *end != '\0')) return false;
    out = v;
    return true;
}

} // namespace

std::vector<double> parseGridCoordinateLine(std::string_view text, bool cumulative) {
    std::vector<double> raw;
    for (const auto& tok : splitTokens(text)) {
        // Tekla tekrar yazimi: "N*deger".
        const std::size_t star = tok.find('*');
        if (star != std::string::npos && star > 0 && star + 1 < tok.size()) {
            double count = 0.0;
            double value = 0.0;
            if (parseDoubleToken(tok.substr(0, star), count) &&
                parseDoubleToken(tok.substr(star + 1), value) &&
                count >= 1.0 && count <= 1024.0) {
                const int n = static_cast<int>(std::lround(count));
                for (int k = 0; k < n; ++k) raw.push_back(value);
                continue;
            }
        }
        double v = 0.0;
        if (parseDoubleToken(tok, v)) raw.push_back(v);
    }
    if (!cumulative) return raw;
    std::vector<double> out;
    out.reserve(raw.size());
    double acc = 0.0;
    for (const double v : raw) {
        acc += v;
        out.push_back(acc);
    }
    return out;
}

std::vector<std::string> parseGridLabelLine(std::string_view text) {
    return splitTokens(text);
}

std::string gridAutoNumberLabel(std::size_t index) {
    return std::to_string(index + 1);
}

std::string gridAutoLetterLabel(std::size_t index) {
    std::string s;
    long n = static_cast<long>(index);
    do {
        s.insert(s.begin(), static_cast<char>('A' + (n % 26)));
        n = n / 26 - 1;
    } while (n >= 0);
    return s;
}

void ensureGridLabels(GridDefinition& grid) {
    // X ailesi (Y'ye paralel DIKEY cizgiler) HARF alir, DUZ SIRA:
    // en kucuk X (en soldaki / orijin aksi) = A, saga dogru artar
    // (A, B, C, ...). Y ailesi (X'e paralel YATAY cizgiler) SAYI alir,
    // "asagidan yukari": en kucuk Y = 1. (Kullanici sarti; A/1 kesisimi
    // pozitif X/Y ile orijinde kalir. Tekla varsayilani X'e sayi / Y'ye
    // harf koyar, bu tersidir.)
    if (!grid.xLabels.empty()) {
        const std::size_t n = grid.xCoords.size();
        for (std::size_t i = grid.xLabels.size(); i < n; ++i)
            grid.xLabels.push_back(gridAutoLetterLabel(i));
    }
    if (!grid.yLabels.empty())
        for (std::size_t i = grid.yLabels.size(); i < grid.yCoords.size(); ++i)
            grid.yLabels.push_back(gridAutoNumberLabel(i));
    if (!grid.zLabels.empty())
        for (std::size_t i = grid.zLabels.size(); i < grid.zCoords.size(); ++i)
            grid.zLabels.push_back(gridAutoNumberLabel(i));
}

namespace {

double maxCoord(const std::vector<double>& values) {
    double span = 0.0;
    for (const double value : values) span = std::max(span, value);
    return span;
}

bool zRangeOf(const std::vector<double>& values, double& low, double& high) {
    if (values.empty()) {
        low = high = 0.0;
        return false;
    }
    low = high = values.front();
    for (const double value : values) {
        low = std::min(low, value);
        high = std::max(high, value);
    }
    return true;
}

std::string labelAt(const std::vector<std::string>& labels, std::size_t index) {
    return index < labels.size() ? labels[index] : std::string{};
}

// Tekla "Line extensions": Left/Below ve Right/Above AYRI. Plan duzleminde
// left/right X yonunde, below/above Y yonundedir; elevasyonda below/above
// DUSEY (Z) uzanti olarak kullanilir.
struct GridExtents {
    double left{};
    double right{};
    double below{};
    double above{};
    double spanX{};
    double spanY{};
};

GridExtents gridExtents(const GridDefinition& grid) {
    GridExtents e;
    e.spanX = maxCoord(grid.xCoords);
    e.spanY = maxCoord(grid.yCoords);
    e.left = std::max(0.0, grid.extendLeft);
    e.right = std::max(0.0, grid.extendRight);
    e.below = std::max(0.0, grid.extendBelow);
    e.above = std::max(0.0, grid.extendAbove);
    return e;
}

// PLAN (XY) duzlemindeki iki aile: X konumlari -> Y'ye PARALEL cizgiler,
// Y konumlari -> X'e PARALEL cizgiler. Ikisi de z = origin.z duzlemindedir.
// Z (kat) duzlemleri XY duzlemine PARALELDIR -> plan gorunusunde cizilmez.
void appendPlanFamilies(const GridDefinition& grid, const GridExtents& e,
                        std::vector<GridAxisLine>& out) {
    const Vec3 o = grid.origin;
    for (std::size_t i = 0; i < grid.xCoords.size(); ++i) {
        GridAxisLine line;
        const double x = o.x + grid.xCoords[i];
        line.from = Vec3{x, o.y - e.below, o.z};
        line.to = Vec3{x, o.y + e.spanY + e.above, o.z};
        line.horizontal = false;
        line.label = labelAt(grid.xLabels, i);
        out.push_back(std::move(line));
    }
    for (std::size_t i = 0; i < grid.yCoords.size(); ++i) {
        GridAxisLine line;
        const double y = o.y + grid.yCoords[i];
        line.from = Vec3{o.x - e.left, y, o.z};
        line.to = Vec3{o.x + e.spanX + e.right, y, o.z};
        line.horizontal = true;
        line.label = labelAt(grid.yLabels, i);
        out.push_back(std::move(line));
    }
}

} // namespace

std::vector<GridAxisLine> gridAxesForView(const GridDefinition& grid, GridViewPlane plane) {
    std::vector<GridAxisLine> out;
    const GridExtents e = gridExtents(grid);
    const Vec3 o = grid.origin;
    if (plane == GridViewPlane::XY) {
        // Plan gorunusu (bakış Z): X ve Y aileleri; kat duzlemleri PARALEL.
        appendPlanFamilies(grid, e, out);
        return out;
    }
    double zLow = 0.0, zHigh = 0.0;
    zRangeOf(grid.zCoords, zLow, zHigh);
    const double zBottom = o.z + zLow - e.below;
    const double zTop = o.z + zHigh + e.above;
    if (plane == GridViewPlane::XZ) {
        // On/arka gorunus (bakış Y): gorunus duzlemi Y = origin.y.
        // X duzlemleri -> DIKEY cizgiler (Z boyunca), Z duzlemleri -> YATAY
        // cizgiler (X boyunca). Y duzlemleri gorunus duzlemine PARALEL -> yok.
        for (std::size_t i = 0; i < grid.xCoords.size(); ++i) {
            GridAxisLine line;
            const double x = o.x + grid.xCoords[i];
            line.from = Vec3{x, o.y, zBottom};
            line.to = Vec3{x, o.y, zTop};
            line.horizontal = false;
            line.label = labelAt(grid.xLabels, i);
            out.push_back(std::move(line));
        }
        for (std::size_t i = 0; i < grid.zCoords.size(); ++i) {
            GridAxisLine line;
            const double z = o.z + grid.zCoords[i];
            line.from = Vec3{o.x - e.left, o.y, z};
            line.to = Vec3{o.x + e.spanX + e.right, o.y, z};
            line.horizontal = true;
            line.label = labelAt(grid.zLabels, i);
            out.push_back(std::move(line));
        }
        return out;
    }
    // YZ (yan gorunus, bakış X): gorunus duzlemi X = origin.x.
    // Y duzlemleri -> DIKEY cizgiler (Z boyunca), Z duzlemleri -> YATAY
    // cizgiler (Y boyunca). X duzlemleri PARALEL -> yok.
    for (std::size_t j = 0; j < grid.yCoords.size(); ++j) {
        GridAxisLine line;
        const double y = o.y + grid.yCoords[j];
        line.from = Vec3{o.x, y, zBottom};
        line.to = Vec3{o.x, y, zTop};
        line.horizontal = false;
        line.label = labelAt(grid.yLabels, j);
        out.push_back(std::move(line));
    }
    for (std::size_t i = 0; i < grid.zCoords.size(); ++i) {
        GridAxisLine line;
        const double z = o.z + grid.zCoords[i];
        line.from = Vec3{o.x, o.y - e.below, z};
        line.to = Vec3{o.x, o.y + e.spanY + e.above, z};
        line.horizontal = true;
        line.label = labelAt(grid.zLabels, i);
        out.push_back(std::move(line));
    }
    return out;
}

GridViewPlane gridViewPlaneForDirection(const Vec3& viewDirection) noexcept {
    const double ax = std::abs(viewDirection.x);
    const double ay = std::abs(viewDirection.y);
    const double az = std::abs(viewDirection.z);
    // Gorunus duzlemi baskin eksene DIKTIR. Esitlikte (izometrik) Z kazanir ->
    // plan (XY) gridi. Karsilastirma TOLERANSLI olmali: uc bilesen ayri
    // ifadelerden geldigi icin (kamera matrisinin 3. satiri) matematiksel
    // esitlik kayan noktada tutmaz ve ISO'da sessizce YZ cikiyordu (olculdu:
    // reset()/Isometric -> (-0.57735,-0.57735,-0.57735), tam esitlik testiyle
    // YZ). Tolerans yalnizca gercek beraberlikler icin.
    constexpr double kTieEpsilon = 1e-9;
    if (az >= ax - kTieEpsilon && az >= ay - kTieEpsilon) return GridViewPlane::XY;
    if (ay >= ax - kTieEpsilon) return GridViewPlane::XZ;
    return GridViewPlane::YZ;
}

void rebuildGridAxes(GridDefinition& grid) {
    const GridExtents e = gridExtents(grid);
    const Vec3 o = grid.origin;
    std::vector<GridAxisLine> axes = gridAxesForView(grid, GridViewPlane::XY);
    // KAT CIZGILERI (on/arka kenarlar): bu turev SNAP (grid kesisimleri) ve
    // zoom-extents tarafindan kullanilir — eski davranis BIREBIR korunur.
    for (std::size_t i = 0; i < grid.zCoords.size(); ++i) {
        const double z = o.z + grid.zCoords[i];
        const std::string label = labelAt(grid.zLabels, i);
        GridAxisLine front;
        front.from = Vec3{o.x - e.left, o.y - e.below, z};
        front.to = Vec3{o.x + e.spanX + e.right, o.y - e.below, z};
        front.horizontal = true;
        front.label = label;
        axes.push_back(std::move(front));

        GridAxisLine back;
        back.from = Vec3{o.x - e.left, o.y + e.spanY + e.above, z};
        back.to = Vec3{o.x + e.spanX + e.right, o.y + e.spanY + e.above, z};
        back.horizontal = true;
        back.label.clear();
        axes.push_back(std::move(back));
    }
    grid.axes = std::move(axes);
}

} // namespace mm
