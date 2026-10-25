#include "model_maker/component.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace mm {

bool ComponentModel::addDefinition(ComponentDefinition definition) noexcept {
    if (definition.name.empty()) return false;
    for (const auto& existing : definitions_)
        if (existing.name == definition.name) return false; // tekillik
    definitions_.push_back(std::move(definition));
    return true;
}

const ComponentDefinition* ComponentModel::findDefinition(const std::wstring& name) const noexcept {
    for (const auto& def : definitions_)
        if (def.name == name) return &def;
    return nullptr;
}

bool ComponentModel::removeDefinition(const std::wstring& name) noexcept {
    const auto it = std::find_if(definitions_.begin(), definitions_.end(),
                                 [&](const ComponentDefinition& d) { return d.name == name; });
    if (it == definitions_.end()) return false;
    definitions_.erase(it);
    // Bu tanima bagli instance'lar artik oksuz: kirlet (recompute'te orphan).
    for (auto& inst : instances_)
        if (inst.definition == name) inst.dirty = true;
    return true;
}

bool ComponentModel::setDefinitionParameters(const std::wstring& name,
                                             std::vector<std::wstring> parameters) noexcept {
    for (auto& def : definitions_) {
        if (def.name != name) continue;
        def.parameters = std::move(parameters);
        // Semasi degisen tanimin tum instance'larini kirlet.
        for (auto& inst : instances_)
            if (inst.definition == name) inst.dirty = true;
        return true;
    }
    return false;
}

void ComponentModel::realignInstance(ComponentInstance& instance) noexcept {
    const ComponentDefinition* def = findDefinition(instance.definition);
    if (!def) {
        instance.orphan = true;
        instance.dirty = false; // yeniden hesaplandi (orphan olarak isaretlendi)
        return;
    }
    instance.orphan = false;
    // Değerleri semayla hizala: semada olanlar korunur (eksik -> 0), olmayanlar atilir.
    std::vector<ComponentParameter> aligned;
    aligned.reserve(def->parameters.size());
    for (const auto& pname : def->parameters) {
        ComponentParameter p;
        p.name = pname;
        for (const auto& v : instance.values)
            if (v.name == pname) { p.value = v.value; break; }
        aligned.push_back(std::move(p));
    }
    instance.values = std::move(aligned);
    instance.dirty = false;
}

std::size_t ComponentModel::addInstance(const std::wstring& definition,
                                        std::vector<ComponentParameter> values, Vec3 origin,
                                        std::vector<ComponentMember> members) noexcept {
    ComponentInstance inst;
    inst.definition = definition;
    inst.values = std::move(values);
    inst.origin = origin;
    inst.members = std::move(members);
    inst.dirty = true;
    realignInstance(inst);
    instances_.push_back(std::move(inst));
    return instances_.size() - 1;
}

bool ComponentModel::setInstanceParameter(std::size_t index, const std::wstring& name,
                                          double value) noexcept {
    if (index >= instances_.size()) return false;
    ComponentInstance& inst = instances_[index];
    for (auto& p : inst.values)
        if (p.name == name) {
            p.value = value;
            inst.dirty = true;
            return true;
        }
    return false; // semada yok
}

bool ComponentModel::setInstanceOrigin(std::size_t index, Vec3 origin) noexcept {
    if (index >= instances_.size()) return false;
    instances_[index].origin = origin;
    instances_[index].dirty = true;
    return true;
}

std::optional<std::size_t> ComponentModel::mainPartIndex(std::size_t instanceIndex) const noexcept {
    if (instanceIndex >= instances_.size()) return std::nullopt;
    const auto& members = instances_[instanceIndex].members;
    for (std::size_t i = 0; i < members.size(); ++i)
        if (members[i].role == PartRole::Main) return i;
    return std::nullopt;
}

std::vector<EntityId> ComponentModel::partsOfInstance(std::size_t instanceIndex) const noexcept {
    std::vector<EntityId> result;
    if (instanceIndex >= instances_.size()) return result;
    for (const auto& member : instances_[instanceIndex].members) {
        if (member.partId == kInvalidEntityId) continue;
        if (std::find(result.begin(), result.end(), member.partId) == result.end())
            result.push_back(member.partId);
    }
    return result;
}

std::vector<std::size_t> ComponentModel::instancesUsingPart(EntityId partId) const noexcept {
    std::vector<std::size_t> result;
    if (partId == kInvalidEntityId) return result;
    for (std::size_t i = 0; i < instances_.size(); ++i)
        for (const auto& member : instances_[i].members)
            if (member.partId == partId) { result.push_back(i); break; }
    return result;
}

std::vector<std::size_t> ComponentModel::dirtyInstances() const noexcept {
    std::vector<std::size_t> result;
    for (std::size_t i = 0; i < instances_.size(); ++i)
        if (instances_[i].dirty) result.push_back(i);
    return result;
}

std::size_t ComponentModel::recomputeDirty() noexcept {
    std::size_t count = 0;
    for (auto& inst : instances_) {
        if (!inst.dirty) continue;
        realignInstance(inst);
        ++count;
    }
    return count;
}

ComponentDefinition makeEndPlateDefinition() noexcept {
    ComponentDefinition def;
    def.name = L"End Plate";
    def.parameters = {L"plateThickness", L"plateWidth", L"plateHeight"};
    return def;
}

const ComponentMember* endPlateMainPart(const ComponentInstance& instance) noexcept {
    for (const auto& member : instance.members)
        if (member.role == PartRole::Main && member.partId != kInvalidEntityId)
            return &member;
    return nullptr;
}

CutModifier endPlateFittingCutModifier(const ComponentInstance& instance,
                                       const Vec3& axisFrom, const Vec3& axisTo) noexcept {
    CutModifier mod;
    mod.kind = CutKind::Fitting;
    mod.name = "End Plate Fitting";
    mod.enabled = true;
    const auto* main = endPlateMainPart(instance);
    if (main) mod.target = main->partId;
    const double dx = axisTo.x - axisFrom.x;
    const double dy = axisTo.y - axisFrom.y;
    const double dz = axisTo.z - axisFrom.z;
    const double len = std::sqrt(dx * dx + dy * dy + dz * dz);
    Vec3 dir{1.0, 0.0, 0.0};
    if (len > 1e-9) dir = {dx / len, dy / len, dz / len};
    mod.planeNormal = dir;
    mod.planeOrigin = axisTo;  // ana part ucu; plaka disa dogru uzanir
    mod.keepPositive = true;   // Fitting tarafi otomatik; bu alan yok sayilir
    return mod;
}

} // namespace mm
