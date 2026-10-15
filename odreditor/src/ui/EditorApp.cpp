#include "EditorApp.h"
#include "Notices.h"
#include <algorithm>
#include <cctype>
#include <imgui.h>
#include <imgui_stdlib.h>
#include <string_view>

namespace odreditor
{
namespace
{
std::string Label(pugi::xml_node node)
{
    if (node.type() == pugi::node_comment)
        return "#comment";
    if (node.type() == pugi::node_cdata)
        return "#cdata";
    if (node.type() == pugi::node_pcdata)
        return "#text";
    std::string label = node.name();
    if (node.attribute("id"))
        label += "  " + std::string(node.attribute("id").value());
    if (node.attribute("name") && *node.attribute("name").value())
        label += "  " + std::string(node.attribute("name").value());
    return label;
}
bool IsVisible(pugi::xml_node node)
{
    if (node.type() != pugi::node_pcdata)
        return true;
    const std::string_view value = node.value();
    return std::any_of(value.begin(), value.end(), [](unsigned char c) { return !std::isspace(c); });
}
std::string Lowercase(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return std::tolower(c); });
    return text;
}
std::string Path(pugi::xml_node node)
{
    std::string path;
    for (; node && node.type() != pugi::node_document; node = node.parent())
    {
        std::string part = node.name();
        if (node.attribute("id"))
            part += "[" + std::string(node.attribute("id").value()) + "]";
        else if (std::string_view(node.name()) == "laneSection")
            part += "[s=" + std::string(node.attribute("s").value()) + "]";
        path = "/" + part + path;
    }
    return path;
}
void DrawSplitter(const char* name, float& width, float direction)
{
    ImGui::SameLine();
    ImGui::InvisibleButton(name, {4, -1});
    if (ImGui::IsItemActive())
        width += direction * ImGui::GetIO().MouseDelta.x;
    if (ImGui::IsItemHovered() || ImGui::IsItemActive())
    {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
        ImGui::GetWindowDrawList()->AddRectFilled(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), ImGui::GetColorU32(ImGuiCol_Header));
    }
    ImGui::SameLine();
}
void Section(const char* title)
{
    const auto pos = ImGui::GetCursorScreenPos();
    const auto width = ImGui::GetContentRegionAvail().x;
    const auto height = ImGui::GetFrameHeight();
    ImGui::GetWindowDrawList()->AddRectFilled(
        pos, {pos.x + width, pos.y + height}, ImGui::GetColorU32(ImGuiCol_Header), ImGui::GetStyle().FrameRounding);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(title);
}
} // namespace
EditorApp::EditorApp()
{
    Selection = Document.GetId(Document.GetNode(DocumentNode).child("OpenDRIVE"));
    EditXml(DocumentNode);
}
void EditorApp::Report(const EditResult& result)
{
    Error = result.Error.empty() ? result.Notice : result.Error;
    if (!result && result.Node && Document.GetNode(result.Node))
        Selection = result.Node;
}
void EditorApp::Queue(const std::function<EditResult()>& edit)
{
    Deferred = [this, edit] { Report(edit()); };
}
void EditorApp::EditXml(NodeId id)
{
    ++XmlGeneration;
    XmlTarget = id;
    XmlOriginal = XmlDraft = Document.SaveSubtree(id);
    XmlRevision = Document.GetRevision();
    ShowXml = true;
}
void EditorApp::OpenDocument(const OpenedFile& file)
{
    if (!file.Error.empty())
    {
        Error = file.Error;
        return;
    }
    const auto result = Document.LoadDocument(file.Bytes);
    Report(result);
    if (!result)
        return;
    FileName = file.Name;
    Selection = Document.GetId(Document.GetNode(DocumentNode).child("OpenDRIVE"));
    if (!Selection)
        Selection = Document.GetId(Document.GetNode(DocumentNode).first_child());
    EditXml(DocumentNode);
}
void EditorApp::NewDocument()
{
    Document.CreateDocument();
    FileName = "untitled.xodr";
    Selection = Document.GetId(Document.GetNode(DocumentNode).child("OpenDRIVE"));
    Error.clear();
    EditXml(DocumentNode);
}
void EditorApp::TrackPropertyInput(NodeId id, PropertyKind kind, const std::string& name, const std::string& value)
{
    if (!ImGui::IsItemActive() && !ImGui::IsItemDeactivatedAfterEdit())
        return;
    if (ActiveProperty && (ActiveProperty->Node != id || ActiveProperty->Kind != kind || ActiveProperty->Name != name))
        PropertyEdits.push_back(std::move(*ActiveProperty));
    ActiveProperty = PropertyDraft{id, kind, name, value};
    PropertyActive = ImGui::IsItemActive();
}
bool EditorApp::CommitPropertyInputs()
{
    if (ActiveProperty && (!PropertyActive || PendingSave || PendingNew || PendingOpen))
    {
        PropertyEdits.push_back(std::move(*ActiveProperty));
        ActiveProperty.reset();
    }
    bool success = true;
    for (const auto& property : PropertyEdits)
    {
        NodeId     edited = property.Node;
        EditResult result;
        if (property.Kind == PropertyKind::Attribute)
            result = Edit.SetAttribute(edited, property.Name, property.Value, edited);
        else if (property.Kind == PropertyKind::Name)
            result = Edit.RenameElement(edited, property.Value, edited);
        else
            result = Edit.SetText(edited, property.Value, edited);
        Report(result);
        success &= bool(result);
    }
    PropertyEdits.clear();
    return success;
}
bool EditorApp::HasUnsavedChanges() const
{
    if (Document.IsModified() || XmlDraft != XmlOriginal)
        return true;
    if (!ActiveProperty)
        return false;
    const auto& property = *ActiveProperty;
    const auto  node = Document.GetNode(property.Node);
    const char* value = property.Kind == PropertyKind::Name   ? node.name()
                        : property.Kind == PropertyKind::Text ? node.value()
                                                              : node.attribute(property.Name.c_str()).value();
    return property.Value != value;
}
void EditorApp::SaveDocument()
{
    if (XmlDraft != XmlOriginal)
    {
        Error = "Apply or revert the XML draft before saving";
        return;
    }
    DownloadFile(FileName, Document.SaveDocument());
    Document.MarkSaved();
}
void EditorApp::DeleteSelection()
{
    if (Selection && Selection != DocumentNode)
    {
        const auto id = Selection;
        Queue([this, id] { return Edit.Delete(id, Selection); });
    }
}
void EditorApp::DrawActions(NodeId id)
{
    if (ImGui::MenuItem("Edit XML"))
    {
        if (XmlDraft != XmlOriginal)
            Error = "Apply or revert the current XML draft first";
        else
            EditXml(id);
    }
    if (ImGui::MenuItem("Duplicate", "Ctrl+D"))
        Queue([this, id] { return Edit.Duplicate(id, Selection); });
    if (ImGui::MenuItem("Delete", "Del"))
        Queue([this, id] { return Edit.Delete(id, Selection); });
}
void EditorApp::DrawMenu()
{
    if (!ImGui::BeginMainMenuBar())
        return;
    if (ImGui::BeginMenu("File"))
    {
        if (ImGui::MenuItem("New", "Ctrl+N"))
            PendingNew = true;
        if (ImGui::MenuItem("Open...", "Ctrl+O"))
            ChooseFile();
        if (ImGui::MenuItem("Save", "Ctrl+S"))
            PendingSave = true;
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Edit"))
    {
        if (ImGui::MenuItem("Undo", "Ctrl+Z", false, Document.UndoLabel()))
            Document.Undo(Selection);
        if (ImGui::MenuItem("Redo", "Ctrl+Shift+Z", false, Document.RedoLabel()))
            Document.Redo(Selection);
        if (Selection && Selection != DocumentNode)
            DrawActions(Selection);
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Add"))
    {
        ImGui::InputTextWithHint("##element", "Element name", &ElementName);
        if (ImGui::MenuItem("Add child", nullptr, false, Document.GetNode(Selection).type() == pugi::node_element && !ElementName.empty()))
        {
            const auto id = Selection;
            const auto name = ElementName;
            Queue([this, id, name] { return Edit.Insert(id, name, Selection); });
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("View"))
    {
        ImGui::MenuItem("XML", nullptr, &ShowXml);
        ImGui::MenuItem("Diagnostics", nullptr, &ShowDiagnostics);
        ImGui::MenuItem("About", nullptr, &ShowNotices);
        ImGui::EndMenu();
    }
    ImGui::SameLine(std::max(ImGui::GetCursorPosX() + 24, ImGui::GetWindowWidth() - ImGui::CalcTextSize(FileName.c_str()).x - 32));
    ImGui::TextDisabled("%s%s", FileName.c_str(), HasUnsavedChanges() ? " *" : "");
    ImGui::EndMainMenuBar();
}
void EditorApp::DrawTreeNode(NodeId id)
{
    auto node = Document.GetNode(id);
    if (!node || !IsVisible(node) || (!Search.empty() && !Matches.count(id)))
        return;
    ImGui::PushID(reinterpret_cast<void*>(static_cast<std::uintptr_t>(id)));
    auto               children = Document.GetChildren(id);
    bool               hasChildren = std::any_of(children.begin(), children.end(), [&](NodeId child) { return IsVisible(Document.GetNode(child)); });
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
    if (!hasChildren)
        flags |= ImGuiTreeNodeFlags_Leaf;
    if (Selection == id)
        flags |= ImGuiTreeNodeFlags_Selected;
    if (node.parent().type() == pugi::node_document)
        flags |= ImGuiTreeNodeFlags_DefaultOpen;
    if (!Search.empty())
        ImGui::SetNextItemOpen(true);
    const auto label = Label(node);
    const bool open = ImGui::TreeNodeEx("node", flags, "%s", label.c_str());
    if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
        Selection = id;
    if (ImGui::BeginPopupContextItem())
    {
        Selection = id;
        DrawActions(id);
        ImGui::EndPopup();
    }
    if (ImGui::BeginDragDropSource())
    {
        ImGui::SetDragDropPayload("XML_NODE", &id, sizeof(id));
        ImGui::TextUnformatted(label.c_str());
        ImGui::EndDragDropSource();
    }
    if (node.type() == pugi::node_element && ImGui::BeginDragDropTarget())
    {
        if (auto payload = ImGui::AcceptDragDropPayload("XML_NODE"))
        {
            const auto source = *static_cast<const NodeId*>(payload->Data);
            Queue([this, source, id] { return Edit.Move(source, id, 0, Selection); });
        }
        ImGui::EndDragDropTarget();
    }
    if (open)
    {
        for (auto child : children)
            DrawTreeNode(child);
        ImGui::TreePop();
    }
    ImGui::PopID();
}
void EditorApp::DrawTree()
{
    Section("Document");
    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##search", "Search elements", &Search);
    if (IndexedSearch != Search || SearchRevision != Document.GetRevision())
    {
        IndexedSearch = Search;
        SearchRevision = Document.GetRevision();
        Matches.clear();
        if (!Search.empty())
        {
            const auto                          query = Lowercase(Search);
            std::function<void(pugi::xml_node)> match = [&](pugi::xml_node node)
            {
                std::string text = Label(node);
                for (auto attribute : node.attributes())
                    text += " " + std::string(attribute.name()) + " " + attribute.value();
                if (Lowercase(text).find(query) != std::string::npos)
                    for (auto ancestor = node; ancestor; ancestor = ancestor.parent())
                        Matches.insert(Document.GetId(ancestor));
                for (auto child : node.children())
                    match(child);
            };
            match(Document.GetNode(DocumentNode));
        }
    }
    ImGui::BeginChild("Elements", {0, 0});
    for (auto id : Document.GetChildren(DocumentNode))
        DrawTreeNode(id);
    ImGui::EndChild();
}
bool EditorApp::DrawReferencePicker(NodeId id, const std::string& name, const std::string& value)
{
    auto reference = std::find_if(References->GetReferences().begin(),
                                  References->GetReferences().end(),
                                  [&](const Reference& field) { return field.Source == id && field.Attribute == name; });
    if (reference == References->GetReferences().end())
        return false;
    if (ImGui::BeginCombo("##target", value.c_str()))
    {
        ImGui::InputTextWithHint("##filter", "Find target", &ReferenceSearch);
        const auto query = Lowercase(ReferenceSearch);
        for (const auto& entity : References->GetEntities())
        {
            if (entity.Kind != reference->Kind ||
                std::find(reference->Target.Scopes.begin(), reference->Target.Scopes.end(), entity.Scope) == reference->Target.Scopes.end())
                continue;
            const auto label = Label(Document.GetNode(entity.Id));
            if (!query.empty() && Lowercase(label).find(query) == std::string::npos)
                continue;
            ImGui::PushID(reinterpret_cast<void*>(static_cast<std::uintptr_t>(entity.Id)));
            if (ImGui::Selectable(label.c_str(), value == entity.Key))
            {
                const auto key = entity.Key;
                Queue([this, id, name, key] { return Edit.SetAttribute(id, name, key, Selection); });
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", Path(Document.GetNode(entity.Id)).c_str());
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }
    return true;
}
void EditorApp::DrawProperties()
{
    Section("Properties");
    auto node = Document.GetNode(Selection);
    if (!node || Selection == DocumentNode)
        return;
    const auto id = Selection;
    ImGui::PushID(reinterpret_cast<void*>(static_cast<std::uintptr_t>(id)));
    std::string name = node.name();
    if (node.type() == pugi::node_element)
    {
        ImGui::SetNextItemWidth(-1);
        ImGui::InputText("##name", &name);
        TrackPropertyInput(id, PropertyKind::Name, {}, name);
    }
    else if (node.type() != pugi::node_declaration)
    {
        std::string value = node.value();
        ImGui::TextDisabled("%s", Label(node).c_str());
        ImGui::InputTextMultiline("##text", &value, {-1, 120});
        TrackPropertyInput(id, PropertyKind::Text, {}, value);
    }
    DrawAttributes(id);
    DrawChildren(id);
    DrawReferences(id);
    DrawSiblingActions(id);
    ImGui::PopID();
}
void EditorApp::DrawAttributes(NodeId id)
{
    auto node = Document.GetNode(id);
    if (ImGui::BeginTable("Attributes", 2, ImGuiTableFlags_SizingStretchProp))
    {
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 0.34f);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 0.66f);
        for (auto attribute : node.attributes())
        {
            const std::string key = attribute.name();
            std::string       value = attribute.value();
            ImGui::PushID(key.c_str());
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(key.c_str());
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", key.c_str());
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(std::max(40.0f, ImGui::GetContentRegionAvail().x - 28));
            if (!DrawReferencePicker(id, key, value))
            {
                ImGui::InputTextWithHint("##value", "(empty)", &value);
                TrackPropertyInput(id, PropertyKind::Attribute, key, value);
            }
            ImGui::SameLine(0, 4);
            if (ImGui::Button("x"))
                Queue([this, id, key] { return Edit.SetAttribute(id, key, std::nullopt, Selection); });
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Remove attribute");
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (node.type() == pugi::node_element || node.type() == pugi::node_declaration)
    {
        if (ImGui::Button("Add attribute"))
            ImGui::OpenPopup("New attribute");
        if (ImGui::BeginPopup("New attribute"))
        {
            ImGui::InputTextWithHint("##key", "Name", &AttributeName);
            ImGui::InputTextWithHint("##value", "Value (may be empty)", &AttributeValue);
            ImGui::BeginDisabled(AttributeName.empty());
            if (ImGui::Button("Add"))
            {
                const auto key = AttributeName, value = AttributeValue;
                Queue([this, id, key, value] { return Edit.SetAttribute(id, key, value, Selection); });
                AttributeName.clear();
                AttributeValue.clear();
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndDisabled();
            ImGui::EndPopup();
        }
    }
}
void EditorApp::DrawChildren(NodeId id)
{
    auto node = Document.GetNode(id);
    ImGui::Spacing();
    if (node.type() == pugi::node_element && ImGui::CollapsingHeader("Children", ImGuiTreeNodeFlags_DefaultOpen))
    {
        for (auto child : Document.GetChildren(id))
        {
            const auto childNode = Document.GetNode(child);
            if (!IsVisible(childNode))
                continue;
            ImGui::PushID(reinterpret_cast<void*>(static_cast<std::uintptr_t>(child)));
            if (ImGui::Selectable(Label(childNode).c_str(), Selection == child))
                Selection = child;
            ImGui::PopID();
        }
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint("##childname", "Element name", &ElementName);
        ImGui::BeginDisabled(ElementName.empty());
        if (ImGui::Button("Add child"))
        {
            const auto childName = ElementName;
            Queue([this, id, childName] { return Edit.Insert(id, childName, Selection); });
        }
        ImGui::EndDisabled();
    }
}
void EditorApp::DrawReferences(NodeId id)
{
    if (References->GetEntity(id) && ImGui::CollapsingHeader("References"))
    {
        for (const auto& reference : References->GetReferences())
        {
            if (std::find(reference.Target.Targets.begin(), reference.Target.Targets.end(), id) == reference.Target.Targets.end())
                continue;
            ImGui::PushID(reinterpret_cast<void*>(static_cast<std::uintptr_t>(reference.Source)));
            ImGui::PushID(reference.Attribute.c_str());
            auto label = Path(Document.GetNode(reference.Source)) + " @" + reference.Attribute;
            if (ImGui::Selectable(label.c_str()))
                Selection = reference.Source;
            ImGui::PopID();
            ImGui::PopID();
        }
    }
}
void EditorApp::DrawSiblingActions(NodeId id)
{
    auto node = Document.GetNode(id);
    ImGui::Spacing();
    const bool lane = GetEntityKind(node) == EntityKind::Lane;
    auto       previous = lane ? node.previous_sibling("lane") : node.previous_sibling();
    auto       next = lane ? node.next_sibling("lane") : node.next_sibling();
    while (previous && !IsVisible(previous))
        previous = previous.previous_sibling();
    while (next && !IsVisible(next))
        next = next.next_sibling();
    const auto parent = Document.GetId(node.parent());
    ImGui::BeginDisabled(!previous || parent == DocumentNode);
    if (ImGui::Button("Up"))
    {
        const auto before = Document.GetId(previous);
        Queue([this, id, parent, before] { return Edit.Move(id, parent, before, Selection); });
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!next || parent == DocumentNode);
    if (ImGui::Button("Down"))
    {
        const auto before = Document.GetId(lane ? next.next_sibling("lane") : next.next_sibling());
        Queue([this, id, parent, before] { return Edit.Move(id, parent, before, Selection); });
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(parent == DocumentNode && node.type() == pugi::node_element);
    if (ImGui::Button("Delete"))
        DeleteSelection();
    ImGui::EndDisabled();
}

void EditorApp::DrawXml()
{
    if (!ShowXml)
    {
        ImGui::TextDisabled("Viewport");
        return;
    }
    if (XmlRevision != Document.GetRevision() && XmlDraft == XmlOriginal)
        EditXml(Document.GetNode(XmlTarget) ? XmlTarget : DocumentNode);
    Section("XML");
    ImGui::BeginDisabled(XmlDraft == XmlOriginal || !Document.GetNode(XmlTarget));
    if (ImGui::Button("Apply"))
    {
        const auto target = XmlTarget;
        const auto draft = XmlDraft;
        Deferred = [this, target, draft]
        {
            if (XmlRevision != Document.GetRevision())
            {
                Error = "Document changed; revert the XML draft before applying";
                return;
            }
            const auto result = Edit.ReplaceXml(target, draft, Selection);
            Report(result);
            if (result)
                EditXml(target == DocumentNode ? DocumentNode : Selection);
        };
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Revert"))
        EditXml(Document.GetNode(XmlTarget) ? XmlTarget : DocumentNode);
    ImGui::SameLine();
    ImGui::TextDisabled("%s", XmlTarget == DocumentNode ? "Whole document" : "Selected subtree");
    ImGui::PushID(static_cast<int>(XmlGeneration));
    ImGui::InputTextMultiline("##xml", &XmlDraft, {-1, -1}, ImGuiInputTextFlags_AllowTabInput);
    ImGui::PopID();
}
void EditorApp::DrawDialogs()
{
    if (PendingNew || PendingOpen)
    {
        if (Document.IsModified() || XmlDraft != XmlOriginal)
            ImGui::OpenPopup("Discard changes?");
        else
        {
            if (PendingNew)
                NewDocument();
            else
                OpenDocument(*PendingOpen);
            PendingNew = false;
            PendingOpen.reset();
        }
    }
    if (ImGui::BeginPopupModal("Discard changes?", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::TextUnformatted("There are unsaved changes.");
        if (ImGui::Button("Discard"))
        {
            if (PendingNew)
                NewDocument();
            else if (PendingOpen)
                OpenDocument(*PendingOpen);
            PendingNew = false;
            PendingOpen.reset();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel"))
        {
            PendingNew = false;
            PendingOpen.reset();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    if (ShowNotices)
    {
        ImGui::SetNextWindowSize({620, 420}, ImGuiCond_FirstUseEver);
        ImGui::Begin("About", &ShowNotices);
        ImGui::TextWrapped("%s", Notices);
        ImGui::End();
    }
    if (ShowDiagnostics)
    {
        ImGui::SetNextWindowSize({480, 160}, ImGuiCond_FirstUseEver);
        ImGui::Begin("Diagnostics", &ShowDiagnostics);
        ImGui::TextWrapped("%s", Error.empty() ? "No command errors" : Error.c_str());
        ImGui::End();
    }
}
void EditorApp::Draw()
{
    PropertyActive = false;
    if (auto file = TakeOpenedFile())
    {
        if (file->Error.empty())
            PendingOpen = std::move(file);
        else
            Error = file->Error;
    }
    const auto& io = ImGui::GetIO();
    DrawMenu();
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S))
        PendingSave = true;
    if (!io.WantTextInput && !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId))
    {
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_O))
            ChooseFile();
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_N))
            PendingNew = true;
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z))
        {
            if (io.KeyShift)
                Document.Redo(Selection);
            else
                Document.Undo(Selection);
        }
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D))
        {
            const auto id = Selection;
            Queue([this, id] { return Edit.Duplicate(id, Selection); });
        }
        if (ImGui::IsKeyPressed(ImGuiKey_Delete))
            DeleteSelection();
    }
    if (!References || ReferenceRevision != Document.GetRevision())
    {
        References = std::make_unique<ReferenceIndex>(Document);
        ReferenceRevision = Document.GetRevision();
    }
    const float top = ImGui::GetFrameHeight(), bottom = ImGui::GetFrameHeight();
    ImGui::SetNextWindowPos({0, top});
    ImGui::SetNextWindowSize({io.DisplaySize.x, std::max(1.0f, io.DisplaySize.y - top - bottom)});
    ImGui::Begin("Workspace", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
    const float width = ImGui::GetContentRegionAvail().x;
    TreeWidth = std::clamp(TreeWidth, 120.0f, std::max(120.0f, width * 0.35f));
    InspectorWidth = std::clamp(InspectorWidth, 180.0f, std::max(180.0f, width * 0.4f));
    ImGui::BeginChild("Document", {TreeWidth, 0});
    DrawTree();
    ImGui::EndChild();
    DrawSplitter("Tree splitter", TreeWidth, 1);
    ImGui::BeginChild("Center", {std::max(1.0f, ImGui::GetContentRegionAvail().x - InspectorWidth - 20), 0});
    DrawXml();
    ImGui::EndChild();
    DrawSplitter("Inspector splitter", InspectorWidth, -1);
    ImGui::BeginChild("Properties", {0, 0});
    DrawProperties();
    ImGui::EndChild();
    ImGui::End();
    const bool inputsValid = CommitPropertyInputs();
    if (!inputsValid)
    {
        PendingNew = false;
        PendingOpen.reset();
    }
    if (Deferred && inputsValid)
    {
        auto action = std::move(Deferred);
        Deferred = {};
        action();
    }
    Deferred = {};
    DrawDialogs();
    if (PendingSave && inputsValid)
        SaveDocument();
    PendingSave = false;
    SetModified(HasUnsavedChanges());
    ImGui::SetNextWindowPos({0, io.DisplaySize.y - bottom});
    ImGui::SetNextWindowSize({io.DisplaySize.x, bottom});
    ImGui::Begin("Status", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
    if (!Error.empty())
        ImGui::TextUnformatted(Error.c_str());
    else
        ImGui::TextDisabled("%s", Path(Document.GetNode(Selection)).c_str());
    ImGui::End();
}
} // namespace odreditor
