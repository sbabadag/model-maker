// End plate paketi URETICI — parameters.json / dialog.json / manifest.json
// mevcut tanimlardan (endPlateDialogSpecs / endPlateDialogDefinition) codec ile
// uretilir; recipe.json elle yazilir ve ARAÇ tarafindan KANONIK metne normalize
// edilir (recipeCanonicalText). ELLE YAZILMAZ.
// Kullanim:  mm_gen_end_plate_package <paket_dizini>
#include "model_maker/component_codec.hpp"
#include "model_maker/component_dialog.hpp"
#include "model_maker/component_engine.hpp"
#include "mm_contract/contract_version.hpp"

#include <nlohmann/json.hpp>

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

using namespace mm;
using json = nlohmann::json;

static std::string readText(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in.good()) return {};
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// recipe.json elle yazilir; araç onu okuyup KANONIK metne (anahtar sirasi +
// alfabetik args + LF-only) normalize eder. Bozuk/okunamazsa HATA.
static bool writeRecipe(const std::string& path) {
    const std::string text = readText(path);
    if (text.empty()) {
        std::printf("HATA: recipe.json okunamadi: %s\n", path.c_str());
        return false;
    }
    const Recipe r = recipeFromJson(nlohmann::json::parse(text, nullptr, false));
    if (r.steps.empty()) {
        std::printf("HATA: recipe.json steps bos: %s\n", path.c_str());
        return false;
    }
    return writePackageJsonFile(path, recipeCanonicalText(r));
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
    manifest["minContractVersion"] = kContractVersion;

    bool ok = writePackageJsonFile(dir + "/parameters.json", packageJsonText(parameters));
    ok = writePackageJsonFile(dir + "/dialog.json", packageJsonText(dialog)) && ok;
    ok = writePackageJsonFile(dir + "/manifest.json", packageJsonText(manifest)) && ok;
    ok = writeRecipe(dir + "/recipe.json") && ok;
    std::printf("end plate paketi %s: %s\n", ok ? "yazildi" : "HATALI", dir.c_str());
    return ok ? 0 : 1;
}
