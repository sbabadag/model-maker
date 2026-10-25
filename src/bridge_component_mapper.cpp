// MODEL MAKER BRIDGE — esleyici (uygulama).

#include "model_maker/bridge_component_mapper.hpp"

#include <cctype>
#include <sstream>

namespace mm {

namespace {

// Tekla plaka profili mi? ("PL10*200", "PLT..."). Yapısal profil (HEA/IPE)
// plaka DEGILDIR.
bool isPlateProfile(const std::string& profile) {
    if (profile.size() < 2) return false;
    char a = static_cast<char>(std::toupper(static_cast<unsigned char>(profile[0])));
    char b = static_cast<char>(std::toupper(static_cast<unsigned char>(profile[1])));
    return a == 'P' && b == 'L';
}

std::string bboxDim(const std::optional<BridgeBBox>& b) {
    if (!b.has_value()) return "?";
    std::ostringstream os;
    os << static_cast<int>(b->max.x - b->min.x) << "x"
       << static_cast<int>(b->max.y - b->min.y) << "x"
       << static_cast<int>(b->max.z - b->min.z);
    return os.str();
}

}  // namespace

BridgeImportResult mapBridgePackage(const BridgePackage& pkg) {
    BridgeImportResult res;
    res.componentType = pkg.manifest.sourceComponentType;
    res.componentName = pkg.manifest.sourceComponentName.empty() ? pkg.component.name
                                                                : pkg.manifest.sourceComponentName;
    res.componentGuid = pkg.component.guid;
    res.componentNumber = pkg.component.number;
    res.parameters = pkg.parameters;
    res.limitations = pkg.knownLimitations;
    res.warnings = pkg.warnings;

    int plateNo = 0, boltNo = 0, weldNo = 0, beamNo = 0, otherNo = 0;

    for (const auto& obj : pkg.objects) {
        BridgeSemanticRecord rec;
        rec.sourceGUID = obj.sourceGUID;
        rec.sourceKind = obj.kind;
        rec.profile = obj.profile;
        rec.material = obj.material;
        rec.bbox = obj.bbox;

        if (obj.kind == "BoltGroup") {
            rec.mappedKind = "BoltGroup";
            rec.supported = true;
            rec.roleKey = "bolt_group_" + std::to_string(boltNo++);
        } else if (obj.kind == "ContourPlate") {
            rec.mappedKind = "Plate";
            rec.supported = true;
            rec.roleKey = "plate_" + std::to_string(plateNo++);
        } else if (obj.kind == "Beam" || obj.kind == "PolyBeam") {
            if (isPlateProfile(obj.profile)) {
                rec.mappedKind = "Plate";
                rec.supported = true;
                rec.roleKey = "plate_" + std::to_string(plateNo++);
            } else {
                // Yapısal profilli child beam: uye, component tarafindan sahiplenilmez.
                rec.mappedKind = "Beam";
                rec.supported = false;
                rec.roleKey = "beam_" + std::to_string(beamNo++);
                rec.unsupportedReason =
                    "component child'i yapisal kiriş; Model Maker'da uye olarak " +
                    std::string("yeniden uretilmez (referans geometri).");
            }
        } else if (obj.kind == "Weld") {
            rec.mappedKind = "Weld";
            rec.supported = false;
            rec.roleKey = "weld_" + std::to_string(weldNo++);
            rec.unsupportedReason = "model-maker weld geometri motoru yok.";
        } else if (obj.kind == "BooleanPart") {
            rec.mappedKind = "Cut";
            rec.supported = false;
            rec.roleKey = "cut_" + std::to_string(otherNo++);
            rec.unsupportedReason = "Tekla boolean/cut geometrisi bu dilimde yeniden uretilmez.";
        } else if (obj.kind == "Fitting") {
            rec.mappedKind = "Unsupported";
            rec.supported = false;
            rec.roleKey = "unsupported_" + std::to_string(otherNo++);
            rec.unsupportedReason = "fitting geometrisi yeniden uretilmez.";
        } else {
            rec.mappedKind = "Unsupported";
            rec.supported = false;
            rec.roleKey = "unsupported_" + std::to_string(otherNo++);
            rec.unsupportedReason = "bilinmeyen nesne türü: " + obj.kind;
        }

        if (rec.supported) res.supportedCount++;
        else res.unsupportedCount++;

        res.records.push_back(std::move(rec));
    }

    // preview metni.
    std::ostringstream os;
    os << res.componentType << " " << res.componentNumber;
    if (!res.componentName.empty()) os << " \"" << res.componentName << "\"";
    if (!res.componentGuid.empty()) os << " (" << res.componentGuid << ")";
    os << "\n";
    os << pkg.objects.size() << " kaynak nesne -> " << res.supportedCount
       << " destekleniyor, " << res.unsupportedCount << " desteklenmiyor\n";
    for (const auto& r : res.records) {
        os << "  " << r.roleKey << "  " << r.mappedKind << "  "
           << (r.supported ? "destekleniyor" : "desteklenmiyor");
        if (r.bbox.has_value()) os << "  (" << bboxDim(r.bbox) << ")";
        if (!r.unsupportedReason.empty()) os << "  [" << r.unsupportedReason << "]";
        os << "\n";
    }

    res.previewText = os.str();
    res.ok = true;
    return res;
}

}  // namespace mm
