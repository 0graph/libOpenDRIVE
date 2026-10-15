#include "XodrDocument.h"
#include <algorithm>
#include <cctype>
#include <sstream>
#include <stdexcept>

namespace odreditor
{
namespace
{
constexpr unsigned ParseFlags = pugi::parse_full | pugi::parse_ws_pcdata;
void               Visit(pugi::xml_node node, const std::function<void(pugi::xml_node)>& action)
{
    action(node);
    for (auto child : node.children())
        Visit(child, action);
}
void CheckDocument(pugi::xml_node document)
{
    unsigned roots = 0;
    for (auto child : document.children())
    {
        if (child.type() == pugi::node_element)
            ++roots;
        if (child.type() == pugi::node_pcdata || child.type() == pugi::node_cdata)
            throw std::runtime_error("Text must be inside the root element");
    }
    if (roots != 1)
        throw std::runtime_error("XML needs one root element");
}
std::string Serialize(pugi::xml_node node)
{
    std::ostringstream stream;
    node.print(stream, "", pugi::format_raw | pugi::format_no_declaration, pugi::encoding_utf8);
    return stream.str();
}
} // namespace

XodrDocument::XodrDocument()
{
    CreateDocument();
}

EditResult XodrDocument::LoadDocument(const std::string& source)
{
    pugi::xml_document candidate;
    auto               result = candidate.load_buffer(source.data(), source.size(), ParseFlags, pugi::encoding_auto);
    if (!result)
        return {std::string(result.description()) + " at byte " + std::to_string(result.offset)};
    try
    {
        CheckDocument(candidate);
    }
    catch (const std::exception& error)
    {
        return {error.what()};
    }
    Xml.reset(candidate);
    Nodes.clear();
    Ids.clear();
    Nodes[DocumentNode] = Xml;
    Ids[Xml.internal_object()] = DocumentNode;
    for (auto child : Xml.children())
        RegisterTree(child);
    History.clear();
    Cursor = 0;
    State = NextState++;
    SavedState = State;
    ++Revision;
    return {};
}

void XodrDocument::CreateDocument()
{
    LoadDocument(
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<OpenDRIVE><header revMajor=\"1\" revMinor=\"4\" name=\"\" version=\"1.0\"/></OpenDRIVE>\n");
}
std::string XodrDocument::SaveDocument() const
{
    return Serialize(Xml);
}
std::string XodrDocument::SaveSubtree(NodeId id) const
{
    return Serialize(RequireNode(id));
}
pugi::xml_node XodrDocument::GetNode(NodeId id) const
{
    auto found = Nodes.find(id);
    return found == Nodes.end() ? pugi::xml_node{} : found->second;
}
NodeId XodrDocument::GetId(pugi::xml_node node) const
{
    auto found = Ids.find(node.internal_object());
    return found == Ids.end() ? 0 : found->second;
}
std::vector<NodeId> XodrDocument::GetChildren(NodeId id) const
{
    std::vector<NodeId> children;
    for (auto child : GetNode(id).children())
        children.push_back(GetId(child));
    return children;
}
pugi::xml_node XodrDocument::RequireNode(NodeId id) const
{
    auto node = GetNode(id);
    if (!node)
        throw std::runtime_error("Element no longer exists");
    return node;
}
void XodrDocument::RequireEdit() const
{
    if (!Pending)
        throw std::logic_error("Mutation requires a transaction");
}
void XodrDocument::Record(Change change)
{
    RequireEdit();
    Pending->Changes.push_back(std::move(change));
    Pending->Changes.back().Redo();
}
void XodrDocument::RegisterTree(pugi::xml_node node, const std::vector<NodeId>* ids)
{
    std::size_t index = 0;
    Visit(node,
          [&](pugi::xml_node child)
          {
              NodeId id = ids ? ids->at(index++) : NextId++;
              Nodes[id] = child;
              Ids[child.internal_object()] = id;
          });
}
void XodrDocument::UnregisterTree(pugi::xml_node node)
{
    Visit(node,
          [&](pugi::xml_node child)
          {
              Nodes.erase(GetId(child));
              Ids.erase(child.internal_object());
          });
}
std::shared_ptr<XodrDocument::StoredTree> XodrDocument::StoreTree(NodeId id, bool freshIds)
{
    auto tree = std::make_shared<StoredTree>();
    auto node = RequireNode(id);
    tree->Xml.append_copy(node);
    Visit(node, [&](pugi::xml_node child) { tree->Ids.push_back(freshIds ? NextId++ : GetId(child)); });
    return tree;
}
void XodrDocument::RestoreTree(NodeId parent, NodeId before, const StoredTree& tree)
{
    auto owner = RequireNode(parent);
    auto node = before ? owner.insert_copy_before(tree.Xml.first_child(), RequireNode(before)) : owner.append_copy(tree.Xml.first_child());
    if (!node)
        throw std::runtime_error("Cannot insert XML here");
    RegisterTree(node, &tree.Ids);
}
void XodrDocument::EraseTree(NodeId id)
{
    auto node = RequireNode(id);
    UnregisterTree(node);
    node.parent().remove_child(node);
}
void XodrDocument::MoveTree(NodeId id, NodeId parent, NodeId before)
{
    auto owner = RequireNode(parent);
    auto node = RequireNode(id);
    auto moved = before ? owner.insert_move_before(node, RequireNode(before)) : owner.append_move(node);
    if (!moved)
        throw std::runtime_error("Cannot move XML here");
}

EditResult XodrDocument::Apply(const std::string& label, NodeId& selection, const std::function<void()>& edit)
{
    if (Pending)
        return {"An edit is already active", selection};
    Pending = Entry{label, {}, selection, 0, State, 0};
    try
    {
        edit();
        CheckDocument(Xml);
    }
    catch (const std::exception& error)
    {
        for (auto it = Pending->Changes.rbegin(); it != Pending->Changes.rend(); ++it)
            it->Undo();
        selection = Pending->BeforeSelection;
        Pending.reset();
        return {error.what(), selection};
    }
    if (!Pending->Changes.empty())
    {
        Pending->AfterSelection = selection;
        Pending->AfterState = NextState++;
        State = Pending->AfterState;
        History.erase(History.begin() + Cursor, History.end());
        History.push_back(std::move(*Pending));
        if (History.size() > 100)
            History.erase(History.begin());
        Cursor = History.size();
        ++Revision;
    }
    Pending.reset();
    return {};
}
bool XodrDocument::Undo(NodeId& selection)
{
    if (Cursor == 0 || Pending)
        return false;
    auto& entry = History[--Cursor];
    for (auto it = entry.Changes.rbegin(); it != entry.Changes.rend(); ++it)
        it->Undo();
    selection = entry.BeforeSelection;
    State = entry.BeforeState;
    ++Revision;
    return true;
}
bool XodrDocument::Redo(NodeId& selection)
{
    if (Cursor == History.size() || Pending)
        return false;
    auto& entry = History[Cursor++];
    for (const auto& change : entry.Changes)
        change.Redo();
    selection = entry.AfterSelection;
    State = entry.AfterState;
    ++Revision;
    return true;
}
const char* XodrDocument::UndoLabel() const
{
    return Cursor ? History[Cursor - 1].Label.c_str() : nullptr;
}
const char* XodrDocument::RedoLabel() const
{
    return Cursor < History.size() ? History[Cursor].Label.c_str() : nullptr;
}

bool XodrDocument::IsXmlName(const std::string& name)
{
    if (name.empty())
        return false;
    auto initial = [](unsigned char c) { return std::isalpha(c) || c == '_' || c == ':' || c >= 128; };
    if (!initial(name.front()))
        return false;
    return std::all_of(name.begin(), name.end(), [&](unsigned char c) { return initial(c) || std::isdigit(c) || c == '-' || c == '.'; });
}
void XodrDocument::SetAttribute(NodeId id, const std::string& name, const std::optional<std::string>& value)
{
    RequireEdit();
    auto node = RequireNode(id);
    if (!IsXmlName(name))
        throw std::runtime_error("Invalid XML attribute name");
    if (node.type() != pugi::node_element && node.type() != pugi::node_declaration)
        throw std::runtime_error("This node has no attributes");
    std::vector<std::pair<std::string, std::string>> before, after;
    bool                                             found = false;
    for (auto attribute : node.attributes())
    {
        before.emplace_back(attribute.name(), attribute.value());
        if (name != attribute.name())
            after.emplace_back(attribute.name(), attribute.value());
        else
        {
            found = true;
            if (value)
                after.emplace_back(name, *value);
        }
    }
    if (!found && value)
        after.emplace_back(name, *value);
    if (before == after)
        return;
    auto write = [this, id](const auto& attributes)
    {
        auto target = RequireNode(id);
        target.remove_attributes();
        for (const auto& [key, text] : attributes)
            target.append_attribute(key.c_str()).set_value(text.c_str());
    };
    Record({[write, before] { write(before); }, [write, after] { write(after); }});
}
void XodrDocument::SetText(NodeId id, const std::string& value)
{
    RequireEdit();
    auto node = RequireNode(id);
    if (node.type() == pugi::node_element || node.type() == pugi::node_document)
        throw std::runtime_error("Select a text node or edit XML");
    if (node.type() == pugi::node_comment && (value.find("--") != std::string::npos || (!value.empty() && value.back() == '-')))
        throw std::runtime_error("Invalid XML comment");
    if (node.type() == pugi::node_pi && value.find("?>") != std::string::npos)
        throw std::runtime_error("Invalid processing instruction");
    const std::string before = node.value();
    if (before == value)
        return;
    Record({[this, id, before] { RequireNode(id).set_value(before.c_str()); }, [this, id, value] { RequireNode(id).set_value(value.c_str()); }});
}
void XodrDocument::RenameElement(NodeId id, const std::string& name)
{
    RequireEdit();
    auto node = RequireNode(id);
    if (node.type() != pugi::node_element || !IsXmlName(name))
        throw std::runtime_error("Invalid XML element name");
    const std::string before = node.name();
    if (before == name)
        return;
    Record({[this, id, before] { RequireNode(id).set_name(before.c_str()); }, [this, id, name] { RequireNode(id).set_name(name.c_str()); }});
}
NodeId XodrDocument::InsertElement(NodeId parent, const std::string& name, NodeId before)
{
    RequireEdit();
    auto owner = RequireNode(parent);
    if (owner.type() != pugi::node_element || !IsXmlName(name))
        throw std::runtime_error("Invalid parent or element name");
    if (before && RequireNode(before).parent() != owner)
        throw std::runtime_error("Destination is not a child of the parent");
    auto tree = std::make_shared<StoredTree>();
    tree->Xml.append_child(name.c_str());
    const NodeId id = NextId++;
    tree->Ids.push_back(id);
    Record({[this, id] { EraseTree(id); }, [this, parent, before, tree] { RestoreTree(parent, before, *tree); }});
    return id;
}
NodeId XodrDocument::DuplicateElement(NodeId id)
{
    RequireEdit();
    auto node = RequireNode(id);
    if (id == DocumentNode || node.parent() == Xml)
        throw std::runtime_error("Cannot duplicate a document root");
    auto       tree = StoreTree(id, true);
    const auto copy = tree->Ids.front(), parent = GetId(node.parent()), before = GetId(node.next_sibling());
    Record({[this, copy] { EraseTree(copy); }, [this, parent, before, tree] { RestoreTree(parent, before, *tree); }});
    return copy;
}
void XodrDocument::RemoveElement(NodeId id)
{
    RequireEdit();
    auto node = RequireNode(id);
    if (id == DocumentNode)
        throw std::runtime_error("Cannot delete the document");
    const auto parent = GetId(node.parent()), before = GetId(node.next_sibling());
    auto       tree = StoreTree(id);
    Record({[this, parent, before, tree] { RestoreTree(parent, before, *tree); }, [this, id] { EraseTree(id); }});
}
void XodrDocument::MoveElement(NodeId id, NodeId parent, NodeId before)
{
    RequireEdit();
    auto node = RequireNode(id), owner = RequireNode(parent);
    if (id == DocumentNode || owner.type() != pugi::node_element)
        throw std::runtime_error("Invalid move destination");
    for (auto ancestor = owner; ancestor; ancestor = ancestor.parent())
        if (ancestor == node)
            throw std::runtime_error("Cannot move an element inside itself");
    if (before && RequireNode(before).parent() != owner)
        throw std::runtime_error("Destination is not a child of the parent");
    const auto oldParent = GetId(node.parent()), oldBefore = GetId(node.next_sibling());
    if (before == id || (parent == oldParent && before == oldBefore))
        return;
    Record({[this, id, oldParent, oldBefore] { MoveTree(id, oldParent, oldBefore); }, [this, id, parent, before] { MoveTree(id, parent, before); }});
}
NodeId XodrDocument::ReplaceSubtree(NodeId id, const std::string& source)
{
    RequireEdit();
    auto               node = RequireNode(id);
    pugi::xml_document parsed;
    auto               result = parsed.load_buffer(source.data(), source.size(), ParseFlags | pugi::parse_fragment, pugi::encoding_utf8);
    if (!result)
        throw std::runtime_error(std::string(result.description()) + " at byte " + std::to_string(result.offset));
    if (id == DocumentNode)
        CheckDocument(parsed);
    else if (!parsed.first_child() || parsed.first_child().next_sibling())
        throw std::runtime_error("Select one XML node to replace this subtree");
    const auto parent = id == DocumentNode ? DocumentNode : GetId(node.parent());
    const auto before = id == DocumentNode ? 0 : GetId(node.next_sibling());
    if (id == DocumentNode)
        for (auto child : GetChildren(DocumentNode))
            RemoveElement(child);
    else
        RemoveElement(id);
    NodeId selected = 0;
    for (auto child : parsed.children())
    {
        auto tree = std::make_shared<StoredTree>();
        tree->Xml.append_copy(child);
        Visit(child, [&](pugi::xml_node) { tree->Ids.push_back(NextId++); });
        const NodeId added = tree->Ids.front();
        Record({[this, added] { EraseTree(added); }, [this, parent, before, tree] { RestoreTree(parent, before, *tree); }});
        if (!selected || child.type() == pugi::node_element)
            selected = added;
    }
    return selected;
}
} // namespace odreditor
