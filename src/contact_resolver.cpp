#include "model_maker/contact_resolver.hpp"

#include <cmath>
#include <vector>

namespace mm {

namespace {

double vdot(const Vec3& a, const Vec3& b) noexcept {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
double vnorm(const Vec3& a) noexcept { return std::sqrt(vdot(a, a)); }
Vec3 vcross(const Vec3& a, const Vec3& b) noexcept {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
Vec3 vneg(const Vec3& a) noexcept { return {a.x * -1.0, a.y * -1.0, a.z * -1.0}; }

// P noktasi secilen yuzun SONLU sinirlari icinde mi? (lokal koordinatlarda)
bool pointWithinFace(const Vec3& P, const MemberLocalFrame& frame, const SectionDims& dims,
                     double length, SemanticFaceRole role, double tol) noexcept {
    const double lu = vdot(P - frame.origin, frame.u);
    const double lv = vdot(P - frame.origin, frame.v);
    const double lw = vdot(P - frame.origin, frame.w);
    const double hb = dims.b * 0.5;
    const double hh = dims.h * 0.5;
    switch (role) {
        case SemanticFaceRole::FlangeOuterPositive:
        case SemanticFaceRole::FlangeOuterNegative:
            return std::abs(lu) <= hb + tol && lw >= -tol && lw <= length + tol;
        case SemanticFaceRole::WebPositive:
        case SemanticFaceRole::WebNegative:
        case SemanticFaceRole::FlangeEdgePositive:
        case SemanticFaceRole::FlangeEdgeNegative:
            return std::abs(lv) <= hh + tol && lw >= -tol && lw <= length + tol;
        case SemanticFaceRole::MemberStart:
        case SemanticFaceRole::MemberEnd:
            return std::abs(lu) <= hb + tol && std::abs(lv) <= hh + tol;
        default:
            return false;
    }
}

// Tercih edilen role gore aday rolleri uret (simetrik aile icin her iki isaret).
std::vector<SemanticFaceRole> candidateRoles(SemanticFaceRole preferred) noexcept {
    switch (preferred) {
        case SemanticFaceRole::FlangeOuterPositive:
        case SemanticFaceRole::FlangeOuterNegative:
            return {SemanticFaceRole::FlangeOuterPositive, SemanticFaceRole::FlangeOuterNegative};
        case SemanticFaceRole::WebPositive:
        case SemanticFaceRole::WebNegative:
            return {SemanticFaceRole::WebPositive, SemanticFaceRole::WebNegative};
        case SemanticFaceRole::FlangeEdgePositive:
        case SemanticFaceRole::FlangeEdgeNegative:
            return {SemanticFaceRole::FlangeEdgePositive, SemanticFaceRole::FlangeEdgeNegative};
        case SemanticFaceRole::MemberStart:
            return {SemanticFaceRole::MemberStart};
        case SemanticFaceRole::MemberEnd:
            return {SemanticFaceRole::MemberEnd};
        case SemanticFaceRole::None:
            return {SemanticFaceRole::FlangeOuterPositive, SemanticFaceRole::FlangeOuterNegative,
                    SemanticFaceRole::WebPositive,        SemanticFaceRole::WebNegative,
                    SemanticFaceRole::FlangeEdgePositive, SemanticFaceRole::FlangeEdgeNegative,
                    SemanticFaceRole::MemberStart,        SemanticFaceRole::MemberEnd};
        default:
            return {};
    }
}

} // namespace

MemberLocalFrame makeMemberLocalFrame(const ProfileSectionFrame& section,
                                      const Vec3& origin) noexcept {
    MemberLocalFrame f;
    f.origin = origin;
    f.u = section.width;
    f.v = section.height;
    f.w = section.axis;
    return f;
}

bool isOrthonormalRightHanded(const MemberLocalFrame& frame, double tol) noexcept {
    if (std::abs(vnorm(frame.u) - 1.0) > tol) return false;
    if (std::abs(vnorm(frame.v) - 1.0) > tol) return false;
    if (std::abs(vnorm(frame.w) - 1.0) > tol) return false;
    if (std::abs(vdot(frame.u, frame.v)) > tol) return false;
    if (std::abs(vdot(frame.u, frame.w)) > tol) return false;
    if (std::abs(vdot(frame.v, frame.w)) > tol) return false;
    const Vec3 uv = vcross(frame.u, frame.v);
    if (std::abs(uv.x - frame.w.x) > tol || std::abs(uv.y - frame.w.y) > tol ||
        std::abs(uv.z - frame.w.z) > tol)
        return false;
    return true;
}

std::optional<ReferencePlane> resolveSemanticFacePlane(const MemberLocalFrame& frame,
                                                       const SectionDims& dims,
                                                       double length,
                                                       SemanticFaceRole role) noexcept {
    ReferencePlane p;
    const Vec3& o = frame.origin;
    switch (role) {
        case SemanticFaceRole::FlangeOuterPositive:
            p.normal = frame.v;
            p.origin = o + frame.v * (dims.h * 0.5);
            break;
        case SemanticFaceRole::FlangeOuterNegative:
            p.normal = vneg(frame.v);
            p.origin = o + frame.v * (dims.h * -0.5);
            break;
        case SemanticFaceRole::WebPositive:
            p.normal = frame.u;
            p.origin = o + frame.u * (dims.tw * 0.5);
            break;
        case SemanticFaceRole::WebNegative:
            p.normal = vneg(frame.u);
            p.origin = o + frame.u * (dims.tw * -0.5);
            break;
        case SemanticFaceRole::FlangeEdgePositive:
            p.normal = frame.u;
            p.origin = o + frame.u * (dims.b * 0.5);
            break;
        case SemanticFaceRole::FlangeEdgeNegative:
            p.normal = vneg(frame.u);
            p.origin = o + frame.u * (dims.b * -0.5);
            break;
        case SemanticFaceRole::MemberStart:
            p.normal = vneg(frame.w);
            p.origin = o;
            break;
        case SemanticFaceRole::MemberEnd:
            p.normal = frame.w;
            p.origin = o + frame.w * length;
            break;
        default:
            return std::nullopt;
    }
    return p;
}

ContactPlacement resolveContactPlacement(const MemberLocalFrame& mainFrame,
                                         const SectionDims& mainDims, double mainLength,
                                         const Vec3& beamAxisOrigin, const Vec3& beamAxisDir,
                                         SemanticFaceRole preferredRole) noexcept {
    ContactPlacement out;
    out.localFrame = mainFrame;
    out.semanticFaceRole = preferredRole;

    // Dejenere eksen.
    const double dl = vnorm(beamAxisDir);
    if (dl < 1e-12) {
        out.status = ResolutionStatus::Unresolved;
        return out;
    }
    const Vec3 dir{beamAxisDir.x / dl, beamAxisDir.y / dl, beamAxisDir.z / dl};

    // Cerceve ortonormal + sag elli olmali.
    if (!isOrthonormalRightHanded(mainFrame, 1e-9)) {
        out.status = ResolutionStatus::Unresolved;
        return out;
    }

    const auto roles = candidateRoles(preferredRole);
    if (roles.empty()) {
        out.status = ResolutionStatus::Unresolved;
        return out;
    }

    const double eps = 1e-12;
    bool anyParallel = false;
    bool anyOutside = false;
    std::vector<ContactPlacement> valid;

    for (const auto role : roles) {
        const auto planeOpt = resolveSemanticFacePlane(mainFrame, mainDims, mainLength, role);
        if (!planeOpt) continue;
        const ReferencePlane& plane = *planeOpt;
        const double denom = vdot(plane.normal, dir);
        if (std::abs(denom) < eps) {
            anyParallel = true;
            continue;
        }
        const double t = vdot(plane.normal, plane.origin - beamAxisOrigin) / denom;
        if (!std::isfinite(t)) continue;
        const Vec3 P{beamAxisOrigin.x + dir.x * t, beamAxisOrigin.y + dir.y * t,
                     beamAxisOrigin.z + dir.z * t};
        if (!pointWithinFace(P, mainFrame, mainDims, mainLength, role, 1e-6)) {
            anyOutside = true;
            continue;
        }
        // Yuz kirise BAKIYOR mu: dis normal yaklasma yonune anti-paralel.
        // (n·dir > 0 ise kiris yuzun ARKA tarafindan yaklasiyor — temas degil.)
        if (vdot(plane.normal, dir) > 0.0) continue;

        ContactPlacement c;
        c.status = ResolutionStatus::Resolved;
        c.referencePlane = plane;
        c.contactPoint = P;
        c.outwardNormal = plane.normal;
        c.localFrame = mainFrame;
        c.semanticFaceRole = role;
        valid.push_back(c);
    }

    if (valid.size() == 1) {
        out = valid[0];
        return out;
    }
    if (valid.size() > 1) {
        // Birden fazla aile esit derecede gecerli (or. koseye carpan capraz kiris).
        out.status = ResolutionStatus::AmbiguousFace;
        out.semanticFaceRole = valid[0].semanticFaceRole;
        return out;
    }
    if (anyParallel) {
        out.status = ResolutionStatus::ParallelAxis;
        return out;
    }
    if (anyOutside) {
        out.status = ResolutionStatus::OutsideFace;
        return out;
    }
    out.status = ResolutionStatus::NoContact;
    return out;
}

} // namespace mm
