#include "model_maker/component_live_preview_widget.hpp"
#include "model_maker/bolt.hpp"
#include "model_maker/component_engine.hpp"

#include <QDoubleSpinBox>
#include <QPainter>
#include <QFontMetrics>
#include <QPainterPath>
#include <QPolygonF>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QResizeEvent>
#include <QSignalBlocker>
#include <QToolTip>
#include <algorithm>
#include <array>
#include <tuple>
#include <utility>
#include <QLinearGradient>
#include <QColor>
#include <cmath>
#include <limits>
#include <cstring>

namespace mm {
namespace {
constexpr double kPi=3.14159265358979323846;
Vec3 plus(Vec3 a,Vec3 b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
Vec3 minus(Vec3 a,Vec3 b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
Vec3 mul(Vec3 p,double k){return {p.x*k,p.y*k,p.z*k};}
double dot(Vec3 a,Vec3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
Vec3 cross(Vec3 a,Vec3 b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
double length(Vec3 p){return std::sqrt(dot(p,p));}
Vec3 unit(Vec3 p){const double n=length(p);return n>1e-10?mul(p,1.0/n):Vec3{0,0,1};}
Vec3 sum(Vec3 a,Vec3 b,Vec3 c){return plus(plus(a,b),c);}

struct Poly {
    std::vector<Vec3> vertices;
    QColor face;
    std::string role;
    double depth=0.0;
};

std::vector<Vec3> plateOutline(const PlateSpec& p, double side) {
    // Serbest kontur (eğik kiriş haunch gövdesi): noktalar levha merkezine
    // göre (u,v); dikdörtgen+pah yolunu ATLA (model ile aynı görünsün).
    if (!p.outline.empty()) {
        std::vector<Vec3> out;
        out.reserve(p.outline.size());
        for (const auto& q : p.outline)
            out.push_back(sum(p.center, mul(p.u, q.first),
                              plus(mul(p.v, q.second),
                                   mul(p.axis, side*p.thickness*0.5))));
        return out;
    }
    // Corner order BL BR TR TL and corresponding rectangular chamfers.
    const double u=p.width*0.5,v=p.height*0.5;
    const std::array<QPointF,4> c{QPointF(-u,-v),QPointF(u,-v),
                                  QPointF(u,v),QPointF(-u,v)};
    const std::array<QPointF,4> prev{QPointF(-u,v),QPointF(-u,-v),
                                     QPointF(u,-v),QPointF(u,v)};
    const std::array<QPointF,4> next{QPointF(u,-v),QPointF(u,v),
                                     QPointF(-u,v),QPointF(-u,-v)};
    std::vector<Vec3> out;
    for(int k=0;k<4;++k) {
        const auto* ch=static_cast<const PlateSpec::Chamfer*>(nullptr);
        for(const auto& cc:p.chamfers)if(cc.corner==k){ch=&cc;break;}
        const auto localPoint=[&](QPointF pt) {
            return sum(p.center,mul(p.u,pt.x()),
                       plus(mul(p.v,pt.y()),mul(p.axis,side*p.thickness*0.5)));
        };
        if(ch && ch->h>0.0 && ch->v>0.0) {
            // Chamfer legs are literal mm, never 50% clipped in this preview.
            const QPointF towardPrev=prev[k]-c[k], towardNext=next[k]-c[k];
            auto along=[](QPointF dir,double dist) {
                const double l=std::hypot(dir.x(),dir.y());
                return l>0?dir*(dist/l):QPointF{};
            };
            const double prevDistance=(std::fabs(towardPrev.x())>0)?ch->h:ch->v;
            const double nextDistance=(std::fabs(towardNext.x())>0)?ch->h:ch->v;
            out.push_back(localPoint(c[k]+along(towardPrev,prevDistance)));
            out.push_back(localPoint(c[k]+along(towardNext,nextDistance)));
        } else out.push_back(localPoint(c[k]));
    }
    return out;
}
// Tekla Haunch (40) stiffeners: role-key prefix -> create flag.
const std::array<std::pair<const char*,const wchar_t*>,6> kStiffeners{{
    {"haunch_beam_stiffener",L"createBeamStiffener"},
    {"haunch_stiff_additional",L"createAddStiffener"},
    {"haunch_stiff_rafter_bottom",L"createRafterBottomStiffener"},
    {"haunch_stiff_compression",L"createComprStiffener"},
    {"haunch_stiff_morris",L"createMorrisStiffener"},
    {"haunch_stiff_diagonal",L"createDiagStiffener"}}};
const wchar_t* stiffenerFlagFor(const std::string& role) {
    for(const auto& [prefix,flag]:kStiffeners)
        if(role.compare(0,std::strlen(prefix),prefix)==0)return flag;
    return nullptr;
}
QColor plateColor(const std::string& role) {
    if(stiffenerFlagFor(role))return QColor("#7fc48f");
    if(role.find("end_plate")!=std::string::npos)return QColor("#7ea7c7");
    if(role.find("web")!=std::string::npos)return QColor("#b8a1d6");
    if(role.find("flange")!=std::string::npos)return QColor("#e5a461");
    if(role.find("top_plate")!=std::string::npos)return QColor("#73b6a8");
    return QColor("#a5b5cb");
}
void addPrism(std::vector<Poly>& polys,
              const std::vector<Vec3>& front,const std::vector<Vec3>& back,
              QColor color,const std::string& role) {
    if(front.size()<3 || front.size()!=back.size())return;
    polys.push_back({back,color.darker(135),role,0.0});
    for(std::size_t i=0;i<front.size();++i) {
        const auto j=(i+1)%front.size();
        polys.push_back({{front[i],front[j],back[j],back[i]},
                         (i%2)?color.darker(116):color.darker(130),role,0.0});
    }
    polys.push_back({front,color,role,0.0});
}
std::vector<Vec3> sectionI(Vec3 center,Vec3 u,Vec3 v,
                           double b,double h,double tw,double tf) {
    b=std::max(10.0,b);h=std::max(10.0,h);
    tw=std::clamp(tw,1.0,b);tf=std::clamp(tf,1.0,h*0.49);
    const double B=b*0.5,H=h*0.5,T=tw*0.5;
    const std::array<QPointF,12> outline={
        QPointF(-B,-H),QPointF(B,-H),QPointF(B,-H+tf),
        QPointF(T,-H+tf),QPointF(T,H-tf),QPointF(B,H-tf),
        QPointF(B,H),QPointF(-B,H),QPointF(-B,H-tf),
        QPointF(-T,H-tf),QPointF(-T,-H+tf),QPointF(-B,-H+tf)
    };
    std::vector<Vec3> result;
    for(const auto& p:outline)result.push_back(sum(center,mul(u,p.x()),mul(v,p.y())));
    return result;
}
void addMember(std::vector<Poly>& polys,const MemberGeometry& m,
               const std::vector<Vec3>& featurePoints,
               const std::vector<CutModifier>& cuts,
               QColor col,const std::string& role) {
    Vec3 axis=unit(minus(m.axisTo,m.axisFrom));
    Vec3 h=minus(m.refNormal,mul(axis,dot(m.refNormal,axis)));
    if(length(h)<0.01)h=minus(Vec3{0,0,1},mul(axis,axis.z));
    if(length(h)<0.01)h=Vec3{0,1,0};
    h=unit(h);
    const Vec3 w=unit(cross(axis,h));
    const double total=length(minus(m.axisTo,m.axisFrom));
    if(total<1)return;
    // P5: keep the entire parent steel context spanning ALL attached child
    // features. A column cap high above a haunch must NEVER float alone
    // because the preview truncated the support around just the end plate.
    double lower=total, upper=0.0;
    for(const Vec3 pt:featurePoints) {
        const double t=dot(minus(pt,m.axisFrom),axis);
        lower=std::min(lower,t-180.0);
        upper=std::max(upper,t+180.0);
    }
    if(featurePoints.empty()){lower=0;upper=total;}
    lower=std::clamp(lower,0.0,total);
    upper=std::clamp(upper,lower,total);
    // P6: visually mirror persistent generated PlaneCut. A source beam
    // overlapping the support must appear TRIMMED at the current end-plate
    // face, not through the cap/column.
    for(const auto& c:cuts) {
        if(c.target!=m.id || c.kind!=CutKind::PlaneCut)continue;
        const double den=dot(c.planeNormal,axis);
        if(std::fabs(den)<1e-9)continue;
        const double t=dot(minus(c.planeOrigin,m.axisFrom),c.planeNormal)/den;
        if(c.keepPositive) {
            if(den>0) lower=std::max(lower,t);
            else upper=std::min(upper,t);
        } else {
            if(den>0)upper=std::min(upper,t);
            else lower=std::max(lower,t);
        }
    }
    lower=std::clamp(lower,0.0,total);
    upper=std::clamp(upper,0.0,total);
    if(upper-lower<1.0)return;
    auto sectionAt=[&](double t){
        Vec3 centre=plus(m.axisFrom,mul(axis,t));
        centre=plus(centre,mul(h,m.sectionCenterOffset));
        return sectionI(centre,w,h,m.secWidth,m.secHeight,
                        m.secWebThickness>0?m.secWebThickness:8.0,
                        m.secFlangeThickness>0?m.secFlangeThickness:12.0);
    };
    addPrism(polys,sectionAt(upper),sectionAt(lower),col,role);
}

double numericParam(const std::vector<CompParam>& values,
                    const std::wstring& name,double fallback) {
    if(const auto* p=findParam(values,name)) {
        if(const auto* v=std::get_if<double>(&p->value))return *v;
        if(const auto* v=std::get_if<int>(&p->value))return *v;
    }
    return fallback;
}
} // namespace

ComponentLivePreviewWidget::ComponentLivePreviewWidget(QWidget* parent)
    :QWidget(parent) {
    setMinimumSize(460,380);
    setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Expanding);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setToolTip(QStringLiteral("Sol tus: dondur | Orta tus: kaydir | Tekerlek: yakinlas | Alt+tik: berkitme ac/kapat"));
    // Short labels correspond to the SAME native schema parameters as the
    // real property panel. No second parameter source of truth.
    // Grouped by the physical child that the user can click.
    // Seven controls PER independent group; only the relevant vertical
    // clearance is editable in each role. No phantom opposite-edge control.
    const std::array<std::tuple<const wchar_t*,const wchar_t*,const char*,QPointF>,25> defs{{
        {L"haunchLength",L"L","haunch_web",QPointF(35,-90)},
        {L"haunchDepth",L"D","haunch_web",QPointF(-150,15)},
        {L"haunchTipDepth",L"tip","haunch_web",QPointF(45,75)},
        {L"haunchWebThickness",L"t web","haunch_web",QPointF(-150,105)},
        {L"haunchFlangeThickness",L"t alt","haunch_lower_flange",QPointF(45,-85)},
        {L"haunchFlangeWidth",L"b alt","haunch_lower_flange",QPointF(60,65)},
        {L"endPlateThickness",L"t ep","haunch_end_plate",QPointF(-150,-95)},
        {L"endPlateWidth",L"b ep","haunch_end_plate",QPointF(55,-95)},
        {L"endPlateHeight",L"h ep","haunch_end_plate",QPointF(-160,90)},
        {L"upperBoltCountX",L"nx","haunch_bolts_upper",QPointF(-150,104)},
        {L"upperBoltCountY",L"ny","haunch_bolts_upper",QPointF(55,104)},
        {L"upperBoltPitchX",L"g","haunch_bolts_upper",QPointF(-150,55)},
        {L"upperBoltPitchY",L"p","haunch_bolts_upper",QPointF(55,55)},
        {L"upperBoltDiameter",L"M","haunch_bolts_upper",QPointF(50,-75)},
        {L"upperHoleDiameter",L"delik","haunch_bolts_upper",QPointF(50,-35)},
        {L"upperBoltEdgeClear",L"eT net","haunch_bolts_upper",QPointF(-150,145)},
        {L"lowerBoltCountX",L"nx","haunch_bolts_lower",QPointF(-150,104)},
        {L"lowerBoltCountY",L"ny","haunch_bolts_lower",QPointF(55,104)},
        {L"lowerBoltPitchX",L"g","haunch_bolts_lower",QPointF(-150,55)},
        {L"lowerBoltPitchY",L"p","haunch_bolts_lower",QPointF(55,55)},
        {L"lowerBoltDiameter",L"M","haunch_bolts_lower",QPointF(50,-75)},
        {L"lowerHoleDiameter",L"delik","haunch_bolts_lower",QPointF(50,-35)},
        {L"lowerBoltEdgeClear",L"eB net","haunch_bolts_lower",QPointF(55,145)},
        {L"stiffenerThickness",L"t stiff","haunch_beam_stiffener",QPointF(75,35)},
        {L"topPlateThickness",L"t cap","haunch_top_plate",QPointF(60,-70)}
    }};
    for(const auto& def:defs) {
        Callout a;
        a.name=std::get<0>(def);
        a.shortLabel=std::get<1>(def);
        a.targetRole=std::get<2>(def);
        a.shift=std::get<3>(def);
        a.spin=new QDoubleSpinBox(this);
        const bool integer=a.name.find(L"BoltCount")==5;
        a.spin->setDecimals(integer?0:1);
        a.spin->setRange(integer?1.0:0.0,integer?4.0:10000.0);
        a.spin->setSingleStep(integer?1.0:5.0);
        if(!integer)a.spin->setSuffix(QStringLiteral(" mm"));
        a.spin->setFixedWidth(96);
        a.spin->setToolTip(QString::fromStdWString(a.name));
        a.spin->setStyleSheet(QStringLiteral(
            "QDoubleSpinBox{background:#ffffff;border:1px solid #93a5b9;"
            "border-radius:5px;padding:3px;color:#1c3146;font-weight:600;}"));
        const std::wstring key=a.name;
        QObject::connect(a.spin,QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                         this,[this,key](double val) {
            const bool integer=key.find(L"BoltCount")==5;
            const CompParamValue next=integer
                ?CompParamValue{static_cast<int>(std::lround(val))}
                :CompParamValue{val};
            for(auto& p:params_)if(p.name==key){p.value=next;break;}
            if(!findParam(params_,key))params_.push_back(CompParam{key,next});
            highlight_=key;
            if(onEdit_) {
                // The containing dialog enforces coupled limits first,
                // then broadcasts ONE normalized parameter set to all views.
                onEdit_(key,val);
            } else {
                recompute();
            }
        });
        callouts_.push_back(std::move(a));
    }
    syncEditors();
}
// P4: role selection drives only the current child element's editors.
void ComponentLivePreviewWidget::selectRole(const std::string& role) {
    if(compact_)return;
    bool exists=role.empty() || role=="column" || role=="beam";
    for(const auto& key:plan_.plateKeys) if(role==key)exists=true;
    for(const auto& key:plan_.boltGroupKeys) if(role==key)exists=true;
    if(!exists)return;
    if(activeRole_==role)return;
    activeRole_=role;
    highlight_.clear();
    // No hidden objects: reframe selected feature but preserve whole support.
    if(!activeRole_.empty() && activeRole_!="column" && activeRole_!="beam")
        centerSelection();
    positionEditors();
    update();
}
std::vector<std::wstring> ComponentLivePreviewWidget::visibleOverlayParams() const {
    std::vector<std::wstring> names;
    for(const auto& c:callouts_)
        if(c.spin->isVisible() && !compact_)
            names.push_back(c.name);
    return names;
}
QPointF ComponentLivePreviewWidget::roleScreenAnchor(const std::string& role) const {
    return project(roleAnchor(role));
}
void ComponentLivePreviewWidget::centerSelection() {
    // Clicking a VISIBLE child must not repeatedly walk the whole model
    // sideways. Only nudge the camera when a programmatically selected
    // child is near/outside the viewport edge; preserve full parent context.
    const QPointF screen=project(roleAnchor(activeRole_));
    const double mx=width()*0.18, my=height()*0.18;
    double dx=0.0,dy=0.0;
    if(screen.x()<mx)dx=mx-screen.x();
    else if(screen.x()>width()-mx)dx=width()-mx-screen.x();
    if(screen.y()<my)dy=my-screen.y();
    else if(screen.y()>height()-my)dy=height()-my-screen.y();
    pan_+=QPointF(std::clamp(dx,-width()*0.15,width()*0.15),
                  std::clamp(dy,-height()*0.12,height()*0.12));
}
std::string ComponentLivePreviewWidget::hitRole(QPoint mouse) const {
    // Bolt heads are thin lines: a proximity test makes them clickable.
    double best=11.0*11.0;
    std::string bolt;
    for(const auto& [pt,role]:boltHits_) {
        const QPointF d=pt-QPointF(mouse);
        const double sq=d.x()*d.x()+d.y()*d.y();
        if(sq<best){best=sq;bolt=role;}
    }
    if(!bolt.empty())return bolt;
    // Depth-sorted faces were painted back-to-front. The last matching
    // component plate is the visible one; support member is fallback only.
    std::string context;
    for(auto it=hits_.rbegin();it!=hits_.rend();++it) {
        if(!it->polygon.containsPoint(QPointF(mouse),Qt::OddEvenFill))continue;
        if(it->role!="column" && it->role!="beam")return it->role;
        if(context.empty())context=it->role;
    }
    return context;
}

void ComponentLivePreviewWidget::setInput(ComponentInput input) {
    // A different pair of members starts a new editing context. Edits to
    // the SAME pair preserve role selection and user-controlled camera.
    const bool differentMembers=haveInput_ &&
        (input_.main.id!=input.main.id ||
         input_.secondary.size()!=input.secondary.size() ||
         (!input_.secondary.empty() && !input.secondary.empty() &&
          input_.secondary.front().id!=input.secondary.front().id));
    if(differentMembers) {
        activeRole_.clear();
        highlight_.clear();
        pan_=QPointF{};
        zoom_=1.0;
        // Last valid geometry from another selected pair is not a valid
        // fallback for this pair: do not display an unrelated connection.
        plan_=GenerationPlan{};
        plan_.ok=false;
        previewWarning_.clear();
    }
    input_=std::move(input);
    haveInput_=input_.main.id!=kInvalidEntityId && !input_.secondary.empty();
    recompute();
}
void ComponentLivePreviewWidget::setParams(const std::vector<CompParam>& values) {
    params_=values;
    syncEditors();
    recompute();
}
void ComponentLivePreviewWidget::setHighlightedParam(const std::wstring& n) {
    highlight_=n;
    update();
}
void ComponentLivePreviewWidget::setOnEdit(std::function<void(const std::wstring&,double)> cb) {
    onEdit_=std::move(cb);
}
void ComponentLivePreviewWidget::setView(const std::string& name) {
    viewName_=name;
    if(name=="front"){yaw_=0.0;pitch_=0.0;}
    else if(name=="side"){yaw_=kPi*0.5;pitch_=0.0;}
    else if(name=="top"){yaw_=0.0;pitch_=kPi*0.5;}
    else {yaw_=0.67;pitch_=0.48;}
    pan_=QPointF{};zoom_=1.0;
    positionEditors();update();
}
void ComponentLivePreviewWidget::setCompact(bool compact) {
    compact_=compact;
    if(compact_){
        setMinimumSize(130,75);
        setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Fixed);
        for(auto& c:callouts_)c.spin->hide();
    }
    positionEditors();
    update();
}
void ComponentLivePreviewWidget::setOnPick(std::function<void()> cb) {
    onPick_=std::move(cb);
}
void ComponentLivePreviewWidget::setOnToggle(
        std::function<void(const std::wstring&,bool)> cb) {
    onToggle_=std::move(cb);
}
std::vector<std::string> ComponentLivePreviewWidget::ghostStiffenerRoles() const {
    std::vector<std::string> roles;
    for(const auto& g:ghosts_)roles.push_back(g.role);
    return roles;
}
QPointF ComponentLivePreviewWidget::ghostScreenAnchor(const std::string& role) const {
    for(const auto& g:ghosts_)
        if(g.role==role)return project(g.plate.center);
    return QPointF(-1.0,-1.0);
}
std::string ComponentLivePreviewWidget::stiffenerRoleAt(QPoint pos) const {
    // Paint order: the last matching polygon is the visible one.
    for(auto it=stiffHits_.rbegin();it!=stiffHits_.rend();++it)
        if(it->polygon.containsPoint(QPointF(pos),Qt::OddEvenFill))return it->role;
    return {};
}
bool ComponentLivePreviewWidget::toggleStiffenerAt(QPoint pos) {
    const std::string role=stiffenerRoleAt(pos);
    const wchar_t* flag=role.empty()?nullptr:stiffenerFlagFor(role);
    if(!flag)return false;
    const bool next=!paramAsBool(params_,flag,false);
    if(onToggle_) {
        // The owning dialog flips the real checkbox, which re-broadcasts the
        // normalized parameter set to every view (including this one).
        onToggle_(flag,next);
    } else {
        bool found=false;
        for(auto& param:params_)if(param.name==flag){param.value=next;found=true;}
        if(!found)params_.push_back(CompParam{flag,CompParamValue{next}});
        recompute();
    }
    return true;
}
void ComponentLivePreviewWidget::syncEditors() {
    const ComponentType* type=ComponentRegistry::instance().findType(L"tekla_haunch_40");
    for(auto& c:callouts_) {
        const CompParamSpec* spec=type?findParamSpec(type->params,c.name):nullptr;
        const bool integer=spec && spec->type==CompParamType::Integer;
        const double fallback=spec
            ?(integer?static_cast<double>(spec->defInt):spec->defDouble):0.0;
        const double value=numericParam(params_,c.name,fallback);
        QSignalBlocker block(c.spin);
        c.spin->setRange(
            integer?(spec->hasMinInt?spec->minInt:1):
                    (spec&&spec->hasMinDouble?spec->minDouble:0.0),
            integer?(spec->hasMaxInt?spec->maxInt:4):
                    (spec&&spec->hasMaxDouble?spec->maxDouble:10000.0));
        c.spin->setValue(value);
    }
}
void ComponentLivePreviewWidget::recompute() {
    if(!haveInput_) {
        plan_.ok=false;plan_.error=L"Modelde bagli kolon ve kiris bulunamadi";
        previewWarning_.clear();
        for(auto& c:callouts_)c.spin->hide();
        update();return;
    }
    input_.params=params_;
    GenerationPlan next=ComponentRegistry::instance().generate(L"tekla_haunch_40",input_);
    if(next.ok) {
        plan_=std::move(next);
        previewWarning_.clear();
    } else if(plan_.ok && !plan_.plates.empty()) {
        // Preserve the last *valid* geometry while indicating clearly that
        // unsaved draft dimensions were NOT applied. Document is untouched.
        previewWarning_=next.error;
    } else {
        plan_=std::move(next);
        previewWarning_.clear();
    }
    // Stiffeners that are OFF: generate each flag alone with the SAME draft
    // parameters so the grey ghost is the exact geometry Alt+click would add.
    ghosts_.clear();
    if(plan_.ok && previewWarning_.empty()) {
        for(const auto& [prefix,flag]:kStiffeners) {
            if(paramAsBool(params_,flag,false))continue;
            ComponentInput probe=input_;
            probe.params=params_;
            bool found=false;
            for(auto& param:probe.params)
                if(param.name==flag){param.value=true;found=true;}
            if(!found)probe.params.push_back(CompParam{flag,CompParamValue{true}});
            const GenerationPlan g=ComponentRegistry::instance().generate(
                L"tekla_haunch_40",probe);
            if(!g.ok)continue;
            for(std::size_t i=0;i<g.plates.size()&&i<g.plateKeys.size();++i)
                if(g.plateKeys[i].compare(0,std::strlen(prefix),prefix)==0)
                    ghosts_.push_back({g.plates[i],g.plateKeys[i]});
        }
    }
    // An optional selected child (e.g. a disabled stiffener) may cease
    // to exist after parameter edits. Clear its selection rather than
    // fading every remaining part against an invisible phantom child.
    if(plan_.ok && !activeRole_.empty() &&
       activeRole_!="column" && activeRole_!="beam") {
        bool exists=false;
        for(const auto& name:plan_.plateKeys)exists|=(name==activeRole_);
        for(const auto& name:plan_.boltGroupKeys)exists|=(name==activeRole_);
        if(!exists){activeRole_.clear();highlight_.clear();}
    }
    // Incompatible values may make generation invalid. Show error, never
    // invent fallback geometry or silently mutate the real Document.
    if(plan_.ok && !plan_.plates.empty()) {
        Vec3 c{};
        for(const auto& plate:plan_.plates)c=plus(c,plate.center);
        worldCenter_=mul(c,1.0/static_cast<double>(plan_.plates.size()));
    }
    positionEditors();update();
}
QPointF ComponentLivePreviewWidget::project(Vec3 p) const {
    const Vec3 d=minus(p,worldCenter_);
    const Vec3 r{std::cos(yaw_),std::sin(yaw_),0.0};
    const Vec3 up{-std::sin(yaw_)*std::sin(pitch_),
                   std::cos(yaw_)*std::sin(pitch_),std::cos(pitch_)};
    const double sc=std::max(0.001,modelScale_*zoom_);
    return QPointF(width()*0.50+pan_.x()+dot(d,r)*sc,
                   height()*0.52+pan_.y()-dot(d,up)*sc);
}
Vec3 ComponentLivePreviewWidget::roleAnchor(const std::string& role) const {
    for(std::size_t i=0;i<plan_.plates.size();++i) {
        if(i<plan_.plateKeys.size()&&plan_.plateKeys[i]==role)
            return plan_.plates[i].center;
    }
    for(std::size_t i=0;i<plan_.boltGroups.size();++i) {
        if(i<plan_.boltGroupKeys.size()&&plan_.boltGroupKeys[i]==role) {
            const auto points=boltGroupBoltOrigins(plan_.boltGroups[i]);
            if(points.empty())return plan_.boltGroups[i].origin;
            Vec3 c{};
            for(const auto& pt:points)c=plus(c,pt);
            return mul(c,1.0/points.size());
        }
    }
    return worldCenter_;
}

ComponentLivePreviewWidget::BoltOverlayMetrics
ComponentLivePreviewWidget::boltOverlayMetrics(const std::string& role) const {
    BoltOverlayMetrics m;
    if(!plan_.ok || role.empty())return m;
    for(const auto& group:plan_.boltGroups)
        m.totalCount+=static_cast<int>(boltGroupBoltOrigins(group).size());
    for(std::size_t i=0;i<plan_.boltGroups.size();++i) {
        if(i>=plan_.boltGroupKeys.size() || plan_.boltGroupKeys[i]!=role)
            continue;
        const auto& group=plan_.boltGroups[i];
        const PlateSpec* plate=nullptr;
        for(std::size_t j=0;j<plan_.plates.size();++j) {
            if(j<plan_.plateKeys.size() &&
               plan_.plateKeys[j]==group.targetOwnedPlateRole) {
                plate=&plan_.plates[j];
                break;
            }
        }
        const auto positions=boltGroupBoltOrigins(group);
        if(!plate || positions.empty() || plate->width<=0.0 ||
           plate->height<=0.0)return m;

        double xmin=std::numeric_limits<double>::max(), xmax=-xmin;
        double ymin=xmin, ymax=-xmin;
        for(const Vec3& pt:positions) {
            const Vec3 d=minus(pt,plate->center);
            const double x=dot(d,plate->u),y=dot(d,plate->v);
            xmin=std::min(xmin,x);xmax=std::max(xmax,x);
            ymin=std::min(ymin,y);ymax=std::max(ymax,y);
        }
        m.valid=true;
        m.count=static_cast<int>(positions.size());
        m.columns=group.countX;
        m.rows=group.countY;
        m.nominalDiameter=group.params.nominalDiameter;
        m.holeDiameter=group.params.holeDiameter;
        m.xmin=xmin;m.xmax=xmax;m.ymin=ymin;m.ymax=ymax;
        m.pitchX=m.columns>1?(xmax-xmin)/(m.columns-1):0.0;
        m.pitchY=m.rows>1?(ymax-ymin)/(m.rows-1):0.0;
        // These are centre-to-edge distances, not hole-edge clearances.
        m.leftEdge=plate->width*0.5+xmin;
        m.rightEdge=plate->width*0.5-xmax;
        m.bottomEdge=plate->height*0.5+ymin;
        m.topEdge=plate->height*0.5-ymax;
        m.plateWidth=plate->width;m.plateHeight=plate->height;
        m.plateThickness=plate->thickness;
        m.plateCenter=plate->center;
        m.plateU=plate->u;m.plateV=plate->v;m.plateAxis=plate->axis;
        return m;
    }
    return m;
}
void ComponentLivePreviewWidget::positionEditors() {
    if(!plan_.ok) {
        for(auto& c:callouts_)c.spin->hide();
        return;
    }
    // Fit true generator output, not a generic fixed illustration. Column cap
    // can be at a different elevation from the connection: include ALL plates.
    double xmin=std::numeric_limits<double>::max(),xmax=-xmin;
    double ymin=xmin,ymax=-xmin;
    auto include=[&](Vec3 p) {
        const double dx=p.x-worldCenter_.x;
        const double dy=p.y-worldCenter_.y;
        const double dz=p.z-worldCenter_.z;
        const double xx=std::cos(yaw_)*dx+std::sin(yaw_)*dy;
        const double yy=(-std::sin(yaw_)*std::sin(pitch_))*dx+
                        (std::cos(yaw_)*std::sin(pitch_))*dy+std::cos(pitch_)*dz;
        xmin=std::min(xmin,xx);xmax=std::max(xmax,xx);
        ymin=std::min(ymin,yy);ymax=std::max(ymax,yy);
    };
    for(const auto& plate:plan_.plates) {
        const auto outline=plateOutline(plate,+1.0);
        const auto back=plateOutline(plate,-1.0);
        for(auto a:outline)include(a);
        for(auto a:back)include(a);
    }
    if(xmin<=xmax && ymin<=ymax) {
        const double w=std::max(100.0,xmax-xmin);
        const double h=std::max(100.0,ymax-ymin);
        modelScale_=std::min((width()-(compact_?16.0:205.0))/w,
                             (height()-(compact_?18.0:155.0))/h);
        modelScale_=std::max(0.035,modelScale_);
    }
    if(compact_) {
        for(auto& c:callouts_)c.spin->hide();
        return;
    }
    for(auto& c:callouts_) {
        // Editor cards are ordinary Qt spinboxes (keyboard, accessibility,
        // validation), positioned with model-space anchor leader lines.
        bool exists=false;
        for(const auto& role:plan_.plateKeys)if(role==c.targetRole)exists=true;
        for(const auto& role:plan_.boltGroupKeys)
            if(role==c.targetRole)exists=true;
        // Independent group: only expose the parameters actually owned by
        // the selected upper OR lower bolt group.
        const bool selected=c.targetRole==activeRole_;
        if(!exists || !selected){c.spin->hide();continue;}
        const std::string anchorRole=c.targetRole;
        const bool boltRole=activeRole_=="haunch_bolts_upper" ||
                            activeRole_=="haunch_bolts_lower";
        if(boltRole) {
            const bool upper=activeRole_=="haunch_bolts_upper";
            static const std::array<std::wstring,7> upperKeys{{
                L"upperBoltCountX",L"upperBoltCountY",
                L"upperBoltPitchX",L"upperBoltPitchY",
                L"upperBoltDiameter",L"upperHoleDiameter",
                L"upperBoltEdgeClear"
            }};
            static const std::array<std::wstring,7> lowerKeys{{
                L"lowerBoltCountX",L"lowerBoltCountY",
                L"lowerBoltPitchX",L"lowerBoltPitchY",
                L"lowerBoltDiameter",L"lowerHoleDiameter",
                L"lowerBoltEdgeClear"
            }};
            const auto& order=upper?upperKeys:lowerKeys;
            const auto it=std::find(order.begin(),order.end(),c.name);
            if(it!=order.end()) {
                const int index=static_cast<int>(it-order.begin());
                c.spin->move(8+(index%2)*108,85+(index/2)*52);
            }
        } else {
            const QPointF q=project(roleAnchor(anchorRole))+c.shift;
            const int xx=std::clamp(static_cast<int>(q.x()),4,
                                    std::max(4,width()-c.spin->width()-4));
            const int yy=std::clamp(static_cast<int>(q.y()),26,
                                    std::max(26,height()-c.spin->height()-8));
            c.spin->move(xx,yy);
        }
        c.spin->show();
        c.spin->raise();
    }
}
void ComponentLivePreviewWidget::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(),QColor("#edf2f8"));
    QLinearGradient bg(0,0,width(),height());
    bg.setColorAt(0,QColor("#f6f9fd"));
    bg.setColorAt(1,QColor("#dce5f1"));
    painter.fillRect(rect(),QBrush(bg));
    if(!plan_.ok) {
        painter.setPen(QColor("#a73c3c"));
        painter.drawText(rect().adjusted(24,40,-24,-30),
                         Qt::AlignCenter|Qt::TextWordWrap,
                         plan_.error.empty()?QStringLiteral("3D preview kullanilamiyor"):
                                             QString::fromStdWString(plan_.error));
        return;
    }
    drawModel(painter);
    if(!previewWarning_.empty()) {
        painter.setPen(QColor("#92400e"));
        painter.setBrush(QColor("#fff2db"));
        const QRect warningRect(10,height()-(compact_?36:72),
                                width()-20,compact_?28:48);
        painter.drawRoundedRect(warningRect,5.0,5.0);
        const QString label=QStringLiteral("Son gecerli geometri (yeni olculer uygulanmadi): ")
                             +QString::fromStdWString(previewWarning_);
        painter.drawText(warningRect.adjusted(7,2,-7,-2),
                         Qt::AlignCenter|Qt::TextWordWrap,label);
    }
    painter.setPen(QColor("#344c64"));
    if(compact_) {
        QFont mini=painter.font();
        mini.setBold(true);mini.setPointSize(9);painter.setFont(mini);
        const QString label=viewName_=="front"?QStringLiteral("ON"):
                            viewName_=="side"?QStringLiteral("YAN"):
                            viewName_=="top"?QStringLiteral("UST"):
                                               QStringLiteral("3D");
        painter.drawText(QPoint(8,15),label);
        return;
    }
    QFont title=painter.font();title.setBold(true);title.setPointSize(12);
    painter.setFont(title);
    painter.drawText(QPoint(16,26),QStringLiteral("HAUNCH 40  ·  NATIVE 3D"));
    title.setBold(false);title.setPointSize(9);painter.setFont(title);
    painter.setPen(QColor("#63758b"));
    painter.drawText(QPoint(16,height()-14),
        QStringLiteral("Parca sec: tikla   •   Don: surukle   •   Kaydir: orta tus   •   Zoom: tekerlek"));
    if(!ghosts_.empty()) {
        painter.setPen(QColor("#6b7a8c"));
        painter.drawText(QPoint(16,height()-30),
            QStringLiteral("Gri = pasif berkitme   •   Alt+tik: ac / kapat"));
    }
    painter.setPen(QColor("#4d687f"));
    const QString partName=activeRole_.empty()
        ?QStringLiteral("Olcu icin bir parca veya civata grubu sec")
        :QStringLiteral("Secili: %1").arg(QString::fromStdString(activeRole_));
    painter.drawText(QPoint(16,48),partName);
    for(const auto& c:callouts_) {
        if(!c.spin->isVisible())continue;
        const std::string anchorRole=(activeRole_=="haunch_bolts_lower" &&
                                      c.targetRole=="haunch_bolts_upper")
                                       ?activeRole_:c.targetRole;
        const bool boltInspector=(activeRole_=="haunch_bolts_upper" ||
                                  activeRole_=="haunch_bolts_lower");
        if(!boltInspector) {
            const QPointF target=project(roleAnchor(anchorRole));
            const QPointF end=c.spin->geometry().center();
            painter.setPen(QPen(highlight_==c.name?QColor("#d47b2c"):QColor("#8295a8"),
                                highlight_==c.name?2.2:1.3));
            painter.drawLine(target,end);
            painter.setBrush(QColor(highlight_==c.name?"#e38b32":"#ffffff"));
            painter.drawEllipse(target,3.8,3.8);
        }
        // In bolt inspector mode the dimension lines already point to
        // the physical holes. Ten extra leader lines would obscure them.
        painter.setPen(QColor("#36516b"));
        painter.drawText(QPointF(c.spin->x()+3,c.spin->y()-4),
                         QString::fromStdWString(c.shortLabel));
    }
    const auto boltMetrics=boltOverlayMetrics(activeRole_);
    if(boltMetrics.valid)drawBoltOverlay(painter,boltMetrics);
}

void ComponentLivePreviewWidget::drawBoltOverlay(
    QPainter& p, const BoltOverlayMetrics& m) {
    p.save();
    QFont dimFont=p.font();
    dimFont.setPointSizeF(8.0);
    dimFont.setBold(true);
    p.setFont(dimFont);

    const auto point=[&](double u,double v) {
        // Slightly in front of the real end-plate face; measured endpoints
        // remain tied to the generator's actual plate and bolt coordinates.
        return plus(plus(m.plateCenter,mul(m.plateU,u)),
                    plus(mul(m.plateV,v),
                         mul(m.plateAxis,m.plateThickness*0.5+1.0)));
    };
    const auto dim=[&](Vec3 from,Vec3 to,const QString& label,
                      QPointF offset) {
        QPointF a=project(from),b=project(to);
        if(std::hypot(a.x()-b.x(),a.y()-b.y())<5.0)return;
        const QPointF aa=a+offset,bb=b+offset;
        p.setPen(QPen(QColor("#25647c"),1.25));
        p.setBrush(Qt::NoBrush);
        p.drawLine(a,aa);p.drawLine(b,bb);p.drawLine(aa,bb);
        const QPointF direction=bb-aa;
        const double distance=std::hypot(direction.x(),direction.y());
        if(distance>8.0) {
            const QPointF dir=direction/distance;
            const QPointF normal(-dir.y(),dir.x());
            const QPointF ar1=aa+dir*5.0+normal*2.4;
            const QPointF ar2=aa+dir*5.0-normal*2.4;
            const QPointF br1=bb-dir*5.0+normal*2.4;
            const QPointF br2=bb-dir*5.0-normal*2.4;
            p.drawLine(aa,ar1);p.drawLine(aa,ar2);
            p.drawLine(bb,br1);p.drawLine(bb,br2);
        }
        const QPointF middle=(aa+bb)*0.5;
        const int textWidth=p.fontMetrics().horizontalAdvance(label);
        const QRectF labelRect(middle.x()-textWidth*0.5-5.0,
                               middle.y()-9.5,textWidth+10.0,19.0);
        p.setPen(QPen(QColor("#a9c1d0"),0.8));
        p.setBrush(QColor(253,254,255,236));
        p.drawRoundedRect(labelRect,4.0,4.0);
        p.setPen(QColor("#194b65"));
        p.drawText(labelRect,Qt::AlignCenter,label);
    };
    const auto mm=[](double v) {return QString::number(v,'f',1);};
    const double hw=m.plateWidth*0.5,hh=m.plateHeight*0.5;
    // eT belongs to the UPPER outermost bolt row; eB to the LOWER
    // outermost row. These four edited dimensions describe the entire
    // bolt arrangement even if the lower group was the clicked role.
    const auto topGroup=boltOverlayMetrics("haunch_bolts_upper");
    const auto bottomGroup=boltOverlayMetrics("haunch_bolts_lower");
    const double topY=topGroup.valid?topGroup.ymax:m.ymax;
    const double bottomY=bottomGroup.valid?bottomGroup.ymin:m.ymin;
    const double topClear=topGroup.valid?topGroup.topEdge:m.topEdge;
    const double bottomClear=bottomGroup.valid?bottomGroup.bottomEdge:m.bottomEdge;
    // Six dimensions: two centre-to-centre pitches and four actual
    // centre-to-plate-edge clearances for the full bolt assembly.
    if(m.columns>1)
        dim(point(m.xmin,m.ymin),point(m.xmax,m.ymin),
            QStringLiteral("g ")+mm(m.pitchX),QPointF(0,24));
    if(m.rows>1)
        dim(point(m.xmin,m.ymin),point(m.xmin,m.ymax),
            QStringLiteral("p ")+mm(m.pitchY),QPointF(-30,0));
    dim(point(-hw,m.ymin),point(m.xmin,m.ymin),
        QStringLiteral("eL ")+mm(m.leftEdge),QPointF(0,46));
    dim(point(m.xmax,m.ymin),point(hw,m.ymin),
        QStringLiteral("eR ")+mm(m.rightEdge),QPointF(0,46));
    const bool upperSelected=activeRole_=="haunch_bolts_upper";
    if(upperSelected)
        dim(point(m.xmax,topY),point(m.xmax,hh),
            QStringLiteral("eT net ")+mm(topClear-m.holeDiameter*0.5),
            QPointF(28,0));
    else
        dim(point(m.xmin,-hh),point(m.xmin,bottomY),
            QStringLiteral("eB net ")+mm(bottomClear-m.holeDiameter*0.5),
            QPointF(-48,0));

    // Readable summary remains useful when orthographic projection makes
    // a dimension nearly edge-on, or Qt spinboxes cover a leader line.
    const qreal panelW=218.0, panelH=109.0;
    const QRectF panel(std::max(8.0,width()-panelW-12.0),
                       59.0,panelW,panelH);
    p.setPen(QPen(QColor("#a6bdcf"),1.0));
    p.setBrush(QColor(255,255,255,236));
    p.drawRoundedRect(panel,7.0,7.0);
    p.setPen(QColor("#1b3b52"));
    p.drawText(panel.adjusted(9,6,-8,-83),Qt::AlignLeft|Qt::AlignVCenter,
        QStringLiteral("%1 adet / %2 toplam · %3x%4")
            .arg(m.count).arg(m.totalCount).arg(m.columns).arg(m.rows));
    p.setPen(QColor("#2a5570"));
    p.drawText(panel.adjusted(9,30,-8,-59),Qt::AlignLeft|Qt::AlignVCenter,
        QStringLiteral("M%1  /  delik Ø%2")
            .arg(mm(m.nominalDiameter)).arg(mm(m.holeDiameter)));
    p.drawText(panel.adjusted(9,53,-8,-36),Qt::AlignLeft|Qt::AlignVCenter,
        QStringLiteral("g %1   p %2 mm").arg(mm(m.pitchX),mm(m.pitchY)));
    p.drawText(panel.adjusted(9,76,-8,-13),Qt::AlignLeft|Qt::AlignVCenter,
        (upperSelected
            ?QStringLiteral("Ust net kenar: %1 mm")
                 .arg(mm(topClear-m.holeDiameter*0.5))
            :QStringLiteral("Alt net kenar: %1 mm")
                 .arg(mm(bottomClear-m.holeDiameter*0.5))));
    p.restore();
}

void ComponentLivePreviewWidget::drawModel(QPainter& p) {
    hits_.clear();
    boltHits_.clear();
    stiffHits_.clear();
    std::vector<Poly> faces;
    if(!plan_.plates.empty()) {
        std::vector<Vec3> anchors;
        for(const auto& plate:plan_.plates) {
            anchors.push_back(plate.center);
            const auto vertices=plateOutline(plate,+1.0);
            anchors.insert(anchors.end(),vertices.begin(),vertices.end());
        }
        // The native generator resolves column/beam by verticality even
        // when the user picks beam first. The preview must agree with that
        // physical role assignment rather than blindly trusting main/secondary.
        const auto verticality=[](const MemberGeometry& m) {
            const Vec3 d=minus(m.axisTo,m.axisFrom);
            const double n=length(d);
            return n>1e-9?std::fabs(d.z)/n:0.0;
        };
        const bool swapped=!input_.secondary.empty() &&
            verticality(input_.main)+1e-6<verticality(input_.secondary.front());
        addMember(faces,input_.main,anchors,plan_.cuts,
                  swapped?QColor("#c5cdd6"):QColor("#a5b5c5"),
                  swapped?"beam":"column");
        for(std::size_t i=0;i<input_.secondary.size();++i) {
            const bool isColumn=swapped && i==0;
            addMember(faces,input_.secondary[i],anchors,plan_.cuts,
                      isColumn?QColor("#a5b5c5"):QColor("#c5cdd6"),
                      isColumn?"column":"beam");
        }
    }
    for(std::size_t i=0;i<plan_.plates.size();++i) {
        const auto& plate=plan_.plates[i];
        const std::string role=i<plan_.plateKeys.size()?plan_.plateKeys[i]:"plate";
        addPrism(faces,plateOutline(plate,+1.0),plateOutline(plate,-1.0),
                 plateColor(role),role);
    }
    for(const auto& g:ghosts_)
        addPrism(faces,plateOutline(g.plate,+1.0),plateOutline(g.plate,-1.0),
                 QColor("#9aa3ad"),"ghost:"+g.role);
    const Vec3 toward{std::cos(pitch_)*std::sin(yaw_),
                      -std::cos(pitch_)*std::cos(yaw_),std::sin(pitch_)};
    for(auto& face:faces) {
        Vec3 centre{};
        for(const auto& v:face.vertices)centre=plus(centre,v);
        face.depth=dot(mul(centre,1.0/face.vertices.size()),toward);
    }
    std::sort(faces.begin(),faces.end(),[](const Poly& a,const Poly& b){
        return a.depth<b.depth;
    });
    for(const auto& face:faces) {
        if(face.vertices.size()<3)continue;
        QPolygonF polygon;
        for(const auto& v:face.vertices)polygon<<project(v);
        if(face.role.compare(0,6,"ghost:")==0) {
            // Disabled stiffener: translucent grey, dashed outline, no
            // selection/fade logic. Alt+click turns it on.
            p.setBrush(QColor(150,160,172,80));
            p.setPen(QPen(QColor("#8794a3"),0.9,Qt::DashLine));
            p.drawPolygon(polygon);
            if(!compact_)stiffHits_.push_back({polygon,face.role.substr(6)});
            continue;
        }
        QColor fill=face.face;
        const bool isSupport=face.role=="column"||face.role=="beam";
        if(!activeRole_.empty() && activeRole_!="column" &&
           activeRole_!="beam") {
            if(isSupport)fill.setAlpha(95);     // P5 keep parent context
            else if(face.role!=activeRole_)fill.setAlpha(122);
            else fill=fill.lighter(125);       // active child in focus
        } else if(isSupport)fill.setAlpha(185);
        if(!highlight_.empty()) {
            const bool sel=(highlight_==L"haunchLength"||
                            highlight_==L"haunchDepth"||
                            highlight_==L"haunchTipDepth")
                              ?(face.role=="haunch_web"||face.role=="haunch_lower_flange")
                              :(highlight_==L"topPlateThickness")
                                ?face.role=="haunch_top_plate"
                                :(highlight_==L"endPlateThickness"||
                                  highlight_==L"endPlateWidth"||
                                  highlight_==L"endPlateHeight"||
                                  highlight_==L"boltPitchX"||
                                  highlight_==L"boltPitchY"||
                                  highlight_==L"boltDiameter")
                                  ?face.role=="haunch_end_plate":false;
            if(sel)fill=fill.lighter(122);
        }
        p.setBrush(fill);
        const bool selected=face.role==activeRole_;
        p.setPen(QPen(selected?QColor("#b86911"):QColor("#455469"),
                      selected?2.2:0.8));
        p.drawPolygon(polygon);
        if(!compact_) {
            hits_.push_back({polygon,face.role});
            if(stiffenerFlagFor(face.role))stiffHits_.push_back({polygon,face.role});
        }
    }
    // Individual bolt groups: preserve exact generator positions, and make
    // the heads hit-testable even though they occupy few screen pixels.
    for(std::size_t gi=0;gi<plan_.boltGroups.size();++gi) {
        const auto& group=plan_.boltGroups[gi];
        const std::string role=gi<plan_.boltGroupKeys.size()
                                   ?plan_.boltGroupKeys[gi]:"bolts";
        const bool selected=role==activeRole_;
        QColor shaft=selected?QColor("#b86911"):QColor("#574f47");
        if(!activeRole_.empty()&&!selected)shaft.setAlpha(145);
        p.setPen(QPen(shaft,selected?3.0:2.0));
        for(const Vec3 pt:boltGroupBoltOrigins(group)) {
            const Vec3 a=plus(pt,mul(group.axis,-0.5*group.gripLength));
            const Vec3 b=plus(pt,mul(group.axis,+0.5*group.gripLength));
            p.drawLine(project(a),project(b));
            p.setBrush(selected?QColor("#f6a642"):QColor("#f4c65d"));
            p.drawEllipse(project(b),selected?6.0:4.0,selected?6.0:4.0);
            p.setBrush(QColor("#a77a47"));
            p.drawEllipse(project(a),3.0,3.0);
            if(!compact_){
                boltHits_.push_back({project(b),role});
                boltHits_.push_back({project(a),role});
            }
        }
    }
}
void ComponentLivePreviewWidget::resizeEvent(QResizeEvent* e) {
    QWidget::resizeEvent(e);
    positionEditors();
}
void ComponentLivePreviewWidget::mousePressEvent(QMouseEvent* e) {
    if(compact_) {
        if(e->button()==Qt::LeftButton && onPick_)onPick_();
        e->accept();return;
    }
    lastMouse_=e->pos();
    mouseDown_=e->pos();
    dragging_=false;
    // Alt+left is the stiffener on/off gesture: no orbit, no role selection.
    altPress_=e->button()==Qt::LeftButton &&
              (e->modifiers()&Qt::AltModifier);
    orbiting_=e->button()==Qt::LeftButton && !altPress_;
    panning_=e->button()==Qt::MiddleButton;
    e->accept();
}
void ComponentLivePreviewWidget::mouseMoveEvent(QMouseEvent* e) {
    const QPoint delta=e->pos()-lastMouse_;
    lastMouse_=e->pos();
    if(orbiting_||panning_) {
        if((e->pos()-mouseDown_).manhattanLength()>5)dragging_=true;
    }
    if(!dragging_)return;  // allow normal click even with minor mouse jitter
    if(orbiting_) {
        yaw_+=delta.x()*0.008;
        pitch_=std::clamp(pitch_-delta.y()*0.007,-1.52,1.52);
    } else if(panning_)pan_+=QPointF(delta);
    else return;
    positionEditors();update();e->accept();
}
void ComponentLivePreviewWidget::mouseReleaseEvent(QMouseEvent* e) {
    if(altPress_ && e->button()==Qt::LeftButton) {
        altPress_=false;
        if((e->pos()-mouseDown_).manhattanLength()<=5)toggleStiffenerAt(e->pos());
        e->accept();
        return;
    }
    const bool wasClick=orbiting_&&!dragging_&&e->button()==Qt::LeftButton;
    orbiting_=panning_=false;
    if(wasClick)selectRole(hitRole(e->pos()));
    QWidget::mouseReleaseEvent(e);
}
void ComponentLivePreviewWidget::wheelEvent(QWheelEvent* e) {
    zoom_=std::clamp(zoom_*std::pow(1.12,e->angleDelta().y()/120.0),0.2,8.0);
    positionEditors();update();e->accept();
}
} // namespace mm
