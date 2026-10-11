# JavaScript / WebAssembly

The optional `OpenDriveWasm` CMake target links the existing `OpenDrive` library
with `Embind.cpp`. Native builds do not compile the bindings. CI uses Emscripten
6.0.6 and Node.js 24 and uploads both output files. Install with
`cmake --install build/wasm --prefix install`; the module goes into
`install/share/OpenDrive/wasm`.

## Loading a map

Keep `libOpenDRIVE.mjs` and `libOpenDRIVE.wasm` together and serve them over HTTP
for browsers. The same ES module works in Node.js and module workers. Await the
factory before accessing any classes. If the files are hosted separately, pass
`locateFile: name => new URL(name, assetBase).href` to the factory.

```js
import createOpenDrive from './libOpenDRIVE.mjs';

const odr = await createOpenDrive();
const xml = await (await fetch('./map.xodr')).text();
// Node.js: const xml = await readFile('map.xodr', 'utf8');
const map = new odr.OpenDriveMap();
try {
    const { errors } = map.load(xml);
    if (errors.length) console.warn(errors);

    const roads = map.id_to_road;
    try {
        for (const id of roads.keys()) {
            const road = roads.get(id);
            try {
                console.log(id, road.length, road.get_xyz(0, 0, 0));
            } finally {
                road.delete();
            }
        }
    } finally {
        roads.delete();
    }

    const mesh = map.get_mesh(0.1);
    try {
        console.log(mesh.get_obj());
    } finally {
        mesh.delete();
    }
} finally {
    map.delete();
}
```

`load(xml, options = {})` accepts the C++ loader's named boolean options:
`with_road_objects`, `with_lateral_profile`, `with_lane_height`,
`local_road_object_outlines_use_absolute_z`, `fix_spiral_edge_cases`,
`with_road_signals`, and `treat_value_zero_as_missing`. Defaults match C++.
A well-formed OpenDRIVE document replaces the current contents, including when
semantic errors leave a partially parsed map. Malformed XML or a missing
`OpenDRIVE` root leaves the existing map intact. Errors are plain JavaScript
objects with `description` and `xml_offset` (UTF-8 byte offset, or -1).
They remain valid after the XML document or map is destroyed.

## API coverage

Classes keep their C++ names and methods, including:

| Area | Exposed API |
| --- | --- |
| Map | `OpenDriveMap`, `OpenDriveMapHeader`, parsing, reset, meshing, routing |
| Roads and lanes | `Road`, `RoadLink`, `Speed`, `LaneSection`, `Lane`, `HeightOffset`, `LaneValidity`, `Crossfall`, `CrossfallRecord` |
| Junctions | `Junction`, `JunctionConnection`, `JunctionLaneLink`, `JunctionController`, `JunctionPriority` |
| Markings | `RoadMark`, `RoadMarkType`, `RoadMarkLine`, `SingleRoadMark` |
| Objects and signals | `RoadObject`, repeat/outline/corner records, `RoadSignal`, mesh factories |
| Geometry | `RefLine`, `RoadGeometry`, `Line`, `Arc`, `Spiral`, `ParamPoly3`, `CubicPoly`, `CubicProfile`, `CubicBounds`, `CubicBezier2D` |
| Sampling | `RefLineSampler`, `RoadSampler`, `LaneSampler`, `EdgePoints`, `DerivativeBounds`, `GeometryBounds`, all free functions in `Sampler.h` |
| Routing | `LaneKey`, `WeightedLaneKey`, `RoutingGraphEdge`, `RoutingGraph`, shortest paths and adjacency |
| Mesh | `Mesh3D`, vertices, normals, indices, ST coordinates, OBJ export, mesh merging |

All model data members are exposed. Scalar, enum, array and scalar-optional
properties are writable; nested objects and container properties are borrowed
and cannot be replaced. Containers expose `size()` and `get(key/index)`; maps
also expose `keys()` and sets expose `has(value)` and `values()`. Missing entries
and empty optionals return `undefined`. Assign `undefined` to clear a writable
optional. Map keys and set values are JavaScript arrays; vector results use
`size()` / `get()`. `Vec2D`, `Vec3D` and Bezier control points are JavaScript arrays.
Container structure is read-only. `RefLine.set_geometry(geometry)` stores a clone;
`RoutingGraph.add_edge(edge)` supports constructing routing graphs.

Enums are reflected with the existing `magic_enum` dependency and exported as
`PRange`, `CrossfallSide`, `RoadLinkType`, `RoadLinkContactPoint`, `TrafficRule`,
`JunctionContactPoint`, `Orientation`, and `RoadObjectCornerType`. For example,
use `odr.TrafficRule.RHT` or `odr.PRange.Normalized`.

`OdrNode.parent()`, `set_parent()` and `xml_offset` are inherited by model nodes.
Core classes have constructors with their required C++ arguments. Set optional
scalar properties afterward. The bindings do not expose pugixml objects,
implementation-only math/triangulation templates, or JavaScript overrides of
abstract C++ geometries.

Pointer output arguments are adapted into ordinary JavaScript results:

| Method | Result |
| --- | --- |
| `Road.get_frame(s, t, h)` | `{ xyz, e_s, e_t, e_h }` |
| `Lane.get_mesh_with_outline(eps)` | `{ mesh, outline_indices }` |
| `OpenDriveMap.get_mesh_with_warnings(eps, enforce_road_bounds)` | `{ mesh, warnings }` |
| `RoadObject.get_mesh_with_warnings(eps, default_h, default_z_offset, enforce_road_bounds)` | `{ mesh, warnings }` |
| `RoadSignal.get_mesh_with_warnings(enforce_road_bounds)` | `{ mesh, warnings }` |
| `OpenDriveMap.get_routing_graph_with_diagnostics()` | `{ graph, errors }` |

`Road.get_xyz` and road/lane `get_surface_pt` accept an optional final
`allow_extrapolate` boolean. `get_mesh`, `get_roadmark_mesh`, and lane sampler
methods have overloads with the usual C++ defaults. `RoadObject.get_mesh` accepts
either `eps` or all four non-pointer arguments. C++ exceptions remain enabled,
including the parser's internal error recovery.

## Ownership

Call `.delete()` on every Embind handle, including nested properties, containers,
and their object elements. Use `try/finally`, releasing children before owners.
Plain arrays, numbers, strings, enums and diagnostic objects need no deletion.

Constructors, geometry `clone()`, returned meshes, profiles, routing graphs,
paths, roadmark vectors and sample sets create owned results. Nested properties,
container `get()`, `parent()`, `get_lane_section()` and `get_geometry()` borrow
objects from their owner. Deleting a borrowed handle releases only the handle.
These handles use `shared_ptr` with a no-op deleter; they do not extend the
native owner's lifetime.
Keep the owner alive and do not retain these handles across `load()`, `reset()`,
`set_geometry()`, or any operation replacing their storage. Samplers borrow
their constructor argument and require its owning map/road/reference line to
remain alive. Map `keys()` and set `values()` copy class-valued keys/elements;
those handles also require deletion.

This preserves V1.0 parent links when traversing roads, lanes, markings and
objects. Native `OdrNode` copies intentionally clear their parent, so ordinary
Embind container accessors that return copies would break geometry operations.

## Maintaining the bindings

`Binding::field` infers value/optional/reference handling and registers container
views once per C++ type. The two local name macros avoid spelling each exported
member twice. Enum values use reflection; general member reflection is not
available in the project's C++17 toolchain. No extra reflection dependency or
changes to the native model are needed.

Run `node wasm/smoke.mjs path/to/libOpenDRIVE.mjs` after building. The test loads
the real WASM module, exercises the repository's map fixture, parent-dependent
geometry, routing, all geometry kinds, optional metadata, diagnostics and
repeated loading, and releases the handles it creates.
