#pragma once
#include "document/XodrDocument.h"
#include <map>
#include <set>
#include <string_view>

namespace odreditor
{
enum class EntityKind
{
    None,
    Road,
    Junction,
    Lane,
    Signal,
    Object,
    Controller,
    Connection,
    Outline,
    Corner
};
struct Entity
{
    NodeId      Id = 0;
    EntityKind  Kind = EntityKind::None;
    NodeId      Scope = DocumentNode;
    std::string Key;
};
struct Resolution
{
    std::vector<NodeId> Targets;
    std::vector<NodeId> Scopes;
    std::string         Error;
};
enum class DeleteRule
{
    RemoveRecord,
    ClearJunction
};
struct Reference
{
    NodeId      Source = 0;
    std::string Attribute;
    EntityKind  Kind = EntityKind::None;
    std::string Key;
    Resolution  Target;
    DeleteRule  OnDelete = DeleteRule::RemoveRecord;
};
struct ValidityScope
{
    NodeId                           Owner = 0;
    std::vector<NodeId>              Sections;
    NodeId                           Road = 0;
    std::string                      Error;
    std::vector<std::pair<int, int>> Ranges;
};
class ReferenceIndex
{
public:
    explicit ReferenceIndex(const XodrDocument& document);
    const std::vector<Entity>& GetEntities() const
    {
        return Entities;
    }
    const std::vector<Reference>& GetReferences() const
    {
        return References;
    }
    const std::vector<ValidityScope>& GetValidities() const
    {
        return Validities;
    }
    const Entity*                GetEntity(NodeId id) const;
    Resolution                   Find(EntityKind kind, const std::string& key, const std::vector<NodeId>& scopes) const;
    Resolution                   GetPredecessorLane(NodeId lane) const;
    Resolution                   GetSuccessorLane(NodeId lane) const;
    Resolution                   GetJunctionLane(NodeId connection, const std::string& key, bool incoming) const;
    std::vector<NodeId>          GetSections(NodeId road, std::string& error) const;
    NodeId                       GetRoad(NodeId id) const;
    static std::optional<int>    LaneNumber(const std::string& text);
    static std::optional<double> Number(const std::string& text);

private:
    const XodrDocument&                                                        Document;
    std::vector<Entity>                                                        Entities;
    std::vector<Reference>                                                     References;
    std::vector<ValidityScope>                                                 Validities;
    std::map<std::tuple<EntityKind, NodeId, std::string>, std::vector<NodeId>> Targets;
    std::unordered_map<NodeId, std::size_t>                                    EntityPositions;
    Resolution                                                                 GetLinkedLane(NodeId lane, bool predecessor) const;
    Resolution                                                                 GetBoundary(const Resolution& roads, const std::string& contact) const;
    ValidityScope                                                              GetValidityScope(NodeId owner) const;
    void Add(NodeId source, const char* attribute, EntityKind kind, Resolution target, DeleteRule rule = DeleteRule::RemoveRecord);
    void BuildReferences();
};
EntityKind GetEntityKind(pugi::xml_node node);
bool       IsDescendant(pugi::xml_node node, pugi::xml_node ancestor);
} // namespace odreditor
