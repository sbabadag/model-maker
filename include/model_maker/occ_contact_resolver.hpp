#pragma once
#ifdef MM_HAS_OCC

// REAL OCCT B-REP CONTACT RESOLVER (Phase R2c).
//
// Lokal kesit geometrisinden (contact_resolver) cozulen SEMANTIK referansi,
// gercek TopoDS_Shape'in DUZLEMSEL yuzlerine esler; kiris ekseninin gercek
// KESILMIS yuzle (trimmed face) kesimini dogrular; temas noktasi + DIS normal +
// durum doner. Delik/kesme icindeki isabetler BRepClass_FaceClassifier ile
// reddedilir. TopoDS_Face SIRA NUMARASI kalici kimlik OLARAK KULLANILMAZ —
// eslesme kaynak EntityId + semantic rol + geometri uzerinden yapilir.
//
// OCCT tipleri BU BASLIKTA kalir; ContactPlacement (contact_resolver.hpp)
// OCCT'sizdir — portable Constraint Solver katmanina OCCT SIZMAZ.

#include "model_maker/contact_resolver.hpp"

#include <TopoDS_Shape.hxx>

namespace mm {

// Main katinin (kolon) gercek B-Rep temas yuzunu cozer.
//
// `mainSolid`: kolonun TopoDS_Shape'i (I-kesit kati). `frame`/`dims`/`length`:
// lokal cerceve + kesit boyutlari + uzunluk (semantic rol esleme icin).
// `beamAxisOrigin`/`beamAxisDir`: kiris baglanti ekseni (nokta + kolona dogru
// birim yon). `preferredRole`: tercih edilen yuz ailesi.
//
// Davranis:
//   - Yalniz DUZLEMSEL yuzler adaydir; semantic rolle eslesen yuz secilir.
//   - Dis normal, yuz orientation + kat merkezi (malzeme ici/disi) ile dogrulanir.
//   - Kiris ekseninin yuzle KESIMI gercek trimmed sinirlar icinde mi kontrol
//     edilir (delik/bosluk reddedilir).
//   - Uzak arka yuz (normal kirise BAKMAYAN) secilmez.
//   - Birden fazla esdeger aday -> AmbiguousFace; paralel eksen -> ParallelAxis;
//     sinir disi -> OutsideFace; esleme yok -> NoContact/Unresolved.
ContactPlacement resolveOcctContact(const TopoDS_Shape& mainSolid,
                                    const MemberLocalFrame& frame,
                                    const SectionDims& dims,
                                    double length,
                                    const Vec3& beamAxisOrigin,
                                    const Vec3& beamAxisDir,
                                    SemanticFaceRole preferredRole);

} // namespace mm

#endif // MM_HAS_OCC
