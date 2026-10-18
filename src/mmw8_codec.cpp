#include "model_maker/mmw8_codec.hpp"

#include "model_maker/sha256.hpp"

#include <cctype>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

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

// --- UTF-8 <-> wchar_t (kod noktasi) donusumu ---------------------------------
// Linux'ta wchar_t 32 bit (UTF-32), Windows'ta 16 bit (UTF-16). Component
// motoru verisi (typeId / parametre adi / deger) BMP/ASCII'dir; her wchar_t
// tek kod noktasi kabul edilir ve UTF-8 ile kayipsiz kodlanir.
std::string toUtf8(const std::wstring& ws) {
    std::string out;
    out.reserve(ws.size());
    for (const wchar_t wc : ws) {
        const std::uint32_t cp = static_cast<std::uint32_t>(wc);
        if (cp < 0x80u) {
            out.push_back(static_cast<char>(cp));
        } else if (cp < 0x800u) {
            out.push_back(static_cast<char>(0xC0u | (cp >> 6)));
            out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
        } else if (cp < 0x10000u) {
            out.push_back(static_cast<char>(0xE0u | (cp >> 12)));
            out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
            out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
        } else {
            out.push_back(static_cast<char>(0xF0u | (cp >> 18)));
            out.push_back(static_cast<char>(0x80u | ((cp >> 12) & 0x3Fu)));
            out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
            out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
        }
    }
    return out;
}

std::wstring fromUtf8(const std::string& s) {
    std::wstring out;
    out.reserve(s.size());
    std::size_t i = 0;
    while (i < s.size()) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        std::uint32_t cp = 0;
        std::size_t extra = 0;
        if (c < 0x80u) {
            cp = c;
        } else if ((c & 0xE0u) == 0xC0u) {
            cp = c & 0x1Fu;
            extra = 1;
        } else if ((c & 0xF0u) == 0xE0u) {
            cp = c & 0x0Fu;
            extra = 2;
        } else if ((c & 0xF8u) == 0xF0u) {
            cp = c & 0x07u;
            extra = 3;
        } else {
            fail("invalid UTF-8 leading byte");
        }
        if (i + extra >= s.size()) fail("truncated UTF-8 sequence");
        ++i;
        for (std::size_t k = 0; k < extra; ++k) {
            const unsigned char cc = static_cast<unsigned char>(s[i]);
            if ((cc & 0xC0u) != 0x80u) fail("invalid UTF-8 continuation byte");
            cp = (cp << 6) | (cc & 0x3Fu);
            ++i;
        }
        out.push_back(static_cast<wchar_t>(cp));
    }
    return out;
}

// --- Component kaydi (Phase C2) serilestirme ---------------------------------
// CompParam deger turu etiketi: 0=double, 1=int, 2=bool, 3=wstring(utf8),
// 4=EntityId. Kayit `dirty`/`orphan` TASIMAZ (calisma ani bayraklar; yuklemede
// yeniden turetilir).
void putCompParam(std::ostream& out, const CompParam& p) {
    putString(out, toUtf8(p.name));
    out << ' ';
    if (const auto* d = std::get_if<double>(&p.value)) {
        out << "0 " << *d;
    } else if (const auto* i = std::get_if<int>(&p.value)) {
        out << "1 " << *i;
    } else if (const auto* b = std::get_if<bool>(&p.value)) {
        out << "2 " << (*b ? 1 : 0);
    } else if (const auto* s = std::get_if<std::wstring>(&p.value)) {
        out << "3 ";
        putString(out, toUtf8(*s));
    } else {
        const auto* e = std::get_if<EntityId>(&p.value);
        out << "4 " << (e ? *e : static_cast<EntityId>(kInvalidEntityId));
    }
}

void writeComponentBody(std::ostream& body, const std::vector<ComponentInstanceRecord>& recs) {
    body << recs.size() << '\n';
    for (const auto& r : recs) {
        body << r.id << ' ' << r.schemaVersion << ' ' << r.mainPart << ' '
             << r.secondaryParts.size();
        for (const EntityId s : r.secondaryParts) body << ' ' << s;
        body << ' ' << r.params.size();
        for (const auto& p : r.params) { body << ' '; putCompParam(body, p); }
        body << ' ' << r.origin.x << ' ' << r.origin.y << ' ' << r.origin.z
             << ' ' << r.u.x << ' ' << r.u.y << ' ' << r.u.z
             << ' ' << r.v.x << ' ' << r.v.y << ' ' << r.v.z
             << ' ' << r.axis.x << ' ' << r.axis.y << ' ' << r.axis.z
             << ' ' << r.ownedPlateIds.size();
        for (const EntityId p : r.ownedPlateIds) body << ' ' << p;
        body << ' ' << r.ownedBoltGroupIds.size();
        for (const EntityId b : r.ownedBoltGroupIds) body << ' ' << b;
        body << ' ' << (r.enabled ? 1 : 0) << ' ';
        putString(body, toUtf8(r.typeId));
        body << '\n';
    }
}

void readComponentBody(std::istream& bs, std::vector<ComponentInstanceRecord>& out) {
    const std::uint64_t count = readUint64(bs, "component count");
    if (count > kMmw8MaxComponents) fail("component count exceeds limit");
    std::string line;
    std::getline(bs, line);  // count satırının kalanı (boş)
    std::unordered_map<EntityId, bool> seenComp;
    for (std::uint64_t ci = 0; ci < count; ++ci) {
        if (!std::getline(bs, line)) fail("missing component record");
        std::istringstream ls(line);
        ComponentInstanceRecord r;
        r.id = static_cast<EntityId>(readUint64(ls, "component id"));
        if (r.id == kInvalidEntityId || r.id == kExhaustedEntityId)
            fail("component id must be a valid identity");
        if (seenComp.count(r.id)) fail("duplicate component id");
        seenComp[r.id] = true;
        const std::int64_t schema = readInt64(ls, "component schemaVersion");
        if (schema < 1) fail("component schemaVersion must be >= 1");
        r.schemaVersion = static_cast<int>(schema);
        r.mainPart = static_cast<EntityId>(readUint64(ls, "component mainPart"));
        const std::uint64_t nSec = readUint64(ls, "component secondary count");
        if (nSec > kMmw8MaxModels) fail("component secondary count exceeds limit");
        r.secondaryParts.reserve(static_cast<std::size_t>(nSec));
        for (std::uint64_t si = 0; si < nSec; ++si)
            r.secondaryParts.push_back(static_cast<EntityId>(readUint64(ls, "component secondary")));
        const std::uint64_t nParams = readUint64(ls, "component param count");
        if (nParams > kMmw8MaxModels) fail("component param count exceeds limit");
        r.params.reserve(static_cast<std::size_t>(nParams));
        for (std::uint64_t pi = 0; pi < nParams; ++pi) {
            CompParam p;
            p.name = fromUtf8(readString(ls));
            const std::uint64_t tag = readUint64(ls, "component param type tag");
            switch (tag) {
                case 0: p.value = CompParamValue{readDouble(ls, "param double")}; break;
                case 1: p.value = CompParamValue{static_cast<int>(readInt64(ls, "param int"))}; break;
                case 2: p.value = CompParamValue{readBool(ls, "param bool")}; break;
                case 3: p.value = CompParamValue{fromUtf8(readString(ls))}; break;
                case 4: p.value = CompParamValue{static_cast<EntityId>(readUint64(ls, "param entity"))}; break;
                default: fail("invalid component param type tag");
            }
            r.params.push_back(std::move(p));
        }
        r.origin.x = readDouble(ls, "component origin x");
        r.origin.y = readDouble(ls, "component origin y");
        r.origin.z = readDouble(ls, "component origin z");
        r.u.x = readDouble(ls, "component u x");
        r.u.y = readDouble(ls, "component u y");
        r.u.z = readDouble(ls, "component u z");
        r.v.x = readDouble(ls, "component v x");
        r.v.y = readDouble(ls, "component v y");
        r.v.z = readDouble(ls, "component v z");
        r.axis.x = readDouble(ls, "component axis x");
        r.axis.y = readDouble(ls, "component axis y");
        r.axis.z = readDouble(ls, "component axis z");
        const std::uint64_t nPlates = readUint64(ls, "component ownedPlate count");
        if (nPlates > kMmw8MaxModels) fail("component ownedPlate count exceeds limit");
        r.ownedPlateIds.reserve(static_cast<std::size_t>(nPlates));
        for (std::uint64_t pi = 0; pi < nPlates; ++pi)
            r.ownedPlateIds.push_back(static_cast<EntityId>(readUint64(ls, "component plate id")));
        const std::uint64_t nBolts = readUint64(ls, "component ownedBolt count");
        if (nBolts > kMmw8MaxModels) fail("component ownedBolt count exceeds limit");
        r.ownedBoltGroupIds.reserve(static_cast<std::size_t>(nBolts));
        for (std::uint64_t bi = 0; bi < nBolts; ++bi)
            r.ownedBoltGroupIds.push_back(static_cast<EntityId>(readUint64(ls, "component bolt id")));
        r.enabled = readBool(ls, "component enabled");
        r.typeId = fromUtf8(readString(ls));
        r.dirty = true;    // yuklenen kayit yeniden hesaplama bekler
        r.orphan = false;  // Document yuklemede registry'ye gore yeniden kurar
        out.push_back(std::move(r));
    }
}

// --- Slice 2d-3: parametrik ref/kisit/cerceve serilestirme ------------------
// NOT: handle anahtari / selector / child id gibi TANIMLAYICI string'ler
// satir-icinde putString ile yazilir; bu yuzden yeni satir ('\n') ICERMEMELIDIR
// (mevcut MMW8 'I'/'E'/'G' bloklarindaki typeId/name/grade ile AYNI kisit).
// Bunlar kullanici serbest metni degil, program uretimli kimliklerdir.
void putVec3(std::ostream& out, const Vec3& v) {
    out << v.x << ' ' << v.y << ' ' << v.z;
}
Vec3 readVec3(std::istream& in, const char* what) {
    Vec3 v;
    v.x = readDouble(in, what);
    v.y = readDouble(in, what);
    v.z = readDouble(in, what);
    return v;
}

// MemberLocalFrame: origin + u/v/w (12 double).
void putMemberLocalFrame(std::ostream& out, const MemberLocalFrame& f) {
    putVec3(out, f.origin); out << ' ';
    putVec3(out, f.u); out << ' ';
    putVec3(out, f.v); out << ' ';
    putVec3(out, f.w);
}
MemberLocalFrame readMemberLocalFrame(std::istream& in, const char* what) {
    MemberLocalFrame f;
    f.origin = readVec3(in, what);
    f.u = readVec3(in, what);
    f.v = readVec3(in, what);
    f.w = readVec3(in, what);
    return f;
}

// LocalPlacement: origin + u/v/w (12 double).
void putLocalPlacement(std::ostream& out, const LocalPlacement& p) {
    putVec3(out, p.origin); out << ' ';
    putVec3(out, p.u); out << ' ';
    putVec3(out, p.v); out << ' ';
    putVec3(out, p.w);
}
LocalPlacement readLocalPlacement(std::istream& in, const char* what) {
    LocalPlacement p;
    p.origin = readVec3(in, what);
    p.u = readVec3(in, what);
    p.v = readVec3(in, what);
    p.w = readVec3(in, what);
    return p;
}

// FaceReference (selector dahil) — BIR satirda. resolved/plane calisma ani
// alanlaridir, YAZILMAZ (yuklemede adapter yeniden cozer).
void putFaceReference(std::ostream& out, const FaceReference& f) {
    out << f.source << ' ' << static_cast<int>(f.role) << ' '
        << static_cast<int>(f.faceRole) << ' ' << (f.localFrame ? 1 : 0);
    if (f.localFrame) { out << ' '; putMemberLocalFrame(out, *f.localFrame); }
    out << ' ';
    putString(out, f.selector);
}
FaceReference readFaceReference(std::istream& in) {
    FaceReference f;
    f.source = static_cast<EntityId>(readUint64(in, "faceRef source"));
    const std::int64_t role = readInt64(in, "faceRef role");
    if (role < 0 || role > 2) fail("invalid faceRef role");
    f.role = static_cast<ComponentInputRole>(role);
    const std::int64_t faceRole = readInt64(in, "faceRef faceRole");
    if (faceRole < 0 || faceRole > 8) fail("invalid faceRef faceRole");
    f.faceRole = static_cast<SemanticFaceRole>(faceRole);
    const bool hasFrame = readBool(in, "faceRef hasFrame");
    if (hasFrame) f.localFrame = readMemberLocalFrame(in, "faceRef localFrame");
    f.selector = readString(in);
    f.resolved = false;  // adapter cozer
    return f;
}

// DistanceConstraint (handle + hedef FaceReference + offset + enabled) — BIR satir.
void putDistanceConstraint(std::ostream& out, const DistanceConstraint& c) {
    putString(out, c.handle.key);
    out << ' ';
    putFaceReference(out, c.target);
    out << ' ' << c.signedOffset << ' ' << (c.enabled ? 1 : 0);
}
DistanceConstraint readDistanceConstraint(std::istream& in) {
    DistanceConstraint c;
    c.handle.key = readString(in);
    c.target = readFaceReference(in);
    c.signedOffset = readDouble(in, "constraint signedOffset");
    c.enabled = readBool(in, "constraint enabled");
    return c;
}

// ConstraintHandleDef (key + tur + baslangic konumu) — BIR satir.
void putHandleDef(std::ostream& out, const ConstraintHandleDef& h) {
    putString(out, h.key.key);
    out << ' ' << static_cast<int>(h.kind) << ' ';
    putVec3(out, h.initialPosition);
}
ConstraintHandleDef readHandleDef(std::istream& in) {
    ConstraintHandleDef h;
    h.key.key = readString(in);
    const std::int64_t kind = readInt64(in, "handle kind");
    if (kind < 0 || kind > 2) fail("invalid handle kind");
    h.kind = static_cast<HandleKind>(kind);
    h.initialPosition = readVec3(in, "handle initialPosition");
    return h;
}

// ConstraintDependency (dependent -> dependsOn) — BIR satir.
void putDependency(std::ostream& out, const ConstraintDependency& d) {
    putString(out, d.dependent.key);
    out << ' ';
    putString(out, d.dependsOn.key);
}
ConstraintDependency readDependency(std::istream& in) {
    ConstraintDependency d;
    d.dependent.key = readString(in);
    d.dependsOn.key = readString(in);
    return d;
}

// ComponentInstanceRecord listesinde EntityId -> indeks (J/D/M bloklari icin).
std::int64_t componentIndexById(const std::vector<ComponentInstanceRecord>& recs, EntityId id) {
    for (std::size_t i = 0; i < recs.size(); ++i)
        if (recs[i].id == id) return static_cast<std::int64_t>(i);
    return -1;
}

// LCS (origin/u/v/axis) -> MemberLocalFrame. LEGACY MIGRATION: eski dosyalarda
// magnetic frame component'in origin/u/v/axis'inden TURETILIR (ayri bir 'M'
// blogu yoksa). ComponentLocalFrame.frame'in w ekseni = component axis'dir.
MemberLocalFrame frameFromLcs(const ComponentInstanceRecord& r) {
    MemberLocalFrame f;
    f.origin = r.origin;
    f.u = r.u;
    f.v = r.v;
    f.w = r.axis;
    return f;
}
bool frameEqual(const MemberLocalFrame& a, const MemberLocalFrame& b, double tol = 1e-12) {
    const auto ve = [&](const Vec3& x, const Vec3& y) {
        return std::abs(x.x - y.x) <= tol && std::abs(x.y - y.y) <= tol &&
               std::abs(x.z - y.z) <= tol;
    };
    return ve(a.origin, b.origin) && ve(a.u, b.u) && ve(a.v, b.v) && ve(a.w, b.w);
}

// 'J' blogu: FaceReference listeleri (component id ile iliskili).
void writeFaceRefBlock(std::ostream& out, const std::vector<ComponentInstanceRecord>& recs) {
    std::size_t total = 0;
    for (const auto& r : recs) total += r.faceRefs.size();
    if (total == 0) return;
    std::ostringstream body;
    body << total << '\n';
    for (const auto& r : recs)
        for (const auto& f : r.faceRefs) {
            body << r.id << ' ';
            putFaceReference(body, f);
            body << '\n';
        }
    const std::string b = body.str();
    out << "J " << b.size() << '\n';
    out.write(b.data(), static_cast<std::streamsize>(b.size()));
}

// 'D' blogu: DistanceConstraint + Handle + Dependency tanimlari.
void writeConstraintBlock(std::ostream& out, const std::vector<ComponentInstanceRecord>& recs) {
    std::size_t nC = 0, nH = 0, nD = 0;
    for (const auto& r : recs) {
        nC += r.constraints.size();
        nH += r.handles.size();
        nD += r.dependencies.size();
    }
    if (nC == 0 && nH == 0 && nD == 0) return;
    std::ostringstream body;
    body << nC << ' ' << nH << ' ' << nD << '\n';
    for (const auto& r : recs)
        for (const auto& c : r.constraints) {
            body << r.id << ' ';
            putDistanceConstraint(body, c);
            body << '\n';
        }
    for (const auto& r : recs)
        for (const auto& h : r.handles) {
            body << r.id << ' ';
            putHandleDef(body, h);
            body << '\n';
        }
    for (const auto& r : recs)
        for (const auto& d : r.dependencies) {
            body << r.id << ' ';
            putDependency(body, d);
            body << '\n';
        }
    const std::string b = body.str();
    out << "D " << b.size() << '\n';
    out.write(b.data(), static_cast<std::streamsize>(b.size()));
}

// 'M' blogu: MagneticFrame / ComponentLocalFrame (frame + alt nesne yerlesimleri).
// Yalnizca "default olmayan" cerceveler yazilir: alt nesne VARSA veya frame
// origin/u/v/axis'ten turetilen legacy cerceveden FARKLIYSA. Boylece (a) eski
// dosyalar bayt-kararli kalir, (b) cocuksuz ama anlamli cerceve KAYBOLMAZ.
void writeFrameBlock(std::ostream& out, const std::vector<ComponentInstanceRecord>& recs) {
    std::size_t nFrames = 0;
    for (const auto& r : recs)
        if (!r.frame.children.empty() || !frameEqual(r.frame.frame, frameFromLcs(r))) ++nFrames;
    if (nFrames == 0) return;
    std::ostringstream body;
    body << nFrames << '\n';
    for (const auto& r : recs) {
        if (r.frame.children.empty() && frameEqual(r.frame.frame, frameFromLcs(r))) continue;
        body << r.id << ' ';
        putMemberLocalFrame(body, r.frame.frame);
        body << ' ' << r.frame.children.size() << '\n';
        for (const auto& c : r.frame.children) {
            putString(body, c.id);
            body << ' ';
            putLocalPlacement(body, c.local);
            body << '\n';
        }
    }
    const std::string b = body.str();
    out << "M " << b.size() << '\n';
    out.write(b.data(), static_cast<std::streamsize>(b.size()));
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
            body << ' ' << mod.source << ' ' << (mod.invertSide ? 1 : 0)
                 << ' ' << mod.diameter << ' ' << mod.owner
                 << ' ' << (mod.hidden ? 1 : 0) << ' ' << mod.depth
                 << '\n';  // P6 kaynak; P8 yon tersi; B1 cap + sahiplik + derinlik; gizli
        }
        const std::string b = body.str();
        out << "C " << b.size() << '\n';
        out.write(b.data(), static_cast<std::streamsize>(b.size()));
    }

    // Kuyruk: G blogu (bulon gruplari, Phase B1).
    if (!doc.boltGroups.empty()) {
        std::ostringstream body;
        body << doc.boltGroups.size() << '\n';
        for (const auto& g : doc.boltGroups) {
            body << g.id << ' ' << g.parts.size();
            for (const EntityId p : g.parts) body << ' ' << p;
            body << ' ' << g.origin.x << ' ' << g.origin.y << ' ' << g.origin.z
                 << ' ' << g.axis.x << ' ' << g.axis.y << ' ' << g.axis.z
                 << ' ' << g.xDir.x << ' ' << g.xDir.y << ' ' << g.xDir.z
                 << ' ' << g.params.nominalDiameter << ' ' << g.params.holeDiameter << ' ';
            putString(body, g.params.grade);
            body << ' ' << (g.params.hasHead ? 1 : 0)
                 << ' ' << (g.params.hasNut ? 1 : 0)
                 << ' ' << (g.params.hasWasher ? 1 : 0)
                 << ' ' << g.displayEntityId << ' ' << (g.enabled ? 1 : 0)
                 << ' ' << g.countX << ' ' << g.countY
                 << ' ' << g.spacingX << ' ' << g.spacingY
                 << ' ' << g.holeDepth
                 << ' ' << g.spacingXs.size();
            for (const double v : g.spacingXs) body << ' ' << v;
            body << ' ' << g.spacingYs.size();
            for (const double v : g.spacingYs) body << ' ' << v;
            body << '\n';
        }
        const std::string b = body.str();
        out << "G " << b.size() << '\n';
        out.write(b.data(), static_cast<std::streamsize>(b.size()));
    }

    // Kuyruk: E blogu (alin levhasi baglantilari, Phase C1).
    if (!doc.endPlateConnections.empty()) {
        std::ostringstream body;
        body << doc.endPlateConnections.size() << '\n';
        for (const auto& c : doc.endPlateConnections) {
            body << c.id << ' ' << c.mainPart << ' ' << c.secondaryPart << ' '
                 << c.plateEntityId << ' ' << c.params.plateThickness << ' '
                 << c.params.plateExtra << ' ' << c.params.boltDiameter << ' '
                 << c.params.holeDiameter << ' ';
            putString(body, c.params.boltGrade);
            body << ' ' << c.params.boltCountX << ' ' << c.params.boltCountY << ' '
                 << c.params.boltEdge << ' ' << (c.params.hasHead ? 1 : 0) << ' '
                 << (c.params.hasNut ? 1 : 0) << ' ' << (c.params.hasWasher ? 1 : 0)
                 << ' ' << (c.enabled ? 1 : 0);
            // Sahip olunan bulon grubu (G blogu formatinda, tek satir).
            const BoltGroup& g = c.bolts;
            body << ' ' << g.id << ' ' << g.parts.size();
            for (const EntityId p : g.parts) body << ' ' << p;
            body << ' ' << g.origin.x << ' ' << g.origin.y << ' ' << g.origin.z
                 << ' ' << g.axis.x << ' ' << g.axis.y << ' ' << g.axis.z
                 << ' ' << g.xDir.x << ' ' << g.xDir.y << ' ' << g.xDir.z
                 << ' ' << g.params.nominalDiameter << ' ' << g.params.holeDiameter << ' ';
            putString(body, g.params.grade);
            body << ' ' << (g.params.hasHead ? 1 : 0) << ' ' << (g.params.hasNut ? 1 : 0)
                 << ' ' << (g.params.hasWasher ? 1 : 0) << ' ' << g.displayEntityId << ' '
                 << (g.enabled ? 1 : 0) << ' ' << g.countX << ' ' << g.countY << ' '
                 << g.spacingX << ' ' << g.spacingY << ' ' << g.holeDepth << ' '
                 << g.spacingXs.size();
            for (const double v : g.spacingXs) body << ' ' << v;
            body << ' ' << g.spacingYs.size();
            for (const double v : g.spacingYs) body << ' ' << v;
            body << ' ' << c.params.gap << ' '
                 << c.params.topExtra << ' ' << c.params.bottomExtra << ' '
                 << c.params.boltAlignH << ' ' << c.params.boltAlignV << ' '
                 << c.params.boltOffsetH << ' ' << c.params.boltOffsetV << ' '
                 << c.params.boltSpacingX << ' ' << c.params.boltSpacingY << ' '
                 << c.params.deletedBolts.size();
            for (const int db : c.params.deletedBolts) body << ' ' << db;
            // Bolt basic properties + bolting direction (faz sonrasi, trailing,
            // geriye uyumlu): once numerik alanlar, sonra iki string.
            body << ' ' << (c.params.threadInMat ? 1 : 0) << ' ' << c.params.siteWorkshop
                 << ' ' << c.params.boltCutLengthMode << ' ' << c.params.boltLengthIncrease
                 << ' ' << c.params.boltDirection << ' ';
            putString(body, c.params.boltStandard);
            body << ' ';
            putString(body, c.params.boltComment);
            // Milestone 2: Tekla upper/lower edge distance + acik boyut override
            // (trailing, geriye uyumlu; eski okuyucu string sonrasi alanlari
            // gormez, yeni okuyucu fail-soft okur).
            body << ' ' << c.params.upperEdgeDistance << ' ' << c.params.lowerEdgeDistance
                 << ' ' << c.params.plateWidthOverride << ' ' << c.params.plateHeightOverride
                 << ' ' << c.params.boltSpacingXs.size();
            for (const double v : c.params.boltSpacingXs) body << ' ' << v;
            body << ' ' << c.params.boltSpacingYs.size();
            for (const double v : c.params.boltSpacingYs) body << ' ' << v;
            body << '\n';
        }
        const std::string b = body.str();
        out << "E " << b.size() << '\n';
        out.write(b.data(), static_cast<std::streamsize>(b.size()));
    }

    // Kuyruk: I blogu (component instance'lari, Phase C2).
    if (!doc.componentInstances.empty()) {
        std::ostringstream body;
        writeComponentBody(body, doc.componentInstances);
        const std::string b = body.str();
        out << "I " << b.size() << '\n';
        out.write(b.data(), static_cast<std::streamsize>(b.size()));
    }

    // Kuyruk: J/D/M bloklari (Slice 2d-3) — component'in parametrik ref/kisit/
    // cerceve durumu. Ileriye-uyumlu AYRI bloklar; eski okuyucu bilinmeyen blogu
    // kayipsiz korur. Bos veri varsa blok YAZILMAZ (dosya bayt-kararli kalir).
    writeFaceRefBlock(out, doc.componentInstances);      // 'J'
    writeConstraintBlock(out, doc.componentInstances);   // 'D'
    writeFrameBlock(out, doc.componentInstances);        // 'M'

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

    // Kuyruk: A blogu (kesit yerlesim anchor'i, Slice 2d-3). Yalniz Top DISI
    // anchor'lar yazilir (Top = varsayilan = eski davranis).
    if (!doc.sectionAnchors.empty()) {
        std::ostringstream body;
        body << doc.sectionAnchors.size() << '\n';
        for (const auto& sa : doc.sectionAnchors)
            body << sa.entityId << ' ' << static_cast<int>(sa.anchor) << '\n';
        const std::string b = body.str();
        out << "A " << b.size() << '\n';
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
    bool sawH = false, sawB = false, sawV = false, sawG = false, sawE = false, sawI = false;
    bool sawJ = false, sawD = false, sawM = false, sawA = false;
    // 'M' bloguyla cerceve ATAYAN component id'leri — legacy turetme (origin/u/v/axis)
    // yalnizca 'M' kaydi OLMAYAN component'lere uygulanir.
    std::unordered_set<EntityId> framedComponents;
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
            std::string line;
            std::getline(cs, line);  // count satırının kalanı (boş)
            for (std::uint64_t ci = 0; ci < count; ++ci) {
                if (!std::getline(cs, line)) fail("missing cutModifier record");
                std::istringstream ls(line);
                CutModifier mod;
                const std::int64_t kind = readInt64(ls, "cut kind");
                if (kind < 0 || kind > 5) fail("invalid cut kind");
                mod.kind = static_cast<CutKind>(kind);
                mod.target = static_cast<EntityId>(readUint64(ls, "cut target"));
                mod.planeOrigin.x = readDouble(ls, "cut origin x");
                mod.planeOrigin.y = readDouble(ls, "cut origin y");
                mod.planeOrigin.z = readDouble(ls, "cut origin z");
                mod.planeNormal.x = readDouble(ls, "cut normal x");
                mod.planeNormal.y = readDouble(ls, "cut normal y");
                mod.planeNormal.z = readDouble(ls, "cut normal z");
                mod.keepPositive = readBool(ls, "cut keepPositive");
                mod.tool = static_cast<EntityId>(readUint64(ls, "cut tool"));
                mod.enabled = readBool(ls, "cut enabled");
                mod.name = readString(ls);
                const std::uint64_t polyCount = readUint64(ls, "cut polygon count");
                if (polyCount > kMmw8MaxVertices) fail("cut polygon too long");
                mod.polygon.reserve(static_cast<std::size_t>(polyCount));
                for (std::uint64_t pi = 0; pi < polyCount; ++pi) {
                    Vec3 p;
                    p.x = readDouble(ls, "polygon x");
                    p.y = readDouble(ls, "polygon y");
                    p.z = readDouble(ls, "polygon z");
                    mod.polygon.push_back(p);
                }
                // P6: kaynak varlik (opsiyonel — eski dosyalarda satir sonu bos).
                std::uint64_t source = 0;
                if (ls >> source) mod.source = static_cast<EntityId>(source);
                // P8: kesme yonu tersi (opsiyonel — eski dosyalarda yok).
                std::uint64_t invertSide = 0;
                if (ls >> invertSide) mod.invertSide = invertSide != 0;
                // B1: delik capi (opsiyonel — eski dosyalarda yok).
                double diameter = 0.0;
                if (ls >> diameter) mod.diameter = diameter;
                // B1: bulon sahipligi (opsiyonel — eski dosyalarda yok).
                std::uint64_t owner = 0;
                if (ls >> owner) mod.owner = static_cast<EntityId>(owner);
                // Gizli (motor eleman) (opsiyonel — eski dosyalarda yok).
                std::uint64_t hidden = 0;
                if (ls >> hidden) mod.hidden = hidden != 0;
                // Delik derinligi (opsiyonel — eski dosyalarda yok -> 0 through-cut).
                double depth = 0.0;
                if (ls >> depth) mod.depth = depth;
                parsed.cutModifiers.push_back(std::move(mod));
            }
        } else if (tag == 'G') {
            if (sawG) fail("repeated G block");
            sawG = true;
            std::istringstream gs(body);
            const std::uint64_t count = readUint64(gs, "boltGroup count");
            if (count > kMmw8MaxBolts) fail("boltGroup count exceeds limit");
            std::string line;
            std::getline(gs, line);  // count satırının kalanı (boş)
            std::unordered_map<EntityId, bool> seenBolt;
            for (std::uint64_t gi = 0; gi < count; ++gi) {
                if (!std::getline(gs, line)) fail("missing boltGroup record");
                std::istringstream ls(line);
                BoltGroup g;
                g.id = static_cast<EntityId>(readUint64(ls, "bolt id"));
                if (g.id == kInvalidEntityId || g.id == kExhaustedEntityId)
                    fail("bolt id must be a valid identity");
                if (seenBolt.count(g.id)) fail("duplicate bolt id");
                seenBolt[g.id] = true;
                const std::uint64_t nParts = readUint64(ls, "bolt part count");
                if (nParts > kMmw8MaxModels) fail("bolt part count exceeds limit");
                g.parts.reserve(static_cast<std::size_t>(nParts));
                for (std::uint64_t pi = 0; pi < nParts; ++pi)
                    g.parts.push_back(static_cast<EntityId>(readUint64(ls, "bolt part")));
                g.origin.x = readDouble(ls, "bolt origin x");
                g.origin.y = readDouble(ls, "bolt origin y");
                g.origin.z = readDouble(ls, "bolt origin z");
                g.axis.x = readDouble(ls, "bolt axis x");
                g.axis.y = readDouble(ls, "bolt axis y");
                g.axis.z = readDouble(ls, "bolt axis z");
                g.xDir.x = readDouble(ls, "bolt xDir x");
                g.xDir.y = readDouble(ls, "bolt xDir y");
                g.xDir.z = readDouble(ls, "bolt xDir z");
                g.params.nominalDiameter = readDouble(ls, "bolt nominal diameter");
                g.params.holeDiameter = readDouble(ls, "bolt hole diameter");
                g.params.grade = readString(ls);
                g.params.hasHead = readBool(ls, "bolt hasHead");
                g.params.hasNut = readBool(ls, "bolt hasNut");
                g.params.hasWasher = readBool(ls, "bolt hasWasher");
                g.displayEntityId = static_cast<EntityId>(readUint64(ls, "bolt display"));
                g.enabled = readBool(ls, "bolt enabled");
                // Pattern (opsiyonel, geriye uyumlu): eski dosyalarda satir
                // `enabled` ile biter; pattern alanlari default kalir.
                if (!(ls >> g.countX >> g.countY >> g.spacingX >> g.spacingY)) {
                    g.countX = 1;
                    g.countY = 1;
                    g.spacingX = 0.0;
                    g.spacingY = 0.0;
                }
                // Delik derinligi (opsiyonel — eski dosyalarda yok -> 0 through-cut).
                double holeDepthVal = 0.0;
                if (!(ls >> holeDepthVal)) holeDepthVal = 0.0;
                g.holeDepth = holeDepthVal;
                // Degisken araliklar (opsiyonel, trailing): count + degerler.
                std::uint64_t nSx = 0, nSy = 0;
                if (ls >> nSx && nSx <= kMmw8MaxBolts) {
                    g.spacingXs.clear();
                    for (std::uint64_t k = 0; k < nSx; ++k) {
                        double v = 0.0;
                        if (ls >> v) g.spacingXs.push_back(v);
                    }
                }
                if (ls >> nSy && nSy <= kMmw8MaxBolts) {
                    g.spacingYs.clear();
                    for (std::uint64_t k = 0; k < nSy; ++k) {
                        double v = 0.0;
                        if (ls >> v) g.spacingYs.push_back(v);
                    }
                }
                parsed.boltGroups.push_back(std::move(g));
            }
        } else if (tag == 'E') {
            if (sawE) fail("repeated E block");
            sawE = true;
            std::istringstream es(body);
            const std::uint64_t count = readUint64(es, "endPlate count");
            if (count > kMmw8MaxEndPlates) fail("endPlate count exceeds limit");
            std::string line;
            std::getline(es, line);  // count satırının kalanı (boş)
            std::unordered_map<EntityId, bool> seenEp;
            for (std::uint64_t ei = 0; ei < count; ++ei) {
                if (!std::getline(es, line)) fail("missing endPlate record");
                std::istringstream ls(line);
                EndPlateConnection c;
                c.id = static_cast<EntityId>(readUint64(ls, "endPlate id"));
                if (c.id == kInvalidEntityId || c.id == kExhaustedEntityId)
                    fail("endPlate id must be a valid identity");
                if (seenEp.count(c.id)) fail("duplicate endPlate id");
                seenEp[c.id] = true;
                c.mainPart = static_cast<EntityId>(readUint64(ls, "endPlate mainPart"));
                c.secondaryPart = static_cast<EntityId>(readUint64(ls, "endPlate secondaryPart"));
                c.plateEntityId = static_cast<EntityId>(readUint64(ls, "endPlate plate"));
                c.params.plateThickness = readDouble(ls, "endPlate plateThickness");
                c.params.plateExtra = readDouble(ls, "endPlate plateExtra");
                c.params.boltDiameter = readDouble(ls, "endPlate boltDiameter");
                c.params.holeDiameter = readDouble(ls, "endPlate holeDiameter");
                c.params.boltGrade = readString(ls);
                c.params.boltCountX = static_cast<int>(readInt64(ls, "endPlate boltCountX"));
                c.params.boltCountY = static_cast<int>(readInt64(ls, "endPlate boltCountY"));
                c.params.boltEdge = readDouble(ls, "endPlate boltEdge");
                c.params.hasHead = readBool(ls, "endPlate hasHead");
                c.params.hasNut = readBool(ls, "endPlate hasNut");
                c.params.hasWasher = readBool(ls, "endPlate hasWasher");
                c.enabled = readBool(ls, "endPlate enabled");
                // Sahip olunan bulon grubu (G blogu formatinda).
                BoltGroup& g = c.bolts;
                g.id = static_cast<EntityId>(readUint64(ls, "endPlate bolt id"));
                if (g.id == kInvalidEntityId || g.id == kExhaustedEntityId)
                    fail("endPlate bolt id must be a valid identity");
                const std::uint64_t nParts = readUint64(ls, "endPlate bolt part count");
                if (nParts > kMmw8MaxModels) fail("endPlate bolt part count exceeds limit");
                g.parts.reserve(static_cast<std::size_t>(nParts));
                for (std::uint64_t pi = 0; pi < nParts; ++pi)
                    g.parts.push_back(static_cast<EntityId>(readUint64(ls, "endPlate bolt part")));
                g.origin.x = readDouble(ls, "endPlate bolt origin x");
                g.origin.y = readDouble(ls, "endPlate bolt origin y");
                g.origin.z = readDouble(ls, "endPlate bolt origin z");
                g.axis.x = readDouble(ls, "endPlate bolt axis x");
                g.axis.y = readDouble(ls, "endPlate bolt axis y");
                g.axis.z = readDouble(ls, "endPlate bolt axis z");
                g.xDir.x = readDouble(ls, "endPlate bolt xDir x");
                g.xDir.y = readDouble(ls, "endPlate bolt xDir y");
                g.xDir.z = readDouble(ls, "endPlate bolt xDir z");
                g.params.nominalDiameter = readDouble(ls, "endPlate bolt nominal diameter");
                g.params.holeDiameter = readDouble(ls, "endPlate bolt hole diameter");
                g.params.grade = readString(ls);
                g.params.hasHead = readBool(ls, "endPlate bolt hasHead");
                g.params.hasNut = readBool(ls, "endPlate bolt hasNut");
                g.params.hasWasher = readBool(ls, "endPlate bolt hasWasher");
                g.displayEntityId = static_cast<EntityId>(readUint64(ls, "endPlate bolt display"));
                g.enabled = readBool(ls, "endPlate bolt enabled");
                if (!(ls >> g.countX >> g.countY >> g.spacingX >> g.spacingY)) {
                    g.countX = 1;
                    g.countY = 1;
                    g.spacingX = 0.0;
                    g.spacingY = 0.0;
                }
                // Delik derinligi (opsiyonel — eski dosyalarda yok -> 0 through-cut).
                double holeDepthVal = 0.0;
                if (!(ls >> holeDepthVal)) holeDepthVal = 0.0;
                g.holeDepth = holeDepthVal;
                // Degisken araliklar (opsiyonel, trailing).
                std::uint64_t nSxG = 0, nSyG = 0;
                if (ls >> nSxG && nSxG <= kMmw8MaxBolts) {
                    g.spacingXs.clear();
                    for (std::uint64_t k = 0; k < nSxG; ++k) {
                        double v = 0.0;
                        if (ls >> v) g.spacingXs.push_back(v);
                    }
                }
                if (ls >> nSyG && nSyG <= kMmw8MaxBolts) {
                    g.spacingYs.clear();
                    for (std::uint64_t k = 0; k < nSyG; ++k) {
                        double v = 0.0;
                        if (ls >> v) g.spacingYs.push_back(v);
                    }
                }
                // gap (faz D1): kayit SONUNDA opsiyonel alan. Eski dosyalarda
                // yoktur -> varsayilan 0.0 (geriye + ileriye uyumlu).
                double gapVal = 0.0;
                if (!(ls >> gapVal)) gapVal = 0.0;
                c.params.gap = gapVal;
                // Ayrı ust/alt kenar payi (faz sonrasi): kayit SONUNDA opsiyonel
                // alanlar. Eski dosyalarda yok -> -1 (plateExtra kullan).
                double topExtraVal = -1.0, bottomExtraVal = -1.0;
                if (!(ls >> topExtraVal)) topExtraVal = -1.0;
                if (!(ls >> bottomExtraVal)) bottomExtraVal = -1.0;
                c.params.topExtra = topExtraVal;
                c.params.bottomExtra = bottomExtraVal;
                // Bolt group dimensions (faz sonrasi): kayit SONUNDA opsiyonel.
                int alignH = 1, alignV = 2;
                double offH = 0.0, offV = -1.0, spX = 0.0, spY = 0.0;
                if (!(ls >> alignH)) alignH = 1;
                if (!(ls >> alignV)) alignV = 2;
                if (!(ls >> offH)) offH = 0.0;
                if (!(ls >> offV)) offV = -1.0;
                if (!(ls >> spX)) spX = 0.0;
                if (!(ls >> spY)) spY = 0.0;
                c.params.boltAlignH = alignH;
                c.params.boltAlignV = alignV;
                c.params.boltOffsetH = offH;
                c.params.boltOffsetV = offV;
                c.params.boltSpacingX = spX;
                c.params.boltSpacingY = spY;
                // Silinen bulon listesi: "adet" + adet tane 1-based numara.
                int ndel = 0;
                if ((ls >> ndel) && ndel > 0) {
                    c.params.deletedBolts.clear();
                    for (int k = 0; k < ndel; ++k) {
                        int dn = 0;
                        if (ls >> dn) c.params.deletedBolts.push_back(dn);
                    }
                }
                // Bolt basic properties + bolting direction (faz sonrasi, trailing,
                // geriye uyumlu): numerik alanlar fail-soft, iki string try/catch.
                int threadInMat = 0, siteW = 0, cutMode = 0, bdir = 1;
                double lenInc = 0.0;
                if (!(ls >> threadInMat)) threadInMat = 0;
                if (!(ls >> siteW)) siteW = 0;
                if (!(ls >> cutMode)) cutMode = 0;
                if (!(ls >> lenInc)) lenInc = 0.0;
                if (!(ls >> bdir)) bdir = 1;
                c.params.threadInMat = (threadInMat != 0);
                c.params.siteWorkshop = siteW;
                c.params.boltCutLengthMode = cutMode;
                c.params.boltLengthIncrease = lenInc;
                c.params.boltDirection = bdir;
                try {
                    c.params.boltStandard = readString(ls);
                    c.params.boltComment = readString(ls);
                } catch (const std::runtime_error&) {
                    c.params.boltStandard.clear();
                    c.params.boltComment.clear();
                }
                // Milestone 2: Tekla upper/lower edge distance + acik boyut
                // override (trailing, fail-soft; eski dosyalarda yok -> sentinel/0).
                double upperED = -1e9, lowerED = -1e9, wOvr = 0.0, hOvr = 0.0;
                if (!(ls >> upperED)) upperED = -1e9;
                if (!(ls >> lowerED)) lowerED = -1e9;
                if (!(ls >> wOvr)) wOvr = 0.0;
                if (!(ls >> hOvr)) hOvr = 0.0;
                c.params.upperEdgeDistance = upperED;
                c.params.lowerEdgeDistance = lowerED;
                c.params.plateWidthOverride = wOvr;
                c.params.plateHeightOverride = hOvr;
                // Degisken bulon araliklari (trailing, fail-soft).
                std::uint64_t nSpX = 0, nSpY = 0;
                if (ls >> nSpX && nSpX <= kMmw8MaxBolts) {
                    c.params.boltSpacingXs.clear();
                    for (std::uint64_t k = 0; k < nSpX; ++k) {
                        double v = 0.0;
                        if (ls >> v) c.params.boltSpacingXs.push_back(v);
                    }
                }
                if (ls >> nSpY && nSpY <= kMmw8MaxBolts) {
                    c.params.boltSpacingYs.clear();
                    for (std::uint64_t k = 0; k < nSpY; ++k) {
                        double v = 0.0;
                        if (ls >> v) c.params.boltSpacingYs.push_back(v);
                    }
                }
                parsed.endPlateConnections.push_back(std::move(c));
            }
        } else if (tag == 'I') {
            if (sawI) fail("repeated I block");
            sawI = true;
            std::istringstream is(body);
            readComponentBody(is, parsed.componentInstances);
        } else if (tag == 'J') {
            if (sawJ) fail("repeated J block");
            if (!sawI) fail("J block before I block (wrong order)");
            sawJ = true;
            std::istringstream js(body);
            const std::uint64_t count = readUint64(js, "faceRef count");
            if (count > kMmw8MaxFaceRefs) fail("faceRef count exceeds limit");
            std::string line;
            std::getline(js, line);  // count satırının kalanı (boş)
            for (std::uint64_t fi = 0; fi < count; ++fi) {
                if (!std::getline(js, line)) fail("missing faceRef record");
                std::istringstream ls(line);
                const EntityId compId = static_cast<EntityId>(readUint64(ls, "faceRef component id"));
                const std::int64_t idx = componentIndexById(parsed.componentInstances, compId);
                if (idx < 0) fail("faceRef component id not in component list");
                parsed.componentInstances[static_cast<std::size_t>(idx)]
                    .faceRefs.push_back(readFaceReference(ls));
            }
        } else if (tag == 'D') {
            if (sawD) fail("repeated D block");
            if (!sawI) fail("D block before I block (wrong order)");
            sawD = true;
            std::istringstream ds(body);
            const std::uint64_t nC = readUint64(ds, "constraint count");
            const std::uint64_t nH = readUint64(ds, "handle count");
            const std::uint64_t nD = readUint64(ds, "dependency count");
            if (nC > kMmw8MaxConstraints) fail("constraint count exceeds limit");
            if (nH > kMmw8MaxHandles) fail("handle count exceeds limit");
            if (nD > kMmw8MaxDependencies) fail("dependency count exceeds limit");
            std::string line;
            std::getline(ds, line);  // nD satırının kalanı (boş)
            const auto compIdx = [&](EntityId compId) -> std::size_t {
                const std::int64_t i = componentIndexById(parsed.componentInstances, compId);
                if (i < 0) fail("constraint component id not in component list");
                return static_cast<std::size_t>(i);
            };
            for (std::uint64_t ci = 0; ci < nC; ++ci) {
                if (!std::getline(ds, line)) fail("missing constraint record");
                std::istringstream ls(line);
                const EntityId compId = static_cast<EntityId>(readUint64(ls, "constraint component id"));
                parsed.componentInstances[compIdx(compId)]
                    .constraints.push_back(readDistanceConstraint(ls));
            }
            for (std::uint64_t hi = 0; hi < nH; ++hi) {
                if (!std::getline(ds, line)) fail("missing handle record");
                std::istringstream ls(line);
                const EntityId compId = static_cast<EntityId>(readUint64(ls, "handle component id"));
                parsed.componentInstances[compIdx(compId)]
                    .handles.push_back(readHandleDef(ls));
            }
            for (std::uint64_t di = 0; di < nD; ++di) {
                if (!std::getline(ds, line)) fail("missing dependency record");
                std::istringstream ls(line);
                const EntityId compId = static_cast<EntityId>(readUint64(ls, "dependency component id"));
                parsed.componentInstances[compIdx(compId)]
                    .dependencies.push_back(readDependency(ls));
            }
            // Ayni component icinde yinelenen handle anahtari -> bozuk dosya.
            for (const auto& r : parsed.componentInstances) {
                std::unordered_map<std::string, bool> seenHandle;
                for (const auto& h : r.handles) {
                    if (seenHandle.count(h.key.key)) fail("duplicate handle key in component");
                    seenHandle[h.key.key] = true;
                }
            }
        } else if (tag == 'M') {
            if (sawM) fail("repeated M block");
            if (!sawI) fail("M block before I block (wrong order)");
            sawM = true;
            std::istringstream ms(body);
            const std::uint64_t count = readUint64(ms, "frame count");
            if (count > kMmw8MaxComponents) fail("frame count exceeds limit");
            std::string line;
            std::getline(ms, line);  // count satırının kalanı (boş)
            std::unordered_map<EntityId, bool> seenFrame;
            for (std::uint64_t fi = 0; fi < count; ++fi) {
                if (!std::getline(ms, line)) fail("missing frame record");
                std::istringstream ls(line);
                const EntityId compId = static_cast<EntityId>(readUint64(ls, "frame component id"));
                const std::int64_t idx = componentIndexById(parsed.componentInstances, compId);
                if (idx < 0) fail("frame component id not in component list");
                if (seenFrame.count(compId)) fail("duplicate frame component id");
                seenFrame[compId] = true;
                ComponentLocalFrame clf;
                clf.frame = readMemberLocalFrame(ls, "frame member local frame");
                const std::uint64_t nChildren = readUint64(ls, "frame child count");
                if (nChildren > kMmw8MaxFrameChildren) fail("frame child count exceeds limit");
                std::unordered_map<std::string, bool> seenChild;
                for (std::uint64_t ci = 0; ci < nChildren; ++ci) {
                    if (!std::getline(ms, line)) fail("missing frame child record");
                    std::istringstream cs(line);
                    MagneticChild child;
                    child.id = readString(cs);
                    child.local = readLocalPlacement(cs, "child local placement");
                    if (seenChild.count(child.id)) fail("duplicate frame child id");
                    seenChild[child.id] = true;
                    clf.children.push_back(std::move(child));
                }
                parsed.componentInstances[static_cast<std::size_t>(idx)].frame = std::move(clf);
                framedComponents.insert(compId);
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
        } else if (tag == 'A') {
            if (sawA) fail("repeated A block");
            sawA = true;
            std::istringstream as(body);
            const std::uint64_t count = readUint64(as, "sectionAnchor count");
            if (count > kMmw8MaxModels) fail("sectionAnchor count exceeds limit");
            std::unordered_map<EntityId, bool> seenSa;
            for (std::uint64_t ai = 0; ai < count; ++ai) {
                Mmw8SectionAnchor sa;
                sa.entityId = static_cast<EntityId>(readUint64(as, "anchor entityId"));
                const std::int64_t anchor = readInt64(as, "anchor kind");
                if (anchor < 0 || anchor > 2) fail("invalid section anchor");
                sa.anchor = static_cast<SectionAnchor>(anchor);
                if (!seenIds.count(sa.entityId)) fail("anchor entityId not in model list");
                if (seenSa.count(sa.entityId)) fail("duplicate anchor entityId");
                seenSa[sa.entityId] = true;
                parsed.sectionAnchors.push_back(sa);
            }
        } else {
            // Bilinmeyen blok: kayipsiz korunur.
            parsed.unknownBlocks.push_back(Mmw8UnknownBlock{tag, std::move(body)});
        }
    }
    if (!sawH) fail("missing mandatory H block");

    // LEGACY MIGRATION (Slice 2d-3): 'M' kaydi OLMAYAN component'lerde magnetic
    // frame origin/u/v/axis'ten turetilir. 'M' kaydi OLANLAR (framedComponents)
    // AYNEN korunur (cocuksuz ama anlamli cerceve dahil).
    for (auto& r : parsed.componentInstances)
        if (!framedComponents.count(r.id)) r.frame.frame = frameFromLcs(r);

    // nextEntityId, en buyuk entityId'den buyuk olmali.
    EntityId maxId = 0;
    for (const EntityId e : parsed.entityIds)
        if (e > maxId) maxId = e;
    if (parsed.nextEntityId <= maxId)
        fail("nextEntityId not greater than max entityId");

    doc = std::move(parsed);
}

} // namespace mm
