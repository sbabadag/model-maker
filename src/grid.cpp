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
    if (!grid.xLabels.empty())
        for (std::size_t i = grid.xLabels.size(); i < grid.xCoords.size(); ++i)
            grid.xLabels.push_back(gridAutoNumberLabel(i));
    if (!grid.yLabels.empty())
        for (std::size_t i = grid.yLabels.size(); i < grid.yCoords.size(); ++i)
            grid.yLabels.push_back(gridAutoLetterLabel(i));
    if (!grid.zLabels.empty())
        for (std::size_t i = grid.zLabels.size(); i < grid.zCoords.size(); ++i)
            grid.zLabels.push_back(gridAutoNumberLabel(i));
}

void rebuildGridAxes(GridDefinition& grid) {
    grid.axes.clear();
    const auto spanOf = [](const std::vector<double>& v) {
        double s = 0.0;
        for (const double d : v) s = std::max(s, d);
        return s;
    };
    const double spanX = spanOf(grid.xCoords);
    const double spanY = spanOf(grid.yCoords);
    const double left = std::max(0.0, grid.extendLeft);
    const double right = std::max(0.0, grid.extendRight);
    const double below = std::max(0.0, grid.extendBelow);
    const double above = std::max(0.0, grid.extendAbove);
    const Vec3 o = grid.origin;

    // X konumlari -> DUKEY akslar (Y boyunca), X etiketleri.
    for (std::size_t i = 0; i < grid.xCoords.size(); ++i) {
        GridAxisLine line;
        const double x = o.x + grid.xCoords[i];
        line.from = Vec3{x, o.y - below, o.z};
        line.to = Vec3{x, o.y + spanY + above, o.z};
        line.horizontal = false;
        line.label = (i < grid.xLabels.size()) ? grid.xLabels[i] : std::string{};
        grid.axes.push_back(std::move(line));
    }
    // Y konumlari -> YATAY akslar (X boyunca), Y etiketleri.
    for (std::size_t i = 0; i < grid.yCoords.size(); ++i) {
        GridAxisLine line;
        const double y = o.y + grid.yCoords[i];
        line.from = Vec3{o.x - left, y, o.z};
        line.to = Vec3{o.x + spanX + right, y, o.z};
        line.horizontal = true;
        line.label = (i < grid.yLabels.size()) ? grid.yLabels[i] : std::string{};
        grid.axes.push_back(std::move(line));
    }
    // Z kotlari -> kat duzlemi kenar cizgileri (on/arka); etiket ON kenarda.
    for (std::size_t i = 0; i < grid.zCoords.size(); ++i) {
        const double z = o.z + grid.zCoords[i];
        const std::string label = (i < grid.zLabels.size()) ? grid.zLabels[i] : std::string{};
        GridAxisLine front;
        front.from = Vec3{o.x - left, o.y - below, z};
        front.to = Vec3{o.x + spanX + right, o.y - below, z};
        front.horizontal = true;
        front.label = label;
        grid.axes.push_back(std::move(front));

        GridAxisLine back;
        back.from = Vec3{o.x - left, o.y + spanY + above, z};
        back.to = Vec3{o.x + spanX + right, o.y + spanY + above, z};
        back.horizontal = true;
        back.label.clear();
        grid.axes.push_back(std::move(back));
    }
}

} // namespace mm
