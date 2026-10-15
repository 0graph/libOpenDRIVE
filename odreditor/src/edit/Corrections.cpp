#include "Corrections.h"
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace odreditor
{
namespace
{
struct AttributeEdit
{
    NodeId      Node;
    std::string Name;
    std::string Value;
};
struct ValidityEdit
{
    NodeId                           Node;
    std::vector<std::pair<int, int>> Ranges;
};
bool Affected(NodeId id, const IdChanges& changes, const DeletedNodes& deleted)
{
    return changes.count(id) || deleted.count(id);
}
bool SameKey(EntityKind kind, const std::string& a, const std::string& b)
{
    return kind == EntityKind::Lane ? ReferenceIndex::LaneNumber(a) == ReferenceIndex::LaneNumber(b) : a == b;
}
bool HasAncestor(const XodrDocument& document, NodeId id, const DeletedNodes& nodes)
{
    for (auto node = document.GetNode(id); node; node = node.parent())
        if (nodes.count(document.GetId(node)))
            return true;
    return false;
}
std::vector<std::pair<int, int>> Ranges(std::vector<int> ids)
{
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    std::vector<std::pair<int, int>> result;
    for (int id : ids)
    {
        if (result.empty() || static_cast<long long>(result.back().second) + 1 != id)
            result.emplace_back(id, id);
        else
            result.back().second = id;
    }
    return result;
}
std::vector<ValidityEdit>
PlanValidities(const XodrDocument& document, const ReferenceIndex& index, const IdChanges& changes, const DeletedNodes& deleted)
{
    std::set<NodeId> affectedSections, affectedRoads;
    for (const auto& entity : index.GetEntities())
        if (entity.Kind == EntityKind::Lane && Affected(entity.Id, changes, deleted))
        {
            affectedSections.insert(entity.Scope);
            affectedRoads.insert(index.GetRoad(entity.Id));
        }
    std::vector<ValidityEdit> edits;
    for (const auto& scope : index.GetValidities())
    {
        if (HasAncestor(document, scope.Owner, deleted))
            continue;
        if (!scope.Error.empty() && affectedRoads.count(scope.Road))
            throw EditException(scope.Owner, scope.Error);
        if (std::none_of(scope.Sections.begin(), scope.Sections.end(), [&](NodeId section) { return affectedSections.count(section); }))
            continue;
        bool                      ownerHasMembers = false;
        std::vector<ValidityEdit> ownerEdits;
        for (auto validity : document.GetNode(scope.Owner).children("validity"))
        {
            auto from = ReferenceIndex::LaneNumber(validity.attribute("fromLane").value()),
                 to = ReferenceIndex::LaneNumber(validity.attribute("toLane").value());
            if (!from || !to || *from > *to)
                throw EditException(scope.Owner, "Validity lane range is invalid");
            bool affected = false;
            for (const auto& lane : index.GetEntities())
            {
                if (lane.Kind != EntityKind::Lane || std::find(scope.Sections.begin(), scope.Sections.end(), lane.Scope) == scope.Sections.end())
                    continue;
                const auto number = ReferenceIndex::LaneNumber(lane.Key);
                if (!number || *number < *from || *number > *to)
                    continue;
                affected |= Affected(lane.Id, changes, deleted);
                ownerHasMembers |= !deleted.count(lane.Id);
            }
            if (!affected)
                continue;
            bool                             changed = false, first = true;
            std::vector<std::pair<int, int>> encoded;
            for (auto section : scope.Sections)
            {
                std::vector<int> membership;
                std::set<int>    unique;
                for (const auto& lane : index.GetEntities())
                {
                    if (lane.Kind != EntityKind::Lane || lane.Scope != section)
                        continue;
                    auto number = ReferenceIndex::LaneNumber(lane.Key);
                    if (!number)
                        throw EditException(scope.Owner, "Validity lane identity is invalid");
                    if (*number < *from || *number > *to)
                        continue;
                    if (!unique.insert(*number).second)
                        throw EditException(scope.Owner, "Validity lane identity is ambiguous");
                    if (Affected(lane.Id, changes, deleted))
                        changed = true;
                    if (deleted.count(lane.Id))
                        continue;
                    membership.push_back(changes.count(lane.Id) ? *ReferenceIndex::LaneNumber(changes.at(lane.Id)) : *number);
                }
                auto ranges = Ranges(std::move(membership));
                if (!first && ranges != encoded)
                    throw EditException(scope.Owner, "Validity spans sections with different lane mappings");
                encoded = std::move(ranges);
                first = false;
            }
            ownerHasMembers |= !encoded.empty();
            if (changed)
                ownerEdits.push_back({document.GetId(validity), std::move(encoded)});
        }
        if (!ownerEdits.empty() && !ownerHasMembers)
            throw EditException(scope.Owner, "Empty validity would broaden applicability; edit its owner or use raw XML");
        edits.insert(edits.end(), ownerEdits.begin(), ownerEdits.end());
    }
    return edits;
}
} // namespace
DeletedNodes CollectSubtree(const XodrDocument& document, NodeId root)
{
    DeletedNodes                nodes;
    std::function<void(NodeId)> visit = [&](NodeId id)
    {
        nodes.insert(id);
        for (auto child : document.GetChildren(id))
            visit(child);
    };
    visit(root);
    return nodes;
}
bool IsLaneSide(pugi::xml_node node)
{
    const std::string_view side = node.name();
    return (side == "left" || side == "right" || side == "center") && std::string_view(node.parent().name()) == "laneSection" &&
           std::string_view(node.parent().parent().name()) == "lanes" && GetEntityKind(node.parent().parent().parent()) == EntityKind::Road;
}
std::vector<NodeId> GetSideLanes(const XodrDocument& document, NodeId side)
{
    auto              node = document.GetNode(side);
    const std::string name = node.name();
    if (!IsLaneSide(node))
        throw std::runtime_error("Select a lane side");
    std::vector<std::pair<long long, NodeId>> lanes;
    std::set<int>                             ids;
    for (auto lane : node.children("lane"))
    {
        auto number = ReferenceIndex::LaneNumber(lane.attribute("id").value());
        if (!number || (name == "left" && *number <= 0) || (name == "right" && *number >= 0) || (name == "center" && *number != 0))
            throw std::runtime_error("Lane ID does not match its side; use raw XML");
        if (!ids.insert(*number).second)
            throw std::runtime_error("Lane ID is ambiguous");
        lanes.emplace_back(std::abs(static_cast<long long>(*number)), document.GetId(lane));
    }
    std::sort(lanes.begin(), lanes.end());
    std::vector<NodeId> result;
    for (const auto& lane : lanes)
        result.push_back(lane.second);
    return result;
}
void NumberLanes(const XodrDocument& document, NodeId side, const std::vector<NodeId>& order, IdChanges& changes)
{
    const std::string name = document.GetNode(side).name();
    if (name == "center" && order.size() > 1)
        throw std::runtime_error("The center already has a lane");
    const int sign = name == "right" ? -1 : name == "left" ? 1 : 0;
    for (std::size_t i = 0; i < order.size(); ++i)
    {
        const auto value = std::to_string(sign * static_cast<int>(i + 1));
        if (value != document.GetNode(order[i]).attribute("id").value())
            changes[order[i]] = value;
    }
}
void ApplyCorrections(XodrDocument& document, const ReferenceIndex& index, const IdChanges& proposed, const DeletedNodes& deleted)
{
    IdChanges changes;
    for (const auto& [id, value] : proposed)
        if (value != document.GetNode(id).attribute("id").value())
            changes[id] = value;
    std::vector<const Entity*> affectedEntities;
    for (const auto& entity : index.GetEntities())
        if (Affected(entity.Id, changes, deleted))
            affectedEntities.push_back(&entity);
    std::vector<AttributeEdit> edits;
    DeletedNodes               removals = deleted;
    for (const auto& reference : index.GetReferences())
    {
        if (HasAncestor(document, reference.Source, deleted))
            continue;
        bool affected =
            std::any_of(reference.Target.Targets.begin(), reference.Target.Targets.end(), [&](NodeId id) { return Affected(id, changes, deleted); });
        for (const auto* pointer : affectedEntities)
        {
            const auto& entity = *pointer;
            if (entity.Kind != reference.Kind)
                continue;
            if (std::find(reference.Target.Scopes.begin(), reference.Target.Scopes.end(), entity.Scope) == reference.Target.Scopes.end())
                continue;
            if (!reference.Target.Error.empty())
                affected = true;
            if (reference.Target.Targets.empty() && changes.count(entity.Id) && SameKey(entity.Kind, reference.Key, changes.at(entity.Id)))
                throw EditException(reference.Source, "Unresolved reference would attach to a different identity");
        }
        if (!affected)
            continue;
        if (reference.Target.Targets.size() != 1 || !reference.Target.Error.empty())
            throw EditException(reference.Source, reference.Target.Error.empty() ? "Reference target is ambiguous" : reference.Target.Error);
        const auto target = reference.Target.Targets.front();
        if (deleted.count(target))
        {
            if (reference.OnDelete == DeleteRule::ClearJunction)
                edits.push_back({reference.Source, reference.Attribute, "-1"});
            else
                removals.insert(reference.Source);
        }
        else if (changes.count(target))
            edits.push_back({reference.Source, reference.Attribute, changes.at(target)});
    }
    auto validities = PlanValidities(document, index, changes, deleted);
    for (const auto& [id, value] : changes)
        if (!HasAncestor(document, id, removals))
            document.SetAttribute(id, "id", value);
    for (const auto& edit : edits)
        if (!HasAncestor(document, edit.Node, removals))
            document.SetAttribute(edit.Node, edit.Name, edit.Value);
    for (const auto& validity : validities)
    {
        if (HasAncestor(document, validity.Node, removals))
            continue;
        if (validity.Ranges.empty())
        {
            removals.insert(validity.Node);
            continue;
        }
        for (std::size_t i = 0; i < validity.Ranges.size(); ++i)
        {
            const auto id = i == 0 ? validity.Node : document.DuplicateElement(validity.Node);
            document.SetAttribute(id, "fromLane", std::to_string(validity.Ranges[i].first));
            document.SetAttribute(id, "toLane", std::to_string(validity.Ranges[i].second));
        }
    }
    std::vector<NodeId> roots;
    for (auto id : removals)
    {
        auto node = document.GetNode(id);
        if (node && !HasAncestor(document, document.GetId(node.parent()), removals))
            roots.push_back(id);
    }
    for (auto id : roots)
        document.RemoveElement(id);
}
void CheckPreservedReferences(const XodrDocument& document, const ReferenceIndex& before)
{
    ReferenceIndex after(document);
    for (const auto& reference : before.GetReferences())
    {
        if (!document.GetNode(reference.Source))
            continue;
        auto found =
            std::find_if(after.GetReferences().begin(),
                         after.GetReferences().end(),
                         [&](const Reference& current) { return current.Source == reference.Source && current.Attribute == reference.Attribute; });
        if (found == after.GetReferences().end() || found->Target.Targets != reference.Target.Targets ||
            found->Target.Error != reference.Target.Error)
            throw EditException(reference.Source, "Edit changes a reference target; edit the relationship first");
    }
    auto memberships = [&](const ReferenceIndex& index, const ValidityScope& scope)
    {
        std::set<std::set<NodeId>> result;
        for (auto section : scope.Sections)
        {
            std::set<NodeId> members;
            for (const auto& [from, to] : scope.Ranges)
            {
                for (const auto& entity : index.GetEntities())
                {
                    if (entity.Kind != EntityKind::Lane || entity.Scope != section)
                        continue;
                    const auto number = ReferenceIndex::LaneNumber(entity.Key);
                    if (number && from <= *number && *number <= to)
                        members.insert(entity.Id);
                }
            }
            result.insert(std::move(members));
        }
        return result;
    };
    for (const auto& scope : before.GetValidities())
    {
        if (!scope.Error.empty())
            continue;
        auto found = std::find_if(
            after.GetValidities().begin(), after.GetValidities().end(), [&](const ValidityScope& current) { return current.Owner == scope.Owner; });
        if (found == after.GetValidities().end() || !found->Error.empty() || memberships(before, scope) != memberships(after, *found))
            throw EditException(scope.Owner, "Edit changes validity membership; edit its owner first");
    }
}
} // namespace odreditor
