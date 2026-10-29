#include "model_maker/sections.hpp"
#include "model_maker/bolt.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <set>
#include <sstream>

namespace mm::drawing {
namespace {
double dot(const Vec3& a, const Vec3& b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
Vec3 sub(const Vec3& a, const Vec3& b) { return {a.x-b.x,a.y-b.y,a.z-b.z}; }
Vec3 axisStart(const WireframeModel& m) { const auto& p=m.properties(); return {p.axisFromX,p.axisFromY,p.axisFromZ}; }
Vec3 axisEnd(const WireframeModel& m) { const auto& p=m.properties(); return {p.axisToX,p.axisToY,p.axisToZ}; }
struct Axis { Vec3 start{}, x{}, y{}, z{}; double length{0}; };
Axis axisOf(const WireframeModel& m) {
    Axis a; a.start=axisStart(m);
    const auto delta=sub(axisEnd(m),a.start);
    a.length=std::sqrt(dot(delta,delta));
    if (a.length<1e-6) return a;
    a.x={delta.x/a.length,delta.y/a.length,delta.z/a.length};
    const Vec3 up=std::fabs(a.x.z)>0.95 ? Vec3{1,0,0} : Vec3{0,0,1};
    const auto cross=[](Vec3 p,Vec3 q) { return Vec3{p.y*q.z-p.z*q.y,p.z*q.x-p.x*q.z,p.x*q.y-p.y*q.x}; };
    a.y=cross(up,a.x); const double yn=std::sqrt(dot(a.y,a.y));
    a.y={a.y.x/yn,a.y.y/yn,a.y.z/yn}; a.z=cross(a.x,a.y);
    return a;
}
std::pair<double,double> extent(const WireframeModel& m,const Axis& a) {
    double lo=1e300,hi=-1e300;
    for (const auto& p:m.vertices()) { const double t=dot(sub(p,a.start),a.x); lo=std::min(lo,t); hi=std::max(hi,t); }
    return {lo,hi};
}
bool transverseContact(const WireframeModel& member,const WireframeModel& other,const Axis& a) {
    if (member.vertices().empty() || other.vertices().empty()) return false;
    const auto bounds=[&](const WireframeModel& model,Vec3 dir) {
        double lo=1e300,hi=-1e300;
        for (const Vec3& p:model.vertices()) {
            const double x=dot(sub(p,a.start),dir);
            lo=std::min(lo,x); hi=std::max(hi,x);
        }
        return std::pair{lo,hi};
    };
    for (Vec3 dir:{a.y,a.z}) {
        const auto [lo,hi]=bounds(member,dir);
        const auto [olo,ohi]=bounds(other,dir);
        if (olo>hi+1.0 || ohi<lo-1.0) return false;
    }
    return true;
}
long long q(double n) { return std::llround(n*10.0); }
}
std::string sectionLabel(std::size_t index) {
    std::string s;
    do { s.insert(s.begin(),char('A'+index%26)); index=index/26; if (!index) break; --index; } while (true);
    return s+"-"+s;
}
std::vector<SectionRequest> changePoints(const Document& doc,const std::vector<EntityId>& parts,
                                         const fab::FabricationInventory& inv) {
    std::vector<SectionRequest> all;
    const std::set<EntityId> selected(parts.begin(),parts.end());
    for (EntityId member:selected) {
        const auto mi=doc.indexOfEntity(member);
        if (!mi || doc.models()[*mi].properties().profileName.empty()) continue;
        const Axis axis=axisOf(doc.models()[*mi]);
        if (axis.length<1e-6) continue;
        std::vector<SectionRequest> candidates;
        const auto add=[&](double t,const char* why,EntityId source) {
            if (t>=-0.5 && t<=axis.length+0.5)
                candidates.push_back({member,std::clamp(t,0.0,axis.length),{},why,source});
        };
        add(0,"member start",member); add(axis.length,"member end",member);
        for (const Vec3& vertex:doc.models()[*mi].vertices())
            add(dot(sub(vertex,axis.start),axis.x),"profile section",member);
        const std::string profile=doc.models()[*mi].properties().profileName;
        for (EntityId id:parts) {
            if (id==member) continue;
            const auto oi=doc.indexOfEntity(id); if (!oi) continue;
            const auto& model=doc.models()[*oi];
            if (!transverseContact(doc.models()[*mi],model,axis)) continue;
            const auto [lo,hi]=extent(model,axis);
            // Profile transitions and attached plate/component boundaries use actual model vertices.
            if (!model.properties().profileName.empty() && model.properties().profileName!=profile) {
                add(lo,"profile change",id); add(hi,"profile change",id);
            } else if (model.properties().profileName.empty()) {
                add(lo,"plate boundary",id); add(hi,"plate boundary",id);
            }
        }
        for (const auto& plate:inv.plates) if (selected.count(plate.plate)) {
            const auto idx=doc.indexOfEntity(plate.plate);
            if (!idx || !transverseContact(doc.models()[*mi],doc.models()[*idx],axis)) continue;
            const auto [lo,hi]=extent(doc.models()[*idx],axis);
            add(lo,"plate boundary",plate.plate); add(hi,"plate boundary",plate.plate);
        }
        for (const auto& bolt:doc.boltGroups()) {
            if (std::find(bolt.parts.begin(),bolt.parts.end(),member)==bolt.parts.end()) continue;
            for (const auto& p:boltGroupBoltOrigins(bolt)) add(dot(sub(p,axis.start),axis.x),"bolt centre",bolt.id);
        }
        for (const auto& w:doc.welds())
            if ((w.mainPart==member || w.secondaryPart==member) &&
                selected.count(w.mainPart) && selected.count(w.secondaryPart))
                add(dot(sub(w.origin,axis.start),axis.x),"weld junction",w.id);
        for (const auto& c:doc.componentInstances()) {
            if (c.mainPart!=member && std::find(c.secondaryParts.begin(),c.secondaryParts.end(),member)==c.secondaryParts.end()) continue;
            add(dot(sub(c.origin,axis.start),axis.x),"component",c.id);
            for (EntityId id:c.ownedPlateIds) {
                const auto i=doc.indexOfEntity(id); if (!i) continue;
                const auto [lo,hi]=extent(doc.models()[*i],axis);
                add(lo,"component boundary",id); add(hi,"component boundary",id);
            }
        }
        std::sort(candidates.begin(),candidates.end(),[](const auto& a,const auto& b) {
            if (a.t!=b.t) return a.t<b.t;
            if (a.source!=b.source) return a.source<b.source;
            return a.reason<b.reason;
        });
        for (const auto& c:candidates)
            if (all.empty() || all.back().member!=member || c.t-all.back().t>1.0) all.push_back(c);
    }
    return all;
}
DrawingView generateSectionView(const Document& doc,const std::vector<EntityId>& parts,
                                const SectionRequest& request,const fab::MemberDatum&) {
    DrawingView v; v.kind=ViewKind::Section; v.subject=request.member;
    v.sectionMember=request.member; v.sectionT=request.t; v.sectionSource=request.source;
    v.sectionReason=request.reason; v.scale=1.0;
    const auto i=doc.indexOfEntity(request.member); if (!i) return v;
    const Axis axis=axisOf(doc.models()[*i]); if (axis.length<1e-6) return v;
    v.frame.right=axis.y; v.frame.up=axis.z; v.frame.normal=axis.x; v.frame.memberAligned=true;
    const auto depth=[&](Vec3 p) { return dot(sub(p,axis.start),axis.x)-request.t; };
    const auto point=[&](Vec3 p) { return Vec2{dot(sub(p,axis.start),axis.y),dot(sub(p,axis.start),axis.z)}; };
    using Key=std::array<long long,4>;
    std::map<Key,std::pair<Vec2,Vec2>> segments;
    const auto push=[&](Vec2 a,Vec2 b) {
        if (std::hypot(a.x-b.x,a.y-b.y)<1e-6) return;
        Key k{q(a.x),q(a.y),q(b.x),q(b.y)}, reverse{q(b.x),q(b.y),q(a.x),q(a.y)};
        segments.emplace(std::min(k,reverse),std::pair{a,b});
    };
    for (EntityId id:parts) {
        const auto idx=doc.indexOfEntity(id); if (!idx) continue;
        const auto& model=doc.models()[*idx]; const auto& pts=model.vertices();
        for (const auto& edge:model.edges()) {
            if (edge.from>=pts.size() || edge.to>=pts.size()) continue;
            const double da=depth(pts[edge.from]), db=depth(pts[edge.to]);
            if (std::fabs(da)<=0.5 && std::fabs(db)<=0.5)
                push(point(pts[edge.from]),point(pts[edge.to]));
        }
        // A face crossing the plane yields a real cut contour, even where its longitudinal
        // wire edges only intersect at points (a simple beam's midspan is otherwise empty).
        for (const Face& face:model.faces()) {
            std::vector<Vec2> hits;
            for (std::size_t j=0;j<face.size();++j) {
                const auto ia=face[j],ib=face[(j+1)%face.size()];
                if (ia>=pts.size() || ib>=pts.size()) continue;
                const double da=depth(pts[ia]),db=depth(pts[ib]);
                if (std::fabs(da)<1e-8 && std::fabs(db)<1e-8) { push(point(pts[ia]),point(pts[ib])); continue; }
                if ((da>0 && db>0) || (da<0 && db<0) || std::fabs(da-db)<1e-12) continue;
                const double u=da/(da-db);
                if (u>=0 && u<=1) {
                    Vec3 p{pts[ia].x+u*(pts[ib].x-pts[ia].x),pts[ia].y+u*(pts[ib].y-pts[ia].y),
                           pts[ia].z+u*(pts[ib].z-pts[ia].z)};
                    Vec2 hit=point(p);
                    bool found=false; for (const auto& old:hits) found|=std::hypot(old.x-hit.x,old.y-hit.y)<1e-6;
                    if (!found) hits.push_back(hit);
                }
            }
            if (hits.size()==2) push(hits[0],hits[1]);
        }
    }
    Bounds2 raw;
    for (const auto& [key,seg]:segments) { raw.include(seg.first); raw.include(seg.second); }
    if (raw.empty) return v;
    v.projectionOffset=raw.min;
    for (const auto& [key,seg]:segments) {
        const Vec2 a{seg.first.x-raw.min.x,seg.first.y-raw.min.y};
        const Vec2 b{seg.second.x-raw.min.x,seg.second.y-raw.min.y};
        v.segments.push_back({a,b,LineStyle::Visible}); v.bounds.include(a); v.bounds.include(b);
    }
    v.members.push_back({request.member,v.bounds});
    return v;
}
std::string sectionSignature(const DrawingView& view) {
    std::set<std::array<long long,4>> lines;
    for (const auto& s:view.segments) {
        std::array<long long,4> a{q(s.a.x),q(s.a.y),q(s.b.x),q(s.b.y)};
        std::array<long long,4> b{a[2],a[3],a[0],a[1]}; lines.insert(std::min(a,b));
    }
    std::set<std::array<long long,3>> circles;
    for (const auto& c:view.circles) circles.insert({q(c.center.x),q(c.center.y),q(c.radius)});
    std::ostringstream os;
    for (const auto& l:lines) for (long long x:l) os<<x<<',';
    os<<'|';
    for (const auto& c:circles) for (long long x:c) os<<x<<',';
    return os.str();
}
}
