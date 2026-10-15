#include "edit/Commands.h"
#include <iostream>
#include <stdexcept>

using namespace odreditor;
namespace
{
void Check(bool value, const std::string& message)
{
    if (!value)
        throw std::runtime_error(message);
}
void Require(EditResult result)
{
    Check(bool(result), result.Error);
}
pugi::xml_node Find(XodrDocument& document, const std::string& path)
{
    return document.GetNode(DocumentNode).select_node(path.c_str()).node();
}
NodeId Id(XodrDocument& document, const std::string& path)
{
    return document.GetId(Find(document, path));
}
std::string Lane(int id, const std::string& contents = {})
{
    return "<lane id='" + std::to_string(id) + "'>" + contents + "</lane>";
}
std::string Section(int s, const std::string& side, const std::string& lanes)
{
    return "<laneSection s='" + std::to_string(s) + "'><" + side + ">" + lanes + "</" + side + "></laneSection>";
}
std::string Road(const std::string& id, const std::string& sections, const std::string& links = {}, const std::string& extra = {})
{
    return "<road id='" + id + "' junction='-1'><link>" + links + "</link><lanes>" + sections + "</lanes>" + extra + "</road>";
}
void SectionCompaction()
{
    for (const int sign : {-1, 1})
    {
        const std::string side = sign == -1 ? "right" : "left";
        const auto        one = std::to_string(sign), two = std::to_string(sign * 2), three = std::to_string(sign * 3);
        XodrDocument      doc;
        Commands          commands(doc);
        const auto        lanes = Lane(sign, "<link><successor id='" + one + "'/></link>") +
                           Lane(sign * 2, "<link><predecessor id='" + two + "'/><successor id='" + two + "'/></link>") + Lane(sign * 3);
        Require(doc.LoadDocument(
            "<OpenDRIVE>" +
            Road("R",
                 Section(0,
                         side,
                         Lane(sign, "<link><successor id='" + one + "'/></link>") + Lane(sign * 2, "<link><successor id='" + two + "'/></link>")) +
                     Section(10, side, lanes) + Section(20, side, Lane(sign * 2, "<link><predecessor id='" + two + "'/></link>"))) +
            "<vendor lane='" + two + "'/></OpenDRIVE>"));
        const std::string section = "/OpenDRIVE/road/lanes/laneSection[@s='10']/" + side;
        NodeId            selection = Id(doc, section + "/lane[@id='" + one + "']");
        const auto        survivor = Id(doc, section + "/lane[@id='" + two + "']");
        const auto        original = doc.SaveDocument();
        Require(commands.Delete(selection, selection));
        Check(std::string(doc.GetNode(survivor).attribute("id").value()) == one, "Survivor identity or ID lost");
        Check(std::string(doc.GetNode(survivor).child("link").child("predecessor").attribute("id").value()) == two,
              "Outgoing target was incorrectly renumbered");
        Check(
            std::string(
                Find(doc, "/OpenDRIVE/road/lanes/laneSection[@s='0']/" + side + "/lane[@id='" + two + "']/link/successor").attribute("id").value()) ==
                one,
            "Incoming link did not follow survivor");
        Check(!Find(doc, "/OpenDRIVE/road/lanes/laneSection[@s='0']/" + side + "/lane[@id='" + one + "']/link/successor"),
              "Deleted target aliased the survivor");
        Check(std::string(Find(doc, "/OpenDRIVE/road/lanes/laneSection[@s='20']/" + side + "/lane/link/predecessor").attribute("id").value()) == one,
              "Following section reference not updated");
        Check(std::string(Find(doc, "/OpenDRIVE/vendor").attribute("lane").value()) == two, "Unknown reference changed");
        Check(doc.Undo(selection) && doc.SaveDocument() == original && doc.GetNode(survivor), "Corrected deletion was not one reversible edit");
        Check(!doc.Undo(selection), "Corrections created separate history entries");
        Require(commands.InsertLane(Id(doc, section), 0, selection));
        Check(std::string(doc.GetNode(survivor).attribute("id").value()) == three, "Insert did not shift outer lane");
        Check(!doc.GetNode(selection).child("link"), "Insert invented a connection");
        doc.Undo(selection);
        auto order = std::vector<NodeId>{Id(doc, section + "/lane[@id='" + three + "']"), survivor, selection};
        Require(commands.ReorderLanes(Id(doc, section), order, selection));
        Check(std::string(doc.GetNode(order.front()).attribute("id").value()) == one, "Reorder lost identity");
        doc.Undo(selection);
        Require(commands.RenameIds({{selection, three}, {order.front(), one}}, selection));
        Check(std::string(doc.GetNode(order.front()).attribute("id").value()) == one, "Simultaneous rename collided");
    }
}
void RoadContactPoints()
{
    for (const auto* direction : {"predecessor", "successor"})
        for (const auto* contact : {"start", "end"})
        {
            XodrDocument      doc;
            Commands          commands(doc);
            const std::string roadLink = "<" + std::string(direction) + " elementType='road' elementId='B' contactPoint='" + contact + "'/>";
            const auto        source = Section(0,
                                        "right",
                                        Lane(-1, "<link><" + std::string(direction) + " id='2'/></link>") +
                                            Lane(-2, "<link><" + std::string(direction) + " id='1'/></link>"));
            Require(doc.LoadDocument("<OpenDRIVE>" + Road("A", source, roadLink) +
                                     Road("B", Section(0, "left", Lane(1) + Lane(2)) + Section(10, "left", Lane(1) + Lane(2))) + "</OpenDRIVE>"));
            const std::string affected = std::string(contact) == "start" ? "0" : "10";
            const std::string other = std::string(contact) == "start" ? "10" : "0";
            NodeId            selection = Id(doc, "/OpenDRIVE/road[@id='B']/lanes/laneSection[@s='" + affected + "']/left/lane[@id='1']");
            const auto        survivor = Id(doc, "/OpenDRIVE/road[@id='B']/lanes/laneSection[@s='" + affected + "']/left/lane[@id='2']");
            ReferenceIndex    index(doc);
            const auto        sourceLane = Id(doc, "/OpenDRIVE/road[@id='A']/lanes/laneSection/right/lane[@id='-1']");
            const auto resolved = std::string(direction) == "predecessor" ? index.GetPredecessorLane(sourceLane) : index.GetSuccessorLane(sourceLane);
            Check(resolved.Targets == std::vector<NodeId>{survivor}, "Road-end resolver chose the wrong boundary");
            Require(commands.Delete(selection, selection));
            Check(std::string(Find(doc, "/OpenDRIVE/road[@id='A']/lanes/laneSection/right/lane[@id='-1']/link/" + std::string(direction))
                                  .attribute("id")
                                  .value()) == "1",
                  "Road-end reference did not follow identity");
            Check(!Find(doc, "/OpenDRIVE/road[@id='A']/lanes/laneSection/right/lane[@id='-2']/link/" + std::string(direction)),
                  "Road-end deleted target aliased a survivor");
            Check(Find(doc, "/OpenDRIVE/road[@id='B']/lanes/laneSection[@s='" + other + "']/left/lane[@id='2']"),
                  "Wrong boundary section was renumbered");
        }
}
void JunctionScopes()
{
    for (const auto* contact : {"start", "end"})
    {
        XodrDocument doc;
        Commands     commands(doc);
        const auto   source = Road("I", Section(0, "right", Lane(-1) + Lane(-2)), "<successor elementType='junction' elementId='J'/>");
        const auto   target = Road("C", Section(0, "right", Lane(-1) + Lane(-2)) + Section(10, "right", Lane(-1) + Lane(-2)));
        const auto   junction = "<junction id='J'><connection id='0' incomingRoad='I' connectingRoad='C' contactPoint='" + std::string(contact) +
                              "'><laneLink from='-1' to='-1'/><laneLink from='-2' to='-2'/></connection></junction>";
        Require(doc.LoadDocument("<OpenDRIVE>" + source + target + junction + "</OpenDRIVE>"));
        NodeId selection = Id(doc, "/OpenDRIVE/road[@id='I']/lanes/laneSection/right/lane[@id='-1']");
        Require(commands.Delete(selection, selection));
        auto mapping = Find(doc, "/OpenDRIVE/junction/connection/laneLink");
        Check(std::string(mapping.attribute("from").value()) == "-1" && std::string(mapping.attribute("to").value()) == "-2" &&
                  !mapping.next_sibling(),
              "Junction from mapping lost its scope");
        const std::string section = std::string(contact) == "start" ? "0" : "10";
        selection = Id(doc, "/OpenDRIVE/road[@id='C']/lanes/laneSection[@s='" + section + "']/right/lane[@id='-1']");
        Require(commands.Delete(selection, selection));
        Check(std::string(Find(doc, "/OpenDRIVE/junction/connection/laneLink").attribute("to").value()) == "-1",
              "Junction to mapping lost contact point");
    }
}
void Validities()
{
    const auto lanes = Section(0, "right", Lane(-1) + Lane(-2) + Lane(-3));
    for (const auto& range : {std::string("fromLane='-3' toLane='-2'"), std::string("fromLane='-1' toLane='-1'")})
    {
        XodrDocument doc;
        Commands     commands(doc);
        Require(doc.LoadDocument("<OpenDRIVE>" + Road("R", lanes, {}, "<signals><signal id='S' s='5'><validity " + range + "/></signal></signals>") +
                                 "</OpenDRIVE>"));
        NodeId     selection = Id(doc, "/OpenDRIVE/road/lanes/laneSection/right/lane[@id='-1']");
        const auto original = doc.SaveDocument();
        auto       result = commands.Delete(selection, selection);
        if (range.find("-3") != std::string::npos)
        {
            Require(result);
            auto validity = Find(doc, "/OpenDRIVE/road/signals/signal/validity");
            Check(std::string(validity.attribute("fromLane").value()) == "-2" && std::string(validity.attribute("toLane").value()) == "-1",
                  "Validity membership did not follow lanes");
            doc.Undo(selection);
            Check(doc.SaveDocument() == original, "Validity correction undo failed");
        }
        else
            Check(!result && doc.SaveDocument() == original && doc.GetNode(selection), "Empty validity broadened applicability");
    }
    XodrDocument doc;
    Commands     commands(doc);
    Require(doc.LoadDocument(
        "<OpenDRIVE>" + Road("R", lanes, {}, "<signals><signal id='S' s='5'><validity fromLane='-2' toLane='-1' vendor='keep'/></signal></signals>") +
        "</OpenDRIVE>"));
    NodeId selection = Id(doc, "/OpenDRIVE/road/lanes/laneSection/right/lane[@id='-1']");
    Require(commands.RenameIds({{selection, "-4"}}, selection));
    Check(Find(doc, "/OpenDRIVE/road/signals/signal/validity[@fromLane='-4'][@toLane='-4'][@vendor='keep']") &&
              Find(doc, "/OpenDRIVE/road/signals/signal/validity[@fromLane='-2'][@toLane='-2']"),
          "Noncontiguous validity was broadened");
    Require(doc.LoadDocument("<OpenDRIVE>" +
                             Road("R",
                                  lanes + Section(10, "right", Lane(-1) + Lane(-2) + Lane(-3)),
                                  {},
                                  "<objects><object id='O' s='0' validLength='20'><validity fromLane='-3' toLane='-1'/></object></objects>") +
                             "</OpenDRIVE>"));
    selection = Id(doc, "/OpenDRIVE/road/lanes/laneSection[@s='0']/right/lane[@id='-1']");
    const auto original = doc.SaveDocument();
    Check(!commands.Delete(selection, selection) && doc.SaveDocument() == original, "Cross-section validity lost meaning");
}
void IdentitiesAndFailures()
{
    XodrDocument doc;
    Commands     commands(doc);
    Require(doc.LoadDocument("<OpenDRIVE>" +
                             Road("R",
                                  Section(0, "right", Lane(-1) + Lane(-2)),
                                  "<successor elementType='road' elementId='R' contactPoint='start'/>",
                                  "<signals><signal id='S' s='0'/><signalReference id='S' s='0'/></signals><vendor road='R'/>") +
                             "<junction id='J'><connection id='C' incomingRoad='R' connectingRoad='R' contactPoint='start'/><priority high='R' "
                             "low='R'/><controller id='T'/></junction><controller id='T'><control signalId='S'/></controller></OpenDRIVE>"));
    NodeId     selection = Id(doc, "/OpenDRIVE/road");
    const auto originalRoad = selection;
    Require(commands.RenameIds({{selection, "changed"}}, selection));
    Check(std::string(Find(doc, "/OpenDRIVE/road/link/successor").attribute("elementId").value()) == "changed", "Road link rename failed");
    Check(std::string(Find(doc, "/OpenDRIVE/junction/connection").attribute("incomingRoad").value()) == "changed", "Junction road rename failed");
    Check(std::string(Find(doc, "/OpenDRIVE/road/vendor").attribute("road").value()) == "R", "Unknown attribute rewritten");
    Require(commands.Duplicate(originalRoad, selection));
    auto copied = doc.GetNode(selection);
    Check(std::string(copied.attribute("id").value()) != "changed" &&
              std::string(copied.child("link").child("successor").attribute("elementId").value()) == copied.attribute("id").value(),
          "Duplicate self-reference did not follow copy");
    Check(std::string(copied.child("signals").child("signal").attribute("id").value()) ==
              copied.child("signals").child("signalReference").attribute("id").value(),
          "Duplicate internal signal reference failed");
    Check(std::string(Find(doc, "/OpenDRIVE/controller/control").attribute("signalId").value()) == "S", "Duplicate rewrote an outside reference");
    Require(commands.Delete(Id(doc, "/OpenDRIVE/road[@id='changed']/signals/signal"), selection));
    Check(!Find(doc, "/OpenDRIVE/road[@id='changed']/signals/signalReference") && !Find(doc, "/OpenDRIVE/controller/control"),
          "Signal reference cleanup failed");
    Require(commands.Delete(originalRoad, selection));
    Check(!Find(doc, "/OpenDRIVE/junction/connection") && !Find(doc, "/OpenDRIVE/junction/priority"), "Road cleanup left required references");
    Require(doc.LoadDocument("<OpenDRIVE>" + Road("R", Section(0, "right", Lane(-1) + Lane(-2) + Lane(-2))) + "</OpenDRIVE>"));
    selection = Id(doc, "/OpenDRIVE/road/lanes/laneSection/right/lane[@id='-1']");
    auto original = doc.SaveDocument();
    Check(!commands.Delete(selection, selection) && doc.SaveDocument() == original, "Ambiguous side was partly changed");
    Require(doc.LoadDocument("<OpenDRIVE>" + Road("R", Section(0, "right", Lane(-1) + Lane(-2)) + Section(10, "left", Lane(1) + Lane(1))) +
                             "</OpenDRIVE>"));
    selection = Id(doc, "/OpenDRIVE/road/lanes/laneSection[@s='0']/right/lane[@id='-1']");
    Require(commands.Delete(selection, selection));
    Check(Find(doc, "/OpenDRIVE/road/lanes/laneSection[@s='10']/left/lane[@id='1']"), "Unrelated invalid section was changed");
}
void ScopedMovesAndAmbiguity()
{
    XodrDocument doc;
    Commands     commands(doc);
    Require(doc.LoadDocument("<OpenDRIVE>" +
                             Road("R",
                                  "<laneSection s='0'><left><lane id='1'/></left><right><lane id='-1'/><lane id='-2'/></right></laneSection>",
                                  {},
                                  "<signals><signal id='S' s='0'><validity fromLane='-2' toLane='-1'/></signal></signals>") +
                             "</OpenDRIVE>"));
    NodeId     selection = Id(doc, "/OpenDRIVE/road/lanes/laneSection/right/lane[@id='-1']");
    const auto moved = selection;
    const auto original = doc.SaveDocument();
    Require(commands.Move(selection, Id(doc, "/OpenDRIVE/road/lanes/laneSection/left"), 0, selection));
    Check(std::string(doc.GetNode(moved).attribute("id").value()) == "2", "Side move did not renumber moved identity");
    Check(Find(doc, "/OpenDRIVE/road/signals/signal/validity[@fromLane='2'][@toLane='2']"), "Side move lost validity member");
    doc.Undo(selection);
    Check(doc.SaveDocument() == original, "Side move undo changed content");
    Require(doc.LoadDocument("<OpenDRIVE>" +
                             Road("R",
                                  Section(0, "right", Lane(-1) + Lane(-2)) + Section(10, "right", Lane(-1)),
                                  {},
                                  "<signals><signal id='S' s='0'><validity fromLane='-2' toLane='-1'/></signal></signals>") +
                             "</OpenDRIVE>"));
    selection = Id(doc, "/OpenDRIVE/road/lanes/laneSection[@s='0']/right/lane[@id='-1']");
    const auto beforeMove = doc.SaveDocument();
    Check(!commands.Move(selection, Id(doc, "/OpenDRIVE/road/lanes/laneSection[@s='10']/right"), 0, selection) && doc.SaveDocument() == beforeMove,
          "Cross-section move changed validity membership");
    Require(doc.LoadDocument("<OpenDRIVE>" +
                             Road("I",
                                  Section(0, "right", Lane(-1) + Lane(-2)),
                                  "<predecessor elementType='junction' elementId='J'/><successor elementType='junction' elementId='J'/>") +
                             Road("C", Section(0, "right", Lane(-1) + Lane(-2))) +
                             "<junction id='J'><connection id='0' incomingRoad='I' connectingRoad='C' contactPoint='start'><laneLink from='-2' "
                             "to='-2'/></connection></junction></OpenDRIVE>"));
    selection = Id(doc, "/OpenDRIVE/road[@id='I']/lanes/laneSection/right/lane[@id='-1']");
    const auto beforeAmbiguity = doc.SaveDocument();
    auto       result = commands.Delete(selection, selection);
    Check(!result && doc.SaveDocument() == beforeAmbiguity && result.Node == Id(doc, "/OpenDRIVE/junction/connection/laneLink"),
          "Ambiguous junction was edited or diagnostic lost its node");
    Require(doc.LoadDocument("<OpenDRIVE>" +
                             Road("A",
                                  Section(0, "right", Lane(-1, "<link><successor id='-1'/></link>")),
                                  "<successor elementType='road' elementId='B' contactPoint='start'/>") +
                             Road("B", Section(0, "right", Lane(-4) + Lane(-7))) + "</OpenDRIVE>"));
    selection = Id(doc, "/OpenDRIVE/road[@id='B']/lanes/laneSection/right/lane[@id='-4']");
    const auto beforeAlias = doc.SaveDocument();
    Check(!commands.Delete(selection, selection) && doc.SaveDocument() == beforeAlias, "An unresolved reference attached to an inherited number");
    Require(doc.LoadDocument(
        "<OpenDRIVE>" +
        Road("R", "<laneSection s='0'><left><lane id='3'/></left><right><lane id='-1'/><lane id='-4'/><lane id='-7'/></right></laneSection>") +
        "</OpenDRIVE>"));
    selection = Id(doc, "/OpenDRIVE/road/lanes/laneSection/right/lane[@id='-1']");
    const auto survivor = Id(doc, "/OpenDRIVE/road/lanes/laneSection/right/lane[@id='-7']");
    Require(commands.Delete(selection, selection));
    Check(std::string(doc.GetNode(survivor).attribute("id").value()) == "-2" && Find(doc, "/OpenDRIVE/road/lanes/laneSection/left/lane[@id='3']"),
          "Pre-gapped compaction changed the other side");
}

} // namespace
int main()
{
    try
    {
        SectionCompaction();
        RoadContactPoints();
        JunctionScopes();
        Validities();
        IdentitiesAndFailures();
        ScopedMovesAndAmbiguity();
        std::cout << "Lane and reference correction families passed\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
