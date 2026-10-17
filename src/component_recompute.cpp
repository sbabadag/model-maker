#include "model_maker/component_recompute.hpp"

namespace mm {

void ComponentDependencyGraph::build(const std::vector<ComponentInstanceRecord>& components) {
    partToComponents_.clear();
    componentCount_ = components.size();
    for (const ComponentInstanceRecord& rec : components) {
        if (rec.id == kInvalidEntityId) continue;
        if (rec.mainPart != kInvalidEntityId) partToComponents_[rec.mainPart].insert(rec.id);
        for (EntityId s : rec.secondaryParts)
            if (s != kInvalidEntityId) partToComponents_[s].insert(rec.id);
    }
}

std::vector<EntityId> ComponentDependencyGraph::dependents(EntityId partId) const {
    std::vector<EntityId> out;
    const auto it = partToComponents_.find(partId);
    if (it == partToComponents_.end()) return out;
    out.assign(it->second.begin(), it->second.end());
    return out;
}

std::vector<EntityId> ComponentDependencyGraph::dependentsOf(
    const std::vector<EntityId>& parts) const {
    std::set<EntityId> acc;
    for (EntityId p : parts) {
        const auto it = partToComponents_.find(p);
        if (it == partToComponents_.end()) continue;
        acc.insert(it->second.begin(), it->second.end());
    }
    return std::vector<EntityId>(acc.begin(), acc.end());
}

bool ComponentDependencyGraph::dependsOn(const ComponentInstanceRecord& record,
                                         EntityId partId) {
    if (record.mainPart == partId) return true;
    for (EntityId s : record.secondaryParts)
        if (s == partId) return true;
    return false;
}

RecomputePlanner::Plan RecomputePlanner::plan(
    const std::vector<ComponentInstanceRecord>& components,
    const std::vector<EntityId>& changedParts, const Resolver& resolver) const {
    ComponentDependencyGraph graph;
    graph.build(components);
    const std::vector<EntityId> affected = graph.dependentsOf(changedParts);

    Plan plan;
    plan.items.reserve(affected.size());
    for (EntityId cid : affected) {
        const ComponentInstanceRecord* rec = nullptr;
        for (const ComponentInstanceRecord& c : components)
            if (c.id == cid) {
                rec = &c;
                break;
            }
        if (!rec) {
            RecomputeItem item;
            item.componentId = cid;
            item.status = RecomputeStatus::Unresolved;
            item.error = L"component kaydi bulunamadi";
            plan.items.push_back(std::move(item));
            continue;
        }
        plan.items.push_back(resolver(*rec));
    }

    plan.allSucceeded = true;
    for (const RecomputeItem& it : plan.items)
        if (it.status != RecomputeStatus::Success) {
            plan.allSucceeded = false;
            break;
        }
    return plan;
}

} // namespace mm
