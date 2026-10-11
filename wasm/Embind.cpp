#include "libodr/Geometries/Arc.h"
#include "libodr/Geometries/Line.h"
#include "libodr/Geometries/ParamPoly3.h"
#include "libodr/Geometries/Spiral.h"
#include "libodr/OpenDriveMap.h"
#include "libodr/Sampler.h"

#include <emscripten/bind.h>
#include <magic_enum/magic_enum.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace
{
using namespace odr;
using namespace emscripten;

template<typename T>
struct IsValue : std::bool_constant<std::is_arithmetic_v<T> || std::is_enum_v<T> || std::is_same_v<T, std::string>>
{
};

template<typename T, std::size_t N>
struct IsValue<std::array<T, N>> : std::true_type
{
};

template<typename T>
struct IsValue<std::optional<T>> : IsValue<T>
{
};

// Model objects must be borrowed: copying an OdrNode deliberately drops its parent.
// A no-op deleter lets JS delete the handle without deleting its owner's storage.
template<typename T>
val borrow(T* value)
{
    return value ? val(std::shared_ptr<T>(value, [](T*) {})) : val::null();
}

template<typename T>
val view(T& value)
{
    if constexpr (IsValue<T>::value)
        return val(value);
    else
        return borrow(&value);
}

template<typename T>
val view(std::optional<T>& value)
{
    return value ? view(*value) : val::undefined();
}

template<typename T>
val view(std::unique_ptr<T>& value)
{
    return value ? view(*value) : val::null();
}

template<typename T>
struct Container
{
    static void bind(const std::string&) {}
};

template<typename T>
struct Container<std::vector<T>>
{
    static void bind(const std::string& name)
    {
        static const bool registered = [&]
        {
            using V = std::vector<T>;
            class_<V>(name.c_str())
                .template smart_ptr<std::shared_ptr<V>>((name + "Handle").c_str())
                .function("size", &V::size)
                .function(
                    "get", +[](V& values, std::size_t i) { return i < values.size() ? view(values[i]) : val::undefined(); });
            return true;
        }();
        (void)registered;
    }
};

template<typename M>
void bind_map(const std::string& name)
{
    static const bool registered = [&]
    {
        Container<typename M::mapped_type>::bind(name + "_value");
        class_<M>(name.c_str())
            .template smart_ptr<std::shared_ptr<M>>((name + "Handle").c_str())
            .function("size", &M::size)
            .function(
                "get",
                +[](M& values, const typename M::key_type& key)
                {
                    auto it = values.find(key);
                    return it == values.end() ? val::undefined() : view(it->second);
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
        return true;
    }();
    (void)registered;
}

template<typename K, typename V>
struct Container<std::map<K, V>>
{
    static void bind(const std::string& name)
    {
        bind_map<std::map<K, V>>(name);
    }
};

template<typename K, typename V>
struct Container<std::unordered_map<K, V>>
{
    static void bind(const std::string& name)
    {
        bind_map<std::unordered_map<K, V>>(name);
    }
};

template<typename S>
void bind_set(const std::string& name)
{
    static const bool registered = [&]
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
        return true;
    }();
    (void)registered;
}

template<typename T>
struct Container<std::unordered_set<T>>
{
    static void bind(const std::string& name)
    {
        bind_set<std::unordered_set<T>>(name);
    }
};

template<typename T>
using NodeBase = std::conditional_t<std::is_base_of_v<OdrNode, T> && !std::is_same_v<OdrNode, T>, base<OdrNode>, void>;

template<typename T, typename Base>
using BoundClass = std::conditional_t<std::is_void_v<Base>, class_<T>, class_<T, Base>>;

template<typename T, typename Base = NodeBase<T>>
struct Binding : BoundClass<T, Base>
{
    explicit Binding(const char* name) : BoundClass<T, Base>(name), name(name)
    {
        this->template smart_ptr<std::shared_ptr<T>>((this->name + "Handle").c_str());
    }

    template<auto Member>
    Binding& field(const char* field_name)
    {
        using F = std::remove_reference_t<decltype(std::declval<T>().*Member)>;
        Container<F>::bind(name + "_" + field_name);
        if constexpr (IsValue<F>::value)
            this->property(field_name, Member);
        else
            // Embind requires const property getters even for mutable borrowed fields.
            this->property(
                field_name, +[](const T& self) { return view(const_cast<T&>(self).*Member); });
        return *this;
    }

    std::string name;
};

// C++17 has no member reflection. Keep each member name in one place; Embind
// deduces its type, while magic_enum reflects enum values without duplicate lists.
#define FIELD(name) field<&T::name>(#name)
#define METHOD(name) function(#name, &T::name)

template<typename E>
void bind_enum(const char* name)
{
    enum_<E> binding(name);
    for (const auto& entry : magic_enum::enum_entries<E>())
        binding.value(std::string(entry.second).c_str(), entry.first);
    register_optional<E>();
}

template<typename A, std::size_t... I>
void bind_array(const char* name, std::index_sequence<I...>)
{
    value_array<A> binding(name);
    (binding.element(emscripten::index<I>()), ...);
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
    bind_enum<ParamPoly3::PRange>("PRange");
    {
        using T = CubicBounds;
        Binding<T> c("CubicBounds");
        c.FIELD(min).FIELD(max).FIELD(d1_min).FIELD(d1_max).FIELD(d1).FIELD(d2).FIELD(d3);
        c.constructor<>();
    }
    {
        using T = CubicPoly;
        Binding<T> c("CubicPoly");
        c.FIELD(a).FIELD(b).FIELD(c).FIELD(d).FIELD(s_origin);
        c.constructor<>().constructor<double, double, double, double>().constructor<double, double, double, double, double>();
        c.METHOD(evaluate).METHOD(derivative).METHOD(bounds).METHOD(add).METHOD(subtract).METHOD(negate).METHOD(is_zero).METHOD(set_zero);
    }
    {
        using T = CubicProfile;
        Binding<T> c("CubicProfile");
        c.FIELD(s_to_poly);
        c.constructor<>().METHOD(evaluate).METHOD(derivative).METHOD(get_poly).METHOD(negate).METHOD(add);
        register_optional<CubicPoly>();
    }
    {
        using T = CubicBezier2D;
        Binding<T> c("CubicBezier2D");
        c.FIELD(valid_length).FIELD(control_points).FIELD(arclen_t);
        c.constructor<std::array<Vec2D, 4>>().METHOD(evaluate).METHOD(derivative).METHOD(get_t).METHOD(get_subcurve).METHOD(get_polyline_t_samples);
        c.class_function("get_control_points", &T::get_control_points).class_function("get_coefficients", &T::get_coefficients);
    }
    {
        using T = RoadGeometry;
        Binding<T> c("RoadGeometry");
        c.FIELD(s).FIELD(x).FIELD(y).FIELD(hdg).FIELD(length);
        c.METHOD(get_xy).METHOD(derivative).METHOD(clone);
    }
    Binding<Line, base<RoadGeometry>>("Line").constructor<double, double, double, double, double>();
    {
        using T = Arc;
        Binding<T, base<RoadGeometry>> c("Arc");
        c.FIELD(curvature).constructor<double, double, double, double, double, double>();
    }
    {
        using T = Spiral;
        Binding<T, base<RoadGeometry>> c("Spiral");
        c.FIELD(curv_start).FIELD(curv_end).FIELD(spiral_s_start).FIELD(spiral_s_end).FIELD(c_dot);
        c.constructor<double, double, double, double, double, double, double>();
    }
    {
        using T = ParamPoly3;
        Binding<T, base<RoadGeometry>> c("ParamPoly3");
        c.FIELD(aU).FIELD(bU).FIELD(cU).FIELD(dU).FIELD(aV).FIELD(bV).FIELD(cV).FIELD(dV).FIELD(p_range).FIELD(cubic_bezier);
        c.constructor<double, double, double, double, double, double, double, double, double, double, double, double, double>();
        c.constructor<double, double, double, double, double, double, double, double, double, double, double, double, double, T::PRange>();
    }
    {
        using T = RefLine;
        Binding<T> c("RefLine");
        c.FIELD(length).FIELD(elevation_profile).FIELD(s_to_geometry);
        c.constructor<double>().METHOD(get_xyz).METHOD(derivative).METHOD(match);
        c.function(
            "get_geometry", +[](T& self, double s) { return borrow(self.get_geometry(s)); });
        c.function(
            "set_geometry", +[](T& self, const RoadGeometry& geometry) { self.s_to_geometry[geometry.s] = geometry.clone(); });
    }
}

void bind_mesh()
{
    using T = Mesh3D;
    Binding<T> c("Mesh3D");
    c.FIELD(vertices).FIELD(indices).FIELD(normals).FIELD(st_coordinates);
    c.constructor<>().METHOD(add_mesh).METHOD(get_obj);
}

void bind_road_marks()
{
    {
        using T = RoadMarkLine;
        Binding<T> c("RoadMarkLine");
        c.FIELD(s_offset).FIELD(t_offset).FIELD(length).FIELD(width).FIELD(space).FIELD(color).FIELD(rule);
        c.constructor<double, double, double>();
    }
    {
        using T = RoadMarkType;
        Binding<T> c("RoadMarkType");
        c.FIELD(name).FIELD(width).FIELD(lines);
        c.constructor<const std::string&>();
    }
    {
        using T = RoadMark;
        Binding<T> c("RoadMark");
        c.FIELD(s_offset).FIELD(type).FIELD(color).FIELD(width).FIELD(height).FIELD(weight).FIELD(material).FIELD(lane_change).FIELD(type_elem);
        c.constructor<double, const std::string&, const std::string&>();
        c.class_property("StandardWidth", &T::StandardWidth).class_property("BoldWidth", &T::BoldWidth);
    }
    {
        using T = SingleRoadMark;
        Binding<T> c("SingleRoadMark");
        c.FIELD(s_start).FIELD(s_end).FIELD(t_offset).FIELD(width).FIELD(type);
        c.constructor<double, double, double, double, const std::string&>();
    }
    Container<std::vector<SingleRoadMark>>::bind("RoadMarkVector");
}

void bind_objects()
{
    bind_enum<RoadObject::Orientation>("Orientation");
    bind_enum<RoadObjectCorner::Type>("RoadObjectCornerType");
    {
        using T = LaneValidity;
        Binding<T> c("LaneValidity");
        c.FIELD(from_lane).FIELD(to_lane).constructor<int, int>();
    }
    {
        using T = RoadObjectRepeat;
        Binding<T> c("RoadObjectRepeat");
        c.FIELD(s).FIELD(length).FIELD(distance).FIELD(t_start).FIELD(t_end).FIELD(height_start).FIELD(height_end);
        c.FIELD(z_offset_start).FIELD(z_offset_end).FIELD(width_start).FIELD(width_end);
        c.constructor<double, double, double>();
    }
    {
        using T = RoadObjectCorner;
        Binding<T> c("RoadObjectCorner");
        c.FIELD(pt).FIELD(height).FIELD(type).FIELD(id).constructor<Vec3D, double, T::Type>();
    }
    {
        using T = RoadObjectOutline;
        Binding<T> c("RoadObjectOutline");
        c.FIELD(id).FIELD(fill_type).FIELD(lane_type).FIELD(outer).FIELD(closed).FIELD(outline).constructor<>();
    }
    {
        using T = RoadObject;
        Binding<T> c("RoadObject");
        c.FIELD(id).FIELD(s).FIELD(t).FIELD(z_offset).FIELD(length).FIELD(valid_length).FIELD(width).FIELD(radius).FIELD(height);
        c.FIELD(hdg).FIELD(pitch).FIELD(roll).FIELD(type).FIELD(name).FIELD(subtype).FIELD(orientation).FIELD(is_dynamic);
        c.FIELD(repeats).FIELD(outlines).FIELD(lane_validities).constructor<const std::string&>();
        c.function(
            "get_mesh", +[](const T& self, double eps) { return self.get_mesh(eps); });
        c.function(
            "get_mesh", +[](const T& self, double eps, double height, double z, bool bounds) { return self.get_mesh(eps, height, z, bounds); });
        c.function(
            "get_mesh_with_warnings",
            +[](const T& self, double eps, double height, double z, bool bounds)
            { return mesh_with_warnings([&](auto& warnings) { return self.get_mesh(eps, height, z, bounds, &warnings); }); });
        c.class_function("get_cylinder", &T::get_cylinder).class_function("get_cube", &T::get_cube);
    }
    {
        using T = RoadSignal;
        Binding<T> c("RoadSignal");
        c.FIELD(id).FIELD(road_id).FIELD(s).FIELD(t).FIELD(z_offset).FIELD(is_dynamic).FIELD(type).FIELD(subtype).FIELD(orientation);
        c.FIELD(value).FIELD(height).FIELD(width).FIELD(hOffset).FIELD(pitch).FIELD(roll).FIELD(name).FIELD(unit).FIELD(text).FIELD(country);
        c.FIELD(lane_validities);
        c.constructor<const std::string&,
                      const std::string&,
                      double,
                      double,
                      double,
                      bool,
                      const std::string&,
                      const std::string&,
                      RoadObject::Orientation>();
        c.function(
            "get_mesh", +[](const T& self) { return self.get_mesh(); });
        c.function(
            "get_mesh", +[](const T& self, bool bounds) { return self.get_mesh(bounds); });
        c.function(
            "get_mesh_with_warnings",
            +[](const T& self, bool bounds) { return mesh_with_warnings([&](auto& warnings) { return self.get_mesh(bounds, &warnings); }); });
        c.class_function("get_box", &T::get_box);
        c.class_property("Thickness", &T::Thickness)
            .class_property("DefaultWidth", &T::DefaultWidth)
            .class_property("DefaultHeight", &T::DefaultHeight);
    }
}

void bind_roads()
{
    bind_enum<Crossfall::Side>("CrossfallSide");
    bind_enum<RoadLink::Type>("RoadLinkType");
    bind_enum<RoadLink::ContactPoint>("RoadLinkContactPoint");
    bind_enum<Road::TrafficRule>("TrafficRule");
    {
        using T = Crossfall::Record;
        Binding<T> c("CrossfallRecord");
        c.FIELD(poly).FIELD(side).constructor<>();
    }
    {
        using T = Crossfall;
        Binding<T> c("Crossfall");
        c.FIELD(records).constructor<>().METHOD(get);
    }
    {
        using T = RoadLink;
        Binding<T> c("RoadLink");
        c.FIELD(id).FIELD(type).FIELD(contact_point).constructor<const std::string&, const std::string&, std::optional<T::ContactPoint>>();
    }
    {
        using T = Speed;
        Binding<T> c("Speed");
        c.FIELD(max).FIELD(unit).constructor<const std::string&, const std::string&>();
    }
    {
        using T = HeightOffset;
        Binding<T> c("HeightOffset");
        c.FIELD(s_offset).FIELD(inner).FIELD(outer).constructor<double, double, double>();
    }
    {
        using T = Lane;
        Binding<T> c("Lane");
        c.FIELD(id).FIELD(type).FIELD(level).FIELD(predecessor).FIELD(successor).FIELD(lane_width).FIELD(outer_border);
        c.FIELD(s_to_height_offset).FIELD(s_to_road_mark).constructor<int>().constructor<int, std::optional<std::string>, std::optional<bool>>();
        c.METHOD(get_roadmarks);
        c.function(
            "get_surface_pt", +[](const T& self, double s, double t) { return self.get_surface_pt(s, t); });
        c.function(
            "get_surface_pt", +[](const T& self, double s, double t, bool extrapolate) { return self.get_surface_pt(s, t, nullptr, extrapolate); });
        c.function(
            "get_mesh", +[](const T& self, double eps) { return self.get_mesh(eps); });
        c.function(
            "get_mesh_with_outline",
            +[](const T& self, double eps)
            {
                std::vector<uint32_t> outline;
                val                   result = val::object();
                result.set("mesh", val(self.get_mesh(eps, &outline)));
                result.set("outline_indices", val::array(outline));
                return result;
            });
        c.function(
            "get_roadmark_mesh", +[](const T& self, const SingleRoadMark& mark, double eps) { return self.get_roadmark_mesh(mark, eps); });
        c.function("get_roadmark_mesh", &T::get_roadmark_mesh);
    }
    {
        using T = LaneSection;
        Binding<T> c("LaneSection");
        c.FIELD(s).FIELD(id_to_lane).constructor<double>().METHOD(get_end).METHOD(get_length).METHOD(get_lane_id);
    }
    {
        using T = Road;
        Binding<T> c("Road");
        c.FIELD(id).FIELD(length).FIELD(junction).FIELD(traffic_rule).FIELD(name).FIELD(predecessor).FIELD(successor);
        c.FIELD(lane_offset).FIELD(superelevation).FIELD(crossfall).FIELD(ref_line).FIELD(s_to_lane_section).FIELD(s_to_type).FIELD(s_to_speed);
        c.FIELD(id_to_object).FIELD(id_to_signal).constructor<const std::string&, double, const std::string&>();
        c.constructor<const std::string&, double, const std::string&, std::optional<T::TrafficRule>, std::optional<std::string>>();
        c.function(
            "get_lane_section", +[](T& self, double s) { return borrow(self.get_lane_section(s)); });
        c.function(
            "get_xyz", +[](const T& self, double s, double t, double h) { return self.get_xyz(s, t, h); });
        c.function(
            "get_xyz",
            +[](const T& self, double s, double t, double h, bool extrapolate)
            { return self.get_xyz(s, t, h, nullptr, nullptr, nullptr, extrapolate); });
        c.function(
            "get_frame",
            +[](const T& self, double s, double t, double h)
            {
                Vec3D es, et, eh;
                val   result = val::object();
                result.set("xyz", val(self.get_xyz(s, t, h, &es, &et, &eh)));
                result.set("e_s", val(es));
                result.set("e_t", val(et));
                result.set("e_h", val(eh));
                return result;
            });
        c.function(
            "get_surface_pt", +[](const T& self, double s, double t) { return self.get_surface_pt(s, t); });
        c.function(
            "get_surface_pt", +[](const T& self, double s, double t, bool extrapolate) { return self.get_surface_pt(s, t, nullptr, extrapolate); });
    }
}

void bind_junctions()
{
    bind_enum<JunctionConnection::ContactPoint>("JunctionContactPoint");
    {
        using T = JunctionLaneLink;
        Binding<T> c("JunctionLaneLink");
        c.FIELD(from).FIELD(to).constructor<int, int>();
    }
    {
        using T = JunctionConnection;
        Binding<T> c("JunctionConnection");
        c.FIELD(id).FIELD(incoming_road).FIELD(connecting_road).FIELD(contact_point).FIELD(lane_links);
        c.constructor<const std::string&, const std::string&, const std::string&, const std::string&>();
    }
    {
        using T = JunctionPriority;
        Binding<T> c("JunctionPriority");
        c.FIELD(high).FIELD(low).constructor<const std::string&, const std::string&>();
    }
    {
        using T = JunctionController;
        Binding<T> c("JunctionController");
        c.FIELD(id).FIELD(type).FIELD(sequence).constructor<const std::string&>();
    }
    {
        using T = Junction;
        Binding<T> c("Junction");
        c.FIELD(id).FIELD(name).FIELD(id_to_connection).FIELD(id_to_controller).FIELD(priorities).constructor<const std::string&>();
    }
}

void bind_routing()
{
    {
        using T = LaneKey;
        Binding<T> c("LaneKey");
        c.FIELD(road_id).FIELD(lane_section_s).FIELD(lane_id).constructor<const std::string&, double, int>().METHOD(to_string);
    }
    {
        using T = WeightedLaneKey;
        Binding<T, base<LaneKey>> c("WeightedLaneKey");
        c.FIELD(weight).constructor<const LaneKey&, double>().constructor<const std::string&, double, int, double>();
    }
    {
        using T = RoutingGraphEdge;
        Binding<T> c("RoutingGraphEdge");
        c.FIELD(from).FIELD(to).FIELD(weight).constructor<LaneKey, LaneKey, double>();
    }
    Container<std::vector<LaneKey>>::bind("RoutingPath");
    {
        using T = RoutingGraph;
        Binding<T> c("RoutingGraph");
        c.FIELD(edges).FIELD(lane_key_to_successors).FIELD(lane_key_to_predecessors);
        c.constructor<>().METHOD(add_edge).METHOD(get_lane_successors).METHOD(get_lane_predecessors).METHOD(shortest_path);
    }
}

void bind_samplers()
{
    {
        using T = DerivativeBounds;
        Binding<T> c("DerivativeBounds");
        c.FIELD(value).FIELD(d1).FIELD(d2).constructor<>();
    }
    {
        using T = GeometryBounds;
        Binding<T> c("GeometryBounds");
        c.FIELD(position_d1).FIELD(position_d2).FIELD(speed_lower).FIELD(tangent_d1).FIELD(tangent_d2);
        c.FIELD(lateral_d1).FIELD(lateral_d2).FIELD(arclength_error).FIELD(frame_mapping_error).constructor<>();
    }
    {
        using T = EdgePoints;
        Binding<T> c("EdgePoints");
        c.FIELD(inner).FIELD(outer).constructor<>();
    }
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
    {
        using T = OpenDriveMapHeader;
        Binding<T> c("OpenDriveMapHeader");
        c.FIELD(rev_major).FIELD(rev_minor).FIELD(north).FIELD(east).FIELD(south).FIELD(west);
        c.FIELD(date).FIELD(name).FIELD(vendor).FIELD(version).FIELD(proj).constructor<>();
    }
    {
        using T = OpenDriveMap;
        Binding<T> c("OpenDriveMap");
        c.FIELD(header).FIELD(id_to_road).FIELD(id_to_junction).constructor<>().METHOD(reset).METHOD(set_node_parents);
        c.function("load", &load_xml);
        c.function(
            "load", +[](T& self, const std::string& xml) { return load_xml(self, xml, val::object()); });
        c.function(
            "get_mesh", +[](const T& self, double eps) { return self.get_mesh(eps); });
        c.function(
            "get_mesh", +[](const T& self, double eps, bool bounds) { return self.get_mesh(eps, bounds); });
        c.function(
            "get_mesh_with_warnings",
            +[](const T& self, double eps, bool bounds)
            { return mesh_with_warnings([&](auto& warnings) { return self.get_mesh(eps, bounds, &warnings); }); });
        c.function(
            "get_routing_graph", +[](const T& self) { return self.get_routing_graph(); });
        c.function(
            "get_routing_graph_with_diagnostics",
            +[](const T& self)
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
}

EMSCRIPTEN_BINDINGS(libOpenDRIVE)
{
    register_optional<bool>();
    register_optional<int>();
    register_optional<uint32_t>();
    register_optional<double>();
    register_optional<std::string>();
    bind_array<Vec2D>("Vec2D", std::make_index_sequence<2>{});
    bind_array<Vec3D>("Vec3D", std::make_index_sequence<3>{});
    bind_array<std::array<Vec2D, 4>>("BezierControlPoints", std::make_index_sequence<4>{});
    bind_set<std::set<double>>("DoubleSet");
    {
        using T = OdrNode;
        Binding<T> c("OdrNode");
        c.FIELD(xml_offset);
        c.function(
            "parent", +[](T& self) { return borrow(self.parent()); });
        c.function("set_parent", &T::set_parent, allow_raw_pointers());
    }
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

#undef FIELD
#undef METHOD
} // namespace
