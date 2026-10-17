#include "model_maker/occ_contact_resolver.hpp"

#ifdef MM_HAS_OCC

#include <BRepAdaptor_Surface.hxx>
#include <BRepTopAdaptor_FClass2d.hxx>
#include <BRep_Tool.hxx>
#include <GeomAPI_IntCS.hxx>
#include <Geom_Line.hxx>
#include <Geom_Surface.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>

#include <gp_Dir.hxx>
#include <gp_Lin.hxx>
#include <gp_Pln.hxx>
#include <gp_Pnt.hxx>
#include <gp_Vec.hxx>

#include <cmath>
#include <vector>

namespace mm {

namespace {

double vdot(const Vec3& a, const Vec3& b) noexcept {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
double vnorm(const Vec3& a) noexcept { return std::sqrt(vdot(a, a)); }

// Planar bir yuzun SEMANTIK rolunu cozer (dis normal + lokal konumdan).
// I-kesit: normal ±v -> FlangeOuter; normal ±u -> Web (±tw/2) veya FlangeEdge
// (±b/2, u-ofsetle ayirt); normal ±w -> MemberStart/End.
SemanticFaceRole classifyFace(const MemberLocalFrame& frame, const Vec3& n,
                              const Vec3& localP, const SectionDims& dims) noexcept {
    const double nu = vdot(n, frame.u);
    const double nv = vdot(n, frame.v);
    const double nw = vdot(n, frame.w);
    const double au = std::abs(nu), av = std::abs(nv), aw = std::abs(nw);
    if (aw >= au && aw >= av) {
        return nw > 0.0 ? SemanticFaceRole::MemberEnd : SemanticFaceRole::MemberStart;
    }
    if (av >= au) {
        return nv > 0.0 ? SemanticFaceRole::FlangeOuterPositive
                        : SemanticFaceRole::FlangeOuterNegative;
    }
    // normal ~ ±u: govde (|lu| = tw/2) vs flans ucu (|lu| = b/2, b >> tw).
    const double lu = std::abs(localP.x);
    if (lu > dims.tw) {
        return nu > 0.0 ? SemanticFaceRole::FlangeEdgePositive
                        : SemanticFaceRole::FlangeEdgeNegative;
    }
    return nu > 0.0 ? SemanticFaceRole::WebPositive : SemanticFaceRole::WebNegative;
}

// Ayni yuz ailesi mi (FlangeOuter±, Web±, FlangeEdge±, MemberStart, MemberEnd)?
bool sameFamily(SemanticFaceRole a, SemanticFaceRole b) noexcept {
    if (a == SemanticFaceRole::None || b == SemanticFaceRole::None) return true;
    const auto fam = [](SemanticFaceRole r) -> int {
        switch (r) {
            case SemanticFaceRole::FlangeOuterPositive:
            case SemanticFaceRole::FlangeOuterNegative:
                return 1;
            case SemanticFaceRole::WebPositive:
            case SemanticFaceRole::WebNegative:
                return 2;
            case SemanticFaceRole::FlangeEdgePositive:
            case SemanticFaceRole::FlangeEdgeNegative:
                return 3;
            case SemanticFaceRole::MemberStart:
                return 4;
            case SemanticFaceRole::MemberEnd:
                return 5;
            default:
                return 0;
        }
    };
    return fam(a) == fam(b);
}

} // namespace

ContactPlacement resolveOcctContact(const TopoDS_Shape& mainSolid,
                                    const MemberLocalFrame& frame, const SectionDims& dims,
                                    double length, const Vec3& beamAxisOrigin,
                                    const Vec3& beamAxisDir, SemanticFaceRole preferredRole) {
    ContactPlacement out;
    out.localFrame = frame;
    out.semanticFaceRole = preferredRole;

    if (mainSolid.IsNull()) {
        out.status = ResolutionStatus::Unresolved;
        return out;
    }
    // Non-finite giris reddi (reviewer onerisi): NaN/Inf koordinat/yön, kesit
    // boyutu veya uzunluk cozum uretmeden Unresolved doner.
    if (!std::isfinite(beamAxisOrigin.x) || !std::isfinite(beamAxisOrigin.y) ||
        !std::isfinite(beamAxisOrigin.z) || !std::isfinite(beamAxisDir.x) ||
        !std::isfinite(beamAxisDir.y) || !std::isfinite(beamAxisDir.z) ||
        !std::isfinite(length) || !std::isfinite(dims.h) || !std::isfinite(dims.b) ||
        !std::isfinite(dims.tw) || !std::isfinite(dims.tf)) {
        out.status = ResolutionStatus::Unresolved;
        return out;
    }
    const double dl = vnorm(beamAxisDir);
    if (dl < 1e-12) {
        out.status = ResolutionStatus::Unresolved;
        return out;
    }
    if (!isOrthonormalRightHanded(frame, 1e-9)) {
        out.status = ResolutionStatus::Unresolved;
        return out;
    }
    const Vec3 dir{beamAxisDir.x / dl, beamAxisDir.y / dl, beamAxisDir.z / dl};

    // Kiris ekseni, tercih edilen yuz ailesinin normaline DIK ise (paralel
    // ise) temas olanaksiz — erkenden ParallelAxis.
    {
        const Vec3 axisDir = [&]() -> Vec3 {
            switch (preferredRole) {
                case SemanticFaceRole::FlangeOuterPositive:
                case SemanticFaceRole::FlangeOuterNegative:
                    return frame.v;
                case SemanticFaceRole::WebPositive:
                case SemanticFaceRole::WebNegative:
                case SemanticFaceRole::FlangeEdgePositive:
                case SemanticFaceRole::FlangeEdgeNegative:
                    return frame.u;
                case SemanticFaceRole::MemberStart:
                case SemanticFaceRole::MemberEnd:
                    return frame.w;
                default:
                    return {0.0, 0.0, 0.0};
            }
        }();
        if (vnorm(axisDir) > 0.5 && std::abs(vdot(dir, axisDir)) < 1e-6) {
            out.status = ResolutionStatus::ParallelAxis;
            return out;
        }
    }

    // Kiris ekseni = sonsuz dogru (kesim parametresi w: onundeki/arkasindaki).
    const gp_Lin line(gp_Pnt(beamAxisOrigin.x, beamAxisOrigin.y, beamAxisOrigin.z),
                      gp_Dir(dir.x, dir.y, dir.z));

    struct Candidate {
        double t;             // dogru parametresi (mesafe, isaretli)
        Vec3 point;
        Vec3 normal;          // dis normal (kirise dogru)
        SemanticFaceRole role;
    };
    std::vector<Candidate> candidates;
    bool anyMatchingFace = false;   // semantic rolle eslesen yuz var mi

    for (TopExp_Explorer ex(mainSolid, TopAbs_FACE); ex.More(); ex.Next()) {
        const TopoDS_Face face = TopoDS::Face(ex.Current());
        if (face.IsNull()) continue;

        BRepAdaptor_Surface adaptor(face, Standard_False);
        if (adaptor.GetType() != GeomAbs_Plane) continue;  // yalniz duzlemsel

        const gp_Pln plane = adaptor.Plane();
        const gp_Pnt loc = plane.Location();
        // DIS normal: yuz ORIENTATION'indan (kapali katinin standart kurali).
        // FORWARD yuzun geometrik normali DISARI, REVERSED yuzunki ICERI bakar
        // -> cevir. (Kat merkezi heuristic'i CONCAV yuzlerde yanlis yon verir:
        // or. flansin IC yuzunun dis normali merkeze DOGRU bakar, merkezden
        // uzaklasma kurali burada ters sonuc uretir.)
        gp_Dir n = plane.Axis().Direction();
        if (face.Orientation() == TopAbs_REVERSED) n.Reverse();
        const Vec3 outward{n.X(), n.Y(), n.Z()};

        // Semantic rol esleme (localP = yuzeyin lokal koordinatlari).
        const Vec3 wloc{loc.X(), loc.Y(), loc.Z()};
        const Vec3 localP{vdot(wloc - frame.origin, frame.u),
                          vdot(wloc - frame.origin, frame.v),
                          vdot(wloc - frame.origin, frame.w)};
        const SemanticFaceRole role = classifyFace(frame, outward, localP, dims);
        if (!sameFamily(role, preferredRole)) continue;
        anyMatchingFace = true;

        // Yuz kirise BAKIYOR mu? (dis normal yaklasma yonune anti-paralel;
        // bu, kolonun uzak ARKA yuzunu eler.)
        if (vdot(outward, dir) >= 0.0) continue;

        // Kiris ekseninin GERCEK yuzle kesimi (parametre uzayinda).
        const Handle(Geom_Surface) surf = BRep_Tool::Surface(face);
        const Handle(Geom_Line) gline = new Geom_Line(line);
        GeomAPI_IntCS inter(gline, surf);
        if (!inter.IsDone() || inter.NbPoints() == 0) continue;
        for (Standard_Integer i = 1; i <= inter.NbPoints(); ++i) {
            Standard_Real u = 0.0, v = 0.0, w = 0.0;
            inter.Parameters(i, u, v, w);
            // Kesim noktasi KESILMIS yuz sinirlari icinde mi? Delik/bosluk
            // icindeki nokta OUT doner. BRepTopAdaptor_FClass2d TEL (pcurve)
            // tabanlidir — ucgenleme gerektirmez; boolean-cut sonrasi bayat
            // mesh'ten ETKILENMEZ (BRepClass_FaceClassifier bayat mesh'te
            // deligi gormeyip IN donerdi).
            BRepTopAdaptor_FClass2d classifier(face, 1e-7);
            const TopAbs_State state = classifier.Perform(gp_Pnt2d(u, v));
            if (state != TopAbs_IN && state != TopAbs_ON) continue;
            const gp_Pnt pt = inter.Point(i);
            Candidate c;
            c.t = w;
            c.point = {pt.X(), pt.Y(), pt.Z()};
            c.normal = outward;
            c.role = role;
            candidates.push_back(c);
            break;  // duzlem icin tek kesim
        }
    }

    if (candidates.empty()) {
        out.status = anyMatchingFace ? ResolutionStatus::OutsideFace
                                     : ResolutionStatus::NoContact;
        return out;
    }

    // En yakin kesim (en kucuk ISARETLI t). Kiris KOLONA DOGRU yaklasir;
    // gerideki (t < -tol) kesimler kirişin ARKASINDAKİ yüzlerdir ve secilmez
    // (|t| kullanimi gerideki yuzu secip yon ile celisirdi).
    constexpr double kBehindTol = 1e-6;
    std::size_t best = candidates.size();
    for (std::size_t i = 0; i < candidates.size(); ++i) {
        if (candidates[i].t < -kBehindTol) continue;
        if (best == candidates.size() || candidates[i].t < candidates[best].t) best = i;
    }
    if (best == candidates.size()) {
        out.status = ResolutionStatus::NoContact;
        return out;
    }

    // Belirsizlik: EN YAKIN kesime neredeyse ESIT mesafede, FARKLI normaldeki
    // birden fazla yuz varsa (or. koseye capraz gelen kiris). Uzak bir ikinci
    // aday (yakin flans + uzak govde) belirsizlik DEGIL — ilk temas kazanir.
    for (std::size_t i = 0; i < candidates.size(); ++i) {
        if (i == best || candidates[i].t < -kBehindTol) continue;
        if (std::abs(candidates[i].t - candidates[best].t) > 1e-6) continue;
        const double nd = vdot(candidates[i].normal, candidates[best].normal);
        if (nd < 0.99) {  // es-duzlemli degil, AYNI mesafede
            out.status = ResolutionStatus::AmbiguousFace;
            out.semanticFaceRole = candidates[best].role;
            return out;
        }
    }

    const Candidate& c = candidates[best];
    out.status = ResolutionStatus::Resolved;
    out.contactPoint = c.point;
    out.outwardNormal = c.normal;
    out.semanticFaceRole = c.role;
    out.referencePlane.origin = c.point;
    out.referencePlane.normal = c.normal;
    return out;
}

} // namespace mm

#endif // MM_HAS_OCC
