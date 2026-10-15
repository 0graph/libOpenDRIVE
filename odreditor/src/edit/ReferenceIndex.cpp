#include "ReferenceIndex.h"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstring>

namespace odreditor
{
namespace
{
bool Named(pugi::xml_node node, std::string_view name)
{
    return node.name() == name;
}
bool RootChild(pugi::xml_node node)
{
    return Named(node.parent(), "OpenDRIVE") && node.parent().parent().type() == pugi::node_document;
}
bool RoadChild(pugi::xml_node node, const char* group)
{
    return Named(node.parent(), group) && Named(node.parent().parent(), "road") && RootChild(node.parent().parent());
}
std::string_view NumericToken(const std::string& text)
{
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos)
        return {};
    std::string_view token(text.data() + first, text.find_last_not_of(" \t\r\n") - first + 1);
    if (token.front() == '+')
    {
        token.remove_prefix(1);
        if (token.empty() || token.front() == '+' || token.front() == '-')
            return {};
    }
    return token;
}
void Append(std::vector<NodeId>& into, const std::vector<NodeId>& from)
{
    for (auto id : from)
        if (std::find(into.begin(), into.end(), id) == into.end())
            into.push_back(id);
}
} // namespace
bool IsDescendant(pugi::xml_node node, pugi::xml_node ancestor)
{
    for (; node; node = node.parent())
        if (node == ancestor)
            return true;
    return false;
}
EntityKind GetEntityKind(pugi::xml_node node)
{
    if (RootChild(node))
    {
        if (Named(node, "road"))
            return EntityKind::Road;
        if (Named(node, "junction"))
            return EntityKind::Junction;
        if (Named(node, "controller"))
            return EntityKind::Controller;
    }
    if (Named(node, "lane") && (Named(node.parent(), "left") || Named(node.parent(), "right") || Named(node.parent(), "center")) &&
        Named(node.parent().parent(), "laneSection") && RoadChild(node.parent().parent(), "lanes"))
        return EntityKind::Lane;
    if (Named(node, "signal") && RoadChild(node, "signals"))
        return EntityKind::Signal;
    if (Named(node, "object") && RoadChild(node, "objects"))
        return EntityKind::Object;
    if (Named(node, "connection") && GetEntityKind(node.parent()) == EntityKind::Junction)
        return EntityKind::Connection;
    if (Named(node, "outline") && Named(node.parent(), "outlines") && GetEntityKind(node.parent().parent()) == EntityKind::Object)
        return EntityKind::Outline;
    if ((Named(node, "cornerLocal") || Named(node, "cornerRoad")) && GetEntityKind(node.parent()) == EntityKind::Outline)
        return EntityKind::Corner;
    return EntityKind::None;
}
std::optional<int> ReferenceIndex::LaneNumber(const std::string& text)
{
    int        value = 0;
    const auto token = NumericToken(text);
    if (token.empty())
        return {};
    auto result = std::from_chars(token.data(), token.data() + token.size(), value);
    if (result.ec != std::errc{} || result.ptr != token.data() + token.size())
        return {};
    return value;
}
std::optional<double> ReferenceIndex::Number(const std::string& text)
{
    double     value = 0;
    const auto token = NumericToken(text);
    if (token.empty())
        return {};
    auto result = std::from_chars(token.data(), token.data() + token.size(), value);
    if (result.ec != std::errc{} || result.ptr != token.data() + token.size() || !std::isfinite(value))
        return {};
    return value;
}
ReferenceIndex::ReferenceIndex(const XodrDocument& document) : Document(document)
{
    std::function<void(pugi::xml_node)> visit = [&](pugi::xml_node node)
    {
        const auto kind = GetEntityKind(node);
        if (kind != EntityKind::None)
        {
            NodeId scope = DocumentNode;
            if (kind == EntityKind::Lane || kind == EntityKind::Outline)
                scope = Document.GetId(node.parent().parent());
            if (kind == EntityKind::Connection || kind == EntityKind::Corner)
                scope = Document.GetId(node.parent());
            Entities.push_back({Document.GetId(node), kind, scope, node.attribute("id").value()});
        }
        for (auto child : node.children())
            visit(child);
    };
    visit(Document.GetNode(DocumentNode));
    for (std::size_t i = 0; i < Entities.size(); ++i)
    {
        const auto& entity = Entities[i];
        auto        number = entity.Kind == EntityKind::Lane ? LaneNumber(entity.Key) : std::nullopt;
        Targets[{entity.Kind, entity.Scope, number ? std::to_string(*number) : entity.Key}].push_back(entity.Id);
        EntityPositions[entity.Id] = i;
    }
    BuildReferences();
}
const Entity* ReferenceIndex::GetEntity(NodeId id) const
{
    auto found = EntityPositions.find(id);
    return found == EntityPositions.end() ? nullptr : &Entities[found->second];
}
Resolution ReferenceIndex::Find(EntityKind kind, const std::string& key, const std::vector<NodeId>& scopes) const
{
    Resolution result{{}, scopes, {}};
    auto       number = kind == EntityKind::Lane ? LaneNumber(key) : std::nullopt;
    const auto normalized = number ? std::to_string(*number) : key;
    for (auto scope : scopes)
    {
        auto found = Targets.find({kind, scope, normalized});
        if (found != Targets.end())
            Append(result.Targets, found->second);
    }
    return result;
}

NodeId ReferenceIndex::GetRoad(NodeId id) const
{
    for (auto node = Document.GetNode(id); node; node = node.parent())
        if (GetEntityKind(node) == EntityKind::Road)
            return Document.GetId(node);
    return 0;
}
std::vector<NodeId> ReferenceIndex::GetSections(NodeId road, std::string& error) const
{
    std::vector<std::pair<double, NodeId>> stations;
    for (auto section : Document.GetNode(road).child("lanes").children("laneSection"))
    {
        auto station = Number(section.attribute("s").value());
        if (!station)
            error = "Lane section position is invalid";
        stations.emplace_back(station.value_or(0), Document.GetId(section));
    }
    std::stable_sort(stations.begin(), stations.end());
    std::vector<NodeId> result;
    for (std::size_t i = 0; i < stations.size(); ++i)
    {
        if (i && stations[i - 1].first == stations[i].first)
            error = "Lane section position is ambiguous";
        result.push_back(stations[i].second);
    }
    if (result.empty())
        error = "Road has no lane sections";
    return result;
}
Resolution ReferenceIndex::GetBoundary(const Resolution& roads, const std::string& contact) const
{
    Resolution result;
    result.Error = roads.Error;
    if (roads.Targets.size() != 1)
        result.Error = "Road target is missing or ambiguous";
    for (auto road : roads.Targets)
    {
        auto sections = GetSections(road, result.Error);
        if (sections.empty())
            continue;
        if (contact == "start")
            result.Targets.push_back(sections.front());
        else if (contact == "end")
            result.Targets.push_back(sections.back());
        else
        {
            Append(result.Targets, sections);
            result.Error = "Road contact point is missing";
        }
    }
    result.Scopes = result.Targets;
    return result;
}
Resolution ReferenceIndex::GetJunctionLane(NodeId connectionId, const std::string& key, bool incoming) const
{
    auto       connection = Document.GetNode(connectionId);
    auto       roads = Find(EntityKind::Road,
                      connection.attribute(incoming                                 ? "incomingRoad"
                                           : connection.attribute("connectingRoad") ? "connectingRoad"
                                                                                    : "linkedRoad")
                          .value(),
                      {DocumentNode});
    Resolution boundary;
    if (!incoming)
        boundary = GetBoundary(roads, connection.attribute("contactPoint").value());
    else
    {
        if (roads.Targets.size() != 1)
            boundary.Error = "Incoming road is missing or ambiguous";
        for (auto roadId : roads.Targets)
        {
            auto              road = Document.GetNode(roadId);
            const std::string junction = connection.parent().attribute("id").value();
            unsigned          matches = 0;
            for (const auto* end : {"predecessor", "successor"})
            {
                auto link = road.child("link").child(end);
                if (std::string_view(link.attribute("elementType").value()) != "junction" || junction != link.attribute("elementId").value())
                    continue;
                auto resolved = GetBoundary({{roadId}, {}, {}}, std::string_view(end) == "predecessor" ? "start" : "end");
                Append(boundary.Targets, resolved.Targets);
                if (!resolved.Error.empty())
                    boundary.Error = resolved.Error;
                ++matches;
            }
            if (matches != 1)
            {
                boundary.Error = "Incoming junction boundary is ambiguous";
                auto sections = GetSections(roadId, boundary.Error);
                Append(boundary.Targets, sections);
            }
        }
    }
    auto result = Find(EntityKind::Lane, key, boundary.Targets);
    result.Error = boundary.Error;
    if (connection.attribute("linkedRoad") || std::string_view(connection.parent().attribute("type").value()) == "direct")
        result.Error = "Direct junction lane references need raw XML";
    return result;
}
Resolution ReferenceIndex::GetLinkedLane(NodeId laneId, bool predecessor, pugi::xml_node specificLink) const
{
    auto        lane = Document.GetNode(laneId);
    const auto* field = predecessor ? "predecessor" : "successor";
    if (!specificLink)
    {
        Resolution result;
        for (auto child : lane.child("link").children(field))
        {
            const auto resolved = GetLinkedLane(laneId, predecessor, child);
            Append(result.Targets, resolved.Targets);
            Append(result.Scopes, resolved.Scopes);
            if (!resolved.Error.empty())
                result.Error = resolved.Error;
        }
        return result;
    }
    auto link = specificLink ? specificLink : lane.child("link").child(field);
    if (!link)
        return {};
    std::string error;
    auto        sections = GetSections(GetRoad(laneId), error);
    if (!error.empty())
    {
        auto result = Find(EntityKind::Lane, link.attribute("id").value(), sections);
        result.Error = error;
        return result;
    }
    const auto sectionId = Document.GetId(lane.parent().parent());
    auto       section = std::find(sections.begin(), sections.end(), sectionId);
    if (section == sections.end())
        return {{}, {}, "Lane section is missing"};
    if ((predecessor && section != sections.begin()) || (!predecessor && section + 1 != sections.end()))
    {
        auto result = Find(EntityKind::Lane, link.attribute("id").value(), {predecessor ? *(section - 1) : *(section + 1)});
        result.Error = error;
        return result;
    }
    auto road = Document.GetNode(GetRoad(laneId));
    auto roadLink = road.child("link").child(field);
    if (roadLink.next_sibling(field))
    {
        std::vector<NodeId> candidates;
        for (auto alternative : road.child("link").children(field))
        {
            std::vector<NodeId> roads;
            if (std::string_view(alternative.attribute("elementType").value()) == "road")
                roads = Find(EntityKind::Road, alternative.attribute("elementId").value(), {DocumentNode}).Targets;
            else if (std::string_view(alternative.attribute("elementType").value()) == "junction")
                for (auto junction : Find(EntityKind::Junction, alternative.attribute("elementId").value(), {DocumentNode}).Targets)
                    for (auto connection : Document.GetNode(junction).children("connection"))
                        for (const auto* attribute : {"incomingRoad", "connectingRoad", "linkedRoad"})
                            Append(roads, Find(EntityKind::Road, connection.attribute(attribute).value(), {DocumentNode}).Targets);
            for (auto target : roads)
                Append(candidates, GetSections(target, error));
        }
        auto result = Find(EntityKind::Lane, link.attribute("id").value(), candidates);
        result.Error = "Road link is ambiguous";
        return result;
    }
    const std::string type = roadLink.attribute("elementType").value();
    if (type == "road")
    {
        auto boundary =
            GetBoundary(Find(EntityKind::Road, roadLink.attribute("elementId").value(), {DocumentNode}), roadLink.attribute("contactPoint").value());
        auto result = Find(EntityKind::Lane, link.attribute("id").value(), boundary.Targets);
        result.Error = error.empty() ? boundary.Error : error;
        return result;
    }
    if (type == "junction")
    {
        Resolution result;
        const auto junctions = Find(EntityKind::Junction, roadLink.attribute("elementId").value(), {DocumentNode});
        for (auto junction : junctions.Targets)
            for (auto connection : Document.GetNode(junction).children("connection"))
                for (auto mapping : connection.children("laneLink"))
                {
                    auto from = GetJunctionLane(Document.GetId(connection), mapping.attribute("from").value(), true);
                    if (std::find(from.Targets.begin(), from.Targets.end(), laneId) == from.Targets.end())
                        continue;
                    auto to = GetJunctionLane(Document.GetId(connection), mapping.attribute("to").value(), false);
                    Append(result.Scopes, to.Scopes);
                    if (LaneNumber(link.attribute("id").value()) == LaneNumber(mapping.attribute("to").value()))
                        Append(result.Targets, to.Targets);
                    if (!from.Error.empty() || !to.Error.empty())
                        result.Error = "Junction lane target is ambiguous";
                }
        if (junctions.Targets.size() != 1)
            result.Error = "Junction target is missing or ambiguous";
        return result;
    }
    return {{}, {}, "Lane road-end link is missing"};
}
Resolution ReferenceIndex::GetPredecessorLane(NodeId lane) const
{
    return GetLinkedLane(lane, true);
}
Resolution ReferenceIndex::GetSuccessorLane(NodeId lane) const
{
    return GetLinkedLane(lane, false);
}
void ReferenceIndex::Add(NodeId source, const char* attribute, EntityKind kind, Resolution target, DeleteRule rule)
{
    auto value = Document.GetNode(source).attribute(attribute);
    if (value)
        References.push_back({source, attribute, kind, value.value(), std::move(target), rule});
}
ValidityScope ReferenceIndex::GetValidityScope(NodeId ownerId) const
{
    ValidityScope result{ownerId, {}, GetRoad(ownerId), {}};
    auto          owner = Document.GetNode(ownerId);
    for (auto validity : owner.children("validity"))
    {
        auto from = LaneNumber(validity.attribute("fromLane").value()), to = LaneNumber(validity.attribute("toLane").value());
        if (!from || !to || *from > *to)
            result.Error = "Validity lane range is invalid";
        else
            result.Ranges.emplace_back(*from, *to);
    }
    auto                                   sections = GetSections(result.Road, result.Error);
    std::vector<std::pair<double, double>> spans;
    auto                                   start = Number(owner.attribute("s").value());
    auto length = owner.attribute("validLength") ? Number(owner.attribute("validLength").value()) : std::optional<double>(0);
    if (!start || !length || *length < 0 || !std::isfinite(*start + *length))
        result.Error = "Validity position or span is invalid";
    else
        spans.emplace_back(*start, *start + *length);
    for (auto repeat : owner.children("repeat"))
    {
        auto s = Number(repeat.attribute("s").value()), extent = Number(repeat.attribute("length").value());
        if (!s || !extent || *extent < 0 || !std::isfinite(*s + *extent))
            result.Error = "Validity repeat span is invalid";
        else
            spans.emplace_back(*s, *s + *extent);
    }
    for (const auto& [begin, end] : spans)
    {
        if (sections.empty())
            continue;
        if (begin < Document.GetNode(sections.front()).attribute("s").as_double())
            result.Error = "Validity begins before the first lane section";
        for (std::size_t i = 0; i < sections.size(); ++i)
        {
            const double s = Document.GetNode(sections[i]).attribute("s").as_double();
            const double next = i + 1 < sections.size() ? Document.GetNode(sections[i + 1]).attribute("s").as_double() : INFINITY;
            if ((begin == end && s <= begin && begin < next) || (begin < next && end > s))
                Append(result.Sections, {sections[i]});
        }
    }
    return result;
}
void ReferenceIndex::BuildReferences()
{
    auto root = Document.GetNode(DocumentNode).child("OpenDRIVE");
    auto global = [&](pugi::xml_node node, const char* field, EntityKind kind, DeleteRule rule = DeleteRule::RemoveRecord)
    { Add(Document.GetId(node), field, kind, Find(kind, node.attribute(field).value(), {DocumentNode}), rule); };
    for (auto road : root.children("road"))
    {
        if (std::string_view(road.attribute("junction").value()) != "-1")
            global(road, "junction", EntityKind::Junction, DeleteRule::ClearJunction);
        for (auto link : road.child("link").children())
        {
            if (!Named(link, "predecessor") && !Named(link, "successor"))
                continue;
            const std::string_view type = link.attribute("elementType").value();
            if (type == "road")
                global(link, "elementId", EntityKind::Road);
            if (type == "junction")
                global(link, "elementId", EntityKind::Junction);
        }
        for (auto signal : road.child("signals").children())
        {
            if (Named(signal, "signalReference"))
                global(signal, "id", EntityKind::Signal);
            if (Named(signal, "signal"))
                for (auto dependency : signal.children("dependency"))
                    global(dependency, "id", EntityKind::Signal);
            if ((Named(signal, "signal") || Named(signal, "signalReference")) && signal.child("validity"))
                Validities.push_back(GetValidityScope(Document.GetId(signal)));
        }
        for (auto object : road.child("objects").children())
        {
            if (Named(object, "objectReference"))
                global(object, "id", EntityKind::Object);
            if ((Named(object, "object") || Named(object, "objectReference")) && object.child("validity"))
                Validities.push_back(GetValidityScope(Document.GetId(object)));
            if (!Named(object, "object"))
                continue;
            for (auto marking : object.child("markings").children("marking"))
            {
                auto outlines = Find(EntityKind::Outline, marking.attribute("outlineId").value(), {Document.GetId(object)});
                if (marking.attribute("outlineId"))
                    Add(Document.GetId(marking), "outlineId", EntityKind::Outline, outlines);
                else
                    for (auto outline : object.child("outlines").children("outline"))
                        outlines.Targets.push_back(Document.GetId(outline));
                for (auto corner : marking.children("cornerReference"))
                    Add(Document.GetId(corner), "id", EntityKind::Corner, Find(EntityKind::Corner, corner.attribute("id").value(), outlines.Targets));
            }
        }
    }
    for (const auto& entity : Entities)
    {
        if (entity.Kind != EntityKind::Lane)
            continue;
        auto lane = Document.GetNode(entity.Id);
        for (auto field : lane.child("link").children())
        {
            const bool predecessor = Named(field, "predecessor");
            if (!predecessor && !Named(field, "successor"))
                continue;
            Add(Document.GetId(field), "id", EntityKind::Lane, GetLinkedLane(entity.Id, predecessor, field));
        }
    }
    for (auto junction : root.children("junction"))
    {
        for (auto connection : junction.children("connection"))
        {
            global(connection, "incomingRoad", EntityKind::Road);
            global(connection, "connectingRoad", EntityKind::Road);
            global(connection, "linkedRoad", EntityKind::Road);
            for (auto mapping : connection.children("laneLink"))
            {
                Add(Document.GetId(mapping),
                    "from",
                    EntityKind::Lane,
                    GetJunctionLane(Document.GetId(connection), mapping.attribute("from").value(), true));
                Add(Document.GetId(mapping),
                    "to",
                    EntityKind::Lane,
                    GetJunctionLane(Document.GetId(connection), mapping.attribute("to").value(), false));
            }
        }
        for (auto priority : junction.children("priority"))
        {
            global(priority, "high", EntityKind::Road);
            global(priority, "low", EntityKind::Road);
        }
        for (auto controller : junction.children("controller"))
            global(controller, "id", EntityKind::Controller);
    }
    for (auto controller : root.children("controller"))
        for (auto control : controller.children("control"))
            global(control, "signalId", EntityKind::Signal);
    for (auto group : root.children("junctionGroup"))
        for (auto reference : group.children("junctionReference"))
            global(reference, "junction", EntityKind::Junction);
}
} // namespace odreditor
