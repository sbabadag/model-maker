#include "model_maker/occ_brep_serialize.hpp"

#include "model_maker/sha256.hpp"

#ifdef MM_HAS_OCC

#include <BRepCheck_Analyzer.hxx>
#include <BinTools.hxx>
#include <BinTools_FormatVersion.hxx>
#include <Standard_Failure.hxx>
#include <Standard_Version.hxx>

#include <cstring>
#include <sstream>
#include <stdexcept>

namespace mm {

const char* occVersionString() noexcept {
    return OCC_VERSION_COMPLETE;
}

int bRepFormatVersion() noexcept {
    return static_cast<int>(BinTools_FormatVersion_CURRENT);
}

std::size_t maxBRepBlobSize() noexcept {
    // 64 MiB — tek bir TopoDS_Shape icin cok genis pay; kaynak tuketimi
    // korumasinin ust siniri. (Buyuk katilar binlerce yuzle bile bu sinirin
    // cok altinda kalir.)
    return 64u * 1024u * 1024u;
}

namespace {

// --- little-endian sabit-genislik yardimcilari (acik bayt sirasi) ------------
void putU32LE(std::string& out, std::uint32_t v) {
    for (int i = 0; i < 4; ++i)
        out.push_back(static_cast<char>((v >> (8 * i)) & 0xffu));
}
void putU64LE(std::string& out, std::uint64_t v) {
    for (int i = 0; i < 8; ++i)
        out.push_back(static_cast<char>((v >> (8 * i)) & 0xffull));
}
std::uint32_t getU32LE(const char* p) {
    std::uint32_t v = 0;
    for (int i = 0; i < 4; ++i)
        v |= static_cast<std::uint32_t>(static_cast<unsigned char>(p[i])) << (8 * i);
    return v;
}
std::uint64_t getU64LE(const char* p) {
    std::uint64_t v = 0;
    for (int i = 0; i < 8; ++i)
        v |= static_cast<std::uint64_t>(static_cast<unsigned char>(p[i])) << (8 * i);
    return v;
}

// Zarf dogrulamasi (magic arayan tarafindan onceden kontrol edildi). Basarida
// `payload` dogrulanmis ham BinTools baytlarini alir; basarisizlikta false doner
// ve `error` set edilir — BinTools::Read ASLA cagrilmaz.
bool openBRepEnvelope(const std::string& blob, std::string& payload, std::string* error) {
    if (blob.size() < kBRepEnvelopeHeaderSize) {
        if (error) *error = "envelope truncated (header incomplete)";
        return false;
    }
    const std::uint32_t version = getU32LE(blob.data() + 8);
    if (version != kBRepEnvelopeVersion) {
        if (error) *error = "unsupported envelope version";
        return false;
    }
    const std::uint64_t payloadLen = getU64LE(blob.data() + 12);
    if (payloadLen > maxBRepBlobSize()) {
        if (error) *error = "envelope payload exceeds size limit";
        return false;
    }
    if (payloadLen != blob.size() - kBRepEnvelopeHeaderSize) {
        if (error) *error = "envelope payload length mismatch (truncated or trailing data)";
        return false;
    }
    const char* payloadStart = blob.data() + kBRepEnvelopeHeaderSize;
    Sha256Digest stored{};
    std::memcpy(stored.bytes, blob.data() + 20, sizeof(stored.bytes));
    const Sha256Digest computed = sha256(payloadStart, static_cast<std::size_t>(payloadLen));
    if (std::memcmp(stored.bytes, computed.bytes, sizeof(stored.bytes)) != 0) {
        if (error) *error = "envelope SHA-256 mismatch";
        return false;
    }
    payload.assign(payloadStart, static_cast<std::size_t>(payloadLen));
    return true;
}

// Eski (zarfsiz) ham BinTools blob icin baslik on-kontrolu (en iyi caba).
// OCCT'nin BinTools binary'si metin basligiyla baslar: "\nOpen CASCADE
// Topology V<N>, (c) Open Cascade". Bu, BinTools::Read oncesi bariz copu
// reddeder; DERIN bozulmayi yakalayamaz (o, zarfin SHA-256'si veya islem
// izolasyonu gerektirir — kalinti risk olarak belgelenir).
bool legacyHeaderLooksValid(const std::string& blob) {
    static const char kPrefix[] = "\nOpen CASCADE Topology V";
    const std::size_t n = sizeof(kPrefix) - 1;
    if (blob.size() < n + 1) return false;
    if (std::memcmp(blob.data(), kPrefix, n) != 0) return false;
    const char v = blob[n];
    return v >= '1' && v <= '4';
}

} // namespace

bool wrapBRepEnvelope(const std::string& rawPayload, std::string& envelope) {
    const Sha256Digest digest = sha256(rawPayload.data(), rawPayload.size());
    envelope.clear();
    envelope.reserve(kBRepEnvelopeHeaderSize + rawPayload.size());
    envelope.append(kBRepEnvelopeMagic, kBRepEnvelopeMagicSize);
    putU32LE(envelope, kBRepEnvelopeVersion);
    putU64LE(envelope, static_cast<std::uint64_t>(rawPayload.size()));
    envelope.append(reinterpret_cast<const char*>(digest.bytes), sizeof(digest.bytes));
    envelope.append(rawPayload);
    return true;
}

bool serializeBRep(const TopoDS_Shape& shape, std::string& blob, std::string* error) {
    blob.clear();
    if (shape.IsNull()) {
        if (error) *error = "null shape (bos geometri)";
        return false;
    }
    std::string payload;
    try {
        std::ostringstream stream(std::ios::binary);
        // Acik secim: triangulation DAHIL (hizli yeniden goruntuleme),
        // normals HARIC (analitik geometriden turetilir), CURRENT format.
        BinTools::Write(shape, stream,
                        /*theWithTriangles=*/true,
                        /*theWithNormals=*/false,
                        BinTools_FormatVersion_CURRENT);
        payload = stream.str();
    } catch (const Standard_Failure& ex) {
        if (error) *error = ex.GetMessageString();
        blob.clear();
        return false;
    } catch (const std::exception& ex) {
        if (error) *error = ex.what();
        blob.clear();
        return false;
    } catch (...) {
        if (error) *error = "unknown OCCT exception";
        blob.clear();
        return false;
    }
    if (payload.empty()) {
        if (error) *error = "serialization produced an empty blob";
        return false;
    }
    if (payload.size() > maxBRepBlobSize()) {
        if (error) *error = "serialized blob exceeds the size limit";
        return false;
    }
    // Ham BinTools payload'unu surumlu butunluk zarfina sar.
    return wrapBRepEnvelope(payload, blob);
}

bool deserializeBRep(const std::string& blob, TopoDS_Shape& shape, std::string* error) {
    if (blob.empty()) {
        if (error) *error = "empty blob";
        return false;
    }
    if (blob.size() > maxBRepBlobSize() + kBRepEnvelopeHeaderSize) {
        if (error) *error = "blob exceeds the size limit";
        return false;
    }

    // Format secimi: magic ile basliyorsa zarfli (yeni), degilse zarfsiz (eski).
    std::string payload;
    const bool enveloped =
        blob.size() >= kBRepEnvelopeMagicSize &&
        std::memcmp(blob.data(), kBRepEnvelopeMagic, kBRepEnvelopeMagicSize) == 0;
    if (enveloped) {
        // Butunluk dogrulamasi — basarisizsa BinTools::Read HIC cagrilmaz.
        if (!openBRepEnvelope(blob, payload, error)) return false;
    } else {
        // Eski zarfsiz blob: BinTools baslik on-kontrolu, sonra dogrudan okuma.
        if (!legacyHeaderLooksValid(blob)) {
            if (error) *error = "raw blob lacks a recognizable B-Rep header";
            return false;
        }
        payload = blob;
    }

    // Basarisizlikta `shape` dokunulmasin: once geciciye oku.
    TopoDS_Shape result;
    try {
        std::istringstream stream(payload, std::ios::binary);
        BinTools::Read(result, stream);
    } catch (const Standard_Failure& ex) {
        if (error) *error = ex.GetMessageString();
        return false;
    } catch (const std::exception& ex) {
        if (error) *error = ex.what();
        return false;
    } catch (...) {
        if (error) *error = "unknown OCCT exception";
        return false;
    }
    if (result.IsNull()) {
        if (error) *error = "deserialization produced a null shape";
        return false;
    }
    shape = result;
    return true;
}

bool validateBRep(const TopoDS_Shape& shape) {
    if (shape.IsNull()) return false;
    try {
        BRepCheck_Analyzer analyzer(shape, true);
        return analyzer.IsValid();
    } catch (const Standard_Failure&) {
        return false;
    } catch (const std::exception&) {
        return false;
    } catch (...) {
        return false;
    }
}

} // namespace mm

#endif // MM_HAS_OCC
