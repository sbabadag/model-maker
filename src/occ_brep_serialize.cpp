#include "model_maker/occ_brep_serialize.hpp"

#ifdef MM_HAS_OCC

#include <BRepCheck_Analyzer.hxx>
#include <BinTools.hxx>
#include <BinTools_FormatVersion.hxx>
#include <Standard_Failure.hxx>
#include <Standard_Version.hxx>

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

bool serializeBRep(const TopoDS_Shape& shape, std::string& blob, std::string* error) {
    blob.clear();
    if (shape.IsNull()) {
        if (error) *error = "null shape (bos geometri)";
        return false;
    }
    try {
        std::ostringstream stream(std::ios::binary);
        // Acik secim: triangulation DAHIL (hizli yeniden goruntuleme),
        // normals HARIC (analitik geometriden turetilir), CURRENT format.
        BinTools::Write(shape, stream,
                        /*theWithTriangles=*/true,
                        /*theWithNormals=*/false,
                        BinTools_FormatVersion_CURRENT);
        blob = stream.str();
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
    if (blob.empty()) {
        if (error) *error = "serialization produced an empty blob";
        return false;
    }
    if (blob.size() > maxBRepBlobSize()) {
        if (error) *error = "serialized blob exceeds the size limit";
        blob.clear();
        return false;
    }
    return true;
}

bool deserializeBRep(const std::string& blob, TopoDS_Shape& shape, std::string* error) {
    if (blob.empty()) {
        if (error) *error = "empty blob";
        return false;
    }
    if (blob.size() > maxBRepBlobSize()) {
        if (error) *error = "blob exceeds the size limit";
        return false;
    }
    // Basarisizlikta `shape` dokunulmasin: once geciciye oku.
    TopoDS_Shape result;
    try {
        std::istringstream stream(blob, std::ios::binary);
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
