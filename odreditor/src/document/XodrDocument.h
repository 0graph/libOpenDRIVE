#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <pugixml.hpp>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace odreditor
{
using NodeId = std::uint64_t;
inline constexpr NodeId DocumentNode = 1;

struct EditResult
{
    std::string Error;
    NodeId      Node = 0;
    std::string Notice;
    explicit    operator bool() const
    {
        return Error.empty();
    }
};

struct EditException : std::runtime_error
{
    NodeId Node;
    EditException(NodeId node, const std::string& error) : std::runtime_error(error), Node(node) {}
};

class XodrDocument
{
public:
    XodrDocument();
    XodrDocument(const XodrDocument&) = delete;
    XodrDocument&       operator=(const XodrDocument&) = delete;
    EditResult          LoadDocument(const std::string& xml);
    void                CreateDocument();
    std::string         SaveDocument() const;
    std::string         SaveSubtree(NodeId id) const;
    pugi::xml_node      GetNode(NodeId id) const;
    NodeId              GetId(pugi::xml_node node) const;
    std::vector<NodeId> GetChildren(NodeId id) const;
    std::uint64_t       GetRevision() const
    {
        return Revision;
    }
    bool IsModified() const
    {
        return State != SavedState;
    }
    void MarkSaved()
    {
        SavedState = State;
    }
    EditResult  Apply(const std::string& label, NodeId& selection, const std::function<void()>& edit);
    bool        Undo(NodeId& selection);
    bool        Redo(NodeId& selection);
    const char* UndoLabel() const;
    const char* RedoLabel() const;

    // Mutation primitives require an active transaction; normal UI uses Commands.
    void        SetAttribute(NodeId id, const std::string& name, const std::optional<std::string>& value);
    void        SetText(NodeId id, const std::string& value);
    void        RenameElement(NodeId id, const std::string& name);
    NodeId      InsertElement(NodeId parent, const std::string& name, NodeId before = 0);
    NodeId      DuplicateElement(NodeId id);
    void        RemoveElement(NodeId id);
    void        MoveElement(NodeId id, NodeId parent, NodeId before = 0);
    NodeId      ReplaceSubtree(NodeId id, const std::string& xml);
    static bool IsXmlName(const std::string& name);

private:
    struct StoredTree
    {
        pugi::xml_document  Xml;
        std::vector<NodeId> Ids;
    };
    struct Change
    {
        std::function<void()> Undo;
        std::function<void()> Redo;
    };
    struct Entry
    {
        std::string         Label;
        std::vector<Change> Changes;
        NodeId              BeforeSelection = 0;
        NodeId              AfterSelection = 0;
        std::uint64_t       BeforeState = 0;
        std::uint64_t       AfterState = 0;
    };
    pugi::xml_document                         Xml;
    std::unordered_map<NodeId, pugi::xml_node> Nodes;
    std::unordered_map<void*, NodeId>          Ids;
    NodeId                                     NextId = 2;
    std::uint64_t                              Revision = 0, State = 0, SavedState = 0, NextState = 1;
    std::vector<Entry>                         History;
    std::size_t                                Cursor = 0;
    std::optional<Entry>                       Pending;
    pugi::xml_node                             RequireNode(NodeId id) const;
    void                                       RequireEdit() const;
    void                                       Record(Change change);
    void                                       RegisterTree(pugi::xml_node node, const std::vector<NodeId>* ids = nullptr);
    void                                       UnregisterTree(pugi::xml_node node);
    std::shared_ptr<StoredTree>                StoreTree(NodeId id, bool freshIds = false);
    void                                       RestoreTree(NodeId parent, NodeId before, const StoredTree& tree);
    void                                       EraseTree(NodeId id);
    void                                       MoveTree(NodeId id, NodeId parent, NodeId before);
};
} // namespace odreditor
