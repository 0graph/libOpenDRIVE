#include "document/XodrDocument.h"
#include <iostream>
#include <stdexcept>

using namespace odreditor;
void Check(bool value, const char* message)
{
    if (!value)
        throw std::runtime_error(message);
}
int main()
{
    try
    {
        XodrDocument document;
        auto         result =
            document.LoadDocument("<?xml version=\"1.0\"?><OpenDRIVE xmlns:v=\"urn:vendor\"><!--keep--><road id=\"1\"><v:extension empty=\"\" "
                                  "zero=\"0\"><v:child><![CDATA[a < b]]></v:child></v:extension><other value=\"1.000\"/></road></OpenDRIVE>");
        Check(bool(result), result.Error.c_str());
        auto       road = document.GetNode(DocumentNode).child("OpenDRIVE").child("road");
        const auto parent = document.GetId(road), extension = document.GetId(road.child("v:extension"));
        const auto descendant = document.GetId(road.child("v:extension").child("v:child"));
        NodeId     selection = extension;
        const auto original = document.SaveDocument();
        result = document.Apply("Grouped edit",
                                selection,
                                [&]
                                {
                                    document.SetAttribute(extension, "zero", std::nullopt);
                                    document.SetAttribute(extension, "empty", "0");
                                    document.SetAttribute(parent, "name", "Road & river");
                                    document.RemoveElement(extension);
                                    selection = parent;
                                });
        Check(bool(result), result.Error.c_str());
        Check(!document.GetNode(extension) && !document.GetNode(descendant), "Deleted descendants remain accessible");
        Check(document.IsModified(), "Edit did not mark modified");
        Check(document.Undo(selection) && selection == extension, "Undo selection lost");
        Check(document.SaveDocument() == original, "Undo did not restore content or attribute order");
        Check(!document.IsModified() && document.GetNode(descendant), "Undo did not restore identity or saved state");
        Check(!document.Undo(selection), "Group created more than one history entry");
        Check(document.Redo(selection) && !document.GetNode(descendant), "Redo did not invalidate descendants");
        document.Undo(selection);
        result = document.Apply("Atomic failure",
                                selection,
                                [&]
                                {
                                    document.SetAttribute(extension, "added", "value");
                                    document.MoveElement(parent, descendant);
                                });
        Check(!result && original == document.SaveDocument(), "Failed group changed document");
        result = document.Apply("Invalid draft", selection, [&] { document.ReplaceSubtree(extension, "<broken>"); });
        Check(!result && document.SaveDocument() == original, "Malformed XML replaced valid content");
        result = document.Apply("Move", selection, [&] { document.MoveElement(extension, parent); });
        Check(bool(result) && document.GetNode(extension) == document.GetNode(parent).last_child(), "Move failed");
        document.Undo(selection);
        Check(document.SaveDocument() == original, "Move undo changed order");
        NodeId duplicate = 0;
        result = document.Apply("Duplicate", selection, [&] { duplicate = document.DuplicateElement(extension); });
        Check(bool(result) && duplicate != extension, "Duplicate reused identity");
        Check(document.SaveSubtree(duplicate).find("<![CDATA[a < b]]>") != std::string::npos, "CDATA was lost");
        document.Undo(selection);
        Check(!document.GetNode(duplicate), "Undone duplicate remains accessible");
        document.Redo(selection);
        Check(document.GetNode(duplicate), "Redo did not restore duplicate identity");
        XodrDocument reopened;
        Check(bool(reopened.LoadDocument(document.SaveDocument())), "Save/reopen failed");
        Check(reopened.SaveDocument() == document.SaveDocument(), "Save/reopen lost XML content");
        const auto saved = document.SaveDocument();
        result = document.LoadDocument("<one/><two/>");
        Check(!result && saved == document.SaveDocument(), "Multiple roots accepted");
        result = document.Apply(
            "Replace document", selection, [&] { selection = document.ReplaceSubtree(DocumentNode, "<OpenDRIVE><road id=\"1\"/></OpenDRIVE>"); });
        Check(bool(result) && !document.GetNode(parent), "Old IDs resolve after document replacement");
        document.Undo(selection);
        Check(document.GetNode(parent) && document.GetNode(descendant), "Whole document undo lost identity");
        std::cout << "Document preservation and history passed\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
