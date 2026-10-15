#include "Commands.h"
#include <algorithm>
#include <set>
#include <stdexcept>
#include <string_view>

namespace odreditor
{
namespace
{
void ValidateRenames(const XodrDocument& document, const ReferenceIndex& index, const IdChanges& changes)
{
    for (const auto& [id, value] : changes)
    {
        const auto* entity = index.GetEntity(id);
        if (!entity || value.empty())
            throw std::runtime_error("Select a known identity and a nonempty ID");
        if (entity->Kind == EntityKind::Lane)
        {
            auto              number = ReferenceIndex::LaneNumber(value);
            const std::string side = document.GetNode(id).parent().name();
            if (!number || (side == "left" && *number <= 0) || (side == "right" && *number >= 0) || (side == "center" && *number != 0))
                throw std::runtime_error("Lane ID must match its side; move the lane to change sides");
        }
        for (const auto& other : index.GetEntities())
        {
            if (other.Id == id || other.Kind != entity->Kind || other.Scope != entity->Scope)
                continue;
            const auto& key = changes.count(other.Id) ? changes.at(other.Id) : other.Key;
            if ((entity->Kind == EntityKind::Lane && ReferenceIndex::LaneNumber(key) == ReferenceIndex::LaneNumber(value)) || key == value)
                throw std::runtime_error("ID already exists in this scope");
        }
    }
}
std::string FreshId(const ReferenceIndex& index, EntityKind kind, NodeId scope, const std::set<std::string>& reserved = {})
{
    for (unsigned value = 1;; ++value)
    {
        const auto key = std::to_string(value);
        const bool referenced =
            std::any_of(index.GetReferences().begin(),
                        index.GetReferences().end(),
                        [&](const Reference& reference)
                        {
                            return reference.Kind == kind && reference.Key == key &&
                                   std::find(reference.Target.Scopes.begin(), reference.Target.Scopes.end(), scope) != reference.Target.Scopes.end();
                        });
        if (!reserved.count(key) && !referenced && index.Find(kind, key, {scope}).Targets.empty())
            return key;
    }
}
std::vector<NodeId> Descendants(const XodrDocument& document, NodeId root)
{
    std::vector<NodeId>         result;
    std::function<void(NodeId)> visit = [&](NodeId id)
    {
        result.push_back(id);
        for (auto child : document.GetChildren(id))
            visit(child);
    };
    visit(root);
    return result;
}
void CheckDestination(const ReferenceIndex& index, const std::vector<NodeId>& moved)
{
    for (auto id : moved)
    {
        const auto* entity = index.GetEntity(id);
        if (entity && index.Find(entity->Kind, entity->Key, {entity->Scope}).Targets.size() != 1)
            throw std::runtime_error("Destination contains a duplicate ID");
    }
}
} // namespace
EditResult Commands::RenameIds(const IdChanges& changes, NodeId& selection)
{
    return Document.Apply("Rename identity",
                          selection,
                          [&]
                          {
                              ReferenceIndex index(Document);
                              ValidateRenames(Document, index, changes);
                              ApplyCorrections(Document, index, changes, {});
                          });
}
EditResult Commands::SetAttribute(NodeId id, const std::string& name, const std::optional<std::string>& value, NodeId& selection)
{
    auto node = Document.GetNode(id);
    auto current = node.attribute(name.c_str());
    if (node && ((value && current && *value == current.value()) || (!value && !current)))
        return {};
    if (name == "id" && GetEntityKind(Document.GetNode(id)) != EntityKind::None)
    {
        if (!value)
            return {"Known identities need an ID; use raw XML to remove it", id};
        return RenameIds({{id, *value}}, selection);
    }
    return Document.Apply("Edit attribute", selection, [&] { Document.SetAttribute(id, name, value); });
}
EditResult Commands::RenameElement(NodeId id, const std::string& name, NodeId& selection)
{
    if (Document.GetNode(id) && name == Document.GetNode(id).name())
        return {};
    if (GetEntityKind(Document.GetNode(id)) != EntityKind::None && name != Document.GetNode(id).name())
        return {"Use raw XML to change an identity-bearing element type", id};
    return Document.Apply("Rename element",
                          selection,
                          [&]
                          {
                              ReferenceIndex before(Document);
                              Document.RenameElement(id, name);
                              CheckDestination(ReferenceIndex(Document), Descendants(Document, id));
                              CheckPreservedReferences(Document, before);
                          });
}
EditResult Commands::SetText(NodeId id, const std::string& value, NodeId& selection)
{
    return Document.Apply("Edit text", selection, [&] { Document.SetText(id, value); });
}
EditResult Commands::InsertLane(NodeId side, std::size_t position, NodeId& selection)
{
    return Document.Apply("Add lane",
                          selection,
                          [&]
                          {
                              ReferenceIndex index(Document);
                              auto           order = GetSideLanes(Document, side);
                              if (position > order.size())
                                  throw std::runtime_error("Lane position is outside this side");
                              const std::string sideName = Document.GetNode(side).name();
                              if (sideName == "center" && !order.empty())
                                  throw std::runtime_error("The center already has a lane");
                              IdChanges changes;
                              const int sign = sideName == "right" ? -1 : sideName == "left" ? 1 : 0;
                              for (std::size_t i = 0; i < order.size(); ++i)
                                  changes[order[i]] = std::to_string(sign * static_cast<int>(i + 1 + (i >= position)));
                              ApplyCorrections(Document, index, changes, {});
                              selection = Document.InsertElement(side, "lane", position < order.size() ? order[position] : 0);
                              Document.SetAttribute(selection, "id", std::to_string(sign * static_cast<int>(position + 1)));
                              Document.SetAttribute(selection, "type", sideName == "center" ? "none" : "driving");
                              CheckPreservedReferences(Document, index);
                          });
}
EditResult Commands::Insert(NodeId parent, const std::string& name, NodeId& selection)
{
    auto owner = Document.GetNode(parent);
    if (name == "lane" && IsLaneSide(owner))
    {
        std::size_t count = 0;
        for (auto lane : owner.children("lane"))
        {
            (void)lane;
            ++count;
        }
        return InsertLane(parent, count, selection);
    }
    return Document.Apply("Add element",
                          selection,
                          [&]
                          {
                              ReferenceIndex before(Document);
                              selection = Document.InsertElement(parent, name);
                              ReferenceIndex after(Document);
                              if (const auto* entity = after.GetEntity(selection))
                                  Document.SetAttribute(selection, "id", FreshId(before, entity->Kind, entity->Scope));
                              CheckPreservedReferences(Document, before);
                          });
}
EditResult Commands::Delete(NodeId id, NodeId& selection)
{
    const bool center = GetEntityKind(Document.GetNode(id)) == EntityKind::Lane && std::string_view(Document.GetNode(id).parent().name()) == "center";
    auto       result = Document.Apply("Delete element",
                                 selection,
                                 [&]
                                 {
                                     ReferenceIndex index(Document);
                                     auto           node = Document.GetNode(id);
                                     if (!node || id == DocumentNode)
                                         throw std::runtime_error("Select an element to delete");
                                     IdChanges changes;
                                     if (GetEntityKind(node) == EntityKind::Lane && std::string_view(node.parent().name()) != "center")
                                     {
                                         auto side = Document.GetId(node.parent());
                                         auto order = GetSideLanes(Document, side);
                                         order.erase(std::remove(order.begin(), order.end(), id), order.end());
                                         NumberLanes(Document, side, order, changes);
                                     }
                                     selection = Document.GetId(node.parent());
                                     ApplyCorrections(Document, index, changes, CollectSubtree(Document, id));
                                     if (!Document.GetNode(selection))
                                         selection = DocumentNode;
                                 });
    if (result && center)
        result.Notice = "Center lane missing";
    return result;
}
EditResult Commands::ReorderLanes(NodeId side, const std::vector<NodeId>& order, NodeId& selection)
{
    return Document.Apply("Reorder lanes",
                          selection,
                          [&]
                          {
                              ReferenceIndex index(Document);
                              auto           existing = GetSideLanes(Document, side), requested = order;
                              std::sort(existing.begin(), existing.end());
                              std::sort(requested.begin(), requested.end());
                              if (existing != requested)
                                  throw std::runtime_error("Lane order must include each lane once");
                              IdChanges changes;
                              NumberLanes(Document, side, order, changes);
                              ApplyCorrections(Document, index, changes, {});
                              for (auto id : order)
                                  Document.MoveElement(id, side);
                          });
}
EditResult Commands::Duplicate(NodeId id, NodeId& selection)
{
    return Document.Apply(
        "Duplicate element",
        selection,
        [&]
        {
            ReferenceIndex             before(Document);
            const auto                 original = Descendants(Document, id);
            auto                       node = Document.GetNode(id);
            std::optional<std::string> duplicateLaneId;
            if (GetEntityKind(node) == EntityKind::Lane)
            {
                const auto side = Document.GetId(node.parent());
                const auto order = GetSideLanes(Document, side);
                const auto position = std::find(order.begin(), order.end(), id) - order.begin() + 1;
                if (std::string_view(node.parent().name()) == "center")
                    throw std::runtime_error("Cannot duplicate the center lane");
                const int sign = std::string_view(node.parent().name()) == "right" ? -1 : 1;
                IdChanges changes;
                for (std::size_t i = 0; i < order.size(); ++i)
                    changes[order[i]] = std::to_string(sign * static_cast<int>(i + 1 + (i >= static_cast<std::size_t>(position))));
                ApplyCorrections(Document, before, changes, {});
                duplicateLaneId = std::to_string(sign * static_cast<int>(position + 1));
            }
            selection = Document.DuplicateElement(id);
            const auto               copy = Descendants(Document, selection);
            std::map<NodeId, NodeId> copies;
            for (std::size_t i = 0; i < original.size(); ++i)
                copies[original[i]] = copy[i];
            std::map<std::pair<EntityKind, NodeId>, std::set<std::string>> reserved;
            std::map<NodeId, std::string>                                  copyKeys;
            for (const auto& entity : before.GetEntities())
            {
                if (!copies.count(entity.Id))
                    continue;
                auto        scope = copies.count(entity.Scope) ? copies.at(entity.Scope) : entity.Scope;
                std::string key = entity.Key;
                if (entity.Id == id && duplicateLaneId)
                    key = *duplicateLaneId;
                else if (scope == entity.Scope)
                    key = FreshId(before, entity.Kind, scope, reserved[{entity.Kind, scope}]);
                reserved[{entity.Kind, scope}].insert(key);
                copyKeys[entity.Id] = key;
                Document.SetAttribute(copies.at(entity.Id), "id", key);
            }
            for (const auto& reference : before.GetReferences())
            {
                if (!copies.count(reference.Source))
                    continue;
                const bool internal = std::any_of(
                    reference.Target.Targets.begin(), reference.Target.Targets.end(), [&](NodeId target) { return copies.count(target); });
                if (!internal)
                    continue;
                if (reference.Target.Targets.size() != 1 || !reference.Target.Error.empty())
                    throw std::runtime_error("Duplicate contains an ambiguous reference");
                Document.SetAttribute(copies.at(reference.Source), reference.Attribute, copyKeys.at(reference.Target.Targets.front()));
            }
            CheckDestination(ReferenceIndex(Document), copy);
            CheckPreservedReferences(Document, before);
        });
}
EditResult Commands::Move(NodeId id, NodeId parent, NodeId beforeId, NodeId& selection)
{
    auto node = Document.GetNode(id);
    if (GetEntityKind(node) == EntityKind::Lane && node.parent() == Document.GetNode(parent))
    {
        try
        {
            auto order = GetSideLanes(Document, parent);
            order.erase(std::remove(order.begin(), order.end(), id), order.end());
            if (beforeId == id)
                return {};
            auto position = beforeId ? std::find(order.begin(), order.end(), beforeId) : order.end();
            if (beforeId && position == order.end())
                return {"Choose a lane as the reorder destination", id};
            order.insert(position, id);
            return ReorderLanes(parent, order, selection);
        }
        catch (const std::exception& error)
        {
            return {error.what(), id};
        }
    }
    return Document.Apply("Move element",
                          selection,
                          [&]
                          {
                              ReferenceIndex before(Document);
                              auto           moved = Descendants(Document, id);
                              IdChanges      changes;
                              if (GetEntityKind(node) == EntityKind::Lane)
                              {
                                  const auto source = Document.GetId(node.parent());
                                  auto       sourceOrder = GetSideLanes(Document, source), targetOrder = GetSideLanes(Document, parent);
                                  sourceOrder.erase(std::remove(sourceOrder.begin(), sourceOrder.end(), id), sourceOrder.end());
                                  auto position = beforeId ? std::find(targetOrder.begin(), targetOrder.end(), beforeId) : targetOrder.end();
                                  if (beforeId && position == targetOrder.end())
                                      throw std::runtime_error("Choose a lane as the move destination");
                                  targetOrder.insert(position, id);
                                  NumberLanes(Document, source, sourceOrder, changes);
                                  NumberLanes(Document, parent, targetOrder, changes);
                                  ApplyCorrections(Document, before, changes, {});
                              }
                              Document.MoveElement(id, parent, beforeId);
                              ReferenceIndex after(Document);
                              for (auto movedId : moved)
                                  if (before.GetEntity(movedId) && !after.GetEntity(movedId))
                                      throw std::runtime_error("Destination changes identity scope; use raw XML");
                              CheckDestination(after, moved);
                              CheckPreservedReferences(Document, before);
                          });
}
EditResult Commands::ReplaceXml(NodeId id, const std::string& xml, NodeId& selection)
{
    return Document.Apply("Apply raw XML", selection, [&] { selection = Document.ReplaceSubtree(id, xml); });
}
} // namespace odreditor
