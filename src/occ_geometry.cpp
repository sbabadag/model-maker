#ifdef MM_HAS_OCC
#include "model_maker/occ_geometry.hpp"

#include <BRepGProp.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <GProp_GProps.hxx>
#include <IFSelect_ReturnStatus.hxx>
#include <STEPControl_Reader.hxx>
#include <STEPControl_Writer.hxx>
#include <TopoDS_Shape.hxx>

#include <stdexcept>

namespace mm {

double boxVolume(double x, double y, double z) {
    const TopoDS_Shape box = BRepPrimAPI_MakeBox(x, y, z).Shape();
    GProp_GProps props;
    BRepGProp::VolumeProperties(box, props);
    return props.Mass();
}

bool writeBoxStep(const std::filesystem::path& path, double x, double y, double z) {
    STEPControl_Writer writer;
    if (writer.Transfer(BRepPrimAPI_MakeBox(x, y, z).Shape(),
                        STEPControl_AsIs) != IFSelect_RetDone)
        return false;
    return writer.Write(path.string().c_str()) == IFSelect_RetDone;
}

double readStepVolume(const std::filesystem::path& path) {
    STEPControl_Reader reader;
    if (reader.ReadFile(path.string().c_str()) != IFSelect_RetDone)
        throw std::runtime_error("STEP dosyasi okunamadi");
    reader.TransferRoots();
    GProp_GProps props;
    BRepGProp::VolumeProperties(reader.OneShape(), props);
    return props.Mass();
}

} // namespace mm
#endif // MM_HAS_OCC
