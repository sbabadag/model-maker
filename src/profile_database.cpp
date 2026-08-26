#include "model_maker/profile_database.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <map>
#include <sstream>

namespace mm {
namespace {

std::string trimmed(const std::string& text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

std::string lowercased(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

// "KEY"                   1.23E+02 -> key + deger
bool parseProperty(const std::string& line, std::string& key, double& value) {
    const auto firstQuote = line.find('"');
    if (firstQuote == std::string::npos) return false;
    const auto secondQuote = line.find('"', firstQuote + 1);
    if (secondQuote == std::string::npos) return false;
    key = line.substr(firstQuote + 1, secondQuote - firstQuote - 1);
    const auto rest = line.substr(secondQuote + 1);
    if (rest.find_first_of("0123456789.eE+-") == std::string::npos) return false;
    char* end = nullptr;
    value = std::strtod(rest.c_str(), &end);
    return end != rest.c_str();
}

bool parseNameLine(const std::string& line, std::string& name) {
    const auto marker = line.find("PROFILE_NAME");
    if (marker == std::string::npos) return false;
    const auto firstQuote = line.find('"', marker);
    if (firstQuote == std::string::npos) return false;
    const auto secondQuote = line.find('"', firstQuote + 1);
    if (secondQuote == std::string::npos) return false;
    name = line.substr(firstQuote + 1, secondQuote - firstQuote - 1);
    return true;
}

} // namespace

std::vector<SteelProfile> parseTeklaProfileDatabase(const std::filesystem::path& lisFile) {
    std::vector<SteelProfile> profiles;
    std::ifstream input(lisFile);
    if (!input) return profiles;

    SteelProfile current;
    bool inProfile = false;
    std::string line;
    while (std::getline(input, line)) {
        std::string name;
        if (parseNameLine(line, name)) {
            if (inProfile) profiles.push_back(std::move(current));
            current = SteelProfile{};
            current.name = std::move(name);
            inProfile = true;
            continue;
        }
        if (!inProfile) continue;
        const auto typePos = line.find("TYPE");
        if (typePos != std::string::npos && line.find('=') != std::string::npos &&
            line.find('"') == std::string::npos) {
            std::istringstream stream(line.substr(line.find('=') + 1));
            stream >> current.type;
            if (line.find("SUB_TYPE") != std::string::npos) current.subType = current.type;
            continue;
        }
        std::string key;
        double value = 0.0;
        if (!parseProperty(line, key, value)) continue;
        if (key == "HEIGHT") current.height = value;
        else if (key == "WIDTH") current.width = value;
        else if (key == "PLATE_THICKNESS") current.plateThickness = value;
        else if (key == "FLANGE_THICKNESS") current.flangeThickness = value;
        else if (key == "ROUNDING_RADIUS") current.roundingRadius = value;
        else if (key == "CROSS_SECTION_AREA") current.crossSectionArea = value;
        else if (key == "WEIGHT_PER_UNIT_LENGTH") current.weightPerUnitLength = value;
        else if (key == "MOMENT_OF_INERTIA_X") current.inertiaX = value;
        else if (key == "MOMENT_OF_INERTIA_Y") current.inertiaY = value;
        else if (key == "SECTION_MODULUS_X") current.sectionModulusX = value;
        else if (key == "SECTION_MODULUS_Y") current.sectionModulusY = value;
        else if (key == "PLASTIC_MODULUS_X") current.plasticModulusX = value;
        else if (key == "PLASTIC_MODULUS_Y") current.plasticModulusY = value;
        else if (key == "RADIUS_OF_GYRATION_X") current.radiusOfGyrationX = value;
        else if (key == "RADIUS_OF_GYRATION_Y") current.radiusOfGyrationY = value;
        else if (key == "TORSIONAL_CONSTANT") current.torsionalConstant = value;
        else if (key == "WARPING_CONSTANT") current.warpingConstant = value;
        else if (key == "COVER_AREA") current.coverArea = value;
    }
    if (inProfile) profiles.push_back(std::move(current));

    // Cift kayitlari ilk gorulen korunarak eler.
    std::vector<SteelProfile> unique;
    std::map<std::string, std::size_t> seen;
    for (auto& profile : profiles) {
        const auto key = lowercased(profile.name);
        if (seen.emplace(key, unique.size()).second)
            unique.push_back(std::move(profile));
    }
    return unique;
}

std::vector<SteelProfile> loadProfileCatalog(const std::filesystem::path& directory) {
    std::vector<SteelProfile> catalog;
    std::map<std::string, std::size_t> seen;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(directory, ec)) {
        if (entry.path().extension() != ".lis") continue;
        auto parsed = parseTeklaProfileDatabase(entry.path());
        for (auto& profile : parsed) {
            const auto key = lowercased(profile.name);
            if (seen.emplace(key, catalog.size()).second)
                catalog.push_back(std::move(profile));
        }
    }
    return catalog;
}

const SteelProfile* findProfile(const std::vector<SteelProfile>& profiles,
                                const std::string& name) {
    const auto key = lowercased(name);
    const auto found = std::find_if(profiles.begin(), profiles.end(),
                                    [&](const SteelProfile& profile) {
                                        return lowercased(profile.name) == key;
                                    });
    return found == profiles.end() ? nullptr : &*found;
}

} // namespace mm
