# libOpenDRIVE

[![DOI](https://zenodo.org/badge/DOI/10.5281/zenodo.7771708.svg)](https://doi.org/10.5281/zenodo.7771708)


libOpenDRIVE is a **lightweight, fast C++ library** providing OpenDRIVE file parsing and 3D model generation. 

It's small and can be easily integrated in other projects. A core function is the parsing of OpenDRIVE files and the generation of 3D models. The library targets OpenDRIVE version 1.4.

## Example
Here's an example of how code using libOpenDRIVE looks. For a more complete example refer to [tests/test.cpp](tests/test.cpp).

```c++
// load xml document
pugi::xml_document xml_doc;
xml_doc.load_file("tests/test.xodr");

// load map
odr::OpenDriveMap odr_map;
odr::XodrParseResult result = odr_map.load(xml_doc);
for (const odr::XodrParseError& error : result.errors)
    std::cerr << error.node.path() << ": " << error.description << std::endl;

// iterate roads
for (const auto& [, road] : odr_map.id_to_road)
    std::cout << "road: " << road.id << " length: " << road.length << std::endl;

// get xyz point for road coordinates
const odr::Road& odr_road = odr_map.id_to_road.at("17");
odr::Vec3D pt_xyz = odr_road.get_xyz(2.1 /*s*/, 1.0 /*t*/, 0.0 /*h*/);

// access road network attributes
const odr::Lane& lane = odr_road.s_to_lane_section.at(0.0).id_to_lane.at(-1);
int lane_id = lane.id;
std::optional<std::string> lane_type = lane.type;

// use routing graph
odr::RoutingGraph routing_graph = odr_map.get_routing_graph();
odr::LaneKey from("17" /*road id*/, 0.0 /*lane section s*/, 1 /*lane id*/);
odr::LaneKey to("41", 0.0, -1);
std::vector<odr::LaneKey> path = routing_graph.shortest_path(from, to);

// get road network mesh
odr::Mesh3D road_network_mesh = odr_map.get_mesh(0.1 /*eps*/);
std::cout << road_network_mesh.get_obj() << std::endl;
```

## Build
To build a static library by default, simply run:
```bash
mkdir build
cd build
cmake ..
make
```

If requiring a shared library, use:
```bash
cmake -DBUILD_SHARED_LIBS=ON ..
```

## WebAssembly

Build the separate Embind module with an activated [Emscripten SDK](https://emscripten.org/docs/getting_started/downloads.html):

```bash
emcmake cmake -S . -B build/wasm -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/wasm --parallel
```

The output is `build/wasm/libOpenDRIVE.mjs` and `libOpenDRIVE.wasm`.

## Viewer
Check out the viewer at [odrviewer.io](https://odrviewer.io) which uses this library. Use the [odrviewer project](https://github.com/pageldev/odrviewer) to post issues and feature requests for odrviewer.io.

<sub>The viewer is maintained separately; the WebAssembly bindings in this repository target the V1.0 API.</sub>
