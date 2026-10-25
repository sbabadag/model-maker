// MODEL MAKER BRIDGE — Bridge -> native parametric component esleme (uygulama).

#include "model_maker/bridge_native_mapper.hpp"

#include "model_maker/bolt.hpp"  // standardBoltHoleDiameter

#include <algorithm>
#include <cctype>
#include <cmath>
#include <set>
#include <sstream>

namespace mm {

namespace {

constexpr double kTol = 1e-6;

// --- kucuk yardimcilar --------------------------------------------------------

std::wstring widen(const std::string& s) { return std::wstring(s.begin(), s.end()); }

double vlen(const Vec3& v) { return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z); }

Vec3 toVec3(const BridgeVec3& v) { return {v.x, v.y, v.z}; }

// "M20" -> 20.0; "20" -> 20.0. Taninamazsa 0.
double parseMetricSize(const std::string& size) {
    std::string digits;
    for (char c : size)
        if (std::isdigit(static_cast<unsigned char>(c)) || c == '.') digits.push_back(c);
    if (digits.empty()) return 0.0;
    return std::atof(digits.c_str());
}

std::string toLower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// Paket parametre listesinden ada gore BridgeParameter (yoksa nullptr).
const BridgeParameter* findBridgeParam(const BridgePackage& pkg, const std::string& name) {
    for (const auto& p : pkg.parameters)
        if (p.name == name) return &p;
    return nullptr;
}

// parametre value'sunu double olarak al (tip uygun ise); yoksa false.
bool paramDouble(const BridgePackage& pkg, const std::string& name, double& out) {
    const BridgeParameter* p = findBridgeParam(pkg, name);
    if (!p) return false;
    if (const double* d = std::get_if<double>(&p->value)) { out = *d; return true; }
    if (const int* i = std::get_if<int>(&p->value)) { out = static_cast<double>(*i); return true; }
    return false;
}

bool paramInt(const BridgePackage& pkg, const std::string& name, int& out) {
    const BridgeParameter* p = findBridgeParam(pkg, name);
    if (!p) return false;
    if (const int* i = std::get_if<int>(&p->value)) { out = *i; return true; }
    if (const double* d = std::get_if<double>(&p->value)) { out = static_cast<int>(*d); return true; }
    return false;
}

// Plaka nesnesini bul (ContourPlate, ya da plaka profilli Beam/PolyBeam).
const BridgeObject* findPlate(const BridgePackage& pkg) {
    for (const auto& o : pkg.objects) {
        if (o.kind == "ContourPlate") return &o;
    }
    for (const auto& o : pkg.objects) {
        if (o.kind == "Beam" || o.kind == "PolyBeam") {
            const std::string pf = toLower(o.profile);
            if (pf.size() >= 2 && pf[0] == 'p' && pf[1] == 'l') return &o;
        }
    }
    return nullptr;
}

const BridgeObject* findBoltGroup(const BridgePackage& pkg) {
    for (const auto& o : pkg.objects)
        if (o.kind == "BoltGroup" && o.boltGroup) return &o;
    return nullptr;
}

const BridgeObject* findWeld(const BridgePackage& pkg) {
    for (const auto& o : pkg.objects)
        if (o.kind == "Weld" && o.weld) return &o;
    return nullptr;
}

// Bulon eksen vektoru (info.axis/direction); yoksa plaka ince boyutundan.
// Basarili ise true + birim eksen + kaynak.
bool resolveBoltAxis(const BridgeObject& bolts, const BridgeBBox* bbox, Vec3& axis,
                     BridgeParamProvenance& provenance) {
    const BridgeBoltGroupInfo& g = *bolts.boltGroup;
    if (g.direction) {
        Vec3 d = toVec3(*g.direction);
        const double l = vlen(d);
        if (l > kTol) { axis = {d.x / l, d.y / l, d.z / l}; provenance = BridgeParamProvenance::Observed; return true; }
    }
    if (!g.axis.empty()) {
        const std::string a = g.axis;
        if (a == "x" || a == "+x") { axis = {1, 0, 0}; provenance = BridgeParamProvenance::Observed; return true; }
        if (a == "-x") { axis = {-1, 0, 0}; provenance = BridgeParamProvenance::Observed; return true; }
        if (a == "y" || a == "+y") { axis = {0, 1, 0}; provenance = BridgeParamProvenance::Observed; return true; }
        if (a == "-y") { axis = {0, -1, 0}; provenance = BridgeParamProvenance::Observed; return true; }
        if (a == "z" || a == "+z") { axis = {0, 0, 1}; provenance = BridgeParamProvenance::Observed; return true; }
        if (a == "-z") { axis = {0, 0, -1}; provenance = BridgeParamProvenance::Observed; return true; }
    }
    if (bbox) {
        const double dx = bbox->max.x - bbox->min.x;
        const double dy = bbox->max.y - bbox->min.y;
        const double dz = bbox->max.z - bbox->min.z;
        if (dx <= 0 || dy <= 0 || dz <= 0) return false;
        if (dx <= dy && dx <= dz) axis = {1, 0, 0};
        else if (dy <= dx && dy <= dz) axis = {0, 1, 0};
        else axis = {0, 0, 1};
        provenance = BridgeParamProvenance::Inferred;  // en ince boyut
        return true;
    }
    return false;
}

// Bulon grid boyutlari (world eksenlerine gore): countX = yatay (dusey eksene dik)
// ayrik koordinat sayisi, countY = dusey (Z) ayrik koordinat sayisi. Bulonlarin
// hepsi ayni eksen-koordinatinda mi, duzgun aralik mi; degilse false (uydurulmaz).
bool detectBoltGrid(const BridgeBoltGroupInfo& g, const Vec3& axis, int& countX,
                    int& countY, double& spacingX, double& spacingY, std::string& whyNot) {
    if (g.bolts.empty()) { whyNot = "bulon listesi bos"; return false; }

    const bool axX = std::fabs(axis.x) > 0.5;
    const bool axY = std::fabs(axis.y) > 0.5;
    const bool axZ = std::fabs(axis.z) > 0.5;

    const double axisCoord = axX ? g.bolts[0].position.x
                           : axY ? g.bolts[0].position.y
                                 : g.bolts[0].position.z;

    std::set<double> horiz, vert;
    for (const auto& b : g.bolts) {
        const BridgeVec3& p = b.position;
        // eksen boyunca koordinat ayni olmali (tek duzlem).
        const double c = axX ? p.x : axY ? p.y : p.z;
        if (std::fabs(c - axisCoord) > kTol) { whyNot = "bulonlar ayni eksen duzleminde degil"; return false; }
        // yatay eksen = Z'ye dik olan (dusey olmayan) eksen; dusey = Z.
        if (axZ) {
            // eksen Z ise: iki yatay eksen X ve Y; hangisi "dusey" belirsiz — X'i yatay, Y'yi dusey say.
            horiz.insert(p.x);
            vert.insert(p.y);
        } else if (axX) {
            horiz.insert(p.y);
            vert.insert(p.z);
        } else {  // axY
            horiz.insert(p.x);
            vert.insert(p.z);
        }
    }
    countX = static_cast<int>(horiz.size());
    countY = static_cast<int>(vert.size());

    // Duzgunluk (esit aralik) dogrulamasi.
    auto uniform = [](const std::set<double>& s, double& spacing) {
        if (s.size() < 2) { spacing = 0.0; return true; }
        auto it = s.begin();
        const double first = *it;
        const double last = *std::prev(s.end());
        spacing = (last - first) / static_cast<double>(s.size() - 1);
        int idx = 0;
        for (double v : s)
            if (std::fabs(v - (first + spacing * idx++)) > kTol) return false;
        return true;
    };
    if (!uniform(horiz, spacingX) || !uniform(vert, spacingY)) {
        whyNot = "bulon araliklari duzgun degil";
        return false;
    }
    return true;
}

// Bulonlarin yatay (genislik) ve dusey (yukseklik) koordinat acikliklari.
// detectBoltGrid ile AYNI eksen mantigi: dusey her zaman Z (shear tab diksiyonu),
// yatay eksene dik yatay eksen. axisX icin yatay=Y, axisY icin yatay=X;
// axisZ (dusey eksen, shear tab'da gercekci degil) icin yatay=X, dusey=Y.
bool boltCoordSpans(const BridgeBoltGroupInfo& g, const Vec3& axis, double& horizMin,
                    double& horizMax, double& vertMin, double& vertMax) {
    if (g.bolts.empty()) return false;
    const bool axX = std::fabs(axis.x) > 0.5;
    const bool axZ = std::fabs(axis.z) > 0.5;
    horizMin = vertMin = 1e18;
    horizMax = vertMax = -1e18;
    for (const auto& b : g.bolts) {
        const BridgeVec3& p = b.position;
        double h, v;
        if (axZ) { h = p.x; v = p.y; }
        else if (axX) { h = p.y; v = p.z; }
        else { h = p.x; v = p.z; }  // axY
        horizMin = std::min(horizMin, h);
        horizMax = std::max(horizMax, h);
        vertMin = std::min(vertMin, v);
        vertMax = std::max(vertMax, v);
    }
    return true;
}

// Plaka bbox boyutlarini (kalinlik/genislik/yukseklik) cikar. kalinlik = eksen
// boyuncaki boyut; yukseklik = dusey (Z) boyut; genislik = kalan yatay boyut.
bool plateDimsFromBBox(const BridgeBBox& bbox, const Vec3& axis, double& thickness,
                       double& width, double& height) {
    const double dx = bbox.max.x - bbox.min.x;
    const double dy = bbox.max.y - bbox.min.y;
    const double dz = bbox.max.z - bbox.min.z;
    if (dx <= 0 || dy <= 0 || dz <= 0) return false;
    const double ax = std::fabs(axis.x), ay = std::fabs(axis.y);
    if (ax > 0.5) {
        thickness = dx;
        height = dz;   // dusey
        width = dy;    // yatay, eksene dik
    } else if (ay > 0.5) {
        thickness = dy;
        height = dz;
        width = dx;
    } else {
        thickness = dz;
        // eksen dusey: yatay boyutlar X ve Y — dusey boyut belirsiz.
        height = std::max(dx, dy);  // INFERRED (cagirici isaretler)
        width = std::min(dx, dy);
        return true;  // caller bu durumu height provenance ile isaretler
    }
    return true;
}

} // namespace

// --- public API ---------------------------------------------------------------

const char* bridgeProvenanceName(BridgeParamProvenance p) noexcept {
    switch (p) {
        case BridgeParamProvenance::Observed: return "observed";
        case BridgeParamProvenance::Documented: return "documented";
        case BridgeParamProvenance::Inferred: return "inferred";
        case BridgeParamProvenance::Unresolved: return "unresolved";
    }
    return "unresolved";
}

BridgeParamProvenance bridgeConfidenceToProvenance(const std::string& confidence) noexcept {
    const std::string c = toLower(confidence);
    if (c == "observed") return BridgeParamProvenance::Observed;
    if (c == "documented") return BridgeParamProvenance::Documented;
    if (c == "inferred") return BridgeParamProvenance::Inferred;
    return BridgeParamProvenance::Unresolved;
}

const char* bridgeEquivalenceName(BridgeEquivalenceState s) noexcept {
    switch (s) {
        case BridgeEquivalenceState::Equivalent: return "equivalent";
        case BridgeEquivalenceState::NotEquivalent: return "not_equivalent";
        case BridgeEquivalenceState::Unverified: return "unverified";
    }
    return "unverified";
}

BridgeNativeMapping mapBridgeToNative(const BridgePackage& pkg,
                                      const std::vector<SteelProfile>& catalog) noexcept {
    BridgeNativeMapping m;

    // --- 1) Tespit: kaynak 146 shear plate mi? --------------------------------
    const int compNumber = pkg.manifest.sourceComponentNumber != 0
                               ? pkg.manifest.sourceComponentNumber
                               : pkg.component.number;
    const std::string compName = pkg.manifest.sourceComponentName.empty()
                                     ? pkg.component.name
                                     : pkg.manifest.sourceComponentName;
    const std::string compNameLower = toLower(compName);
    const bool nameHints146 =
        compNameLower.find("shear plate") != std::string::npos ||
        compNameLower.find("shear tab") != std::string::npos;

    if (compNumber == 146 || nameHints146) {
        m.detected = true;
        m.typeId = L"tekla_shear_plate_146";
        m.detectionReason = compNumber == 146
                                ? "sourceComponentNumber == 146"
                                : "component adi shear plate/tab iceriyor";
    } else {
        m.detected = false;
        m.detectionReason = "kaynak 146 shear plate degil (number=" +
                            std::to_string(compNumber) + ", name=\"" + compName + "\")";
        return m;
    }

    // --- 2) Kaynak nesneler ---------------------------------------------------
    const BridgeObject* plate = findPlate(pkg);
    const BridgeObject* bolts = findBoltGroup(pkg);
    const BridgeObject* weld = findWeld(pkg);

    // --- 3) Bulon ekseni (plaka boyutlari + grid icin) ------------------------
    Vec3 boltAxis{1, 0, 0};
    BridgeParamProvenance axisProv = BridgeParamProvenance::Unresolved;
    const BridgeBBox* plateBBox = (plate && plate->bbox) ? &*plate->bbox : nullptr;
    const bool axisOk = bolts ? resolveBoltAxis(*bolts, plateBBox, boltAxis, axisProv) : false;
    if (!axisOk) m.unsupportedFeatures.push_back("bulon ekseni belirlenemedi (uydurulmadi)");

    // --- 4) Plaka boyutlari (params -> bbox) ----------------------------------
    const auto addLength = [&](const std::wstring& name, double v,
                               BridgeParamProvenance prov, const std::string& note) {
        m.params.push_back(BridgeMappedParam{name, CompParamValue{v}, prov, note});
    };
    const auto addInt = [&](const std::wstring& name, int v,
                            BridgeParamProvenance prov, const std::string& note) {
        m.params.push_back(BridgeMappedParam{name, CompParamValue{v}, prov, note});
    };
    const auto addStr = [&](const std::wstring& name, const std::string& v,
                            BridgeParamProvenance prov, const std::string& note) {
        m.params.push_back(BridgeMappedParam{name, CompParamValue{widen(v)}, prov, note});
    };

    double obsThickness = 0.0, obsWidth = 0.0, obsHeight = 0.0;
    bool haveDims = false;
    if (plateBBox) {
        const bool ok = plateDimsFromBBox(*plateBBox, boltAxis, obsThickness, obsWidth, obsHeight);
        if (ok) haveDims = true;
    }

    // Kalinlik: once acik parametre, yoksa bbox.
    {
        double v = 0.0;
        if (paramDouble(pkg, "plateThickness", v) && v > 0.0)
            addLength(L"plateThickness", v, BridgeParamProvenance::Observed, "parametre plateThickness");
        else if (haveDims)
            addLength(L"plateThickness", obsThickness, BridgeParamProvenance::Observed, "plaka bbox ince boyut");
        else
            addLength(L"plateThickness", 0.0, BridgeParamProvenance::Unresolved, "kaynakta yok");
    }
    {
        double v = 0.0;
        if (paramDouble(pkg, "plateWidth", v) && v > 0.0)
            addLength(L"plateWidth", v, BridgeParamProvenance::Observed, "parametre plateWidth");
        else if (haveDims)
            addLength(L"plateWidth", obsWidth, BridgeParamProvenance::Observed, "plaka bbox (yatay, eksene dik)");
        else
            addLength(L"plateWidth", 0.0, BridgeParamProvenance::Unresolved, "kaynakta yok");
    }
    {
        double v = 0.0;
        if (paramDouble(pkg, "plateHeight", v) && v > 0.0)
            addLength(L"plateHeight", v, BridgeParamProvenance::Observed, "parametre plateHeight");
        else if (haveDims)
            addLength(L"plateHeight", obsHeight, BridgeParamProvenance::Observed, "plaka bbox (dusey)");
        else
            addLength(L"plateHeight", 0.0, BridgeParamProvenance::Unresolved, "kaynakta yok");
    }

    // --- 5) Malzeme -----------------------------------------------------------
    if (plate && !plate->material.empty()) {
        m.plateMaterial = plate->material;
        m.materialProvenance = BridgeParamProvenance::Observed;
    } else if (!pkg.component.primaryMember.material.empty()) {
        m.plateMaterial = pkg.component.primaryMember.material;
        m.materialProvenance = BridgeParamProvenance::Observed;
    }

    // --- 6) Bulonlar ----------------------------------------------------------
    double nominalDia = 0.0, holeDia = 0.0;
    BridgeParamProvenance holeProv = BridgeParamProvenance::Unresolved;
    int boltTotal = 0, gridX = 1, gridY = 1;
    double spX = 0.0, spY = 0.0;
    bool gridOk = false;
    std::string gridWhy;

    if (bolts && bolts->boltGroup) {
        const BridgeBoltGroupInfo& g = *bolts->boltGroup;
        for (const auto& b : g.bolts)
            if (b.nominalDiameter > 0.0) { nominalDia = b.nominalDiameter; break; }
        if (nominalDia <= 0.0) nominalDia = parseMetricSize(g.size);

        for (const auto& b : g.bolts)
            if (b.holeDiameter > 0.0) {
                holeDia = b.holeDiameter;
                holeProv = b.holeDiameterSource == "standard"
                               ? BridgeParamProvenance::Inferred   // standard tablodan
                               : BridgeParamProvenance::Observed;  // olculdu
                break;
            }
        if (holeDia <= 0.0 && nominalDia > 0.0) {
            holeDia = standardBoltHoleDiameter(nominalDia);
            holeProv = BridgeParamProvenance::Inferred;  // ISO standart tablo
        }

        boltTotal = static_cast<int>(g.bolts.size());
        gridOk = detectBoltGrid(g, boltAxis, gridX, gridY, spX, spY, gridWhy);
    }

    // boltSize parametresi nominal capi kesin veriyorsa onu kullan.
    {
        std::string sz;
        const BridgeParameter* bp = findBridgeParam(pkg, "boltSize");
        if (bp && std::get_if<std::string>(&bp->value)) sz = std::get<std::string>(bp->value);
        if (sz.empty()) sz = bolts && bolts->boltGroup ? bolts->boltGroup->size : "";
        const double fromSize = parseMetricSize(sz);
        if (fromSize > 0.0) nominalDia = fromSize;
    }

    if (nominalDia > 0.0)
        addLength(L"boltDiameter", nominalDia, BridgeParamProvenance::Observed, "boltSize/boltGroup.size");
    else
        addLength(L"boltDiameter", 0.0, BridgeParamProvenance::Unresolved, "kaynakta yok");
    addLength(L"holeDiameter", holeDia > 0.0 ? holeDia : 0.0,
              holeDia > 0.0 ? holeProv : BridgeParamProvenance::Unresolved,
              holeDia > 0.0 ? (holeProv == BridgeParamProvenance::Inferred
                                   ? "standard tablo (M->delik)"
                                   : "bulon holeDiameter")
                            : "kaynakta yok");

    // Native boltCount = DUSEY sira sayisi (countY); boltCountX = kolon sayisi
    // (countX). Grid cozulduyse ikisi de gozlenen; cozulmediyse toplam adet tek
    // kolon olarak isaretlenir (uydurulmaz, Inferred).
    int paramBoltCount = 0;
    const bool haveCountParam = paramInt(pkg, "boltCount", paramBoltCount) && paramBoltCount > 0;
    if (gridOk) {
        addInt(L"boltCount", gridY, BridgeParamProvenance::Observed,
               "bulon konumlarindan dusey sira sayisi");
        addInt(L"boltCountX", gridX, BridgeParamProvenance::Observed,
               "bulon konumlarindan kolon sayisi");
    } else if (haveCountParam) {
        addInt(L"boltCount", paramBoltCount, BridgeParamProvenance::Inferred,
               "parametre boltCount (toplam " + std::to_string(paramBoltCount) +
                   "); grid cozulemedi, tek kolon varsayildi");
        addInt(L"boltCountX", 1, BridgeParamProvenance::Inferred,
               "grid cozulemedi; tek kolon varsayildi");
    } else if (boltTotal > 0) {
        addInt(L"boltCount", boltTotal, BridgeParamProvenance::Inferred,
               "toplam bulon sayisi (grid cozulemedi; tek kolon varsayildi)");
        addInt(L"boltCountX", 1, BridgeParamProvenance::Inferred,
               "grid cozulemedi; tek kolon varsayildi");
    } else {
        addInt(L"boltCount", 0, BridgeParamProvenance::Unresolved, "kaynakta yok");
        addInt(L"boltCountX", 0, BridgeParamProvenance::Unresolved, "kaynakta yok");
    }

    // Kenar mesafeleri (yatay + dusey): (boyut - aciklik)/2, SIMETRIK kabulu.
    // Native kenar mesafesi tek deger urettigi icin simetrik esdeger raporlanir;
    // gercek Tekla yerlesimi asimetrikse bu bir YAKLASIKLIKTIR ve asagida acikca
    // unsupportedFeatures'e (ya da issue) yazilir — sessizce dogru sayilmaz.
    if (haveDims && gridOk && bolts && bolts->boltGroup) {
        double hmin, hmax, vmin, vmax;
        if (boltCoordSpans(*bolts->boltGroup, boltAxis, hmin, hmax, vmin, vmax)) {
            const double hspan = hmax - hmin;
            const double vspan = vmax - vmin;
            if (gridX > 1 && obsWidth > 0.0 && hspan >= 0.0 && hspan <= obsWidth + kTol) {
                const double ex = (obsWidth - hspan) * 0.5;
                addLength(L"edgeX", ex, BridgeParamProvenance::Observed,
                          "bulon konumlari + plaka genisliginden (simetrik)");
            }
            if (obsHeight > 0.0 && vspan >= 0.0 && vspan <= obsHeight + kTol) {
                const double ey = (obsHeight - vspan) * 0.5;
                addLength(L"edgeY", ey, BridgeParamProvenance::Observed,
                          "bulon konumlari + plaka yuksekliginden (simetrik)");
                addLength(L"boltEdge", ey, BridgeParamProvenance::Observed,
                          "bulon konumlari + plaka yuksekliginden (geriye donuk)");
            }
        }
    }

    // Stage4: NC1 independently verified non-central bolt pattern.
    // Existing native 146 generator can now retain this offset instead of
    // silently re-centering an asymmetric grid.
    {
        double measuredX=0.0, measuredY=0.0;
        if (paramDouble(pkg, "bridgeNcBoltOffsetX", measuredX))
            addLength(L"boltGroupOffsetX", measuredX, BridgeParamProvenance::Observed,
                      "NC1 + Part.GetDSTVCoordinateSystem checked source offset");
        if (paramDouble(pkg, "bridgeNcBoltOffsetY", measuredY))
            addLength(L"boltGroupOffsetY", measuredY, BridgeParamProvenance::Observed,
                      "NC1 + Part.GetDSTVCoordinateSystem checked source offset");
    }

    // Bulon araliklari: gozlenen esit aralik -> pitchX/pitchY; aksi halde auto.
    if (gridOk && gridX > 1 && spX > 0.0) {
        addLength(L"pitchX", spX, BridgeParamProvenance::Observed,
                  "bulon konumlarindan yatay aralik");
    }
    if (gridOk && gridY > 1 && spY > 0.0) {
        addLength(L"pitchY", spY, BridgeParamProvenance::Observed,
                  "bulon konumlarindan dusey aralik");
        std::ostringstream os;
        os << spY;
        addStr(L"boltSpacing", os.str(), BridgeParamProvenance::Observed,
               "bulon konumlarindan dusey aralik (geriye donuk)");
    }

    // Kalite: kaynakta belirtilmemis (native varsayilan 8.8).
    {
        const BridgeParameter* bp = findBridgeParam(pkg, "grade");
        if (bp && std::get_if<std::string>(&bp->value)) {
            addStr(L"grade", std::get<std::string>(bp->value), BridgeParamProvenance::Observed,
                   "parametre grade");
        } else if (bp && std::get_if<int>(&bp->value)) {
            addStr(L"grade", std::to_string(std::get<int>(bp->value)), BridgeParamProvenance::Observed,
                   "parametre grade");
        } else {
            addStr(L"grade", "8.8", BridgeParamProvenance::Unresolved,
                   "kaynakta belirtilmemis; native varsayilan 8.8");
        }
    }

    // --- 7) Kaynak / fitting / cut -> native'de karsiligi yok -----------------
    if (weld && weld->weld) {
        m.unsupportedFeatures.push_back(
            "kaynak (weld) " + weld->weld->type + " " +
            std::to_string(weld->weld->size) + "mm gozlendi; native generator kaynak "
            "geometrisi uretmez (yalniz metadata)");
    }
    for (const auto& o : pkg.objects) {
        if (o.kind == "Fitting" && o.fitting)
            m.unsupportedFeatures.push_back("fitting (hedef " + o.fitting->targetPartGUID +
                                            ") native'de yeniden uretilmez");
        else if (o.kind == "BooleanPart" && o.booleanPart)
            m.unsupportedFeatures.push_back("boolean " + o.booleanPart->operationType +
                                            " native'de yeniden uretilmez");
    }

    // --- 8) Uye geometrisi (profil katalogundan + paketten) --------------------
    const auto reconstruct = [&](const BridgeMemberRef& ref) {
        BridgeReconstructedMember rm;
        rm.guid = ref.guid;
        rm.profile = ref.profile;
        const SteelProfile* sp = findProfile(catalog, ref.profile);
        if (sp) {
            rm.hasSection = true;
            rm.geometry.secWidth = sp->width;   // flans genisligi
            rm.geometry.secHeight = sp->height; // kesit yuksekligi
            rm.geometry.sectionCenterOffset = 0.0;  // merkez ankraj (inferred)
        }
        if (ref.startPoint && ref.endPoint) {
            rm.geometry.axisFrom = toVec3(*ref.startPoint);
            rm.geometry.axisTo = toVec3(*ref.endPoint);
            rm.hasAxis = vlen(Vec3{rm.geometry.axisTo.x - rm.geometry.axisFrom.x,
                                   rm.geometry.axisTo.y - rm.geometry.axisFrom.y,
                                   rm.geometry.axisTo.z - rm.geometry.axisFrom.z}) > kTol;
            if (rm.hasAxis) rm.axisProvenance = BridgeParamProvenance::Observed;
        }
        rm.geometry.refNormal = {0.0, 0.0, 1.0};
        rm.geometry.id = kInvalidEntityId;
        return rm;
    };
    m.primary = reconstruct(pkg.component.primaryMember);
    for (const auto& sm : pkg.component.secondaryMembers) m.secondary.push_back(reconstruct(sm));

    // --- 9) Esdegerlik: native generator gercekten yeniden uretiyor mu ---------
    // Statik kontroller ONCE (uye geometrisi gerekmez): eksik veri ve cok kolonlu
    // bulon grubu, uye baglami olmadan da kesin sonuc verir.
    if (!haveDims || !bolts || !bolts->boltGroup) {
        m.equivalence.state = BridgeEquivalenceState::Unverified;
        m.equivalence.issues.push_back("plaka bbox veya bulon grubu yok; karsilastirma yapilamadi");
    } else {
        const bool membersReady = m.primary.hasAxis && m.primary.hasSection &&
                                  !m.secondary.empty() && m.secondary[0].hasAxis &&
                                  m.secondary[0].hasSection;
        if (!membersReady) {
            m.equivalence.state = BridgeEquivalenceState::Unverified;
            m.equivalence.issues.push_back(
                "uye geometrisi pakette eksik (eksen/kesit); native generator calistirilmadi "
                "— uydurulmadi");
        } else {
            ComponentInput in;
            in.main = m.primary.geometry;
            in.secondary.push_back(m.secondary[0].geometry);
            for (const auto& pm : m.params)
                in.params.push_back(CompParam{pm.name, pm.value});

            const GenerationPlan plan = generateShearPlateSimple146(in);
            if (!plan.ok) {
                m.equivalence.state = BridgeEquivalenceState::NotEquivalent;
                m.equivalence.issues.push_back("native generator hata: " +
                                               std::string(plan.error.begin(), plan.error.end()));
            } else {
                // Levha boyut karsilastirmasi (generatorun levhasi parametrelerden;
                // gozlenen bbox ile tutarli mi).
                bool plateMatch = false;
                for (const auto& ps : plan.plates)
                    if (std::fabs(ps.thickness - obsThickness) < 0.5 &&
                        std::fabs(ps.width - obsWidth) < 0.5 &&
                        std::fabs(ps.height - obsHeight) < 0.5) { plateMatch = true; break; }

                bool boltMatch = false;
                for (const auto& bg : plan.boltGroups)
                    if (bg.countY == gridY && bg.countX == gridX) { boltMatch = true; break; }

                if (plateMatch && boltMatch) {
                    m.equivalence.state = BridgeEquivalenceState::Equivalent;
                } else {
                    m.equivalence.state = BridgeEquivalenceState::NotEquivalent;
                    if (!plateMatch)
                        m.equivalence.issues.push_back(
                            "generator levha boyutlari gozlenen bbox ile uyusmuyor");
                    if (!boltMatch)
                        m.equivalence.issues.push_back(
                            "generator bulon duzeni gozlenen grid ile uyusmuyor");
                }
            }
        }
    }

    return m;
}

std::vector<CompParam> mappedParamsToCompParams(const BridgeNativeMapping& mapping,
                                                bool skipUnresolved) {
    std::vector<CompParam> out;
    out.reserve(mapping.params.size());
    for (const auto& pm : mapping.params) {
        if (skipUnresolved && pm.provenance == BridgeParamProvenance::Unresolved) continue;
        out.push_back(CompParam{pm.name, pm.value});
    }
    return out;
}

} // namespace mm
