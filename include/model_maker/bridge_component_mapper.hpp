#pragma once
// MODEL MAKER BRIDGE — Tekla kaynak nesnelerini Model Maker semantik türlerine
// esleyen katman (import preview). OCCT'siz; Linux OCC-off ctest'te kosar.
//
// DURUSTLUK: yalnizca model-maker kernel'in GERCEKTEN destekledigi türler
// "supported" isaretlenir (Plate, BoltGroup). Weld/Fitting/Boolean/karmasik
// sekiller "Unsupported"/"ReferenceGeometry" olarak korunur, UYDURULMAZ.

#include "model_maker/bridge_package.hpp"

#include <optional>
#include <string>
#include <vector>

namespace mm {

// Bir kaynak nesnenin eslenmis kaydi.
struct BridgeSemanticRecord {
    std::string sourceGUID;
    std::string roleKey;       // kararli (kind + ordinal); Model Maker roleKey'i DEGIL
    std::string sourceKind;    // Tekla kind
    std::string mappedKind;    // Plate/BoltGroup/Beam/Weld/Cut/ReferenceGeometry/Unsupported
    bool supported = false;    // model-maker kernel bu türü destekliyor mu
    std::string unsupportedReason;
    std::string profile;
    std::string material;
    std::optional<BridgeBBox> bbox;
};

struct BridgeImportResult {
    bool ok = false;
    std::vector<std::string> errors;

    std::string componentType;
    std::string componentName;
    std::string componentGuid;
    int componentNumber = 0;

    std::vector<BridgeSemanticRecord> records;
    std::vector<BridgeParameter> parameters;

    std::vector<std::string> limitations;
    std::vector<std::string> warnings;

    int supportedCount = 0;
    int unsupportedCount = 0;

    std::string previewText;  // insan-okunur ozet (UI icin)
};

// Paketi esler. ok=false ise errors dolu (yarim sonuc kullanilmamali).
BridgeImportResult mapBridgePackage(const BridgePackage& pkg);

}  // namespace mm
