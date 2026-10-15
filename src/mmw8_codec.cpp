#include "model_maker/mmw8_codec.hpp"

#include "model_maker/sha256.hpp"

#include <cctype>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace mm {

namespace {

[[noreturn]] void fail(const std::string& msg) {
    throw std::runtime_error("MMW8: " + msg);
}

// --- uzunluk-oncelikli string -------------------------------------------------
void putString(std::ostream& out, const std::string& s) {
    out << s.size() << ':';
    out.write(s.data(), static_cast<std::streamsize>(s.size()));
}

std::string readString(std::istream& in) {
    std::size_t len = 0;
    char colon = '\0';
    if (!(in >> len >> colon) || colon != ':')
        fail("invalid length-prefixed string");
    if (len > kMmw8MaxStringBytes)
        fail("string length exceeds limit");
    std::string s(len, '\0');
    if (len) {
        in.read(s.data(), static_cast<std::streamsize>(len));
        if (in.gcount() != static_cast<std::streamsize>(len))
            fail("truncated string");
    }
    return s;
}

// --- tam sayi ayristirma (tasma/negatif reddi) --------------------------------
std::string readToken(std::istream& in) {
    std::string tok;
    if (!(in >> tok)) fail("unexpected end of file");
    return tok;
}

std::uint64_t parseUint64(const std::string& tok, const char* what) {
    if (tok.empty() || tok[0] == '-' || tok[0] == '+')
        fail(std::string("invalid (signed/empty) ") + what);
    std::uint64_t v = 0;
    for (char c : tok) {
        if (c < '0' || c > '9') fail(std::string("invalid ") + what);
        const std::uint64_t digit = static_cast<std::uint64_t>(c - '0');
        if (v > (std::numeric_limits<std::uint64_t>::max() - digit) / 10)
            fail(std::string("integer overflow in ") + what);
        v = v * 10 + digit;
    }
    return v;
}

std::int64_t parseInt64(const std::string& tok, const char* what) {
    if (tok.empty()) fail(std::string("invalid (empty) ") + what);
    bool neg = false;
    std::size_t i = 0;
    if (tok[0] == '-') { neg = true; i = 1; }
    else if (tok[0] == '+') { i = 1; }
    if (i == tok.size()) fail(std::string("invalid ") + what);
    std::uint64_t mag = 0;
    for (; i < tok.size(); ++i) {
        char c = tok[i];
        if (c < '0' || c > '9') fail(std::string("invalid ") + what);
        const std::uint64_t digit = static_cast<std::uint64_t>(c - '0');
        if (mag > (std::numeric_limits<std::uint64_t>::max() - digit) / 10)
            fail(std::string("integer overflow in ") + what);
        mag = mag * 10 + digit;
    }
    constexpr std::uint64_t kInt64Max = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
    if (neg) {
        if (mag > kInt64Max + 1) fail(std::string("integer overflow in ") + what);
        return (mag == kInt64Max + 1) ? std::numeric_limits<std::int64_t>::min()
                                      : -static_cast<std::int64_t>(mag);
    }
    if (mag > kInt64Max) fail(std::string("integer overflow in ") + what);
    return static_cast<std::int64_t>(mag);
}

std::uint64_t readUint64(std::istream& in, const char* what) {
    return parseUint64(readToken(in), what);
}
std::int64_t readInt64(std::istream& in, const char* what) {
    return parseInt64(readToken(in), what);
}

// Bool 0/1 okuma.
bool readBool(std::istream& in, const char* what) {
    const std::uint64_t v = readUint64(in, what);
    if (v > 1) fail(std::string("invalid boolean in ") + what);
    return v == 1;
}

// Double okuma (sonlu kontrolu).
double readDouble(std::istream& in, const char* what) {
    double v = 0.0;
    if (!(in >> v)) fail(std::string("invalid double in ") + what);
    if (!std::isfinite(v)) fail(std::string("non-finite double in ") + what);
    return v;
}

// Tam bir yeni satir tuket (raw bayt oncesi).
void consumeNewline(std::istream& in) {
    char c = '\0';
    if (!in.get(c) || c != '\n') fail("expected newline before raw bytes");
}

// Bir modeli isaret bayraklarina gore yeniden kurar (Document::load ile ayni).
WireframeModel buildModel(std::vector<Vec3> vertices, std::vector<Edge> edges,
                          std::vector<Face> faces, bool pointEntity, bool face3D) {
    if (pointEntity && vertices.size() == 1)
        return WireframeModel::point(vertices.front());
    if (face3D && vertices.size() == 4)
        return WireframeModel::face3D({vertices[0], vertices[1], vertices[2], vertices[3]});
    return WireframeModel(std::move(vertices), std::move(edges), std::move(faces));
}

// Bir blob blogunun (B veya V) govdesini ayrir ve `out`a ekler. SHA-256 dogrulur,
// entityId'nin model listesinde olmasini ve yinelenmemesini sart kosar.
void readBlobEntries(std::istream& bs, std::vector<Mmw8Blob>& out,
                     const std::unordered_map<EntityId, bool>& seenIds,
                     std::size_t& totalBlobBytes) {
    const std::uint64_t blobCount = readUint64(bs, "blobCount");
    if (blobCount > kMmw8MaxModels) fail("blobCount exceeds limit");
    for (std::uint64_t bi = 0; bi < blobCount; ++bi) {
        const std::uint64_t eid = readUint64(bs, "blob entityId");
        const std::uint64_t blobLen = readUint64(bs, "blob length");
        const std::string sha = readToken(bs);
        consumeNewline(bs);
        if (eid == 0 || eid == kExhaustedEntityId)
            fail("blob entityId must be a valid identity");
        if (blobLen > kMmw8MaxBlobSize) fail("blob exceeds size limit");
        if (sha.size() != 64) fail("invalid SHA-256 hex length");
        totalBlobBytes += static_cast<std::size_t>(blobLen);
        if (totalBlobBytes > kMmw8MaxTotalBlobBytes) fail("total blob bytes exceed limit");
        Mmw8Blob blob;
        blob.entityId = static_cast<EntityId>(eid);
        blob.bytes.resize(static_cast<std::size_t>(blobLen));
        if (blobLen) {
            bs.read(blob.bytes.data(), static_cast<std::streamsize>(blobLen));
            if (bs.gcount() != static_cast<std::streamsize>(blobLen))
                fail("truncated blob");
        }
        blob.sha256 = sha;
        if (sha256Hex(blob.bytes) != sha) fail("SHA-256 mismatch");
        if (!seenIds.count(blob.entityId)) fail("blob entityId not in model list");
        for (const auto& existing : out)
            if (existing.entityId == blob.entityId) fail("duplicate blob entityId");
        out.push_back(std::move(blob));
    }
}

} // namespace

void writeMmw8(std::ostream& out, const Mmw8Document& doc) {
    if (doc.models.size() != doc.entityIds.size())
        fail("models/entityIds size mismatch");
    out << std::setprecision(17);

    // Baslik.
    out << "MMW8 " << doc.models.size() << ' ' << doc.nextEntityId << '\n';

    // L blogu (katmanlar).
    out << "L " << doc.layers.size() << '\n';
    for (const auto& [name, layer] : doc.layers) {
        putString(out, name);
        out << ' ' << layer.colorIndex << ' ';
        if (layer.trueColor) out << "1 " << *layer.trueColor << ' ';
        else out << "0 0 ";
        out << (layer.visible ? 1 : 0) << ' ' << (layer.frozen ? 1 : 0) << ' '
            << (layer.locked ? 1 : 0) << '\n';
    }

    // Model kayitlari.
    for (std::size_t i = 0; i < doc.models.size(); ++i) {
        const auto& model = doc.models[i];
        const EntityId eid = doc.entityIds[i];
        if (eid == kInvalidEntityId || eid == kExhaustedEntityId)
            fail("entityId must be a valid identity (0 and UINT64_MAX are reserved)");
        out << model.vertices().size() << ' ' << model.edges().size() << ' '
            << model.faces().size() << ' ' << (model.isPointEntity() ? 1 : 0) << ' '
            << (model.isFace3D() ? 1 : 0) << ' ' << eid << '\n';
        for (const auto& v : model.vertices()) out << v.x << ' ' << v.y << ' ' << v.z << '\n';
        for (const auto& e : model.edges()) out << e.from << ' ' << e.to << '\n';
        for (const auto& f : model.faces()) {
            out << f.size();
            for (const std::size_t idx : f) out << ' ' << idx;
            out << '\n';
        }
        const auto& p = model.properties();
        out << "P ";
        putString(out, p.layer); out << ' ';
        putString(out, p.profileName); out << ' ';
        out << p.profileRotation << ' ' << p.profileSourceLine << ' '
            << p.axisFromX << ' ' << p.axisFromY << ' ' << p.axisFromZ << ' '
            << p.axisToX << ' ' << p.axisToY << ' ' << p.axisToZ << ' '
            << p.sectionNormalX << ' ' << p.sectionNormalY << ' ' << p.sectionNormalZ << ' ';
        putString(out, p.lineType); out << ' ';
        putString(out, p.material); out << ' ';
        out << p.colorIndex << ' ';
        if (p.trueColor) out << "1 " << *p.trueColor << ' ';
        else out << "0 0 ";
        out << p.lineWeight << ' ' << p.thickness << ' ' << p.lineTypeScale << ' '
            << p.transparency << ' ' << (p.visible ? 1 : 0) << ' ' << (p.frozen ? 1 : 0) << ' '
            << (p.locked ? 1 : 0) << ' ' << (p.plottable ? 1 : 0) << ' ';
        putString(out, p.description);
        out << '\n';
    }

    // G blogu (gridler).
    out << "G " << doc.grids.size() << '\n';
    for (const auto& grid : doc.grids) {
        putString(out, grid.name);
        out << ' ' << (grid.visible ? 1 : 0) << ' ' << (grid.magnetic ? 1 : 0) << ' '
            << static_cast<int>(grid.kind) << ' '
            << grid.origin.x << ' ' << grid.origin.y << ' ' << grid.origin.z << ' '
            << grid.extendLeft << ' ' << grid.extendRight << ' '
            << grid.extendBelow << ' ' << grid.extendAbove << ' '
            << grid.color << ' ' << grid.labelColor << ' ' << grid.labelFontSize << '\n';
        const auto numRow = [&out](const std::vector<double>& v) {
            out << v.size();
            for (const double d : v) out << ' ' << d;
            out << '\n';
        };
        const auto labelRow = [&out](const std::vector<std::string>& v) {
            out << v.size();
            for (const auto& s : v) { out << ' '; putString(out, s); }
            out << '\n';
        };
        numRow(grid.xCoords); numRow(grid.yCoords); numRow(grid.zCoords);
        labelRow(grid.xLabels); labelRow(grid.yLabels); labelRow(grid.zLabels);
    }

    // Kuyruk: H blogu (metadata, ZORUNLU, ilk).
    {
        std::ostringstream body;
        body << "F " << doc.metadata.binToolsFormatVersion << " O ";
        body << doc.metadata.occtVersion.size() << ':' << doc.metadata.occtVersion;
        const std::string b = body.str();
        out << "H " << b.size() << '\n';
        out.write(b.data(), static_cast<std::streamsize>(b.size()));
    }

    // Kuyruk: B blogu (B-Rep blob'lari).
    if (!doc.blobs.empty()) {
        std::ostringstream body;
        body << doc.blobs.size() << '\n';
        for (const auto& blob : doc.blobs) {
            if (blob.entityId == kInvalidEntityId || blob.entityId == kExhaustedEntityId)
                fail("blob entityId must be a valid identity");
            body << blob.entityId << ' ' << blob.bytes.size() << ' '
                 << sha256Hex(blob.bytes) << '\n';
            body.write(blob.bytes.data(), static_cast<std::streamsize>(blob.bytes.size()));
        }
        const std::string b = body.str();
        out << "B " << b.size() << '\n';
        out.write(b.data(), static_cast<std::streamsize>(b.size()));
    }

    // Kuyruk: C blogu (kesme modifier'lari, Phase 4.2).
    if (!doc.cutModifiers.empty()) {
        std::ostringstream body;
        body << doc.cutModifiers.size() << '\n';
        for (const auto& mod : doc.cutModifiers) {
            body << static_cast<int>(mod.kind) << ' ' << mod.target << ' '
                 << mod.planeOrigin.x << ' ' << mod.planeOrigin.y << ' ' << mod.planeOrigin.z << ' '
                 << mod.planeNormal.x << ' ' << mod.planeNormal.y << ' ' << mod.planeNormal.z << ' '
                 << (mod.keepPositive ? 1 : 0) << ' ' << mod.tool << ' '
                 << (mod.enabled ? 1 : 0) << ' ';
            putString(body, mod.name);
            body << ' ' << mod.polygon.size();
            for (const auto& p : mod.polygon) body << ' ' << p.x << ' ' << p.y << ' ' << p.z;
            body << '\n';
        }
        const std::string b = body.str();
        out << "C " << b.size() << '\n';
        out.write(b.data(), static_cast<std::streamsize>(b.size()));
    }

    // Kuyruk: K blogu (konstruksiyon turleri, Phase 4.1).
    if (!doc.constructionKinds.empty()) {
        std::ostringstream body;
        body << doc.constructionKinds.size() << '\n';
        for (const auto& ck : doc.constructionKinds)
            body << ck.entityId << ' ' << static_cast<int>(ck.kind) << '\n';
        const std::string b = body.str();
        out << "K " << b.size() << '\n';
        out.write(b.data(), static_cast<std::streamsize>(b.size()));
    }

    // Kuyruk: V blogu (taban B-Rep blob'lari, Phase 4.2).
    if (!doc.baseBlobs.empty()) {
        std::ostringstream body;
        body << doc.baseBlobs.size() << '\n';
        for (const auto& blob : doc.baseBlobs) {
            if (blob.entityId == kInvalidEntityId || blob.entityId == kExhaustedEntityId)
                fail("base blob entityId must be a valid identity");
            body << blob.entityId << ' ' << blob.bytes.size() << ' '
                 << sha256Hex(blob.bytes) << '\n';
            body.write(blob.bytes.data(), static_cast<std::streamsize>(blob.bytes.size()));
        }
        const std::string b = body.str();
        out << "V " << b.size() << '\n';
        out.write(b.data(), static_cast<std::streamsize>(b.size()));
    }

    // Kuyruk: bilinmeyen bloklar (kayipsiz korunur).
    for (const auto& ub : doc.unknownBlocks) {
        out << ub.tag << ' ' << ub.body.size() << '\n';
        out.write(ub.body.data(), static_cast<std::streamsize>(ub.body.size()));
    }
    out.flush();
}

void readMmw8(std::istream& in, Mmw8Document& doc) {
    Mmw8Document parsed;

    // --- Baslik ---
    std::string signature;
    if (!(in >> signature)) fail("short file (missing header)");
    if (signature != "MMW8") fail("not an MMW8 file");
    const std::uint64_t modelCount = readUint64(in, "modelCount");
    const std::uint64_t nextEntityId = readUint64(in, "nextEntityId");
    if (modelCount > kMmw8MaxModels) fail("modelCount exceeds limit");
    if (nextEntityId == 0) fail("nextEntityId must be >= 1");
    parsed.nextEntityId = static_cast<EntityId>(nextEntityId);

    // --- L blogu ---
    {
        std::string tag;
        if (!(in >> tag) || tag != "L") fail("missing layer block (L)");
        const std::uint64_t layerCount = readUint64(in, "layerCount");
        if (layerCount > kMmw8MaxLayers) fail("layerCount exceeds limit");
        for (std::uint64_t li = 0; li < layerCount; ++li) {
            std::string name = readString(in);
            EntityProperties layer;
            layer.colorIndex = static_cast<int>(readInt64(in, "layer colorIndex"));
            const bool hasTC = readBool(in, "layer hasTrueColor");
            const std::uint64_t tcValue = readUint64(in, "layer trueColor");
            layer.trueColor = hasTC ? std::optional<std::uint32_t>(static_cast<std::uint32_t>(tcValue))
                                    : std::nullopt;
            layer.visible = readBool(in, "layer visible");
            layer.frozen = readBool(in, "layer frozen");
            layer.locked = readBool(in, "layer locked");
            if (layer.trueColor) layer.effectiveColor = *layer.trueColor;
            else layer.effectiveColor = static_cast<std::uint32_t>(layer.colorIndex);
            if (name.empty()) fail("empty layer name");
            if (parsed.layers.count(name)) fail("duplicate layer name");
            parsed.layers.emplace(std::move(name), std::move(layer));
        }
    }

    // --- Model kayitlari ---
    parsed.models.reserve(modelCount);
    parsed.entityIds.reserve(modelCount);
    std::unordered_map<EntityId, bool> seenIds;
    for (std::uint64_t mi = 0; mi < modelCount; ++mi) {
        const std::uint64_t vCount = readUint64(in, "vertexCount");
        const std::uint64_t eCount = readUint64(in, "edgeCount");
        const std::uint64_t fCount = readUint64(in, "faceCount");
        const bool pointEntity = readBool(in, "isPointEntity");
        const bool face3D = readBool(in, "isFace3D");
        const std::uint64_t eid = readUint64(in, "entityId");
        if (vCount > kMmw8MaxVertices || eCount > kMmw8MaxEdges || fCount > kMmw8MaxFaces)
            fail("model geometry exceeds limits");
        if (eid == 0 || eid == kExhaustedEntityId)
            fail("entityId must be a valid identity in model record");
        if (seenIds.count(static_cast<EntityId>(eid))) fail("duplicate entityId");
        seenIds[static_cast<EntityId>(eid)] = true;

        std::vector<Vec3> vertices(vCount);
        for (auto& v : vertices) {
            v.x = readDouble(in, "vertex x");
            v.y = readDouble(in, "vertex y");
            v.z = readDouble(in, "vertex z");
        }
        std::vector<Edge> edges(eCount);
        for (auto& e : edges) {
            e.from = static_cast<std::size_t>(readUint64(in, "edge from"));
            e.to = static_cast<std::size_t>(readUint64(in, "edge to"));
            if (e.from >= vCount || e.to >= vCount) fail("edge index out of range");
        }
        std::vector<Face> faces;
        faces.reserve(fCount);
        for (std::uint64_t fi = 0; fi < fCount; ++fi) {
            const std::uint64_t cornerCount = readUint64(in, "face size");
            if (cornerCount < 3 || cornerCount > vCount) fail("invalid face size");
            Face face(cornerCount);
            for (auto& idx : face) {
                const std::uint64_t iv = readUint64(in, "face index");
                if (iv >= vCount) fail("face index out of range");
                idx = static_cast<std::size_t>(iv);
            }
            faces.push_back(std::move(face));
        }

        // P blogu (ozellikler).
        std::string tag;
        if (!(in >> tag) || tag != "P") fail("missing property block (P)");
        EntityProperties props;
        props.layer = readString(in);
        props.profileName = readString(in);
        props.profileRotation = readDouble(in, "profileRotation");
        props.profileSourceLine = readInt64(in, "profileSourceLine");
        props.axisFromX = readDouble(in, "axisFromX"); props.axisFromY = readDouble(in, "axisFromY");
        props.axisFromZ = readDouble(in, "axisFromZ");
        props.axisToX = readDouble(in, "axisToX"); props.axisToY = readDouble(in, "axisToY");
        props.axisToZ = readDouble(in, "axisToZ");
        props.sectionNormalX = readDouble(in, "sectionNormalX");
        props.sectionNormalY = readDouble(in, "sectionNormalY");
        props.sectionNormalZ = readDouble(in, "sectionNormalZ");
        props.lineType = readString(in);
        props.material = readString(in);
        props.colorIndex = static_cast<int>(readInt64(in, "colorIndex"));
        const bool hasTC = readBool(in, "hasTrueColor");
        const std::uint64_t tcValue = readUint64(in, "trueColor");
        props.trueColor = hasTC ? std::optional<std::uint32_t>(static_cast<std::uint32_t>(tcValue))
                                : std::nullopt;
        props.lineWeight = static_cast<int>(readInt64(in, "lineWeight"));
        props.thickness = readDouble(in, "thickness");
        props.lineTypeScale = readDouble(in, "lineTypeScale");
        props.transparency = static_cast<int>(readInt64(in, "transparency"));
        props.visible = readBool(in, "visible");
        props.frozen = readBool(in, "frozen");
        props.locked = readBool(in, "locked");
        props.plottable = readBool(in, "plottable");
        props.description = readString(in);
        if (props.trueColor) props.effectiveColor = *props.trueColor;

        WireframeModel built = buildModel(std::move(vertices), std::move(edges),
                                          std::move(faces), pointEntity, face3D);
        built.setProperties(std::move(props));
        parsed.models.push_back(std::move(built));
        parsed.entityIds.push_back(static_cast<EntityId>(eid));
    }

    // --- G blogu ---
    {
        std::string tag;
        if (!(in >> tag) || tag != "G") fail("missing grid block (G)");
        const std::uint64_t gridCount = readUint64(in, "gridCount");
        if (gridCount > kMmw8MaxGrids) fail("gridCount exceeds limit");
        for (std::uint64_t gi = 0; gi < gridCount; ++gi) {
            GridDefinition grid;
            grid.name = readString(in);
            grid.visible = readBool(in, "grid visible");
            grid.magnetic = readBool(in, "grid magnetic");
            grid.kind = static_cast<GridKind>(readInt64(in, "grid kind"));
            grid.origin.x = readDouble(in, "origin x");
            grid.origin.y = readDouble(in, "origin y");
            grid.origin.z = readDouble(in, "origin z");
            grid.extendLeft = readDouble(in, "extendLeft");
            grid.extendRight = readDouble(in, "extendRight");
            grid.extendBelow = readDouble(in, "extendBelow");
            grid.extendAbove = readDouble(in, "extendAbove");
            grid.color = static_cast<unsigned int>(readUint64(in, "grid color"));
            grid.labelColor = static_cast<unsigned int>(readUint64(in, "grid labelColor"));
            grid.labelFontSize = static_cast<int>(readInt64(in, "labelFontSize"));
            const auto readNumRow = [&in](std::vector<double>& out) {
                const std::uint64_t n = readUint64(in, "grid coord row");
                if (n > 4096) fail("grid coord row too long");
                out.resize(n);
                for (auto& d : out) d = readDouble(in, "grid coord");
            };
            const auto readLabelRow = [&in](std::vector<std::string>& out) {
                const std::uint64_t n = readUint64(in, "grid label row");
                if (n > 4096) fail("grid label row too long");
                out.resize(n);
                for (auto& s : out) s = readString(in);
            };
            readNumRow(grid.xCoords); readNumRow(grid.yCoords); readNumRow(grid.zCoords);
            readLabelRow(grid.xLabels); readLabelRow(grid.yLabels); readLabelRow(grid.zLabels);
            parsed.grids.push_back(std::move(grid));
        }
    }

    // --- Kuyruk: etiket-gudumlu bloklar ---
    bool sawH = false, sawB = false, sawV = false;
    std::size_t totalBlobBytes = 0;
    for (;;) {
        in >> std::ws;
        const int peek = in.peek();
        if (peek == std::char_traits<char>::eof()) break;
        const char tag = static_cast<char>(in.get());
        const std::uint64_t bodyLen = readUint64(in, "block bodyLen");
        consumeNewline(in);
        std::string body(bodyLen, '\0');
        if (bodyLen) {
            in.read(body.data(), static_cast<std::streamsize>(bodyLen));
            if (in.gcount() != static_cast<std::streamsize>(bodyLen))
                fail("truncated block body");
        }
        if (tag == 'H') {
            if (sawH) fail("repeated H block");
            if (sawB) fail("H block after B block (wrong order)");
            sawH = true;
            std::istringstream hs(body);
            std::string ftok;
            if (!(hs >> ftok) || ftok != "F") fail("invalid H block");
            const std::int64_t fv = readInt64(hs, "binTools format version");
            if (fv < kMmw8SupportedBinToolsVersionMin || fv > kMmw8SupportedBinToolsVersionMax)
                fail("unsupported BinTools format version");
            std::string otok;
            if (!(hs >> otok) || otok != "O") fail("invalid H block (missing O)");
            parsed.metadata.binToolsFormatVersion = static_cast<int>(fv);
            parsed.metadata.occtVersion = readString(hs);
            // OCC surumu BOS olabilir (OCC-kapali derleme / wireframe-only);
            // bu GECERLI bir durumdur.
        } else if (tag == 'B') {
            if (sawB) fail("repeated B block");
            if (!sawH) fail("B block before H block (wrong order)");
            sawB = true;
            std::istringstream bs(body);
            readBlobEntries(bs, parsed.blobs, seenIds, totalBlobBytes);
        } else if (tag == 'V') {
            if (sawV) fail("repeated V block");
            if (!sawH) fail("V block before H block (wrong order)");
            sawV = true;
            std::istringstream vs(body);
            readBlobEntries(vs, parsed.baseBlobs, seenIds, totalBlobBytes);
        } else if (tag == 'C') {
            std::istringstream cs(body);
            const std::uint64_t count = readUint64(cs, "cutModifier count");
            if (count > kMmw8MaxCutModifiers) fail("cutModifier count exceeds limit");
            for (std::uint64_t ci = 0; ci < count; ++ci) {
                CutModifier mod;
                const std::int64_t kind = readInt64(cs, "cut kind");
                if (kind < 0 || kind > 4) fail("invalid cut kind");
                mod.kind = static_cast<CutKind>(kind);
                mod.target = static_cast<EntityId>(readUint64(cs, "cut target"));
                mod.planeOrigin.x = readDouble(cs, "cut origin x");
                mod.planeOrigin.y = readDouble(cs, "cut origin y");
                mod.planeOrigin.z = readDouble(cs, "cut origin z");
                mod.planeNormal.x = readDouble(cs, "cut normal x");
                mod.planeNormal.y = readDouble(cs, "cut normal y");
                mod.planeNormal.z = readDouble(cs, "cut normal z");
                mod.keepPositive = readBool(cs, "cut keepPositive");
                mod.tool = static_cast<EntityId>(readUint64(cs, "cut tool"));
                mod.enabled = readBool(cs, "cut enabled");
                mod.name = readString(cs);
                const std::uint64_t polyCount = readUint64(cs, "cut polygon count");
                if (polyCount > kMmw8MaxVertices) fail("cut polygon too long");
                mod.polygon.reserve(static_cast<std::size_t>(polyCount));
                for (std::uint64_t pi = 0; pi < polyCount; ++pi) {
                    Vec3 p;
                    p.x = readDouble(cs, "polygon x");
                    p.y = readDouble(cs, "polygon y");
                    p.z = readDouble(cs, "polygon z");
                    mod.polygon.push_back(p);
                }
                parsed.cutModifiers.push_back(std::move(mod));
            }
        } else if (tag == 'K') {
            std::istringstream ks(body);
            const std::uint64_t count = readUint64(ks, "constructionKind count");
            if (count > kMmw8MaxModels) fail("constructionKind count exceeds limit");
            std::unordered_map<EntityId, bool> seenCk;
            for (std::uint64_t ki = 0; ki < count; ++ki) {
                Mmw8ConstructionKind ck;
                ck.entityId = static_cast<EntityId>(readUint64(ks, "construction entityId"));
                const std::int64_t kind = readInt64(ks, "construction kind");
                if (kind < 0 || kind > 3) fail("invalid construction kind");
                ck.kind = static_cast<ConstructionKind>(kind);
                if (!seenIds.count(ck.entityId)) fail("construction entityId not in model list");
                if (seenCk.count(ck.entityId)) fail("duplicate construction entityId");
                seenCk[ck.entityId] = true;
                parsed.constructionKinds.push_back(ck);
            }
        } else {
            // Bilinmeyen blok: kayipsiz korunur.
            parsed.unknownBlocks.push_back(Mmw8UnknownBlock{tag, std::move(body)});
        }
    }
    if (!sawH) fail("missing mandatory H block");

    // nextEntityId, en buyuk entityId'den buyuk olmali.
    EntityId maxId = 0;
    for (const EntityId e : parsed.entityIds)
        if (e > maxId) maxId = e;
    if (parsed.nextEntityId <= maxId)
        fail("nextEntityId not greater than max entityId");

    doc = std::move(parsed);
}

} // namespace mm
