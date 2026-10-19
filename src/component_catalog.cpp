#include "model_maker/component_catalog.hpp"

#include <algorithm>

namespace mm {

std::vector<CatalogEntry> buildComponentCatalog(const ComponentRegistry& reg,
                                                const ComponentDialogRegistry& dialogs) noexcept {
    std::vector<CatalogEntry> out;
    for (const std::wstring& typeId : reg.typeIds()) {
        const ComponentType* type = reg.findType(typeId);
        if (!type) continue;

        CatalogEntry e;
        e.typeId = type->typeId;
        e.displayName = type->displayName;
        e.sourceComponentNumber = type->catalog.sourceComponentNumber;
        e.category = type->catalog.category;
        e.primaryMemberType = type->catalog.primaryMemberType;
        e.secondaryMemberType = type->catalog.secondaryMemberType;
        e.supportedFeatures = type->catalog.supportedFeatures;
        e.coverageStatus = type->catalog.coverageStatus;
        e.pickCount = type->catalog.pickCount > 0 ? type->catalog.pickCount : 2;
        e.catalogVisible = type->catalog.catalogVisible;
        e.hasGenerator = reg.generator(typeId) != nullptr;
        e.hasDialog = false;

        if (const ComponentDialogDefinition* d = dialogs.find(typeId)) {
            e.hasDialog = true;
            for (const ComponentDialogGroup& g : d->schema.groups) {
                if (!g.title.empty()) e.dialogGroups.push_back(g.title);
            }
        }

        if (e.catalogVisible) out.push_back(std::move(e));
    }

    std::sort(out.begin(), out.end(), [](const CatalogEntry& a, const CatalogEntry& b) {
        if (a.category != b.category) return a.category < b.category;
        const bool aEmpty = a.sourceComponentNumber.empty();
        const bool bEmpty = b.sourceComponentNumber.empty();
        if (aEmpty != bEmpty) return bEmpty;  // bos numara sonda
        if (!aEmpty && a.sourceComponentNumber != b.sourceComponentNumber)
            return a.sourceComponentNumber < b.sourceComponentNumber;
        if (a.displayName != b.displayName) return a.displayName < b.displayName;
        return a.typeId < b.typeId;
    });
    return out;
}

const CatalogEntry* findCatalogEntry(const std::vector<CatalogEntry>& catalog,
                                     const std::wstring& typeId) noexcept {
    for (const CatalogEntry& e : catalog) {
        if (e.typeId == typeId) return &e;
    }
    return nullptr;
}

} // namespace mm
