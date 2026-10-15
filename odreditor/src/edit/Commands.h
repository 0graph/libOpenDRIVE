#pragma once
#include "document/XodrDocument.h"

namespace odreditor
{
class Commands
{
public:
    explicit Commands(XodrDocument& document) : Document(document) {}
    EditResult SetAttribute(NodeId id, const std::string& name, const std::optional<std::string>& value, NodeId& selection);
    EditResult RenameElement(NodeId id, const std::string& name, NodeId& selection);
    EditResult SetText(NodeId id, const std::string& value, NodeId& selection);
    EditResult Insert(NodeId parent, const std::string& name, NodeId& selection);
    EditResult Delete(NodeId id, NodeId& selection);
    EditResult Duplicate(NodeId id, NodeId& selection);
    EditResult Move(NodeId id, NodeId parent, NodeId before, NodeId& selection);
    EditResult ReplaceXml(NodeId id, const std::string& xml, NodeId& selection);

private:
    XodrDocument& Document;
};
} // namespace odreditor
