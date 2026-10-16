#pragma once
#ifdef MM_HAS_OCC
// ALIN LEVHASI BAGLANTI GEOMETRISI (Phase C1) — gercek OCCT B-Rep.
//
// Alin levhasi (dikdortgen prizma) uretimi + baglanti montaji (levha + bulon
// grubu). Delikler mevcut CutKind::Hole yoluyla, bulon montaji mevcut
// buildBoltGroupAssemblyShape ile uretilir — burada yalnizca levha ve
// birlestirilmis baglanti gorseli vardir.
//
// windows.h yok; Linux'ta gercek OCCT ile derlenir/test edilir.

#include "model_maker/end_plate.hpp"

#include <TopoDS_Shape.hxx>

namespace mm {

// Alin levhasini uretir: u x v duzleminde plateWidth x plateHeight dikdortgen,
// axis (birim) boyunca plateThickness kalinlik, merkez plateCenter. Levha
// merkezi, dikdortgenin ORTASINDA ve axis boyunca ortalanmistir (plateCenter
// ± thickness/2). Gecersiz boyut/taban icin NULL sekil doner.
TopoDS_Shape buildEndPlateSolid(const Vec3& plateCenter, const Vec3& u, const Vec3& v,
                                const Vec3& axis, double plateWidth, double plateHeight,
                                double plateThickness);

// EndPlatePlacement'tan levhayi uretir (buildEndPlateSolid'i cagirir).
TopoDS_Shape buildEndPlateFromPlacement(const EndPlatePlacement& placement);

// Baglanti GORSEL montaji: levha + bulon grubu (buildBoltGroupAssemblyShape)
// tek kati halinde fuse edilir. gripLength = bulonlarin eksen boyunca deldigi
// yigin uzunlugu (levha + kolon flansi; Document.boltGripLength ile hesaplanir).
// Levha uretilemez veya bulon montaji null ise mevcut olan kismi dondurur.
TopoDS_Shape buildEndPlateAssemblyShape(const EndPlatePlacement& placement, double gripLength);

} // namespace mm
#endif // MM_HAS_OCC
