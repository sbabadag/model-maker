#include "model_maker/component_live_preview_widget.hpp"
#include "model_maker/bolt.hpp"
#include "model_maker/component_engine.hpp"

#include <QDoubleSpinBox>
#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QResizeEvent>
#include <QSignalBlocker>
#include <QToolTip>
#include <algorithm>
#include <array>
#include <tuple>
#include <QLinearGradient>
#include <QColor>
#include <cmath>
#include <limits>

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
QColor plateColor(const std::string& role) {
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
               Vec3 focus,QColor col,const std::string& role) {
    Vec3 axis=unit(minus(m.axisTo,m.axisFrom));
    Vec3 h=minus(m.refNormal,mul(axis,dot(m.refNormal,axis)));
    if(length(h)<0.01)h=minus(Vec3{0,0,1},mul(axis,axis.z));
    if(length(h)<0.01)h=Vec3{0,1,0};
    h=unit(h);
    const Vec3 w=unit(cross(axis,h));
    const double total=length(minus(m.axisTo,m.axisFrom));
    if(total<1)return;
    const double pos=dot(minus(focus,m.axisFrom),axis);
    const double lower=std::clamp(pos-450.0,0.0,total);
    const double upper=std::clamp(pos+620.0,lower,total);
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
    setToolTip(QStringLiteral("Sol tus: dondur | Orta tus: kaydir | Tekerlek: yakinlas"));
    // Short labels correspond to the SAME native schema parameters as the
    // real property panel. No second parameter source of truth.
    const std::array<std::tuple<const wchar_t*,const wchar_t*,const char*,QPointF>,7> defs{{
        {L"haunchLength",L"L","haunch_web",QPointF(50,-75)},
        {L"haunchDepth",L"D","haunch_web",QPointF(-165,40)},
        {L"haunchTipDepth",L"tip","haunch_lower_flange",QPointF(55,60)},
        {L"endPlateThickness",L"t ep","haunch_end_plate",QPointF(-160,-90)},
        {L"boltPitchX",L"g","haunch_end_plate",QPointF(-175,100)},
        {L"boltPitchY",L"p","haunch_end_plate",QPointF(60,105)},
        {L"topPlateThickness",L"t cap","haunch_top_plate",QPointF(65,-75)}
    }};
    for(const auto& def:defs) {
        Callout a;
        a.name=std::get<0>(def);
        a.shortLabel=std::get<1>(def);
        a.targetRole=std::get<2>(def);
        a.shift=std::get<3>(def);
        a.spin=new QDoubleSpinBox(this);
        a.spin->setDecimals(1);
        a.spin->setRange(0.0,10000.0);
        a.spin->setSingleStep(5.0);
        a.spin->setSuffix(QStringLiteral(" mm"));
        a.spin->setFixedWidth(96);
        a.spin->setToolTip(QString::fromStdWString(a.name));
        a.spin->setStyleSheet(QStringLiteral(
            "QDoubleSpinBox{background:#ffffff;border:1px solid #93a5b9;"
            "border-radius:5px;padding:3px;color:#1c3146;font-weight:600;}"));
        const std::wstring key=a.name;
        QObject::connect(a.spin,QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                         this,[this,key](double val) {
            for(auto& p:params_)if(p.name==key){p.value=val;break;}
            if(!findParam(params_,key))params_.push_back(CompParam{key,val});
            highlight_=key;
            recompute();
            if(onEdit_)onEdit_(key,val);
        });
        callouts_.push_back(std::move(a));
    }
    syncEditors();
}
void ComponentLivePreviewWidget::setInput(ComponentInput input) {
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
void ComponentLivePreviewWidget::syncEditors() {
    const ComponentType* type=ComponentRegistry::instance().findType(L"tekla_haunch_40");
    for(auto& c:callouts_) {
        const CompParamSpec* spec=type?findParamSpec(type->params,c.name):nullptr;
        const double value=numericParam(params_,c.name,spec?spec->defDouble:0.0);
        QSignalBlocker block(c.spin);
        c.spin->setRange(spec&&spec->hasMinDouble?spec->minDouble:0.0,
                         spec&&spec->hasMaxDouble?spec->maxDouble:10000.0);
        c.spin->setValue(value);
    }
}
void ComponentLivePreviewWidget::recompute() {
    if(!haveInput_) {
        plan_.ok=false;plan_.error=L"Modelde bagli kolon ve kiris bulunamadi";
        for(auto& c:callouts_)c.spin->hide();
        update();return;
    }
    input_.params=params_;
    plan_=ComponentRegistry::instance().generate(L"tekla_haunch_40",input_);
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
    return worldCenter_;
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
        if(!exists){c.spin->hide();continue;}
        const QPointF q=project(roleAnchor(c.targetRole))+c.shift;
        const int xx=std::clamp(static_cast<int>(q.x()),4,
                                std::max(4,width()-c.spin->width()-4));
        const int yy=std::clamp(static_cast<int>(q.y()),26,
                                std::max(26,height()-c.spin->height()-8));
        c.spin->move(xx,yy);
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
    painter.fillRect(rect(),bg);
    if(!plan_.ok) {
        painter.setPen(QColor("#a73c3c"));
        painter.drawText(rect().adjusted(24,40,-24,-30),
                         Qt::AlignCenter|Qt::TextWordWrap,
                         plan_.error.empty()?QStringLiteral("3D preview kullanilamiyor"):
                                             QString::fromStdWString(plan_.error));
        return;
    }
    drawModel(painter);
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
        QStringLiteral("Drag: orbit   •   Middle-drag: pan   •   Wheel: zoom"));
    for(const auto& c:callouts_) {
        if(!c.spin->isVisible())continue;
        const QPointF target=project(roleAnchor(c.targetRole));
        const QPointF end=c.spin->geometry().center();
        painter.setPen(QPen(highlight_==c.name?QColor("#d47b2c"):QColor("#8295a8"),
                            highlight_==c.name?2.2:1.3));
        painter.drawLine(target,end);
        painter.setBrush(QColor(highlight_==c.name?"#e38b32":"#ffffff"));
        painter.drawEllipse(target,3.8,3.8);
        painter.setPen(QColor("#36516b"));
        painter.drawText(QPointF(c.spin->x()+3,c.spin->y()-4),
                         QString::fromStdWString(c.shortLabel));
    }
}
void ComponentLivePreviewWidget::drawModel(QPainter& p) {
    std::vector<Poly> faces;
    if(!plan_.plates.empty()) {
        const Vec3 focus=plan_.plates.front().center;
        addMember(faces,input_.main,focus,QColor("#a5b5c5"),"column");
        for(const auto& b:input_.secondary)
            addMember(faces,b,focus,QColor("#c5cdd6"),"beam");
    }
    for(std::size_t i=0;i<plan_.plates.size();++i) {
        const auto& plate=plan_.plates[i];
        const std::string role=i<plan_.plateKeys.size()?plan_.plateKeys[i]:"plate";
        addPrism(faces,plateOutline(plate,+1.0),plateOutline(plate,-1.0),
                 plateColor(role),role);
    }
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
        QColor fill=face.face;
        if(face.role=="column"||face.role=="beam") fill.setAlpha(185);
        if(!highlight_.empty()) {
            const bool sel=(highlight_==L"haunchLength"||
                            highlight_==L"haunchDepth"||
                            highlight_==L"haunchTipDepth")
                              ?(face.role=="haunch_web"||face.role=="haunch_lower_flange")
                              :(highlight_==L"topPlateThickness")
                                ?face.role=="haunch_top_plate"
                                :(highlight_==L"endPlateThickness"||
                                  highlight_==L"boltPitchX"||highlight_==L"boltPitchY")
                                  ?face.role=="haunch_end_plate":false;
            if(sel)fill=fill.lighter(122);
        }
        p.setBrush(fill);
        p.setPen(QPen(QColor("#455469"),0.8));
        p.drawPolygon(polygon);
    }
    // Individual bolts from the actual production bolt group algorithm.
    p.setPen(QPen(QColor("#574f47"),2.0));
    for(const auto& group:plan_.boltGroups) {
        for(const Vec3 pt:boltGroupBoltOrigins(group)) {
            const Vec3 a=plus(pt,mul(group.axis,-0.5*group.gripLength));
            const Vec3 b=plus(pt,mul(group.axis,+0.5*group.gripLength));
            p.drawLine(project(a),project(b));
            p.setBrush(QColor("#f4c65d"));
            p.drawEllipse(project(b),4.0,4.0);
            p.setBrush(QColor("#a77a47"));
            p.drawEllipse(project(a),3.0,3.0);
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
    orbiting_=e->button()==Qt::LeftButton;
    panning_=e->button()==Qt::MiddleButton;
    e->accept();
}
void ComponentLivePreviewWidget::mouseMoveEvent(QMouseEvent* e) {
    const QPoint delta=e->pos()-lastMouse_;
    lastMouse_=e->pos();
    if(orbiting_) {
        yaw_+=delta.x()*0.008;
        pitch_=std::clamp(pitch_-delta.y()*0.007,-1.52,1.52);
    } else if(panning_)pan_+=QPointF(delta);
    else return;
    positionEditors();update();e->accept();
}
void ComponentLivePreviewWidget::mouseReleaseEvent(QMouseEvent* e) {
    orbiting_=panning_=false;QWidget::mouseReleaseEvent(e);
}
void ComponentLivePreviewWidget::wheelEvent(QWheelEvent* e) {
    zoom_=std::clamp(zoom_*std::pow(1.12,e->angleDelta().y()/120.0),0.2,8.0);
    positionEditors();update();e->accept();
}
} // namespace mm
