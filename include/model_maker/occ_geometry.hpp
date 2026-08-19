#pragma once
#ifdef MM_HAS_OCC
#include "model_maker/geometry.hpp"

#include <TopoDS_Shape.hxx>

#include <filesystem>

namespace mm {

// OCC ilk yuzeyi: kati kutu uretimi + hacim + STEP yazma/okuma.
// Daha genis modelleme API'si bu modulde buyuyecek (BRep, boolean,
// fillet, STEP/IGES alisverisi, dogrudan goruntuleme).

double boxVolume(double x, double y, double z);

bool writeBoxStep(const std::filesystem::path& path, double x, double y, double z);

double readStepVolume(const std::filesystem::path& path);

// --- Kati ilkeller: OCC katisini uygulamanin WireframeModel'ine donusturur ---

// Herhangi bir TopoDS_Shape'in kenarlarini dogru parcaciklarina ayirir;
// daire/egri kenarlar circleSegments adedine orneklenir, duz kenarlar tek
// parcadir. Paylasilan kose noktalari tek vertex'te birlesir.
WireframeModel shapeToWireframe(const TopoDS_Shape& shape, int circleSegments = 48);

WireframeModel solidBoxWireframe(double dx, double dy, double dz);

WireframeModel solidCylinderWireframe(double radius, double height, int circleSegments = 48);

} // namespace mm
#endif // MM_HAS_OCC
