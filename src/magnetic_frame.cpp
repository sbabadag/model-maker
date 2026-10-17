#include "model_maker/magnetic_frame.hpp"
#include "model_maker/contact_resolver.hpp"  // isOrthonormalRightHanded

namespace mm {

bool isRigidLocalPlacement(const LocalPlacement& p, double tol) noexcept {
    const MemberLocalFrame f{p.origin, p.u, p.v, p.w};
    return isOrthonormalRightHanded(f, tol);
}

bool isValidComponentLocalFrame(const ComponentLocalFrame& clf, double tol) noexcept {
    if (!isOrthonormalRightHanded(clf.frame, tol)) return false;
    for (const MagneticChild& c : clf.children)
        if (!isRigidLocalPlacement(c.local, tol)) return false;
    return true;
}

Vec3 magneticTransformVec(const MemberLocalFrame& frame, const Vec3& local) noexcept {
    return frame.u * local.x + frame.v * local.y + frame.w * local.z;
}

Vec3 magneticTransformPoint(const MemberLocalFrame& frame, const Vec3& local) noexcept {
    return frame.origin + magneticTransformVec(frame, local);
}

MemberLocalFrame magneticTransformFrame(const MemberLocalFrame& parent,
                                        const MemberLocalFrame& childLocal) noexcept {
    MemberLocalFrame out;
    out.origin = magneticTransformPoint(parent, childLocal.origin);
    out.u = magneticTransformVec(parent, childLocal.u);
    out.v = magneticTransformVec(parent, childLocal.v);
    out.w = magneticTransformVec(parent, childLocal.w);
    return out;
}

std::vector<std::pair<std::string, MemberLocalFrame>> computeChildWorldFrames(
    const ComponentLocalFrame& clf) {
    std::vector<std::pair<std::string, MemberLocalFrame>> out;
    if (!isOrthonormalRightHanded(clf.frame, 1e-9)) return out;  // dejenere cerceve -> bos
    out.reserve(clf.children.size());
    for (const MagneticChild& c : clf.children) {
        if (!isRigidLocalPlacement(c.local, 1e-9)) continue;  // rijit olmayan alt nesneyi atla
        MemberLocalFrame local;
        local.origin = c.local.origin;
        local.u = c.local.u;
        local.v = c.local.v;
        local.w = c.local.w;
        out.emplace_back(c.id, magneticTransformFrame(clf.frame, local));
    }
    return out;
}

} // namespace mm
