#include "model_maker/component_recompute.hpp"

namespace mm {

ComponentRefs componentRefs(const ComponentInstanceRecord& record) noexcept {
    ComponentRefs refs;
    refs.id = record.id;
    if (record.mainPart != kInvalidEntityId) refs.parts.push_back(record.mainPart);
    for (EntityId s : record.secondaryParts)
        if (s != kInvalidEntityId) refs.parts.push_back(s);
    return refs;
}

ComponentRefs componentRefs(const EndPlateConnection& conn) noexcept {
    ComponentRefs refs;
    refs.id = conn.id;
    if (conn.mainPart != kInvalidEntityId) refs.parts.push_back(conn.mainPart);
    if (conn.secondaryPart != kInvalidEntityId) refs.parts.push_back(conn.secondaryPart);
    return refs;
}

void ComponentDependencyGraph::buildFromRefs(const std::vector<ComponentRefs>& refs) {
    partToComponents_.clear();
    componentCount_ = refs.size();
    for (const ComponentRefs& r : refs) {
        if (r.id == kInvalidEntityId) continue;
        for (EntityId p : r.parts)
            if (p != kInvalidEntityId) partToComponents_[p].insert(r.id);
    }
}

void ComponentDependencyGraph::build(const std::vector<ComponentInstanceRecord>& components) {
    std::vector<ComponentRefs> refs;
    refs.reserve(components.size());
    for (const ComponentInstanceRecord& rec : components) refs.push_back(componentRefs(rec));
    buildFromRefs(refs);
}

void ComponentDependencyGraph::build(const std::vector<ComponentRefs>& components) {
    buildFromRefs(components);
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

bool ComponentDependencyGraph::dependsOn(const ComponentRefs& refs, EntityId partId) noexcept {
    for (EntityId p : refs.parts)
        if (p == partId) return true;
    return false;
}

RecomputePlanner::Plan RecomputePlanner::plan(
    const std::vector<ComponentInstanceRecord>& components,
    const std::vector<EntityId>& changedParts, const Resolver& resolver) const {
    std::vector<ComponentRefs> refs;
    refs.reserve(components.size());
    for (const ComponentInstanceRecord& rec : components) refs.push_back(componentRefs(rec));
    return planRefs(refs, changedParts,
                    [&](EntityId cid) -> RecomputeItem {
                        for (const ComponentInstanceRecord& c : components)
                            if (c.id == cid) return resolver(c);
                        RecomputeItem item;
                        item.componentId = cid;
                        item.status = RecomputeStatus::Unresolved;
                        item.error = L"component kaydi bulunamadi";
                        return item;
                    });
}

RecomputePlanner::Plan RecomputePlanner::planRefs(
    const std::vector<ComponentRefs>& components,
    const std::vector<EntityId>& changedParts, const ResolverById& resolver) const {
    ComponentDependencyGraph graph;
    graph.build(components);
    const std::vector<EntityId> affected = graph.dependentsOf(changedParts);

    Plan plan;
    plan.items.reserve(affected.size());
    for (EntityId cid : affected) plan.items.push_back(resolver(cid));

    plan.allSucceeded = true;
    for (const RecomputeItem& it : plan.items)
        if (it.status != RecomputeStatus::Success) {
            plan.allSucceeded = false;
            break;
        }
    return plan;
}

} // namespace mm
