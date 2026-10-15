#pragma once
#include "document/XodrDocument.h"
#include "edit/Commands.h"
#include "platform/FileBridge.h"
#include <functional>
#include <optional>
#include <unordered_set>

namespace odreditor
{
class EditorApp
{
public:
    EditorApp();
    void Draw();

private:
    XodrDocument                    Document;
    Commands                        Edit{Document};
    NodeId                          Selection = 0;
    float                           TreeWidth = 250, InspectorWidth = 310;
    std::string                     FileName = "untitled.xodr";
    std::string                     Search, Error;
    std::string                     AttributeName, AttributeValue, ElementName;
    std::string                     XmlDraft, XmlOriginal;
    NodeId                          XmlTarget = DocumentNode;
    std::uint64_t                   XmlRevision = 0;
    bool                            ShowXml = true, ShowNotices = false, ShowDiagnostics = false;
    bool                            PendingNew = false;
    std::optional<OpenedFile>       PendingOpen;
    std::function<void()>           Deferred;
    std::unordered_set<NodeId>      Matches;
    std::unique_ptr<ReferenceIndex> References;
    std::uint64_t                   ReferenceRevision = 0;
    std::string                     ReferenceSearch;
    bool                            DrawReferencePicker(NodeId id, const std::string& name, const std::string& value);
    void                            DrawMenu();
    void                            DrawTree();
    void                            DrawTreeNode(NodeId id);
    void                            DrawProperties();
    void                            DrawXml();
    void                            DrawDialogs();
    void                            OpenDocument(const OpenedFile& file);
    void                            NewDocument();
    void                            SaveDocument();
    void                            EditXml(NodeId id);
    void                            Report(const EditResult& result);
    void                            Queue(const std::function<EditResult()>& edit);
    void                            DeleteSelection();
    void                            DrawActions(NodeId id);
};
} // namespace odreditor
