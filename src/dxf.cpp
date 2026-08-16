#include "model_maker/dxf.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <numeric>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace mm {
namespace {

struct Pair {
    int code{};
    std::string value;
};

std::string trim(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

class PairReader {
public:
    PairReader(std::istream& input, std::stop_token stopToken, std::uint64_t totalBytes,
               DxfFile::ProgressCallback progress)
        : input_(input), stopToken_(stopToken), totalBytes_(totalBytes), progress_(std::move(progress)) {}

    std::optional<Pair> next() {
        if (pending_) {
            auto result = std::move(pending_);
            pending_.reset();
            return result;
        }
        if (stopToken_.stop_requested()) throw DxfImportCancelled();
        std::string codeLine;
        std::string valueLine;
        if (!std::getline(input_, codeLine)) {
            if (progress_) progress_(totalBytes_, totalBytes_);
            return std::nullopt;
        }
        if (!std::getline(input_, valueLine)) throw std::runtime_error("Truncated DXF group pair");
        bytesRead_ += codeLine.size() + valueLine.size() + 2;
        if (progress_ && (++pairCount_ % 8192 == 0)) progress_(bytesRead_, totalBytes_);
        try {
            std::size_t used{};
            const int code = std::stoi(trim(codeLine), &used);
            if (used != trim(codeLine).size()) throw std::runtime_error("Invalid DXF group code");
            return Pair{code, trim(valueLine)};
        } catch (const std::exception&) {
            throw std::runtime_error("Invalid DXF group code");
        }
    }

    void putBack(Pair pair) {
        if (pending_) throw std::logic_error("DXF parser lookahead overflow");
        pending_ = std::move(pair);
    }

private:
    std::istream& input_;
    std::optional<Pair> pending_;
    std::stop_token stopToken_;
    std::uint64_t totalBytes_{};
    std::uint64_t bytesRead_{};
    std::uint64_t pairCount_{};
    DxfFile::ProgressCallback progress_;
};

std::vector<Pair> fields(PairReader& reader) {
    std::vector<Pair> result;
    while (const auto pair = reader.next()) {
        if (pair->code == 0) {
            reader.putBack(*pair);
            break;
        }
        if (result.size() >= 1'000'000) throw std::runtime_error("DXF entity is too large");
        result.push_back(*pair);
    }
    return result;
}

double number(const std::vector<Pair>& values, int code, double fallback = 0.0) {
    for (const auto& pair : values) {
        if (pair.code != code) continue;
        try {
            const double value = std::stod(pair.value);
            if (!std::isfinite(value)) throw std::runtime_error("Non-finite DXF coordinate");
            return value;
        } catch (const std::exception&) {
            throw std::runtime_error("Invalid DXF numeric value");
        }
    }
    return fallback;
}

int integer(const std::vector<Pair>& values, int code, int fallback = 0) {
    return static_cast<int>(number(values, code, fallback));
}

Vec3 point(const std::vector<Pair>& values, int xCode, int yCode, int zCode) {
    return {number(values, xCode), number(values, yCode), number(values, zCode)};
}

std::string text(const std::vector<Pair>& values, int code, std::string fallback = {}) {
    for (const auto& pair : values) if (pair.code == code) return pair.value;
    return fallback;
}

std::optional<int> optionalInteger(const std::vector<Pair>& values, int code) {
    for (const auto& pair : values) {
        if (pair.code != code) continue;
        try { return std::stoi(pair.value); }
        catch (...) { throw std::runtime_error("Invalid DXF integer value"); }
    }
    return std::nullopt;
}

std::uint32_t aciColor(int index) noexcept {
    index = std::abs(index);
    switch (index) {
    case 1: return 0xFF0000;
    case 2: return 0xFFFF00;
    case 3: return 0x00FF00;
    case 4: return 0x00FFFF;
    case 5: return 0x0000FF;
    case 6: return 0xFF00FF;
    case 7: return 0xFFFFFF;
    case 8: return 0x808080;
    case 9: return 0xC0C0C0;
    case 250: return 0x333333;
    case 251: return 0x505050;
    case 252: return 0x696969;
    case 253: return 0x828282;
    case 254: return 0xBEBEBE;
    case 255: return 0xFFFFFF;
    default: break;
    }
    if (index < 10 || index > 249) return 0xFFFFFF;
    const int family = (index - 10) / 10;
    const int shade = (index - 10) % 10;
    constexpr double values[5]{1.0, 0.74, 0.506, 0.408, 0.31};
    const double hue = family * 15.0 / 60.0;
    const double saturation = (shade % 2 == 0) ? 1.0 : 1.0 / 3.0;
    const double value = values[shade / 2];
    const int sector = static_cast<int>(std::floor(hue)) % 6;
    const double fraction = hue - std::floor(hue);
    const double p = value * (1.0 - saturation);
    const double q = value * (1.0 - saturation * fraction);
    const double t = value * (1.0 - saturation * (1.0 - fraction));
    double red{}, green{}, blue{};
    switch (sector) {
    case 0: red = value; green = t; blue = p; break;
    case 1: red = q; green = value; blue = p; break;
    case 2: red = p; green = value; blue = t; break;
    case 3: red = p; green = q; blue = value; break;
    case 4: red = t; green = p; blue = value; break;
    default: red = value; green = p; blue = q; break;
    }
    const auto byte = [](double component) {
        return static_cast<std::uint32_t>(std::clamp(std::lround(component * 255.0), 0L, 255L));
    };
    return (byte(red) << 16) | (byte(green) << 8) | byte(blue);
}

using LayerMap = std::unordered_map<std::string, EntityProperties>;

EntityProperties readLayerProperties(const std::vector<Pair>& values) {
    EntityProperties layer;
    layer.layer = text(values, 2, "0");
    layer.lineType = text(values, 6, "CONTINUOUS");
    const int rawColor = optionalInteger(values, 62).value_or(7);
    const int flags = integer(values, 70);
    layer.visible = rawColor >= 0;
    layer.frozen = (flags & 1) != 0;
    layer.locked = (flags & 4) != 0;
    layer.plottable = optionalInteger(values, 290).value_or(1) != 0;
    layer.description = text(values, 4, "");
    layer.colorIndex = std::abs(rawColor);
    if (const auto trueColor = optionalInteger(values, 420))
        layer.trueColor = static_cast<std::uint32_t>(*trueColor) & 0xFFFFFFu;
    layer.lineWeight = optionalInteger(values, 370).value_or(-3);
    layer.effectiveColor = layer.trueColor.value_or(aciColor(layer.colorIndex));
    layer.effectiveLineWeight = layer.lineWeight >= 0 ? layer.lineWeight : 0;
    layer.effectiveLineType = layer.lineType;
    return layer;
}

EntityProperties readEntityProperties(const std::vector<Pair>& values, const LayerMap& layers) {
    EntityProperties result;
    result.layer = text(values, 8, "0");
    result.lineType = text(values, 6, "BYLAYER");
    const int rawColor = optionalInteger(values, 62).value_or(256);
    result.visible = rawColor >= 0 && optionalInteger(values, 60).value_or(0) == 0;
    result.colorIndex = std::abs(rawColor);
    if (const auto trueColor = optionalInteger(values, 420))
        result.trueColor = static_cast<std::uint32_t>(*trueColor) & 0xFFFFFFu;
    result.lineWeight = optionalInteger(values, 370).value_or(-1);
    result.thickness = number(values, 39, 0.0);
    result.lineTypeScale = number(values, 48, 1.0);
    result.transparency = optionalInteger(values, 440).value_or(0);
    const auto layerIt = layers.find(result.layer);
    const EntityProperties* layer = layerIt == layers.end() ? nullptr : &layerIt->second;
    if (result.trueColor) result.effectiveColor = *result.trueColor;
    else if (result.colorIndex > 0 && result.colorIndex < 256) result.effectiveColor = aciColor(result.colorIndex);
    else result.effectiveColor = layer ? layer->effectiveColor : aciColor(7);
    result.effectiveLineWeight = result.lineWeight >= 0 ? result.lineWeight
                                : layer ? layer->effectiveLineWeight : 0;
    result.effectiveLineType = (result.lineType != "BYLAYER" && result.lineType != "BYBLOCK")
                             ? result.lineType
                             : layer ? layer->effectiveLineType : "CONTINUOUS";
    return result;
}

Vec3 readVertex(PairReader& reader) {
    Vec3 result{};
    while (const auto pair = reader.next()) {
        if (pair->code == 0) { reader.putBack(*pair); break; }
        try {
            if (pair->code == 10) result.x = std::stod(pair->value);
            else if (pair->code == 20) result.y = std::stod(pair->value);
            else if (pair->code == 30) result.z = std::stod(pair->value);
        } catch (const std::exception&) {
            throw std::runtime_error("Invalid POLYLINE vertex");
        }
    }
    if (!std::isfinite(result.x) || !std::isfinite(result.y) || !std::isfinite(result.z))
        throw std::runtime_error("Non-finite POLYLINE vertex");
    return result;
}

WireframeModel arcModel(Vec3 center, double radius, double startDegrees, double endDegrees) {
    if (radius <= 0.0) throw std::runtime_error("DXF ARC radius must be positive");
    while (endDegrees <= startDegrees) endDegrees += 360.0;
    const double span = std::min(360.0, endDegrees - startDegrees);
    const std::size_t segmentCount = std::max<std::size_t>(8, static_cast<std::size_t>(std::ceil(span / 5.0)));
    std::vector<Vec3> vertices;
    std::vector<Edge> edges;
    vertices.reserve(segmentCount + 1);
    edges.reserve(segmentCount);
    constexpr double radians = 3.14159265358979323846 / 180.0;
    for (std::size_t i = 0; i <= segmentCount; ++i) {
        const double angle = (startDegrees + span * static_cast<double>(i) /
                              static_cast<double>(segmentCount)) * radians;
        vertices.push_back({center.x + radius * std::cos(angle),
                            center.y + radius * std::sin(angle), center.z});
        if (i) edges.push_back({i - 1, i});
    }
    return {std::move(vertices), std::move(edges)};
}

WireframeModel textStrokeModel(const std::vector<Pair>& values) {
    std::string content;
    for (const auto& field : values)
        if (field.code == 3 || field.code == 1) content += field.value;
    std::string cleaned;
    bool formatting{};
    for (std::size_t i = 0; i < content.size(); ++i) {
        if (content[i] == '\\') {
            if (i + 1 < content.size() && (content[i + 1] == 'P' || content[i + 1] == 'p')) {
                cleaned.push_back(' '); ++i; continue;
            }
            formatting = true;
            continue;
        }
        if (formatting) {
            if (content[i] == ';') formatting = false;
            continue;
        }
        if (content[i] != '{' && content[i] != '}') cleaned.push_back(content[i]);
    }
    if (cleaned.empty()) cleaned = "?";
    const double height = std::max(1e-9, number(values, 40, 1.0));
    const double advance = height * 0.78;
    const double width = height * 0.60;
    double angle = number(values, 50, 0.0) * 3.14159265358979323846 / 180.0;
    const bool hasDirection = std::any_of(values.begin(), values.end(), [](const Pair& value) {
        return value.code == 11 || value.code == 21;
    });
    if (hasDirection) angle = std::atan2(number(values, 21), number(values, 11, 1.0));
    const double cosine = std::cos(angle), sine = std::sin(angle);
    const Vec3 origin = point(values, 10, 20, 30);
    double alignment = 0.0;
    const int attachment = integer(values, 71, 1);
    if (attachment == 2 || attachment == 5 || attachment == 8)
        alignment = -advance * static_cast<double>(cleaned.size()) * 0.5;
    else if (attachment == 3 || attachment == 6 || attachment == 9)
        alignment = -advance * static_cast<double>(cleaned.size());
    std::vector<Vec3> vertices;
    std::vector<Edge> edges;
    const auto addSegment = [&](double x1, double y1, double x2, double y2) {
        const auto world = [&](double x, double y) {
            return Vec3{origin.x + cosine * x - sine * y,
                        origin.y + sine * x + cosine * y, origin.z};
        };
        const std::size_t first = vertices.size();
        vertices.push_back(world(x1, y1));
        vertices.push_back(world(x2, y2));
        edges.push_back({first, first + 1});
    };
    constexpr unsigned char digitSegments[10]{0x3F, 0x06, 0x5B, 0x4F, 0x66,
                                               0x6D, 0x7D, 0x07, 0x7F, 0x6F};
    for (std::size_t index = 0; index < cleaned.size(); ++index) {
        const double x = alignment + static_cast<double>(index) * advance;
        unsigned char mask{};
        const char character = cleaned[index];
        if (character >= '0' && character <= '9') mask = digitSegments[character - '0'];
        else if (character == '-') mask = 0x40;
        else if (character == '.') {
            addSegment(x + width * 0.48, 0.0, x + width * 0.52, height * 0.04); continue;
        } else if (character == ' ') continue;
        else {
            addSegment(x, 0.0, x, height); addSegment(x, height, x + width, height);
            addSegment(x + width, height, x + width, 0.0); addSegment(x + width, 0.0, x, 0.0);
            addSegment(x, 0.0, x + width, height);
            continue;
        }
        if (mask & 0x01) addSegment(x, height, x + width, height);
        if (mask & 0x02) addSegment(x + width, height, x + width, height * 0.5);
        if (mask & 0x04) addSegment(x + width, height * 0.5, x + width, 0.0);
        if (mask & 0x08) addSegment(x + width, 0.0, x, 0.0);
        if (mask & 0x10) addSegment(x, 0.0, x, height * 0.5);
        if (mask & 0x20) addSegment(x, height * 0.5, x, height);
        if (mask & 0x40) addSegment(x, height * 0.5, x + width, height * 0.5);
    }
    return {std::move(vertices), std::move(edges)};
}

void addSimpleEntity(Document& document, const std::string& type, const std::vector<Pair>& values,
                     const LayerMap& layers) {
    std::optional<WireframeModel> model;
    if (type == "LINE") {
        model = WireframeModel::line(point(values, 10, 20, 30), point(values, 11, 21, 31));
    } else if (type == "POINT") {
        model = WireframeModel::point(point(values, 10, 20, 30));
    } else if (type == "CIRCLE") {
        const double radius = number(values, 40);
        if (radius <= 0.0) throw std::runtime_error("DXF CIRCLE radius must be positive");
        model = WireframeModel::circle(point(values, 10, 20, 30), radius);
    } else if (type == "ARC") {
        model = arcModel(point(values, 10, 20, 30), number(values, 40),
                         number(values, 50), number(values, 51));
    } else if (type == "LWPOLYLINE") {
        const double elevation = number(values, 38);
        std::vector<Vec3> vertices;
        for (const auto& pair : values) {
            if (pair.code == 10) {
                try { vertices.push_back({std::stod(pair.value), 0.0, elevation}); }
                catch (const std::exception&) { throw std::runtime_error("Invalid LWPOLYLINE vertex"); }
            } else if (pair.code == 20 && !vertices.empty()) {
                try { vertices.back().y = std::stod(pair.value); }
                catch (const std::exception&) { throw std::runtime_error("Invalid LWPOLYLINE vertex"); }
            }
        }
        if (vertices.empty()) return;
        std::vector<Edge> edges;
        edges.reserve(vertices.size());
        for (std::size_t i = 1; i < vertices.size(); ++i) edges.push_back({i - 1, i});
        if ((integer(values, 70) & 1) && vertices.size() > 1) edges.push_back({vertices.size() - 1, 0});
        model.emplace(std::move(vertices), std::move(edges));
    } else if (type == "3DFACE") {
        model = WireframeModel::face3D({point(values, 10, 20, 30), point(values, 11, 21, 31),
                                        point(values, 12, 22, 32), point(values, 13, 23, 33)});
    } else if (type == "SOLID" || type == "TRACE") {
        std::vector<Vec3> vertices{point(values, 10, 20, 30), point(values, 11, 21, 31),
                                   point(values, 13, 23, 33), point(values, 12, 22, 32)};
        if (vertices[2] == vertices[3]) vertices.pop_back();
        std::vector<Edge> edges;
        for (std::size_t i = 0; i < vertices.size(); ++i) edges.push_back({i, (i + 1) % vertices.size()});
        Face face(vertices.size());
        std::iota(face.begin(), face.end(), std::size_t{0});
        model.emplace(std::move(vertices), std::move(edges), std::vector<Face>{std::move(face)});
    } else if (type == "TEXT" || type == "MTEXT") {
        model = textStrokeModel(values);
    }
    if (model) {
        model->setProperties(readEntityProperties(values, layers));
        document.addModel(std::move(*model));
    }
}

void readLegacyPolyline(Document& document, PairReader& reader, const std::vector<Pair>& header,
                        const LayerMap& layers) {
    std::vector<Vec3> vertices;
    while (const auto marker = reader.next()) {
        if (marker->code != 0) continue;
        if (marker->value == "VERTEX") {
            vertices.push_back(readVertex(reader));
        } else if (marker->value == "SEQEND") {
            (void)fields(reader);
            break;
        } else {
            reader.putBack(*marker);
            break;
        }
    }
    if (vertices.empty()) return;
    std::vector<Edge> edges;
    edges.reserve(vertices.size());
    for (std::size_t i = 1; i < vertices.size(); ++i) edges.push_back({i - 1, i});
    if ((integer(header, 70) & 1) && vertices.size() > 1) edges.push_back({vertices.size() - 1, 0});
    WireframeModel model(std::move(vertices), std::move(edges));
    model.setProperties(readEntityProperties(header, layers));
    document.addModel(std::move(model));
}

struct InsertRecord { std::vector<Pair> fields; bool dimension{}; };
struct BlockDefinition {
    Vec3 base{};
    std::vector<WireframeModel> models;
    std::vector<InsertRecord> inserts;
};
using BlockMap = std::unordered_map<std::string, BlockDefinition>;

std::string blockKey(std::string name) {
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char value) {
        return static_cast<char>(std::toupper(value));
    });
    return name;
}

struct AffineTransform {
    std::array<std::array<double, 3>, 3> matrix{{{1.0, 0.0, 0.0},
                                                {0.0, 1.0, 0.0},
                                                {0.0, 0.0, 1.0}}};
    Vec3 translation{};

    Vec3 vector(Vec3 point) const noexcept {
        return {matrix[0][0] * point.x + matrix[0][1] * point.y + matrix[0][2] * point.z,
                matrix[1][0] * point.x + matrix[1][1] * point.y + matrix[1][2] * point.z,
                matrix[2][0] * point.x + matrix[2][1] * point.y + matrix[2][2] * point.z};
    }
    Vec3 point(Vec3 value) const noexcept { return vector(value) + translation; }
};

AffineTransform compose(const AffineTransform& parent, const AffineTransform& child) {
    AffineTransform result;
    for (int row = 0; row < 3; ++row) {
        for (int column = 0; column < 3; ++column) {
            result.matrix[row][column] = 0.0;
            for (int inner = 0; inner < 3; ++inner)
                result.matrix[row][column] += parent.matrix[row][inner] * child.matrix[inner][column];
        }
    }
    result.translation = parent.point(child.translation);
    return result;
}

AffineTransform insertTransform(const std::vector<Pair>& values, Vec3 blockBase,
                                int column = 0, int row = 0) {
    const double scaleX = number(values, 41, 1.0);
    const double scaleY = number(values, 42, 1.0);
    const double scaleZ = number(values, 43, 1.0);
    constexpr double radians = 3.14159265358979323846 / 180.0;
    const double angle = number(values, 50) * radians;
    const double cosine = std::cos(angle), sine = std::sin(angle);
    AffineTransform result;
    result.matrix = {{{cosine * scaleX, -sine * scaleY, 0.0},
                      {sine * scaleX, cosine * scaleY, 0.0},
                      {0.0, 0.0, scaleZ}}};
    const Vec3 insertion = point(values, 10, 20, 30);
    const double localX = static_cast<double>(column) * number(values, 44);
    const double localY = static_cast<double>(row) * number(values, 45);
    const Vec3 arrayOffset{cosine * localX - sine * localY,
                           sine * localX + cosine * localY, 0.0};
    result.translation = insertion + arrayOffset - result.vector(blockBase);
    return result;
}

EntityProperties inheritBlockProperties(EntityProperties properties, const EntityProperties& insert,
                                        const LayerMap& layers) {
    if (properties.layer == "0") properties.layer = insert.layer;
    const auto layerIt = layers.find(properties.layer);
    const EntityProperties* layer = layerIt == layers.end() ? nullptr : &layerIt->second;
    properties.visible = properties.visible && insert.visible && (!layer || layer->visible);
    if (properties.trueColor) properties.effectiveColor = *properties.trueColor;
    else if (properties.colorIndex == 0) properties.effectiveColor = insert.effectiveColor;
    else if (properties.colorIndex > 0 && properties.colorIndex < 256)
        properties.effectiveColor = aciColor(properties.colorIndex);
    else properties.effectiveColor = layer ? layer->effectiveColor : insert.effectiveColor;
    properties.effectiveLineWeight = properties.lineWeight >= 0 ? properties.lineWeight
        : properties.lineWeight == -2 ? insert.effectiveLineWeight
        : layer ? layer->effectiveLineWeight : insert.effectiveLineWeight;
    properties.effectiveLineType = properties.lineType == "BYBLOCK" ? insert.effectiveLineType
        : properties.lineType == "BYLAYER" ? (layer ? layer->effectiveLineType : insert.effectiveLineType)
        : properties.lineType;
    return properties;
}

WireframeModel transformedModel(const WireframeModel& source, const AffineTransform& transform,
                                EntityProperties properties) {
    WireframeModel result;
    if (source.isPointEntity() && !source.vertices().empty()) {
        result = WireframeModel::point(transform.point(source.vertices().front()));
    } else if (source.isFace3D() && source.vertices().size() == 4) {
        result = WireframeModel::face3D({transform.point(source.vertices()[0]),
                                         transform.point(source.vertices()[1]),
                                         transform.point(source.vertices()[2]),
                                         transform.point(source.vertices()[3])});
    } else {
        std::vector<Vec3> vertices;
        vertices.reserve(source.vertices().size());
        for (const auto& vertex : source.vertices())
            vertices.push_back(transform.point(vertex));
        result = WireframeModel(std::move(vertices), source.edges(), source.faces());
    }
    result.setProperties(std::move(properties));
    return result;
}

void expandInsert(Document& document, const std::vector<Pair>& sourceFields, bool dimension,
                  const BlockMap& blocks, const LayerMap& layers,
                  const AffineTransform& parentTransform, const EntityProperties& parentInsert,
                  std::unordered_set<std::string>& stack, std::size_t& expandedCount, int depth) {
    if (depth > 32) throw std::runtime_error("DXF nested INSERT depth exceeded");
    const std::string name = blockKey(text(sourceFields, 2));
    const auto found = blocks.find(name);
    if (found == blocks.end())
        throw std::runtime_error("DXF INSERT references undefined block: " + name);
    std::vector<Pair> dimensionFields;
    const std::vector<Pair>* insertFields = &sourceFields;
    if (dimension) {
        dimensionFields.push_back({2, text(sourceFields, 2)});
        for (const auto& field : sourceFields) {
            if (field.code == 8 || field.code == 6 || field.code == 62 || field.code == 420 ||
                field.code == 370 || field.code == 39 || field.code == 48 || field.code == 60 || field.code == 440)
                dimensionFields.push_back(field);
        }
        dimensionFields.push_back({10, std::to_string(found->second.base.x)});
        dimensionFields.push_back({20, std::to_string(found->second.base.y)});
        dimensionFields.push_back({30, std::to_string(found->second.base.z)});
        insertFields = &dimensionFields;
    }
    if (!stack.insert(name).second) throw std::runtime_error("Cyclic DXF block reference");
    const auto& block = found->second;
    EntityProperties insertProperties = inheritBlockProperties(
        readEntityProperties(*insertFields, layers), parentInsert, layers);
    const int columns = std::max(1, integer(*insertFields, 70, 1));
    const int rows = std::max(1, integer(*insertFields, 71, 1));
    if (columns > 10'000 || rows > 10'000 || static_cast<std::uint64_t>(columns) * rows > 2'000'000)
        throw std::runtime_error("DXF INSERT array limit exceeded");
    for (int row = 0; row < rows; ++row) {
        for (int column = 0; column < columns; ++column) {
            const auto worldTransform = compose(parentTransform,
                insertTransform(*insertFields, block.base, column, row));
            for (const auto& model : block.models) {
                if (++expandedCount > 2'000'000) throw std::runtime_error("DXF entity limit exceeded");
                document.addModel(transformedModel(model, worldTransform,
                    inheritBlockProperties(model.properties(), insertProperties, layers)));
            }
            for (const auto& nested : block.inserts)
                expandInsert(document, nested.fields, nested.dimension, blocks, layers, worldTransform,
                             insertProperties, stack, expandedCount, depth + 1);
        }
    }
    stack.erase(name);
}

void appendBlockEntity(BlockDefinition& block, const std::string& type, PairReader& reader,
                       const std::vector<Pair>& entityFields, const LayerMap& layers) {
    if (type == "INSERT" || type == "DIMENSION") {
        block.inserts.push_back({entityFields, type == "DIMENSION"});
        return;
    }
    Document temporary;
    if (type == "POLYLINE") readLegacyPolyline(temporary, reader, entityFields, layers);
    else addSimpleEntity(temporary, type, entityFields, layers);
    for (const auto& model : temporary.models()) block.models.push_back(model);
}

} // anonymous namespace

Document DxfFile::read(const std::filesystem::path& path) {
    return read(path, std::stop_token{}, nullptr);
}

Document DxfFile::read(const std::filesystem::path& path, std::stop_token stopToken,
                       ProgressCallback progress) {
    if (stopToken.stop_requested()) throw DxfImportCancelled();
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("Could not open DXF file");
    input.seekg(0, std::ios::end);
    const std::uint64_t totalBytes = static_cast<std::uint64_t>(input.tellg());
    input.seekg(0, std::ios::beg);
    if (totalBytes == 0) throw std::runtime_error("DXF file is empty");

    PairReader reader(input, stopToken, totalBytes, std::move(progress));
    Document document;
    document.reserveModels(4096);
    std::string section;
    LayerMap layers;
    BlockMap blocks;
    std::string activeBlock;
    EntityProperties defaultLayer;
    defaultLayer.layer = "0";
    defaultLayer.lineType = defaultLayer.effectiveLineType = "CONTINUOUS";
    defaultLayer.colorIndex = 7;
    defaultLayer.effectiveColor = aciColor(7);
    layers.emplace("0", defaultLayer);
    document.setLayerProperties(defaultLayer);
    std::size_t entityCount{};
    while (const auto pair = reader.next()) {
        if (pair->code != 0) continue;
        if (pair->value == "SECTION") {
            const auto name = reader.next();
            section = name && name->code == 2 ? name->value : std::string{};
            continue;
        }
        if (pair->value == "ENDSEC") { section.clear(); activeBlock.clear(); continue; }
        if (pair->value == "EOF") break;
        if (section == "TABLES" && pair->value == "LAYER") {
            auto layer = readLayerProperties(fields(reader));
            document.setLayerProperties(layer);
            layers[layer.layer] = std::move(layer);
            continue;
        }
        if (section == "BLOCKS") {
            if (pair->value == "BLOCK") {
                const auto header = fields(reader);
                activeBlock = blockKey(text(header, 2, text(header, 3)));
                if (!activeBlock.empty()) {
                    auto& block = blocks[activeBlock];
                    block = BlockDefinition{};
                    block.base = point(header, 10, 20, 30);
                }
            } else if (pair->value == "ENDBLK") {
                (void)fields(reader);
                activeBlock.clear();
            } else if (!activeBlock.empty()) {
                const auto entityFields = fields(reader);
                appendBlockEntity(blocks.at(activeBlock), pair->value, reader, entityFields, layers);
            }
            continue;
        }
        if (section != "ENTITIES") continue;
        if (++entityCount > 2'000'000) throw std::runtime_error("DXF entity limit exceeded");
        const auto entityFields = fields(reader);
        if (pair->value == "INSERT" || pair->value == "DIMENSION") {
            std::unordered_set<std::string> stack;
            std::size_t expandedCount = document.models().size();
            const auto root = layers.find("0");
            const EntityProperties rootProperties = root == layers.end() ? EntityProperties{} : root->second;
            expandInsert(document, entityFields, pair->value == "DIMENSION", blocks, layers,
                         AffineTransform{}, rootProperties,
                         stack, expandedCount, 0);
        } else if (pair->value == "POLYLINE") readLegacyPolyline(document, reader, entityFields, layers);
        else addSimpleEntity(document, pair->value, entityFields, layers);
    }
    return document;
}

void writePair(std::ostream& output, int code, const auto& value) {
    output << code << '\n' << value << '\n';
}

void writeProperties(std::ostream& output, const EntityProperties& properties) {
    writePair(output, 8, properties.layer.empty() ? "0" : properties.layer);
    if (!properties.lineType.empty()) writePair(output, 6, properties.lineType);
    writePair(output, 62, properties.colorIndex);
    if (properties.trueColor) writePair(output, 420, *properties.trueColor);
    writePair(output, 370, properties.lineWeight);
    if (std::abs(properties.thickness) > 1e-12) writePair(output, 39, properties.thickness);
    if (std::abs(properties.lineTypeScale - 1.0) > 1e-12) writePair(output, 48, properties.lineTypeScale);
    if (properties.transparency) writePair(output, 440, properties.transparency);
    if (!properties.visible) writePair(output, 60, 1);
}

void writeLine(std::ostream& output, Vec3 a, Vec3 b, const EntityProperties& properties) {
    writePair(output, 0, "LINE"); writeProperties(output, properties);
    writePair(output, 10, a.x); writePair(output, 20, a.y); writePair(output, 30, a.z);
    writePair(output, 11, b.x); writePair(output, 21, b.y); writePair(output, 31, b.z);
}

bool isPlanarChain(const WireframeModel& model, bool& closed) {
    const auto& vertices = model.vertices();
    const auto& edges = model.edges();
    if (vertices.size() < 2 || (edges.size() != vertices.size() - 1 && edges.size() != vertices.size())) return false;
    for (std::size_t i = 0; i + 1 < vertices.size(); ++i)
        if (edges[i] != Edge{i, i + 1}) return false;
    closed = edges.size() == vertices.size();
    if (closed && edges.back() != Edge{vertices.size() - 1, 0}) return false;
    double z = vertices.front().z;
    for (const auto& vertex : vertices)
        if (std::abs(vertex.z - z) > 1e-9) return false;
    return true;
}

void DxfFile::write(const Document& document, const std::filesystem::path& path) {
    std::ofstream output(path, std::ios::binary);
    if (!output) throw std::runtime_error("Could not open DXF file for writing");
    output << std::setprecision(17);
    writePair(output, 0, "SECTION"); writePair(output, 2, "HEADER");
    writePair(output, 9, "$ACADVER"); writePair(output, 1, "AC1015"); writePair(output, 0, "ENDSEC");

    LayerMap exportLayers = document.layers();
    for (const auto& model : document.models()) {
        const auto& entity = model.properties();
        if (exportLayers.contains(entity.layer)) continue;
        EntityProperties layer;
        layer.layer = entity.layer;
        layer.lineType = layer.effectiveLineType = entity.effectiveLineType;
        layer.trueColor = layer.effectiveColor = entity.effectiveColor;
        layer.lineWeight = layer.effectiveLineWeight = entity.effectiveLineWeight;
        exportLayers.emplace(layer.layer, std::move(layer));
    }
    writePair(output, 0, "SECTION"); writePair(output, 2, "TABLES");
    writePair(output, 0, "TABLE"); writePair(output, 2, "LAYER");
    writePair(output, 70, exportLayers.size());
    for (const auto& [name, layer] : exportLayers) {
        writePair(output, 0, "LAYER"); writePair(output, 2, name);
        writePair(output, 70, layer.visible ? 0 : 1);
        writePair(output, 62, layer.colorIndex > 0 && layer.colorIndex < 256 ? layer.colorIndex : 7);
        if (layer.trueColor) writePair(output, 420, *layer.trueColor);
        writePair(output, 6, layer.effectiveLineType.empty() ? "CONTINUOUS" : layer.effectiveLineType);
        writePair(output, 370, layer.effectiveLineWeight);
    }
    writePair(output, 0, "ENDTAB"); writePair(output, 0, "ENDSEC");

    writePair(output, 0, "SECTION"); writePair(output, 2, "ENTITIES");

    for (const auto& model : document.models()) {
        if (model.isFace3D() && model.vertices().size() == 4) {
            writePair(output, 0, "3DFACE"); writeProperties(output, model.properties());
            for (std::size_t index = 0; index < 4; ++index) {
                const auto& vertex = model.vertices()[index];
                writePair(output, static_cast<int>(10 + index), vertex.x);
                writePair(output, static_cast<int>(20 + index), vertex.y);
                writePair(output, static_cast<int>(30 + index), vertex.z);
            }
            continue;
        }
        if (model.isPointEntity() && !model.vertices().empty()) {
            const auto point = model.vertices().front();
            writePair(output, 0, "POINT"); writeProperties(output, model.properties());
            writePair(output, 10, point.x); writePair(output, 20, point.y); writePair(output, 30, point.z);
            continue;
        }
        if (model.analyticCenter() && model.analyticRadius()) {
            const Vec3 center = *model.analyticCenter();
            const bool xyCircle = std::all_of(model.vertices().begin(), model.vertices().end(),
                [&](const Vec3& vertex) { return std::abs(vertex.z - center.z) <= 1e-9; });
            if (xyCircle) {
                writePair(output, 0, "CIRCLE"); writeProperties(output, model.properties());
                writePair(output, 10, center.x); writePair(output, 20, center.y); writePair(output, 30, center.z);
                writePair(output, 40, *model.analyticRadius());
                continue;
            }
        }
        bool closed{};
        if (isPlanarChain(model, closed)) {
            writePair(output, 0, "LWPOLYLINE"); writeProperties(output, model.properties());
            writePair(output, 90, model.vertices().size()); writePair(output, 70, closed ? 1 : 0);
            writePair(output, 38, model.vertices().front().z);
            for (const auto& vertex : model.vertices()) {
                writePair(output, 10, vertex.x); writePair(output, 20, vertex.y);
            }
            continue;
        }
        for (const auto& edge : model.edges())
            writeLine(output, model.vertices()[edge.from], model.vertices()[edge.to], model.properties());
    }
    writePair(output, 0, "ENDSEC"); writePair(output, 0, "EOF");
    if (!output) throw std::runtime_error("Could not write DXF file");
}

} // namespace mm
