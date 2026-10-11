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

// Reference properties and model collection elements belong to their C++ owner.
// Do not delete them from JS or use them after owner destruction/reset/mutation.
// Constructed objects and results returned by value must be deleted from JS.
// Const member pointers select read-only properties without copying their contents.
// Optional object getters cast away Embind's required const getter parameter;
// their backing model is mutable, just like the member properties.

// Embind's standard container getters copy elements, dropping OdrNode parents.
template<typename T>
void bind_reference_vector(const char* name)
{
    using V = std::vector<T>;
    class_<V>(name)
        .function("size", &V::size)
        .function(
            "get", +[](V& values, std::size_t i) -> T* { return i < values.size() ? &values[i] : nullptr; }, return_value_policy::reference());
}

template<typename M>
void bind_reference_map(const char* name)
{
    class_<M>(name)
        .function("size", &M::size)
        .function(
            "get",
            +[](M& values, const typename M::key_type& key) -> typename M::mapped_type*
            {
                const auto it = values.find(key);
                return it == values.end() ? nullptr : &it->second;
            },
            return_value_policy::reference())
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

// Embind has no built-in set binding. Return copies so JS cannot mutate set keys.
template<typename S>
void bind_set(const char* name)
{
    class_<S>(name)
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
    bind_reference_map<std::map<double, CubicPoly>>("CubicProfile_s_to_poly");
    register_map<double, double>("CubicBezier2D_arclen_t");
    using GeometryMap = std::map<double, std::unique_ptr<RoadGeometry>>;
    class_<GeometryMap>("RefLine_s_to_geometry")
        .function("size", &GeometryMap::size)
        .function(
            "get",
            +[](GeometryMap& values, double s) -> RoadGeometry*
            {
                const auto it = values.find(s);
                return it == values.end() ? nullptr : it->second.get();
            },
            return_value_policy::reference())
        .function(
            "keys",
            +[](const GeometryMap& values)
            {
                val keys = val::array();
                for (const auto& entry : values)
                    keys.call<void>("push", entry.first);
                return keys;
            });

    enum_<ParamPoly3::PRange>("PRange").value("Normalized", ParamPoly3::PRange::Normalized).value("ArcLength", ParamPoly3::PRange::ArcLength);
    register_optional<ParamPoly3::PRange>();

    class_<CubicBounds>("CubicBounds")
        .property("min", &CubicBounds::min)
        .property("max", &CubicBounds::max)
        .property("d1_min", &CubicBounds::d1_min)
        .property("d1_max", &CubicBounds::d1_max)
        .property("d1", &CubicBounds::d1)
        .property("d2", &CubicBounds::d2)
        .property("d3", &CubicBounds::d3)
        .constructor<>();

    class_<CubicPoly>("CubicPoly")
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
        .property(
            "s_to_poly", static_cast<const std::map<double, CubicPoly> CubicProfile::*>(&CubicProfile::s_to_poly), return_value_policy::reference())
        .constructor<>()
        .function("evaluate", &CubicProfile::evaluate)
        .function("derivative", &CubicProfile::derivative)
        .function("get_poly", &CubicProfile::get_poly)
        .function("negate", &CubicProfile::negate)
        .function("add", &CubicProfile::add);
    register_optional<CubicPoly>();

    class_<CubicBezier2D>("CubicBezier2D")
        .property("valid_length", &CubicBezier2D::valid_length)
        .property("control_points", &CubicBezier2D::control_points)
        .property(
            "arclen_t", static_cast<const std::map<double, double> CubicBezier2D::*>(&CubicBezier2D::arclen_t), return_value_policy::reference())
        .constructor<std::array<Vec2D, 4>>()
        .function("evaluate", &CubicBezier2D::evaluate)
        .function("derivative", &CubicBezier2D::derivative)
        .function("get_t", &CubicBezier2D::get_t)
        .function("get_subcurve", &CubicBezier2D::get_subcurve)
        .function("get_polyline_t_samples", &CubicBezier2D::get_polyline_t_samples)
        .class_function("get_control_points", &CubicBezier2D::get_control_points)
        .class_function("get_coefficients", &CubicBezier2D::get_coefficients);

    class_<RoadGeometry>("RoadGeometry")
        .property("s", &RoadGeometry::s)
        .property("x", &RoadGeometry::x)
        .property("y", &RoadGeometry::y)
        .property("hdg", &RoadGeometry::hdg)
        .property("length", &RoadGeometry::length)
        .function("get_xy", &RoadGeometry::get_xy)
        .function("derivative", &RoadGeometry::derivative)
        .function("clone", &RoadGeometry::clone);

    class_<Line, base<RoadGeometry>>("Line").constructor<double, double, double, double, double>();

    class_<Arc, base<RoadGeometry>>("Arc").property("curvature", &Arc::curvature).constructor<double, double, double, double, double, double>();

    class_<Spiral, base<RoadGeometry>>("Spiral")
        .property("curv_start", &Spiral::curv_start)
        .property("curv_end", &Spiral::curv_end)
        .property("spiral_s_start", &Spiral::spiral_s_start)
        .property("spiral_s_end", &Spiral::spiral_s_end)
        .property("c_dot", &Spiral::c_dot)
        .constructor<double, double, double, double, double, double, double>();

    class_<ParamPoly3, base<RoadGeometry>>("ParamPoly3")
        .property("aU", &ParamPoly3::aU)
        .property("bU", &ParamPoly3::bU)
        .property("cU", &ParamPoly3::cU)
        .property("dU", &ParamPoly3::dU)
        .property("aV", &ParamPoly3::aV)
        .property("bV", &ParamPoly3::bV)
        .property("cV", &ParamPoly3::cV)
        .property("dV", &ParamPoly3::dV)
        .property("p_range", &ParamPoly3::p_range)
        .property("cubic_bezier", static_cast<const CubicBezier2D ParamPoly3::*>(&ParamPoly3::cubic_bezier), return_value_policy::reference())
        .constructor<double, double, double, double, double, double, double, double, double, double, double, double, double>()
        .constructor<double, double, double, double, double, double, double, double, double, double, double, double, double, ParamPoly3::PRange>();

    class_<RefLine>("RefLine")
        .property("length", &RefLine::length)
        .property("elevation_profile", static_cast<const CubicProfile RefLine::*>(&RefLine::elevation_profile), return_value_policy::reference())
        .property("s_to_geometry",
                  static_cast<const std::map<double, std::unique_ptr<RoadGeometry>> RefLine::*>(&RefLine::s_to_geometry),
                  return_value_policy::reference())
        .constructor<double>()
        .function("get_xyz", &RefLine::get_xyz)
        .function("derivative", &RefLine::derivative)
        .function("match", &RefLine::match)
        .function("get_geometry", select_overload<RoadGeometry*(double)>(&RefLine::get_geometry), return_value_policy::reference())
        .function(
            "set_geometry", +[](RefLine& self, const RoadGeometry& geometry) { self.s_to_geometry[geometry.s] = geometry.clone(); });
}

void bind_mesh()
{
    register_vector<Vec3D>("Mesh3D_vertices");
    register_vector<uint32_t>("Mesh3D_indices");
    register_vector<Vec2D>("Mesh3D_st_coordinates");

    class_<Mesh3D>("Mesh3D")
        .property("vertices", static_cast<const std::vector<Vec3D> Mesh3D::*>(&Mesh3D::vertices), return_value_policy::reference())
        .property("indices", static_cast<const std::vector<uint32_t> Mesh3D::*>(&Mesh3D::indices), return_value_policy::reference())
        .property("normals", static_cast<const std::vector<Vec3D> Mesh3D::*>(&Mesh3D::normals), return_value_policy::reference())
        .property("st_coordinates", static_cast<const std::vector<Vec2D> Mesh3D::*>(&Mesh3D::st_coordinates), return_value_policy::reference())
        .constructor<>()
        .function("add_mesh", &Mesh3D::add_mesh)
        .function("get_obj", &Mesh3D::get_obj);
}

void bind_road_marks()
{
    bind_reference_vector<RoadMarkLine>("RoadMarkType_lines");

    class_<RoadMarkLine, base<OdrNode>>("RoadMarkLine")
        .property("s_offset", &RoadMarkLine::s_offset)
        .property("t_offset", &RoadMarkLine::t_offset)
        .property("length", &RoadMarkLine::length)
        .property("width", &RoadMarkLine::width)
        .property("space", &RoadMarkLine::space)
        .property("color", &RoadMarkLine::color)
        .property("rule", &RoadMarkLine::rule)
        .constructor<double, double, double>();

    class_<RoadMarkType, base<OdrNode>>("RoadMarkType")
        .property("name", &RoadMarkType::name)
        .property("width", &RoadMarkType::width)
        .property("lines", static_cast<const std::vector<RoadMarkLine> RoadMarkType::*>(&RoadMarkType::lines), return_value_policy::reference())
        .constructor<const std::string&>();

    class_<RoadMark, base<OdrNode>>("RoadMark")
        .property("s_offset", &RoadMark::s_offset)
        .property("type", &RoadMark::type)
        .property("color", &RoadMark::color)
        .property("width", &RoadMark::width)
        .property("height", &RoadMark::height)
        .property("weight", &RoadMark::weight)
        .property("material", &RoadMark::material)
        .property("lane_change", &RoadMark::lane_change)
        .property(
            "type_elem",
            +[](const RoadMark& self)
            { return self.type_elem ? val(const_cast<RoadMarkType*>(&*self.type_elem), return_value_policy::reference()) : val::undefined(); })
        .constructor<double, const std::string&, const std::string&>()
        .class_property("StandardWidth", &RoadMark::StandardWidth)
        .class_property("BoldWidth", &RoadMark::BoldWidth);

    class_<SingleRoadMark>("SingleRoadMark")
        .property("s_start", &SingleRoadMark::s_start)
        .property("s_end", &SingleRoadMark::s_end)
        .property("t_offset", &SingleRoadMark::t_offset)
        .property("width", &SingleRoadMark::width)
        .property("type", &SingleRoadMark::type)
        .constructor<double, double, double, double, const std::string&>();
    register_vector<SingleRoadMark>("RoadMarkVector");
}

void bind_objects()
{
    bind_reference_vector<RoadObjectCorner>("RoadObjectOutline_outline");
    bind_reference_vector<RoadObjectRepeat>("RoadObject_repeats");
    bind_reference_vector<RoadObjectOutline>("RoadObject_outlines");
    bind_reference_vector<LaneValidity>("RoadObject_lane_validities");

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
        .property("from_lane", &LaneValidity::from_lane)
        .property("to_lane", &LaneValidity::to_lane)
        .constructor<int, int>();

    class_<RoadObjectRepeat, base<OdrNode>>("RoadObjectRepeat")
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
        .property("pt", &RoadObjectCorner::pt)
        .property("height", &RoadObjectCorner::height)
        .property("type", &RoadObjectCorner::type)
        .property("id", &RoadObjectCorner::id)
        .constructor<Vec3D, double, RoadObjectCorner::Type>();

    class_<RoadObjectOutline, base<OdrNode>>("RoadObjectOutline")
        .property("id", &RoadObjectOutline::id)
        .property("fill_type", &RoadObjectOutline::fill_type)
        .property("lane_type", &RoadObjectOutline::lane_type)
        .property("outer", &RoadObjectOutline::outer)
        .property("closed", &RoadObjectOutline::closed)
        .property("outline",
                  static_cast<const std::vector<RoadObjectCorner> RoadObjectOutline::*>(&RoadObjectOutline::outline),
                  return_value_policy::reference())
        .constructor<>();

    class_<RoadObject, base<OdrNode>>("RoadObject")
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
        .property("repeats", static_cast<const std::vector<RoadObjectRepeat> RoadObject::*>(&RoadObject::repeats), return_value_policy::reference())
        .property(
            "outlines", static_cast<const std::vector<RoadObjectOutline> RoadObject::*>(&RoadObject::outlines), return_value_policy::reference())
        .property("lane_validities",
                  static_cast<const std::vector<LaneValidity> RoadObject::*>(&RoadObject::lane_validities),
                  return_value_policy::reference())
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
        .property("lane_validities",
                  static_cast<const std::vector<LaneValidity> RoadSignal::*>(&RoadSignal::lane_validities),
                  return_value_policy::reference())
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
    bind_reference_map<std::map<double, Crossfall::Record>>("Crossfall_records");
    bind_reference_map<std::map<double, HeightOffset>>("Lane_s_to_height_offset");
    bind_reference_map<std::map<double, RoadMark>>("Lane_s_to_road_mark");
    bind_reference_map<std::map<int, Lane>>("LaneSection_id_to_lane");
    bind_reference_map<std::map<double, LaneSection>>("Road_s_to_lane_section");
    register_map<double, std::string>("Road_s_to_type");
    bind_reference_map<std::map<double, Speed>>("Road_s_to_speed");
    bind_reference_map<std::map<std::string, RoadObject>>("Road_id_to_object");
    bind_reference_map<std::map<std::string, RoadSignal>>("Road_id_to_signal");

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
        .property("poly", static_cast<const CubicPoly Crossfall::Record::*>(&Crossfall::Record::poly), return_value_policy::reference())
        .property("side", &Crossfall::Record::side)
        .constructor<>();

    class_<Crossfall>("Crossfall")
        .property(
            "records", static_cast<const std::map<double, Crossfall::Record> Crossfall::*>(&Crossfall::records), return_value_policy::reference())
        .constructor<>()
        .function("get", &Crossfall::get);

    class_<RoadLink, base<OdrNode>>("RoadLink")
        .property("id", &RoadLink::id)
        .property("type", &RoadLink::type)
        .property("contact_point", &RoadLink::contact_point)
        .constructor<const std::string&, const std::string&, std::optional<RoadLink::ContactPoint>>();

    class_<Speed, base<OdrNode>>("Speed")
        .property("max", &Speed::max)
        .property("unit", &Speed::unit)
        .constructor<const std::string&, const std::string&>();

    class_<HeightOffset, base<OdrNode>>("HeightOffset")
        .property("s_offset", &HeightOffset::s_offset)
        .property("inner", &HeightOffset::inner)
        .property("outer", &HeightOffset::outer)
        .constructor<double, double, double>();

    class_<Lane, base<OdrNode>>("Lane")
        .property("id", &Lane::id)
        .property("type", &Lane::type)
        .property("level", &Lane::level)
        .property("predecessor", &Lane::predecessor)
        .property("successor", &Lane::successor)
        .property("lane_width", static_cast<const CubicProfile Lane::*>(&Lane::lane_width), return_value_policy::reference())
        .property("outer_border", static_cast<const CubicProfile Lane::*>(&Lane::outer_border), return_value_policy::reference())
        .property("s_to_height_offset",
                  static_cast<const std::map<double, HeightOffset> Lane::*>(&Lane::s_to_height_offset),
                  return_value_policy::reference())
        .property("s_to_road_mark", static_cast<const std::map<double, RoadMark> Lane::*>(&Lane::s_to_road_mark), return_value_policy::reference())
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
        .property("s", &LaneSection::s)
        .property("id_to_lane", static_cast<const std::map<int, Lane> LaneSection::*>(&LaneSection::id_to_lane), return_value_policy::reference())
        .constructor<double>()
        .function("get_end", &LaneSection::get_end)
        .function("get_length", &LaneSection::get_length)
        .function("get_lane_id", &LaneSection::get_lane_id);

    class_<Road, base<OdrNode>>("Road")
        .property("id", &Road::id)
        .property("length", &Road::length)
        .property("junction", &Road::junction)
        .property("traffic_rule", &Road::traffic_rule)
        .property("name", &Road::name)
        .property(
            "predecessor",
            +[](const Road& self)
            { return self.predecessor ? val(const_cast<RoadLink*>(&*self.predecessor), return_value_policy::reference()) : val::undefined(); })
        .property(
            "successor",
            +[](const Road& self)
            { return self.successor ? val(const_cast<RoadLink*>(&*self.successor), return_value_policy::reference()) : val::undefined(); })
        .property("lane_offset", static_cast<const CubicProfile Road::*>(&Road::lane_offset), return_value_policy::reference())
        .property("superelevation", static_cast<const CubicProfile Road::*>(&Road::superelevation), return_value_policy::reference())
        .property("crossfall", static_cast<const Crossfall Road::*>(&Road::crossfall), return_value_policy::reference())
        .property("ref_line", static_cast<const RefLine Road::*>(&Road::ref_line), return_value_policy::reference())
        .property(
            "s_to_lane_section", static_cast<const std::map<double, LaneSection> Road::*>(&Road::s_to_lane_section), return_value_policy::reference())
        .property("s_to_type", static_cast<const std::map<double, std::string> Road::*>(&Road::s_to_type), return_value_policy::reference())
        .property("s_to_speed", static_cast<const std::map<double, Speed> Road::*>(&Road::s_to_speed), return_value_policy::reference())
        .property("id_to_object", static_cast<const std::map<std::string, RoadObject> Road::*>(&Road::id_to_object), return_value_policy::reference())
        .property("id_to_signal", static_cast<const std::map<std::string, RoadSignal> Road::*>(&Road::id_to_signal), return_value_policy::reference())
        .constructor<const std::string&, double, const std::string&>()
        .constructor<const std::string&, double, const std::string&, std::optional<Road::TrafficRule>, std::optional<std::string>>()
        .function("get_lane_section", select_overload<LaneSection*(double)>(&Road::get_lane_section), return_value_policy::reference())
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
    bind_reference_vector<JunctionLaneLink>("JunctionConnection_lane_links");
    bind_reference_map<std::map<std::string, JunctionConnection>>("Junction_id_to_connection");
    bind_reference_map<std::map<std::string, JunctionController>>("Junction_id_to_controller");
    bind_reference_vector<JunctionPriority>("Junction_priorities");

    enum_<JunctionConnection::ContactPoint>("JunctionContactPoint")
        .value("None", JunctionConnection::ContactPoint::None)
        .value("Start", JunctionConnection::ContactPoint::Start)
        .value("End", JunctionConnection::ContactPoint::End);
    register_optional<JunctionConnection::ContactPoint>();

    class_<JunctionLaneLink, base<OdrNode>>("JunctionLaneLink")
        .property("from", &JunctionLaneLink::from)
        .property("to", &JunctionLaneLink::to)
        .constructor<int, int>();

    class_<JunctionConnection, base<OdrNode>>("JunctionConnection")
        .property("id", &JunctionConnection::id)
        .property("incoming_road", &JunctionConnection::incoming_road)
        .property("connecting_road", &JunctionConnection::connecting_road)
        .property("contact_point", &JunctionConnection::contact_point)
        .property("lane_links",
                  static_cast<const std::vector<JunctionLaneLink> JunctionConnection::*>(&JunctionConnection::lane_links),
                  return_value_policy::reference())
        .constructor<const std::string&, const std::string&, const std::string&, const std::string&>();

    class_<JunctionPriority, base<OdrNode>>("JunctionPriority")
        .property("high", &JunctionPriority::high)
        .property("low", &JunctionPriority::low)
        .constructor<const std::string&, const std::string&>();

    class_<JunctionController, base<OdrNode>>("JunctionController")
        .property("id", &JunctionController::id)
        .property("type", &JunctionController::type)
        .property("sequence", &JunctionController::sequence)
        .constructor<const std::string&>();

    class_<Junction, base<OdrNode>>("Junction")
        .property("id", &Junction::id)
        .property("name", &Junction::name)
        .property("id_to_connection",
                  static_cast<const std::map<std::string, JunctionConnection> Junction::*>(&Junction::id_to_connection),
                  return_value_policy::reference())
        .property("id_to_controller",
                  static_cast<const std::map<std::string, JunctionController> Junction::*>(&Junction::id_to_controller),
                  return_value_policy::reference())
        .property("priorities", static_cast<const std::vector<JunctionPriority> Junction::*>(&Junction::priorities), return_value_policy::reference())
        .constructor<const std::string&>();
}

void bind_routing()
{
    bind_set<std::unordered_set<RoutingGraphEdge>>("RoutingGraph_edges");
    bind_set<std::unordered_set<WeightedLaneKey>>("RoutingGraph_lane_key_to_successors_value");
    bind_reference_map<std::unordered_map<LaneKey, std::unordered_set<WeightedLaneKey>>>("RoutingGraph_lane_key_to_successors");

    class_<LaneKey>("LaneKey")
        .property("road_id", &LaneKey::road_id)
        .property("lane_section_s", &LaneKey::lane_section_s)
        .property("lane_id", &LaneKey::lane_id)
        .constructor<const std::string&, double, int>()
        .function("to_string", &LaneKey::to_string);

    class_<WeightedLaneKey, base<LaneKey>>("WeightedLaneKey")
        .property("weight", &WeightedLaneKey::weight)
        .constructor<const LaneKey&, double>()
        .constructor<const std::string&, double, int, double>();

    class_<RoutingGraphEdge>("RoutingGraphEdge")
        .property("from", static_cast<const LaneKey RoutingGraphEdge::*>(&RoutingGraphEdge::from), return_value_policy::reference())
        .property("to", static_cast<const LaneKey RoutingGraphEdge::*>(&RoutingGraphEdge::to), return_value_policy::reference())
        .property("weight", &RoutingGraphEdge::weight)
        .constructor<LaneKey, LaneKey, double>();
    register_vector<LaneKey>("RoutingPath");

    class_<RoutingGraph>("RoutingGraph")
        .property(
            "edges", static_cast<const std::unordered_set<RoutingGraphEdge> RoutingGraph::*>(&RoutingGraph::edges), return_value_policy::reference())
        .property("lane_key_to_successors",
                  static_cast<const std::unordered_map<LaneKey, std::unordered_set<WeightedLaneKey>> RoutingGraph::*>(
                      &RoutingGraph::lane_key_to_successors),
                  return_value_policy::reference())
        .property("lane_key_to_predecessors",
                  static_cast<const std::unordered_map<LaneKey, std::unordered_set<WeightedLaneKey>> RoutingGraph::*>(
                      &RoutingGraph::lane_key_to_predecessors),
                  return_value_policy::reference())
        .constructor<>()
        .function("add_edge", &RoutingGraph::add_edge)
        .function("get_lane_successors", &RoutingGraph::get_lane_successors)
        .function("get_lane_predecessors", &RoutingGraph::get_lane_predecessors)
        .function("shortest_path", &RoutingGraph::shortest_path);
}

void bind_samplers()
{
    class_<DerivativeBounds>("DerivativeBounds")
        .property("value", &DerivativeBounds::value)
        .property("d1", &DerivativeBounds::d1)
        .property("d2", &DerivativeBounds::d2)
        .constructor<>();

    class_<GeometryBounds>("GeometryBounds")
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

    class_<EdgePoints>("EdgePoints").property("inner", &EdgePoints::inner).property("outer", &EdgePoints::outer).constructor<>();

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
    bind_reference_map<std::map<std::string, Road>>("OpenDriveMap_id_to_road");
    bind_reference_map<std::map<std::string, Junction>>("OpenDriveMap_id_to_junction");

    class_<OpenDriveMapHeader, base<OdrNode>>("OpenDriveMapHeader")
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
        .property("header", static_cast<const OpenDriveMapHeader OpenDriveMap::*>(&OpenDriveMap::header), return_value_policy::reference())
        .property(
            "id_to_road", static_cast<const std::map<std::string, Road> OpenDriveMap::*>(&OpenDriveMap::id_to_road), return_value_policy::reference())
        .property("id_to_junction",
                  static_cast<const std::map<std::string, Junction> OpenDriveMap::*>(&OpenDriveMap::id_to_junction),
                  return_value_policy::reference())
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
    register_vector<double>("DoubleVector");
    bind_set<std::set<double>>("DoubleSet");

    class_<OdrNode>("OdrNode")
        .property("xml_offset", &OdrNode::xml_offset)
        .function("parent", select_overload<OdrNode*()>(&OdrNode::parent), return_value_policy::reference())
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
