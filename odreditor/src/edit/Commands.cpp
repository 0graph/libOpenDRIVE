#include "Commands.h"
#include <string_view>

namespace odreditor
{
namespace
{
bool HasIdentity(pugi::xml_node node)
{
    const std::string_view name = node.name();
    return name == "road" || name == "junction" || name == "lane" || name == "signal" || name == "object" || name == "controller" ||
           name == "connection" || name == "outline" || name == "cornerLocal" || name == "cornerRoad";
}
bool ContainsIdentity(pugi::xml_node node)
{
    if (HasIdentity(node))
        return true;
    for (auto child : node.children())
        if (ContainsIdentity(child))
            return true;
    return false;
}
EditResult NeedsCorrections(NodeId id)
{
    return {"Use raw XML for identity edits until reference corrections are available", id};
}
} // namespace
EditResult Commands::SetAttribute(NodeId id, const std::string& name, const std::optional<std::string>& value, NodeId& selection)
{
    if (name == "id" && HasIdentity(Document.GetNode(id)))
        return NeedsCorrections(id);
    return Document.Apply("Edit attribute", selection, [&] { Document.SetAttribute(id, name, value); });
}
EditResult Commands::RenameElement(NodeId id, const std::string& name, NodeId& selection)
{
    if (HasIdentity(Document.GetNode(id)))
        return NeedsCorrections(id);
    return Document.Apply("Rename element", selection, [&] { Document.RenameElement(id, name); });
}
EditResult Commands::SetText(NodeId id, const std::string& value, NodeId& selection)
{
    return Document.Apply("Edit text", selection, [&] { Document.SetText(id, value); });
}
EditResult Commands::Insert(NodeId parent, const std::string& name, NodeId& selection)
{
    return Document.Apply("Add element", selection, [&] { selection = Document.InsertElement(parent, name); });
}
EditResult Commands::Delete(NodeId id, NodeId& selection)
{
    if (ContainsIdentity(Document.GetNode(id)))
        return NeedsCorrections(id);
    return Document.Apply("Delete element",
                          selection,
                          [&]
                          {
                              selection = Document.GetId(Document.GetNode(id).parent());
                              Document.RemoveElement(id);
                          });
}
EditResult Commands::Duplicate(NodeId id, NodeId& selection)
{
    if (ContainsIdentity(Document.GetNode(id)))
        return NeedsCorrections(id);
    return Document.Apply("Duplicate element", selection, [&] { selection = Document.DuplicateElement(id); });
}
EditResult Commands::Move(NodeId id, NodeId parent, NodeId before, NodeId& selection)
{
    if (ContainsIdentity(Document.GetNode(id)))
        return NeedsCorrections(id);
    return Document.Apply("Move element", selection, [&] { Document.MoveElement(id, parent, before); });
}
EditResult Commands::ReplaceXml(NodeId id, const std::string& xml, NodeId& selection)
{
    return Document.Apply("Apply raw XML", selection, [&] { selection = Document.ReplaceSubtree(id, xml); });
}
} // namespace odreditor
