// End plate paketi URETICI — parameters.json / dialog.json / manifest.json
// mevcut tanimlardan (endPlateDialogSpecs / endPlateDialogDefinition) codec ile
// uretilir. ELLE YAZILMAZ. Kullanim:  mm_gen_end_plate_package <paket_dizini>
#include "model_maker/component_codec.hpp"
#include "model_maker/component_dialog.hpp"
#include "model_maker/component_engine.hpp"
#include "mm_contract/contract_version.hpp"

#include <nlohmann/json.hpp>

#include <cstdio>
#include <fstream>
#include <string>

using namespace mm;
using json = nlohmann::json;

static bool writeJson(const std::string& path, const json& j) {
    std::ofstream out(path);
    if (!out.good()) { std::printf("HATA: yazilamadi: %s\n", path.c_str()); return false; }
    out << j.dump(2) << "\n";
    return true;
}

int main(int argc, char** argv) {
    const std::string dir = (argc > 1) ? argv[1] : "packages/end_plate";
    const ComponentType type = endPlateComponentType();
    const ComponentDialogDefinition def = endPlateDialogDefinition();
    const std::vector<CompParamSpec> dialogSpecs = endPlateDialogSpecs();

    // parameters.json — dialog parametre semasindan (endPlateDialogSpecs) URETILIR.
    json parameters;
    parameters["formatVersion"] = kCodecFormatVersion;
    parameters["typeId"] = utf8FromWide(type.typeId);
    json arr = json::array();
    for (const auto& s : dialogSpecs) arr.push_back(compParamSpecToJson(s));
    parameters["parameters"] = arr;

    // dialog.json — endPlateDialogDefinition().schema'dan URETILIR.
    const json dialog = componentDialogSchemaToJson(def.schema);

    // manifest.json — kayit tanimindan URETILIR.
    json manifest;
    manifest["formatVersion"] = kCodecFormatVersion;
    manifest["typeId"] = utf8FromWide(type.typeId);
    manifest["displayName"] = utf8FromWide(type.displayName);
    manifest["schemaVersion"] = type.schemaVersion;
    manifest["contractVersion"] = kContractVersion;

    bool ok = writeJson(dir + "/parameters.json", parameters);
    ok = writeJson(dir + "/dialog.json", dialog) && ok;
    ok = writeJson(dir + "/manifest.json", manifest) && ok;
    std::printf("end plate paketi %s: %s\n", ok ? "yazildi" : "HATALI", dir.c_str());
    return ok ? 0 : 1;
}
