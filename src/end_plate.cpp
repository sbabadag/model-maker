#include "model_maker/end_plate.hpp"

#include <cmath>

namespace mm {

static Vec3 unitOr(const Vec3& a, const Vec3& fallback) noexcept {
    const double len = std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z);
    if (len < 1e-12) return fallback;
    return Vec3{a.x / len, a.y / len, a.z / len};
}

static Vec3 cross(const Vec3& a, const Vec3& b) noexcept {
    return Vec3{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z,
                a.x * b.y - a.y * b.x};
}

// Tekla 144 isaret sozlesmesiyle etkin kenar mesafelerini uretir (Milestone 2).
// upperD/lowerD: levha kenari - kesit kenari (POZITIF = levhayi kucult).
// upperEdgeDistance/lowerEdgeDistance tanimliysa (>= -1e8) Tekla degeri kullan;
// degilse legacy topExtra/bottomExtra (pozitif = buyut) ISARET CEVIRILEREK
// migrate edilir: upperD = -topExtra (legacy +20 buyume = Tekla -20).
static void effectiveEdgeDistances(const EndPlateParams& p, double& upperD,
                                   double& lowerD) noexcept {
    if (p.upperEdgeDistance > -1e8) {
        upperD = p.upperEdgeDistance;
    } else {
        const double topE = (p.topExtra >= 0.0) ? p.topExtra : p.plateExtra;
        upperD = -topE;
    }
    if (p.lowerEdgeDistance > -1e8) {
        lowerD = p.lowerEdgeDistance;
    } else {
        const double botE = (p.bottomExtra >= 0.0) ? p.bottomExtra : p.plateExtra;
        lowerD = -botE;
    }
}

EndPlateFrame endPlateFrame(const Vec3& beamAxis, const Vec3& referenceNormal) noexcept {
    EndPlateFrame f;
    f.axis = unitOr(beamAxis, Vec3{0.0, 0.0, 1.0});
    // referenceNormal axis'e paralelse yatay genislik icin farkli ref dene.
    Vec3 ref = unitOr(referenceNormal, Vec3{0.0, 0.0, 1.0});
    const double dot = std::abs(f.axis.x * ref.x + f.axis.y * ref.y + f.axis.z * ref.z);
    if (dot > 0.9) ref = Vec3{1.0, 0.0, 0.0};
    f.u = unitOr(cross(ref, f.axis), Vec3{1.0, 0.0, 0.0});
    f.v = cross(f.axis, f.u);
    f.v = unitOr(f.v, Vec3{0.0, 1.0, 0.0});
    // f.axis, f.u, f.v sag-el ortonormal olmali (u x v = axis). Yeniden duzelt.
    f.v = cross(f.axis, f.u);
    f.v = unitOr(f.v, Vec3{0.0, 1.0, 0.0});
    return f;
}

// Bulon grubunu levha boyutlari + cerceveden kurar (iki yerlesim fonksiyonu
// ortak kullanir — tek dogruluk kaynagi). Genislik yonundeki civata acikligi
// KIRIS FLANSINA dayanir (kullanıcı şartı: "profil başlık / 2" gauge cizgisi):
// toplam aciklik = flans/2, yani 2 kolon icin civatalar ±flans/4'te. Yukseklik
// yonu levha boyutundan kalir (params.boltEdge kenar payi).
static BoltGroup makeEndPlateBolts(const EndPlatePlacement& out,
                                   const EndPlateParams& params) noexcept {
    const int nx = params.boltCountX > 0 ? params.boltCountX : 1;
    const int ny = params.boltCountY > 0 ? params.boltCountY : 1;

    // Yatay aralik: elle girilmediyse flans/2 gauge (kullanıcı şartı: "profil
    // başlık / 2"). Dikey aralik: elle girilmediyse levha - 2*boltEdge.
    double sX = params.boltSpacingX;
    if (nx > 1 && sX <= 0.0) {
        // Flans gauge'i KIRIS KESIT genisliginden gelir (secWidth), levha
        // boyutundan DEGIL — genislik override'i bulon acikligini bozmamali.
        const double flangeW = (out.secWidth > 0.0)
                                   ? out.secWidth
                                   : (out.plateWidth - 2.0 * params.plateExtra);
        const double total = (flangeW > 0.0) ? 0.5 * flangeW
                                             : (out.plateWidth - 2.0 * params.boltEdge);
        sX = total / static_cast<double>(nx - 1);
    } else if (nx <= 1) {
        sX = 0.0;
    }
    double sY = params.boltSpacingY;
    if (ny > 1 && sY <= 0.0) {
        sY = (out.plateHeight - 2.0 * params.boltEdge) / static_cast<double>(ny - 1);
    } else if (ny <= 1) {
        sY = 0.0;
    }

    const double gw = (nx - 1) * sX;  // grup yatay acikligi
    const double gh = (ny - 1) * sY;  // grup dikey acikligi

    // Yatay hizalama: en sol bulonun levha sol kenarindan u-yonlu uzakligi.
    double leftX;
    switch (params.boltAlignH) {
        case 0:  // Sol: en sol bulon sol kenardan offsetH iceride
            leftX = (params.boltOffsetH >= 0.0) ? params.boltOffsetH : params.boltEdge;
            break;
        case 2:  // Sag: en sag bulon sag kenardan offsetH iceride
            leftX = out.plateWidth - gw -
                    ((params.boltOffsetH >= 0.0) ? params.boltOffsetH : params.boltEdge);
            break;
        default:  // Orta: grup merkezi levha merkezinde + offsetH
            leftX = (out.plateWidth - gw) * 0.5 + params.boltOffsetH;
            break;
    }
    // Dikey hizalama: en alt bulonun levha alt kenarindan v-yonlu uzakligi.
    double botY;
    switch (params.boltAlignV) {
        case 0:  // Ust: en ust bulon ust kenardan offsetV iceride
            botY = out.plateHeight - gh -
                   ((params.boltOffsetV >= 0.0) ? params.boltOffsetV : params.boltEdge);
            break;
        case 1:  // Orta: grup merkezi levha merkezinde + offsetV
            botY = (out.plateHeight - gh) * 0.5 + params.boltOffsetV;
            break;
        default:  // Alt: en alt bulon alt kenardan offsetV iceride
            botY = (params.boltOffsetV >= 0.0) ? params.boltOffsetV : params.boltEdge;
            break;
    }

    // Origin = levha SOL-ALT kosesinden + u*leftX + v*botY.
    const double ox = out.plateCenter.x - out.u.x * (out.plateWidth * 0.5 - leftX) -
                      out.v.x * (out.plateHeight * 0.5 - botY);
    const double oy = out.plateCenter.y - out.u.y * (out.plateWidth * 0.5 - leftX) -
                      out.v.y * (out.plateHeight * 0.5 - botY);
    const double oz = out.plateCenter.z - out.u.z * (out.plateWidth * 0.5 - leftX) -
                      out.v.z * (out.plateHeight * 0.5 - botY);

    BoltGroup b;
    b.origin = Vec3{ox, oy, oz};
    b.axis = out.axis;
    b.xDir = out.u;
    b.countX = nx;
    b.countY = ny;
    b.spacingX = sX;
    b.spacingY = sY;
    b.deletedBolts = params.deletedBolts;
    b.params.nominalDiameter = params.boltDiameter;
    b.params.holeDiameter = params.holeDiameter;
    b.params.grade = params.boltGrade;
    b.params.hasHead = params.hasHead;
    b.params.hasNut = params.hasNut;
    b.params.hasWasher = params.hasWasher;
    b.params.standard = params.boltStandard;
    b.params.threadInMat = params.threadInMat;
    b.params.siteWorkshop = params.siteWorkshop;
    b.params.cutLengthMode = params.boltCutLengthMode;
    b.params.lengthIncrease = params.boltLengthIncrease;
    b.params.direction = params.boltDirection;
    b.params.comment = params.boltComment;
    return b;
}

EndPlatePlacement computeEndPlatePlacement(const Vec3& beamEnd, const Vec3& beamAxis,
                                           double secWidth, double secHeight,
                                           const EndPlateParams& params) noexcept {
    EndPlatePlacement out;
    const EndPlateFrame f = endPlateFrame(beamAxis, Vec3{0.0, 0.0, 1.0});
    out.u = f.u;
    out.v = f.v;
    out.axis = f.axis;
    double upperD, lowerD;
    effectiveEdgeDistances(params, upperD, lowerD);
    out.plateWidth = (params.plateWidthOverride > 0.0)
                         ? params.plateWidthOverride
                         : (secWidth + 2.0 * params.plateExtra);
    out.plateHeight = (params.plateHeightOverride > 0.0)
                          ? params.plateHeightOverride
                          : (secHeight - upperD - lowerD);
    out.plateThickness = params.plateThickness;
    out.secWidth = secWidth;
    if (out.plateWidth < 1.0) out.plateWidth = 1.0;
    if (out.plateHeight < 1.0) out.plateHeight = 1.0;

    // Levha merkezi = kiris ucu + axis * thickness/2 (levha kiris ucuna oturur,
    // kolona dogru uzanir).
    const double halfT = params.plateThickness * 0.5;
    out.plateCenter = Vec3{beamEnd.x + f.axis.x * halfT, beamEnd.y + f.axis.y * halfT,
                           beamEnd.z + f.axis.z * halfT};

    // Ayrı ust/alt kenar: levha merkezini v yonunde (lowerD-upperD)/2 kaydir.
    // (Tekla 144: pozitif UST mesafe levhayi ASAGI kaydirir — ust kenar iceri
    //  cekilir; pozitif ALT mesafe levhayi yukari kaydirir.)
    const double vShift = 0.5 * (lowerD - upperD);
    if (std::abs(vShift) > 1e-12) {
        out.plateCenter.x += f.v.x * vShift;
        out.plateCenter.y += f.v.y * vShift;
        out.plateCenter.z += f.v.z * vShift;
    }

    out.bolts = makeEndPlateBolts(out, params);
    return out;
}

EndPlatePlacement computeEndPlatePlacementOnFace(const ReferencePlane& face, double gap,
                                                 double secWidth, double secHeight,
                                                 const EndPlateParams& params,
                                                 const Vec3& sectionHeightDir,
                                                 double anchorOffset,
                                                 const Vec3& sectionWidthDir) noexcept {
    EndPlatePlacement out;
    // Levha normali = kolon yuzey normali (kolondan DISARI, kirise dogru).
    const Vec3 axis = unitOr(face.normal, Vec3{0.0, 0.0, 1.0});
    // Levha duzlemindeki eksenler (Tekla 3.4): DIKEY = kiris kesit YUKSEKLIK
    // ekseninin levha duzlemindeki gecerli izdusumu; YATAY = dikey x normal
    // (sag-el). Dunya Z'si YALNIZCA acik/dejenere fallback'tir. Boylece donmus
    // kiriste (profileRotation) levha cercevesi gercek kesit yonune oturur,
    // yanlis 90° donme olmaz. Kesit ofseti ile normal yonlu gap matematiksel
    // ayri tutulur (skew kirisde gap bozulmaz).
    const double dotN = axis.x * sectionHeightDir.x + axis.y * sectionHeightDir.y +
                        axis.z * sectionHeightDir.z;
    Vec3 vv{sectionHeightDir.x - axis.x * dotN, sectionHeightDir.y - axis.y * dotN,
            sectionHeightDir.z - axis.z * dotN};
    const double vvLen = std::sqrt(vv.x * vv.x + vv.y * vv.y + vv.z * vv.z);
    Vec3 uu;
    if (vvLen > 1e-9) {
        vv = Vec3{vv.x / vvLen, vv.y / vvLen, vv.z / vvLen};
        uu = cross(vv, axis);  // sag-el: u x v = axis
    } else {
        // Yukseklik ekseni normalle cakisik (dejenere) -> GENISLIK eksenini dene.
        const double dotW = axis.x * sectionWidthDir.x + axis.y * sectionWidthDir.y +
                            axis.z * sectionWidthDir.z;
        Vec3 ww{sectionWidthDir.x - axis.x * dotW, sectionWidthDir.y - axis.y * dotW,
                sectionWidthDir.z - axis.z * dotW};
        const double wwLen = std::sqrt(ww.x * ww.x + ww.y * ww.y + ww.z * ww.z);
        if (wwLen > 1e-9) {
            uu = Vec3{ww.x / wwLen, ww.y / wwLen, ww.z / wwLen};
            vv = cross(axis, uu);
        } else {
            // Acik, tanimli fallback: dunya Z kurali (eski davranis).
            const EndPlateFrame f = endPlateFrame(face.normal, Vec3{0.0, 0.0, 1.0});
            uu = f.u;
            vv = f.v;
        }
    }
    out.u = uu;
    out.v = vv;
    out.axis = axis;
    double upperD, lowerD;
    effectiveEdgeDistances(params, upperD, lowerD);
    out.plateWidth = (params.plateWidthOverride > 0.0)
                         ? params.plateWidthOverride
                         : (secWidth + 2.0 * params.plateExtra);
    out.plateHeight = (params.plateHeightOverride > 0.0)
                          ? params.plateHeightOverride
                          : (secHeight - upperD - lowerD);
    out.plateThickness = params.plateThickness;
    out.secWidth = secWidth;
    if (out.plateWidth < 1.0) out.plateWidth = 1.0;
    if (out.plateHeight < 1.0) out.plateHeight = 1.0;

    // Levha merkezi: temas noktasindan normal yonunde gap + t/2; duzlem-ici
    // kesit ofseti (anchor + asimetrik kenar) YALNIZ levha dikey ekseni vv
    // boyunca uygulanir -> gap normalde bozulmaz, kolona girip cikmaz.
    const double halfT = params.plateThickness * 0.5;
    const double inPlaneShift = anchorOffset + 0.5 * (lowerD - upperD);
    out.plateCenter = Vec3{face.origin.x + axis.x * (gap + halfT) + vv.x * inPlaneShift,
                           face.origin.y + axis.y * (gap + halfT) + vv.y * inPlaneShift,
                           face.origin.z + axis.z * (gap + halfT) + vv.z * inPlaneShift};

    out.bolts = makeEndPlateBolts(out, params);
    return out;
}

} // namespace mm
