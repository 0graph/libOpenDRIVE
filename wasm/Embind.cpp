#include "libodr/Geometries/Arc.h"
#include "libodr/Geometries/Line.h"
#include "libodr/Geometries/ParamPoly3.h"
#include "libodr/Geometries/Spiral.h"
#include "libodr/OpenDriveMap.h"
#include "libodr/Sampler.h"

#include <emscripten/bind.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace
{
using namespace odr;
using namespace emscripten;

// Borrowed handles preserve parent links and never delete their owner's storage.
// Keep the owner alive and do not retain handles across its reset or mutation.
// Embind requires const property getters; the underlying model remains mutable.
template<typename T>
val borrow(const T* value)
{
    return value ? val(std::shared_ptr<T>(const_cast<T*>(value), [](T*) {})) : val::null();
}

template<typename T>
val borrow(const std::optional<T>& value)
{
    return value ? borrow(&*value) : val::undefined();
}

template<typename T>
val borrow_value(const T& value)
{
    return borrow(&value);
}

template<typename T>
val borrow_value(const std::unique_ptr<T>& value)
{
    return borrow(value.get());
}

template<typename T>
val copy_value(const T& value)
{
    return val(value);
}

// Collection access borrows model objects and copies scalar/array values.
template<typename T, val (*Get)(const T&)>
void bind_vector(const std::string& name)
{
    using V = std::vector<T>;

    class_<V>(name.c_str())
        .template smart_ptr<std::shared_ptr<V>>((name + "Handle").c_str())
        .function("size", &V::size)
        .function(
            "get", +[](const V& values, std::size_t i) { return i < values.size() ? Get(values[i]) : val::undefined(); });
}

template<typename M, val (*Get)(const typename M::mapped_type&)>
void bind_map(const std::string& name)
{
    class_<M>(name.c_str())
        .template smart_ptr<std::shared_ptr<M>>((name + "Handle").c_str())
        .function("size", &M::size)
        .function(
            "get",
            +[](const M& values, const typename M::key_type& key)
            {
                const auto it = values.find(key);
                return it == values.end() ? val::undefined() : Get(it->second);
            })
        .function(
            "keys",
            +[](const M& values)
            {
                val keys = val::array();
                for (const auto& entry : values)
                    keys.call<void>("push", val(entry.first));
                return keys;
            });
}

template<typename S>
void bind_set(const std::string& name)
{
    class_<S>(name.c_str())
        .template smart_ptr<std::shared_ptr<S>>((name + "Handle").c_str())
        .function("size", &S::size)
        .function(
            "has", +[](const S& values, const typename S::value_type& key) { return values.count(key) != 0; })
        .function(
            "values",
            +[](const S& values)
            {
                val result = val::array();
                for (const auto& value : values)
                    result.call<void>("push", val(value));
                return result;
            });
}

val diagnostic(const std::string& description, std::ptrdiff_t offset)
{
    val result = val::object();
    result.set("description", description);
    result.set("xml_offset", offset);
    return result;
}

val load_xml(OpenDriveMap& map, const std::string& xml, const val& options)
{
    val                errors = val::array();
    pugi::xml_document document;
    const auto         parsed = document.load_buffer(xml.data(), xml.size());
    if (!parsed)
        errors.call<void>("push", diagnostic(parsed.description(), parsed.offset));
    else if (!document.child("OpenDRIVE"))
        errors.call<void>("push", diagnostic("missing OpenDRIVE root element", -1));
    else
    {
        const auto flag = [&](const char* name, bool fallback)
        {
            if (options.isUndefined() || options.isNull())
                return fallback;
            const val value = options[name];
            return value.isUndefined() ? fallback : value.as<bool>();
        };
        map.reset();
        const auto result = map.load(document,
                                     flag("with_road_objects", true),
                                     flag("with_lateral_profile", true),
                                     flag("with_lane_height", true),
                                     flag("local_road_object_outlines_use_absolute_z", false),
                                     flag("fix_spiral_edge_cases", true),
                                     flag("with_road_signals", true),
                                     flag("treat_value_zero_as_missing", true));
        // Copy diagnostics before document is destroyed; never expose pugi node pointers.
        for (const auto& error : result.errors)
            errors.call<void>("push", diagnostic(error.description, error.node.offset_debug()));
    }
    val result = val::object();
    result.set("errors", errors);
    return result;
}

template<typename F>
val mesh_with_warnings(F&& make_mesh)
{
    std::vector<std::string> warnings;
    val                      result = val::object();
    result.set("mesh", val(make_mesh(warnings)));
    result.set("warnings", val::array(warnings));
    return result;
}

void bind_geometry()
{
    bind_map<std::map<double, CubicPoly>, borrow_value>("CubicProfile_s_to_poly");
    bind_map<std::map<double, double>, copy_value>("CubicBezier2D_arclen_t");
    bind_map<std::map<double, std::unique_ptr<RoadGeometry>>, borrow_value>("RefLine_s_to_geometry");

    enum_<ParamPoly3::PRange>("PRange").value("Normalized", ParamPoly3::PRange::Normalized).value("ArcLength", ParamPoly3::PRange::ArcLength);
    register_optional<ParamPoly3::PRange>();

    class_<CubicBounds>("CubicBounds")
        .smart_ptr<std::shared_ptr<CubicBounds>>("CubicBoundsHandle")
        .property("min", &CubicBounds::min)
        .property("max", &CubicBounds::max)
        .property("d1_min", &CubicBounds::d1_min)
        .property("d1_max", &CubicBounds::d1_max)
        .property("d1", &CubicBounds::d1)
        .property("d2", &CubicBounds::d2)
        .property("d3", &CubicBounds::d3)
        .constructor<>();

    class_<CubicPoly>("CubicPoly")
        .smart_ptr<std::shared_ptr<CubicPoly>>("CubicPolyHandle")
        .property("a", &CubicPoly::a)
        .property("b", &CubicPoly::b)
        .property("c", &CubicPoly::c)
        .property("d", &CubicPoly::d)
        .property("s_origin", &CubicPoly::s_origin)
        .constructor<>()
        .constructor<double, double, double, double>()
        .constructor<double, double, double, double, double>()
        .function("evaluate", &CubicPoly::evaluate)
        .function("derivative", &CubicPoly::derivative)
        .function("bounds", &CubicPoly::bounds)
        .function("add", &CubicPoly::add)
        .function("subtract", &CubicPoly::subtract)
        .function("negate", &CubicPoly::negate)
        .function("is_zero", &CubicPoly::is_zero)
        .function("set_zero", &CubicPoly::set_zero);

    class_<CubicProfile>("CubicProfile")
        .smart_ptr<std::shared_ptr<CubicProfile>>("CubicProfileHandle")
        .property(
            "s_to_poly", +[](const CubicProfile& self) { return borrow(&self.s_to_poly); })
        .constructor<>()
        .function("evaluate", &CubicProfile::evaluate)
        .function("derivative", &CubicProfile::derivative)
        .function("get_poly", &CubicProfile::get_poly)
        .function("negate", &CubicProfile::negate)
        .function("add", &CubicProfile::add);
    register_optional<CubicPoly>();

    class_<CubicBezier2D>("CubicBezier2D")
        .smart_ptr<std::shared_ptr<CubicBezier2D>>("CubicBezier2DHandle")
        .property("valid_length", &CubicBezier2D::valid_length)
        .property("control_points", &CubicBezier2D::control_points)
        .property(
            "arclen_t", +[](const CubicBezier2D& self) { return borrow(&self.arclen_t); })
        .constructor<std::array<Vec2D, 4>>()
        .function("evaluate", &CubicBezier2D::evaluate)
        .function("derivative", &CubicBezier2D::derivative)
        .function("get_t", &CubicBezier2D::get_t)
        .function("get_subcurve", &CubicBezier2D::get_subcurve)
        .function("get_polyline_t_samples", &CubicBezier2D::get_polyline_t_samples)
        .class_function("get_control_points", &CubicBezier2D::get_control_points)
        .class_function("get_coefficients", &CubicBezier2D::get_coefficients);

    class_<RoadGeometry>("RoadGeometry")
        .smart_ptr<std::shared_ptr<RoadGeometry>>("RoadGeometryHandle")
        .property("s", &RoadGeometry::s)
        .property("x", &RoadGeometry::x)
        .property("y", &RoadGeometry::y)
        .property("hdg", &RoadGeometry::hdg)
        .property("length", &RoadGeometry::length)
        .function("get_xy", &RoadGeometry::get_xy)
        .function("derivative", &RoadGeometry::derivative)
        .function("clone", &RoadGeometry::clone);

    class_<Line, base<RoadGeometry>>("Line").smart_ptr<std::shared_ptr<Line>>("LineHandle").constructor<double, double, double, double, double>();

    class_<Arc, base<RoadGeometry>>("Arc")
        .smart_ptr<std::shared_ptr<Arc>>("ArcHandle")
        .property("curvature", &Arc::curvature)
        .constructor<double, double, double, double, double, double>();

    class_<Spiral, base<RoadGeometry>>("Spiral")
        .smart_ptr<std::shared_ptr<Spiral>>("SpiralHandle")
        .property("curv_start", &Spiral::curv_start)
        .property("curv_end", &Spiral::curv_end)
        .property("spiral_s_start", &Spiral::spiral_s_start)
        .property("spiral_s_end", &Spiral::spiral_s_end)
        .property("c_dot", &Spiral::c_dot)
        .constructor<double, double, double, double, double, double, double>();

    class_<ParamPoly3, base<RoadGeometry>>("ParamPoly3")
        .smart_ptr<std::shared_ptr<ParamPoly3>>("ParamPoly3Handle")
        .property("aU", &ParamPoly3::aU)
        .property("bU", &ParamPoly3::bU)
        .property("cU", &ParamPoly3::cU)
        .property("dU", &ParamPoly3::dU)
        .property("aV", &ParamPoly3::aV)
        .property("bV", &ParamPoly3::bV)
        .property("cV", &ParamPoly3::cV)
        .property("dV", &ParamPoly3::dV)
        .property("p_range", &ParamPoly3::p_range)
        .property(
            "cubic_bezier", +[](const ParamPoly3& self) { return borrow(&self.cubic_bezier); })
        .constructor<double, double, double, double, double, double, double, double, double, double, double, double, double>()
        .constructor<double, double, double, double, double, double, double, double, double, double, double, double, double, ParamPoly3::PRange>();

    class_<RefLine>("RefLine")
        .smart_ptr<std::shared_ptr<RefLine>>("RefLineHandle")
        .property("length", &RefLine::length)
        .property(
            "elevation_profile", +[](const RefLine& self) { return borrow(&self.elevation_profile); })
        .property(
            "s_to_geometry", +[](const RefLine& self) { return borrow(&self.s_to_geometry); })
        .constructor<double>()
        .function("get_xyz", &RefLine::get_xyz)
        .function("derivative", &RefLine::derivative)
        .function("match", &RefLine::match)
        .function(
            "get_geometry", +[](RefLine& self, double s) { return borrow(self.get_geometry(s)); })
        .function(
            "set_geometry", +[](RefLine& self, const RoadGeometry& geometry) { self.s_to_geometry[geometry.s] = geometry.clone(); });
}

void bind_mesh()
{
    bind_vector<Vec3D, copy_value>("Mesh3D_vertices");
    bind_vector<uint32_t, copy_value>("Mesh3D_indices");
    bind_vector<Vec2D, copy_value>("Mesh3D_st_coordinates");

    class_<Mesh3D>("Mesh3D")
        .smart_ptr<std::shared_ptr<Mesh3D>>("Mesh3DHandle")
        .property(
            "vertices", +[](const Mesh3D& self) { return borrow(&self.vertices); })
        .property(
            "indices", +[](const Mesh3D& self) { return borrow(&self.indices); })
        .property(
            "normals", +[](const Mesh3D& self) { return borrow(&self.normals); })
        .property(
            "st_coordinates", +[](const Mesh3D& self) { return borrow(&self.st_coordinates); })
        .constructor<>()
        .function("add_mesh", &Mesh3D::add_mesh)
        .function("get_obj", &Mesh3D::get_obj);
}

void bind_road_marks()
{
    bind_vector<RoadMarkLine, borrow_value>("RoadMarkType_lines");

    class_<RoadMarkLine, base<OdrNode>>("RoadMarkLine")
        .smart_ptr<std::shared_ptr<RoadMarkLine>>("RoadMarkLineHandle")
        .property("s_offset", &RoadMarkLine::s_offset)
        .property("t_offset", &RoadMarkLine::t_offset)
        .property("length", &RoadMarkLine::length)
        .property("width", &RoadMarkLine::width)
        .property("space", &RoadMarkLine::space)
        .property("color", &RoadMarkLine::color)
        .property("rule", &RoadMarkLine::rule)
        .constructor<double, double, double>();

    class_<RoadMarkType, base<OdrNode>>("RoadMarkType")
        .smart_ptr<std::shared_ptr<RoadMarkType>>("RoadMarkTypeHandle")
        .property("name", &RoadMarkType::name)
        .property("width", &RoadMarkType::width)
        .property(
            "lines", +[](const RoadMarkType& self) { return borrow(&self.lines); })
        .constructor<const std::string&>();

    class_<RoadMark, base<OdrNode>>("RoadMark")
        .smart_ptr<std::shared_ptr<RoadMark>>("RoadMarkHandle")
        .property("s_offset", &RoadMark::s_offset)
        .property("type", &RoadMark::type)
        .property("color", &RoadMark::color)
        .property("width", &RoadMark::width)
        .property("height", &RoadMark::height)
        .property("weight", &RoadMark::weight)
        .property("material", &RoadMark::material)
        .property("lane_change", &RoadMark::lane_change)
        .property(
            "type_elem", +[](const RoadMark& self) { return borrow(self.type_elem); })
        .constructor<double, const std::string&, const std::string&>()
        .class_property("StandardWidth", &RoadMark::StandardWidth)
        .class_property("BoldWidth", &RoadMark::BoldWidth);

    class_<SingleRoadMark>("SingleRoadMark")
        .smart_ptr<std::shared_ptr<SingleRoadMark>>("SingleRoadMarkHandle")
        .property("s_start", &SingleRoadMark::s_start)
        .property("s_end", &SingleRoadMark::s_end)
        .property("t_offset", &SingleRoadMark::t_offset)
        .property("width", &SingleRoadMark::width)
        .property("type", &SingleRoadMark::type)
        .constructor<double, double, double, double, const std::string&>();
    bind_vector<SingleRoadMark, borrow_value>("RoadMarkVector");
}

void bind_objects()
{
    bind_vector<RoadObjectCorner, borrow_value>("RoadObjectOutline_outline");
    bind_vector<RoadObjectRepeat, borrow_value>("RoadObject_repeats");
    bind_vector<RoadObjectOutline, borrow_value>("RoadObject_outlines");
    bind_vector<LaneValidity, borrow_value>("RoadObject_lane_validities");

    enum_<RoadObject::Orientation>("Orientation")
        .value("None", RoadObject::Orientation::None)
        .value("Positive", RoadObject::Orientation::Positive)
        .value("Negative", RoadObject::Orientation::Negative);
    register_optional<RoadObject::Orientation>();

    enum_<RoadObjectCorner::Type>("RoadObjectCornerType")
        .value("Local_RelZ", RoadObjectCorner::Type::Local_RelZ)
        .value("Local_AbsZ", RoadObjectCorner::Type::Local_AbsZ)
        .value("Road", RoadObjectCorner::Type::Road);
    register_optional<RoadObjectCorner::Type>();

    class_<LaneValidity, base<OdrNode>>("LaneValidity")
        .smart_ptr<std::shared_ptr<LaneValidity>>("LaneValidityHandle")
        .property("from_lane", &LaneValidity::from_lane)
        .property("to_lane", &LaneValidity::to_lane)
        .constructor<int, int>();

    class_<RoadObjectRepeat, base<OdrNode>>("RoadObjectRepeat")
        .smart_ptr<std::shared_ptr<RoadObjectRepeat>>("RoadObjectRepeatHandle")
        .property("s", &RoadObjectRepeat::s)
        .property("length", &RoadObjectRepeat::length)
        .property("distance", &RoadObjectRepeat::distance)
        .property("t_start", &RoadObjectRepeat::t_start)
        .property("t_end", &RoadObjectRepeat::t_end)
        .property("height_start", &RoadObjectRepeat::height_start)
        .property("height_end", &RoadObjectRepeat::height_end)
        .property("z_offset_start", &RoadObjectRepeat::z_offset_start)
        .property("z_offset_end", &RoadObjectRepeat::z_offset_end)
        .property("width_start", &RoadObjectRepeat::width_start)
        .property("width_end", &RoadObjectRepeat::width_end)
        .constructor<double, double, double>();

    class_<RoadObjectCorner, base<OdrNode>>("RoadObjectCorner")
        .smart_ptr<std::shared_ptr<RoadObjectCorner>>("RoadObjectCornerHandle")
        .property("pt", &RoadObjectCorner::pt)
        .property("height", &RoadObjectCorner::height)
        .property("type", &RoadObjectCorner::type)
        .property("id", &RoadObjectCorner::id)
        .constructor<Vec3D, double, RoadObjectCorner::Type>();

    class_<RoadObjectOutline, base<OdrNode>>("RoadObjectOutline")
        .smart_ptr<std::shared_ptr<RoadObjectOutline>>("RoadObjectOutlineHandle")
        .property("id", &RoadObjectOutline::id)
        .property("fill_type", &RoadObjectOutline::fill_type)
        .property("lane_type", &RoadObjectOutline::lane_type)
        .property("outer", &RoadObjectOutline::outer)
        .property("closed", &RoadObjectOutline::closed)
        .property(
            "outline", +[](const RoadObjectOutline& self) { return borrow(&self.outline); })
        .constructor<>();

    class_<RoadObject, base<OdrNode>>("RoadObject")
        .smart_ptr<std::shared_ptr<RoadObject>>("RoadObjectHandle")
        .property("id", &RoadObject::id)
        .property("s", &RoadObject::s)
        .property("t", &RoadObject::t)
        .property("z_offset", &RoadObject::z_offset)
        .property("length", &RoadObject::length)
        .property("valid_length", &RoadObject::valid_length)
        .property("width", &RoadObject::width)
        .property("radius", &RoadObject::radius)
        .property("height", &RoadObject::height)
        .property("hdg", &RoadObject::hdg)
        .property("pitch", &RoadObject::pitch)
        .property("roll", &RoadObject::roll)
        .property("type", &RoadObject::type)
        .property("name", &RoadObject::name)
        .property("subtype", &RoadObject::subtype)
        .property("orientation", &RoadObject::orientation)
        .property("is_dynamic", &RoadObject::is_dynamic)
        .property(
            "repeats", +[](const RoadObject& self) { return borrow(&self.repeats); })
        .property(
            "outlines", +[](const RoadObject& self) { return borrow(&self.outlines); })
        .property(
            "lane_validities", +[](const RoadObject& self) { return borrow(&self.lane_validities); })
        .constructor<const std::string&>()
        .function(
            "get_mesh", +[](const RoadObject& self, double eps) { return self.get_mesh(eps); })
        .function(
            "get_mesh",
            +[](const RoadObject& self, double eps, double height, double z, bool bounds) { return self.get_mesh(eps, height, z, bounds); })
        .function(
            "get_mesh_with_warnings",
            +[](const RoadObject& self, double eps, double height, double z, bool bounds)
            { return mesh_with_warnings([&](auto& warnings) { return self.get_mesh(eps, height, z, bounds, &warnings); }); })
        .class_function("get_cylinder", &RoadObject::get_cylinder)
        .class_function("get_cube", &RoadObject::get_cube);

    class_<RoadSignal, base<OdrNode>>("RoadSignal")
        .smart_ptr<std::shared_ptr<RoadSignal>>("RoadSignalHandle")
        .property("id", &RoadSignal::id)
        .property("road_id", &RoadSignal::road_id)
        .property("s", &RoadSignal::s)
        .property("t", &RoadSignal::t)
        .property("z_offset", &RoadSignal::z_offset)
        .property("is_dynamic", &RoadSignal::is_dynamic)
        .property("type", &RoadSignal::type)
        .property("subtype", &RoadSignal::subtype)
        .property("orientation", &RoadSignal::orientation)
        .property("value", &RoadSignal::value)
        .property("height", &RoadSignal::height)
        .property("width", &RoadSignal::width)
        .property("hOffset", &RoadSignal::hOffset)
        .property("pitch", &RoadSignal::pitch)
        .property("roll", &RoadSignal::roll)
        .property("name", &RoadSignal::name)
        .property("unit", &RoadSignal::unit)
        .property("text", &RoadSignal::text)
        .property("country", &RoadSignal::country)
        .property(
            "lane_validities", +[](const RoadSignal& self) { return borrow(&self.lane_validities); })
        .constructor<const std::string&,
                     const std::string&,
                     double,
                     double,
                     double,
                     bool,
                     const std::string&,
                     const std::string&,
                     RoadObject::Orientation>()
        .function(
            "get_mesh", +[](const RoadSignal& self) { return self.get_mesh(); })
        .function(
            "get_mesh", +[](const RoadSignal& self, bool bounds) { return self.get_mesh(bounds); })
        .function(
            "get_mesh_with_warnings",
            +[](const RoadSignal& self, bool bounds) { return mesh_with_warnings([&](auto& warnings) { return self.get_mesh(bounds, &warnings); }); })
        .class_function("get_box", &RoadSignal::get_box)
        .class_property("Thickness", &RoadSignal::Thickness)
        .class_property("DefaultWidth", &RoadSignal::DefaultWidth)
        .class_property("DefaultHeight", &RoadSignal::DefaultHeight);
}

void bind_roads()
{
    bind_map<std::map<double, Crossfall::Record>, borrow_value>("Crossfall_records");
    bind_map<std::map<double, HeightOffset>, borrow_value>("Lane_s_to_height_offset");
    bind_map<std::map<double, RoadMark>, borrow_value>("Lane_s_to_road_mark");
    bind_map<std::map<int, Lane>, borrow_value>("LaneSection_id_to_lane");
    bind_map<std::map<double, LaneSection>, borrow_value>("Road_s_to_lane_section");
    bind_map<std::map<double, std::string>, copy_value>("Road_s_to_type");
    bind_map<std::map<double, Speed>, borrow_value>("Road_s_to_speed");
    bind_map<std::map<std::string, RoadObject>, borrow_value>("Road_id_to_object");
    bind_map<std::map<std::string, RoadSignal>, borrow_value>("Road_id_to_signal");

    enum_<Crossfall::Side>("CrossfallSide")
        .value("Both", Crossfall::Side::Both)
        .value("Left", Crossfall::Side::Left)
        .value("Right", Crossfall::Side::Right);
    register_optional<Crossfall::Side>();

    enum_<RoadLink::Type>("RoadLinkType").value("Road", RoadLink::Type::Road).value("Junction", RoadLink::Type::Junction);
    register_optional<RoadLink::Type>();

    enum_<RoadLink::ContactPoint>("RoadLinkContactPoint").value("Start", RoadLink::ContactPoint::Start).value("End", RoadLink::ContactPoint::End);
    register_optional<RoadLink::ContactPoint>();

    enum_<Road::TrafficRule>("TrafficRule").value("LHT", Road::TrafficRule::LHT).value("RHT", Road::TrafficRule::RHT);
    register_optional<Road::TrafficRule>();

    class_<Crossfall::Record>("CrossfallRecord")
        .smart_ptr<std::shared_ptr<Crossfall::Record>>("CrossfallRecordHandle")
        .property(
            "poly", +[](const Crossfall::Record& self) { return borrow(&self.poly); })
        .property("side", &Crossfall::Record::side)
        .constructor<>();

    class_<Crossfall>("Crossfall")
        .smart_ptr<std::shared_ptr<Crossfall>>("CrossfallHandle")
        .property(
            "records", +[](const Crossfall& self) { return borrow(&self.records); })
        .constructor<>()
        .function("get", &Crossfall::get);

    class_<RoadLink, base<OdrNode>>("RoadLink")
        .smart_ptr<std::shared_ptr<RoadLink>>("RoadLinkHandle")
        .property("id", &RoadLink::id)
        .property("type", &RoadLink::type)
        .property("contact_point", &RoadLink::contact_point)
        .constructor<const std::string&, const std::string&, std::optional<RoadLink::ContactPoint>>();

    class_<Speed, base<OdrNode>>("Speed")
        .smart_ptr<std::shared_ptr<Speed>>("SpeedHandle")
        .property("max", &Speed::max)
        .property("unit", &Speed::unit)
        .constructor<const std::string&, const std::string&>();

    class_<HeightOffset, base<OdrNode>>("HeightOffset")
        .smart_ptr<std::shared_ptr<HeightOffset>>("HeightOffsetHandle")
        .property("s_offset", &HeightOffset::s_offset)
        .property("inner", &HeightOffset::inner)
        .property("outer", &HeightOffset::outer)
        .constructor<double, double, double>();

    class_<Lane, base<OdrNode>>("Lane")
        .smart_ptr<std::shared_ptr<Lane>>("LaneHandle")
        .property("id", &Lane::id)
        .property("type", &Lane::type)
        .property("level", &Lane::level)
        .property("predecessor", &Lane::predecessor)
        .property("successor", &Lane::successor)
        .property(
            "lane_width", +[](const Lane& self) { return borrow(&self.lane_width); })
        .property(
            "outer_border", +[](const Lane& self) { return borrow(&self.outer_border); })
        .property(
            "s_to_height_offset", +[](const Lane& self) { return borrow(&self.s_to_height_offset); })
        .property(
            "s_to_road_mark", +[](const Lane& self) { return borrow(&self.s_to_road_mark); })
        .constructor<int>()
        .constructor<int, std::optional<std::string>, std::optional<bool>>()
        .function("get_roadmarks", &Lane::get_roadmarks)
        .function(
            "get_surface_pt", +[](const Lane& self, double s, double t) { return self.get_surface_pt(s, t); })
        .function(
            "get_surface_pt", +[](const Lane& self, double s, double t, bool extrapolate) { return self.get_surface_pt(s, t, nullptr, extrapolate); })
        .function(
            "get_mesh", +[](const Lane& self, double eps) { return self.get_mesh(eps); })
        .function(
            "get_mesh_with_outline",
            +[](const Lane& self, double eps)
            {
                std::vector<uint32_t> outline;
                val                   result = val::object();
                result.set("mesh", val(self.get_mesh(eps, &outline)));
                result.set("outline_indices", val::array(outline));
                return result;
            })
        .function(
            "get_roadmark_mesh", +[](const Lane& self, const SingleRoadMark& mark, double eps) { return self.get_roadmark_mesh(mark, eps); })
        .function("get_roadmark_mesh", &Lane::get_roadmark_mesh);

    class_<LaneSection, base<OdrNode>>("LaneSection")
        .smart_ptr<std::shared_ptr<LaneSection>>("LaneSectionHandle")
        .property("s", &LaneSection::s)
        .property(
            "id_to_lane", +[](const LaneSection& self) { return borrow(&self.id_to_lane); })
        .constructor<double>()
        .function("get_end", &LaneSection::get_end)
        .function("get_length", &LaneSection::get_length)
        .function("get_lane_id", &LaneSection::get_lane_id);

    class_<Road, base<OdrNode>>("Road")
        .smart_ptr<std::shared_ptr<Road>>("RoadHandle")
        .property("id", &Road::id)
        .property("length", &Road::length)
        .property("junction", &Road::junction)
        .property("traffic_rule", &Road::traffic_rule)
        .property("name", &Road::name)
        .property(
            "predecessor", +[](const Road& self) { return borrow(self.predecessor); })
        .property(
            "successor", +[](const Road& self) { return borrow(self.successor); })
        .property(
            "lane_offset", +[](const Road& self) { return borrow(&self.lane_offset); })
        .property(
            "superelevation", +[](const Road& self) { return borrow(&self.superelevation); })
        .property(
            "crossfall", +[](const Road& self) { return borrow(&self.crossfall); })
        .property(
            "ref_line", +[](const Road& self) { return borrow(&self.ref_line); })
        .property(
            "s_to_lane_section", +[](const Road& self) { return borrow(&self.s_to_lane_section); })
        .property(
            "s_to_type", +[](const Road& self) { return borrow(&self.s_to_type); })
        .property(
            "s_to_speed", +[](const Road& self) { return borrow(&self.s_to_speed); })
        .property(
            "id_to_object", +[](const Road& self) { return borrow(&self.id_to_object); })
        .property(
            "id_to_signal", +[](const Road& self) { return borrow(&self.id_to_signal); })
        .constructor<const std::string&, double, const std::string&>()
        .constructor<const std::string&, double, const std::string&, std::optional<Road::TrafficRule>, std::optional<std::string>>()
        .function(
            "get_lane_section", +[](Road& self, double s) { return borrow(self.get_lane_section(s)); })
        .function(
            "get_xyz", +[](const Road& self, double s, double t, double h) { return self.get_xyz(s, t, h); })
        .function(
            "get_xyz",
            +[](const Road& self, double s, double t, double h, bool extrapolate)
            { return self.get_xyz(s, t, h, nullptr, nullptr, nullptr, extrapolate); })
        .function(
            "get_frame",
            +[](const Road& self, double s, double t, double h)
            {
                Vec3D es, et, eh;
                val   result = val::object();
                result.set("xyz", val(self.get_xyz(s, t, h, &es, &et, &eh)));
                result.set("e_s", val(es));
                result.set("e_t", val(et));
                result.set("e_h", val(eh));
                return result;
            })
        .function(
            "get_surface_pt", +[](const Road& self, double s, double t) { return self.get_surface_pt(s, t); })
        .function(
            "get_surface_pt",
            +[](const Road& self, double s, double t, bool extrapolate) { return self.get_surface_pt(s, t, nullptr, extrapolate); });
}

void bind_junctions()
{
    bind_vector<JunctionLaneLink, borrow_value>("JunctionConnection_lane_links");
    bind_map<std::map<std::string, JunctionConnection>, borrow_value>("Junction_id_to_connection");
    bind_map<std::map<std::string, JunctionController>, borrow_value>("Junction_id_to_controller");
    bind_vector<JunctionPriority, borrow_value>("Junction_priorities");

    enum_<JunctionConnection::ContactPoint>("JunctionContactPoint")
        .value("None", JunctionConnection::ContactPoint::None)
        .value("Start", JunctionConnection::ContactPoint::Start)
        .value("End", JunctionConnection::ContactPoint::End);
    register_optional<JunctionConnection::ContactPoint>();

    class_<JunctionLaneLink, base<OdrNode>>("JunctionLaneLink")
        .smart_ptr<std::shared_ptr<JunctionLaneLink>>("JunctionLaneLinkHandle")
        .property("from", &JunctionLaneLink::from)
        .property("to", &JunctionLaneLink::to)
        .constructor<int, int>();

    class_<JunctionConnection, base<OdrNode>>("JunctionConnection")
        .smart_ptr<std::shared_ptr<JunctionConnection>>("JunctionConnectionHandle")
        .property("id", &JunctionConnection::id)
        .property("incoming_road", &JunctionConnection::incoming_road)
        .property("connecting_road", &JunctionConnection::connecting_road)
        .property("contact_point", &JunctionConnection::contact_point)
        .property(
            "lane_links", +[](const JunctionConnection& self) { return borrow(&self.lane_links); })
        .constructor<const std::string&, const std::string&, const std::string&, const std::string&>();

    class_<JunctionPriority, base<OdrNode>>("JunctionPriority")
        .smart_ptr<std::shared_ptr<JunctionPriority>>("JunctionPriorityHandle")
        .property("high", &JunctionPriority::high)
        .property("low", &JunctionPriority::low)
        .constructor<const std::string&, const std::string&>();

    class_<JunctionController, base<OdrNode>>("JunctionController")
        .smart_ptr<std::shared_ptr<JunctionController>>("JunctionControllerHandle")
        .property("id", &JunctionController::id)
        .property("type", &JunctionController::type)
        .property("sequence", &JunctionController::sequence)
        .constructor<const std::string&>();

    class_<Junction, base<OdrNode>>("Junction")
        .smart_ptr<std::shared_ptr<Junction>>("JunctionHandle")
        .property("id", &Junction::id)
        .property("name", &Junction::name)
        .property(
            "id_to_connection", +[](const Junction& self) { return borrow(&self.id_to_connection); })
        .property(
            "id_to_controller", +[](const Junction& self) { return borrow(&self.id_to_controller); })
        .property(
            "priorities", +[](const Junction& self) { return borrow(&self.priorities); })
        .constructor<const std::string&>();
}

void bind_routing()
{
    bind_set<std::unordered_set<RoutingGraphEdge>>("RoutingGraph_edges");
    bind_set<std::unordered_set<WeightedLaneKey>>("RoutingGraph_lane_key_to_successors_value");
    bind_map<std::unordered_map<LaneKey, std::unordered_set<WeightedLaneKey>>, borrow_value>("RoutingGraph_lane_key_to_successors");

    class_<LaneKey>("LaneKey")
        .smart_ptr<std::shared_ptr<LaneKey>>("LaneKeyHandle")
        .property("road_id", &LaneKey::road_id)
        .property("lane_section_s", &LaneKey::lane_section_s)
        .property("lane_id", &LaneKey::lane_id)
        .constructor<const std::string&, double, int>()
        .function("to_string", &LaneKey::to_string);

    class_<WeightedLaneKey, base<LaneKey>>("WeightedLaneKey")
        .smart_ptr<std::shared_ptr<WeightedLaneKey>>("WeightedLaneKeyHandle")
        .property("weight", &WeightedLaneKey::weight)
        .constructor<const LaneKey&, double>()
        .constructor<const std::string&, double, int, double>();

    class_<RoutingGraphEdge>("RoutingGraphEdge")
        .smart_ptr<std::shared_ptr<RoutingGraphEdge>>("RoutingGraphEdgeHandle")
        .property(
            "from", +[](const RoutingGraphEdge& self) { return borrow(&self.from); })
        .property(
            "to", +[](const RoutingGraphEdge& self) { return borrow(&self.to); })
        .property("weight", &RoutingGraphEdge::weight)
        .constructor<LaneKey, LaneKey, double>();
    bind_vector<LaneKey, borrow_value>("RoutingPath");

    class_<RoutingGraph>("RoutingGraph")
        .smart_ptr<std::shared_ptr<RoutingGraph>>("RoutingGraphHandle")
        .property(
            "edges", +[](const RoutingGraph& self) { return borrow(&self.edges); })
        .property(
            "lane_key_to_successors", +[](const RoutingGraph& self) { return borrow(&self.lane_key_to_successors); })
        .property(
            "lane_key_to_predecessors", +[](const RoutingGraph& self) { return borrow(&self.lane_key_to_predecessors); })
        .constructor<>()
        .function("add_edge", &RoutingGraph::add_edge)
        .function("get_lane_successors", &RoutingGraph::get_lane_successors)
        .function("get_lane_predecessors", &RoutingGraph::get_lane_predecessors)
        .function("shortest_path", &RoutingGraph::shortest_path);
}

void bind_samplers()
{
    class_<DerivativeBounds>("DerivativeBounds")
        .smart_ptr<std::shared_ptr<DerivativeBounds>>("DerivativeBoundsHandle")
        .property("value", &DerivativeBounds::value)
        .property("d1", &DerivativeBounds::d1)
        .property("d2", &DerivativeBounds::d2)
        .constructor<>();

    class_<GeometryBounds>("GeometryBounds")
        .smart_ptr<std::shared_ptr<GeometryBounds>>("GeometryBoundsHandle")
        .property("position_d1", &GeometryBounds::position_d1)
        .property("position_d2", &GeometryBounds::position_d2)
        .property("speed_lower", &GeometryBounds::speed_lower)
        .property("tangent_d1", &GeometryBounds::tangent_d1)
        .property("tangent_d2", &GeometryBounds::tangent_d2)
        .property("lateral_d1", &GeometryBounds::lateral_d1)
        .property("lateral_d2", &GeometryBounds::lateral_d2)
        .property("arclength_error", &GeometryBounds::arclength_error)
        .property("frame_mapping_error", &GeometryBounds::frame_mapping_error)
        .constructor<>();

    class_<EdgePoints>("EdgePoints")
        .smart_ptr<std::shared_ptr<EdgePoints>>("EdgePointsHandle")
        .property("inner", &EdgePoints::inner)
        .property("outer", &EdgePoints::outer)
        .constructor<>();

    class_<RefLineSampler>("RefLineSampler")
        .constructor<const RefLine&>()
        .function("get_polyline_s_samples", &RefLineSampler::get_polyline_s_samples);

    class_<RoadSampler>("RoadSampler").constructor<const Road&>().function("get_polyline_s_samples", &RoadSampler::get_polyline_s_samples);

    class_<LaneSampler>("LaneSampler")
        .constructor<const Lane&>()
        .function("edge_points", &LaneSampler::edge_points)
        .function("get_mesh_s_samples", &LaneSampler::get_mesh_s_samples)
        .function(
            "get_mesh_s_samples",
            +[](const LaneSampler& self, double start, double end, double eps) { return self.get_mesh_s_samples(start, end, eps); })
        .function("get_border_s_samples", &LaneSampler::get_border_s_samples)
        .function(
            "get_border_s_samples",
            +[](const LaneSampler& self, double start, double end, double offset, double eps)
            { return self.get_border_s_samples(start, end, offset, eps); });
    function("tan_bounds", &tan_bounds);
    function("product", &product);
    function("sum", &sum);
    function("magnitudes", &magnitudes);
    function("geometry_bounds", &geometry_bounds);
    function("get_ref_line_mandatory_s_samples", &get_ref_line_mandatory_s_samples);
    function("get_road_mandatory_s_samples", &get_road_mandatory_s_samples);
    function("get_lane_mandatory_s_samples", &get_lane_mandatory_s_samples);
}

void bind_map()
{
    bind_map<std::map<std::string, Road>, borrow_value>("OpenDriveMap_id_to_road");
    bind_map<std::map<std::string, Junction>, borrow_value>("OpenDriveMap_id_to_junction");

    class_<OpenDriveMapHeader, base<OdrNode>>("OpenDriveMapHeader")
        .smart_ptr<std::shared_ptr<OpenDriveMapHeader>>("OpenDriveMapHeaderHandle")
        .property("rev_major", &OpenDriveMapHeader::rev_major)
        .property("rev_minor", &OpenDriveMapHeader::rev_minor)
        .property("north", &OpenDriveMapHeader::north)
        .property("east", &OpenDriveMapHeader::east)
        .property("south", &OpenDriveMapHeader::south)
        .property("west", &OpenDriveMapHeader::west)
        .property("date", &OpenDriveMapHeader::date)
        .property("name", &OpenDriveMapHeader::name)
        .property("vendor", &OpenDriveMapHeader::vendor)
        .property("version", &OpenDriveMapHeader::version)
        .property("proj", &OpenDriveMapHeader::proj)
        .constructor<>();

    class_<OpenDriveMap>("OpenDriveMap")
        .smart_ptr<std::shared_ptr<OpenDriveMap>>("OpenDriveMapHandle")
        .property(
            "header", +[](const OpenDriveMap& self) { return borrow(&self.header); })
        .property(
            "id_to_road", +[](const OpenDriveMap& self) { return borrow(&self.id_to_road); })
        .property(
            "id_to_junction", +[](const OpenDriveMap& self) { return borrow(&self.id_to_junction); })
        .constructor<>()
        .function("reset", &OpenDriveMap::reset)
        .function("set_node_parents", &OpenDriveMap::set_node_parents)
        .function("load", &load_xml)
        .function(
            "load", +[](OpenDriveMap& self, const std::string& xml) { return load_xml(self, xml, val::object()); })
        .function(
            "get_mesh", +[](const OpenDriveMap& self, double eps) { return self.get_mesh(eps); })
        .function(
            "get_mesh", +[](const OpenDriveMap& self, double eps, bool bounds) { return self.get_mesh(eps, bounds); })
        .function(
            "get_mesh_with_warnings",
            +[](const OpenDriveMap& self, double eps, bool bounds)
            { return mesh_with_warnings([&](auto& warnings) { return self.get_mesh(eps, bounds, &warnings); }); })
        .function(
            "get_routing_graph", +[](const OpenDriveMap& self) { return self.get_routing_graph(); })
        .function(
            "get_routing_graph_with_diagnostics",
            +[](const OpenDriveMap& self)
            {
                RoutingParseResult diagnostics;
                val                result = val::object();
                result.set("graph", val(self.get_routing_graph(&diagnostics)));
                val errors = val::array();
                for (const auto& error : diagnostics.errors)
                    errors.call<void>("push", diagnostic(error.description, error.node ? error.node->xml_offset : -1));
                result.set("errors", errors);
                return result;
            });
}

EMSCRIPTEN_BINDINGS(libOpenDRIVE)
{
    register_optional<bool>();
    register_optional<int>();
    register_optional<uint32_t>();
    register_optional<double>();
    register_optional<std::string>();
    value_array<Vec2D>("Vec2D").element(emscripten::index<0>()).element(emscripten::index<1>());
    value_array<Vec3D>("Vec3D").element(emscripten::index<0>()).element(emscripten::index<1>()).element(emscripten::index<2>());
    value_array<std::array<Vec2D, 4>>("BezierControlPoints")
        .element(emscripten::index<0>())
        .element(emscripten::index<1>())
        .element(emscripten::index<2>())
        .element(emscripten::index<3>());
    bind_set<std::set<double>>("DoubleSet");

    class_<OdrNode>("OdrNode")
        .smart_ptr<std::shared_ptr<OdrNode>>("OdrNodeHandle")
        .property("xml_offset", &OdrNode::xml_offset)
        .function(
            "parent", +[](OdrNode& self) { return borrow(self.parent()); })
        .function("set_parent", &OdrNode::set_parent, allow_raw_pointers());
    bind_geometry();
    bind_mesh();
    bind_road_marks();
    bind_objects();
    bind_roads();
    bind_junctions();
    bind_routing();
    bind_samplers();
    bind_map();
}

} // namespace
