import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { resolve } from 'node:path';
import { test } from 'node:test';
import { pathToFileURL } from 'node:url';

const modulePath = resolve(process.argv[2] ?? 'build/wasm/wasm/libOpenDRIVE.mjs');
const { default: createOpenDrive } = await import(pathToFileURL(modulePath));
const odr = await createOpenDrive();
const fixture = await readFile(new URL('../tests/test.xodr', import.meta.url), 'utf8');

function withHandles(run) {
    const handles = [];
    const keep = value => {
        handles.push(value);
        return value;
    };
    try {
        run(keep);
    } finally {
        for (const handle of handles.reverse()) handle.delete();
    }
}

function near(actual, expected, tolerance = 1e-8) {
    assert.ok(Math.abs(actual - expected) < tolerance, `${actual} != ${expected}`);
}

// Include optional nested records that the repository fixture does not contain.
const xml = `<OpenDRIVE>
  <header revMajor="1" revMinor="4" name="WASM test"><geoReference>local</geoReference></header>
  <road id="r" length="10" junction="-1" name="Straight road" rule="RHT">
    <type s="0" type="town"><speed max="30" unit="km/h"/></type>
    <planView><geometry s="0" x="0" y="0" hdg="0" length="10"><line/></geometry></planView>
    <elevationProfile><elevation s="0" a="0" b="0" c="0" d="0"/></elevationProfile>
    <lateralProfile><crossfall s="0" a="0" b="0" c="0" d="0" side="both"/></lateralProfile>
    <lanes><laneSection s="0">
      <center><lane id="0" type="none"/></center>
      <right><lane id="-1" type="driving" level="false">
        <width sOffset="0" a="3" b="0" c="0" d="0"/>
        <height sOffset="0" inner="0" outer="0"/>
        <roadMark sOffset="0" type="broken" color="white" width="0.12">
          <type name="dash" width="0.12"><line sOffset="0" tOffset="0" length="2" space="2"/></type>
        </roadMark>
      </lane></right>
    </laneSection></lanes>
    <objects><object id="box" s="2" t="-1" zOffset="0" length="1" width="1" height="1" orientation="+">
      <repeat s="2" length="4" distance="2" tStart="-1" tEnd="-1"/>
      <outlines><outline id="1" closed="true" outer="true">
        <cornerLocal id="0" u="0" v="0" z="0" height="1"/>
        <cornerLocal id="1" u="1" v="0" z="0" height="1"/>
        <cornerLocal id="2" u="1" v="1" z="0" height="1"/>
        <cornerLocal id="3" u="0" v="1" z="0" height="1"/>
      </outline></outlines>
      <validity fromLane="-1" toLane="-1"/>
    </object></objects>
    <signals><signal id="sign" s="5" t="-3" zOffset="1" dynamic="no" orientation="+" type="274" subtype="30">
      <validity fromLane="-1" toLane="-1"/>
    </signal></signals>
  </road>
  <junction id="j" name="test">
    <connection id="c" incomingRoad="r" connectingRoad="r" contactPoint="start"><laneLink from="-1" to="-1"/></connection>
    <priority high="r" low="r"/><controller id="ctrl" type="signal" sequence="1"/>
  </junction>
</OpenDRIVE>`;

test('V1.0 traversal preserves parent links and produces lane geometry', () => withHandles(keep => {
    const map = keep(new odr.OpenDriveMap());
    assert.deepEqual(map.load(xml).errors, []);
    const header = keep(map.header);
    assert.equal(header.name, 'WASM test');
    assert.equal(header.proj, 'local');
    assert.equal(header.vendor, undefined);
    header.vendor = 'test vendor';
    assert.equal(header.vendor, 'test vendor');
    header.vendor = undefined;
    assert.equal(header.vendor, undefined);

    const roads = keep(map.id_to_road);
    assert.deepEqual(roads.keys(), ['r']);
    assert.equal(roads.get('missing'), undefined);
    const road = keep(roads.get('r'));
    assert.equal(road.traffic_rule, odr.TrafficRule.RHT);
    assert.equal(road.predecessor, undefined);
    assert.deepEqual(road.get_xyz(2, -1, 0), [2, -1, 0]);
    assert.deepEqual(road.get_frame(2, -1, 0).e_s, [1, 0, 0]);
    assert.deepEqual(road.get_surface_pt(2, -1, false), [2, -1, 0]);
    const sections = keep(road.s_to_lane_section);
    const section = keep(sections.get(0));
    near(section.get_length(), 10);
    assert.equal(keep(section.parent()).id, road.id);
    assert.equal(keep(road.get_lane_section(1)).s, 0);
    const lanes = keep(section.id_to_lane);
    const lane = keep(lanes.get(-1));
    assert.equal(lane.type, 'driving');
    assert.equal(lane.level, false);
    assert.equal(keep(lane.parent()).s, 0);
    assert.equal(section.get_lane_id(1, -1), -1);
    assert.deepEqual(lane.get_surface_pt(1, -1), [1, -1, 0]);
    assert.equal(keep(lane.lane_width).evaluate(2), 3);
    const poly = keep(keep(lane.lane_width).get_poly(2));
    near(poly.evaluate(2), 3);
    assert.equal(keep(keep(lane.s_to_height_offset).get(0)).outer, 0);
    const speed = keep(keep(road.s_to_speed).get(0));
    assert.equal(speed.max, '30');
    assert.equal(keep(road.s_to_type).get(0), 'town');
    assert.equal(keep(keep(road.crossfall).records).size(), 1);

    const laneMesh = lane.get_mesh_with_outline(0.1);
    const mesh = keep(laneMesh.mesh);
    const vertices = keep(mesh.vertices);
    const indices = keep(mesh.indices);
    assert.ok(vertices.size() >= 4);
    assert.ok(indices.size() >= 6);
    assert.equal(laneMesh.outline_indices.length % 2, 0);
    for (const i of laneMesh.outline_indices) assert.ok(i < vertices.size());
    for (let i = 0; i < indices.size(); ++i) assert.ok(indices.get(i) < vertices.size());
    assert.ok(mesh.get_obj().includes('v '));
    assert.equal(vertices.get(vertices.size()), undefined);
    assert.ok(keep(mesh.normals).size() > 0);
    assert.ok(keep(mesh.st_coordinates).size() > 0);

    const sampler = keep(new odr.LaneSampler(lane));
    const samples = keep(sampler.get_mesh_s_samples(0, 10, 0.1));
    assert.deepEqual(samples.values(), [0, 10]);
    assert.ok(samples.has(0));
    assert.deepEqual(keep(sampler.edge_points(2)).outer, [2, -3, 0]);
    assert.deepEqual(keep(sampler.get_border_s_samples(0, 10, 0, 0.1, false)).values(), [0, 10]);
    const ref = keep(road.ref_line);
    const refSampler = keep(new odr.RefLineSampler(ref));
    assert.deepEqual(keep(refSampler.get_polyline_s_samples(0, 10, 0.1)).values(), [0, 10]);
    const roadSampler = keep(new odr.RoadSampler(road));
    assert.deepEqual(keep(roadSampler.get_polyline_s_samples(0, 0, 10, 0, 0.1)).values(), [0, 10]);
    assert.ok(keep(odr.get_ref_line_mandatory_s_samples(ref, 0, 10)).size() >= 2);
    assert.ok(keep(odr.get_road_mandatory_s_samples(road, 0, 10)).size() >= 2);
    assert.ok(keep(odr.get_lane_mandatory_s_samples(road, lane, keep(lanes.get(0)), 0, 10)).size() >= 2);
}));

test('markings, objects, signals and junction records are usable', () => withHandles(keep => {
    const map = keep(new odr.OpenDriveMap());
    assert.deepEqual(map.load(xml).errors, []);
    const road = keep(keep(map.id_to_road).get('r'));
    const lane = keep(keep(keep(road.get_lane_section(0)).id_to_lane).get(-1));
    const mark = keep(keep(lane.s_to_road_mark).get(0));
    assert.equal(mark.type, 'broken');
    assert.equal(keep(mark.type_elem).name, 'dash');
    const markLine = keep(keep(keep(mark.type_elem).lines).get(0));
    assert.equal(markLine.space, 2);
    assert.equal(keep(markLine.parent()).name, 'dash');
    const marks = keep(lane.get_roadmarks(0, 10));
    assert.ok(marks.size() > 0);
    const single = keep(marks.get(0));
    const markMesh = keep(lane.get_roadmark_mesh(single, 0.1, false));
    assert.ok(keep(markMesh.vertices).size() > 0);

    const object = keep(keep(road.id_to_object).get('box'));
    assert.equal(object.orientation, odr.Orientation.Positive);
    const repeat = keep(keep(object.repeats).get(0));
    assert.equal(repeat.distance, 2);
    assert.equal(keep(repeat.parent()).id, 'box');
    const outline = keep(keep(object.outlines).get(0));
    assert.equal(outline.closed, true);
    const corner = keep(keep(outline.outline).get(1));
    assert.deepEqual(corner.pt, [1, 0, 0]);
    assert.equal(corner.type, odr.RoadObjectCornerType.Local_RelZ);
    assert.equal(keep(keep(object.lane_validities).get(0)).from_lane, -1);
    const objectResult = object.get_mesh_with_warnings(0.1, 0, 0, false);
    assert.ok(keep(keep(objectResult.mesh).vertices).size() > 0);
    const signal = keep(keep(road.id_to_signal).get('sign'));
    assert.equal(signal.height, undefined);
    const signalResult = signal.get_mesh_with_warnings(false);
    assert.ok(keep(keep(signalResult.mesh).vertices).size() > 0);
    assert.ok(signalResult.warnings.length > 0);
    assert.equal(keep(keep(signal.lane_validities).get(0)).to_lane, -1);

    const junction = keep(keep(map.id_to_junction).get('j'));
    const connection = keep(keep(junction.id_to_connection).get('c'));
    assert.equal(connection.contact_point, odr.JunctionContactPoint.Start);
    assert.equal(keep(keep(connection.lane_links).get(0)).from, -1);
    assert.equal(keep(keep(junction.priorities).get(0)).high, 'r');
    assert.equal(keep(keep(junction.id_to_controller).get('ctrl')).sequence, 1);
    const merged = map.get_mesh_with_warnings(0.1, false);
    assert.ok(keep(keep(merged.mesh).indices).size() > 0);
    assert.ok(merged.warnings.length > 0);
}));

test('all road geometry kinds, profiles, bounds and mesh factories', () => withHandles(keep => {
    const line = keep(new odr.Line(0, 0, 0, 0, 10));
    assert.deepEqual(line.get_xy(2), [2, 0]);
    assert.deepEqual(keep(line.clone()).derivative(2), [1, 0]);
    const arc = keep(new odr.Arc(0, 0, 0, 0, 10, 0.1));
    near(arc.get_xy(2)[0], 10 * Math.sin(0.2));
    const spiral = keep(new odr.Spiral(0, 0, 0, 0, 10, 0, 0.1));
    assert.ok(spiral.get_xy(2).every(Number.isFinite));
    const param = keep(new odr.ParamPoly3(0, 0, 0, 0, 10, 0, 10, 0, 0, 0, 0, 0, 0, odr.PRange.Normalized));
    near(param.get_xy(2)[0], 2, 1e-5);
    assert.ok(keep(param.cubic_bezier).valid_length > 0);
    const ref = keep(new odr.RefLine(10));
    ref.set_geometry(line);
    assert.ok(keep(ref.get_geometry(2)) instanceof odr.Line);
    assert.deepEqual(keep(keep(ref.s_to_geometry).get(0)).get_xy(3), [3, 0]);
    near(ref.match(4, 2), 4, 1e-2);

    const poly = keep(new odr.CubicPoly(1, 2, 3, 4));
    near(poly.evaluate(1), 10);
    near(poly.derivative(1), 20);
    const bounds = keep(poly.bounds(0, 1));
    assert.ok(bounds.max >= 10);
    const magnitude = keep(odr.magnitudes(bounds, true));
    const total = keep(odr.sum(magnitude, magnitude));
    near(total.value, 2 * magnitude.value);
    assert.ok(keep(odr.product(magnitude, magnitude)).value >= magnitude.value);
    assert.ok(keep(odr.geometry_bounds(line, 0, 10)).speed_lower > 0);
    const zeroBounds = keep(new odr.CubicBounds());
    near(keep(odr.tan_bounds(zeroBounds)).value, 0);
    const profile = keep(new odr.CubicProfile());
    assert.equal(profile.evaluate(0), undefined);
    assert.equal(keep(profile.negate()).evaluate(0), undefined);
    const points = [[0, 0], [1, 0], [2, 1], [3, 1]];
    const bezier = keep(new odr.CubicBezier2D(points));
    assert.deepEqual(bezier.evaluate(0), points[0]);
    assert.deepEqual(bezier.get_subcurve(0, 1), points);
    assert.ok(keep(bezier.get_polyline_t_samples(0.1)).size() >= 2);
    assert.deepEqual(odr.CubicBezier2D.get_control_points(odr.CubicBezier2D.get_coefficients(points)), points);
    for (const mesh of [odr.RoadObject.get_cube(1, 2, 3), odr.RoadObject.get_cylinder(0.1, 1, 2), odr.RoadSignal.get_box(1, 2, 3)]) {
        keep(mesh);
        assert.ok(keep(mesh.vertices).size() > 0);
    }
}));

test('repository fixture routing, optional links and independent results', () => withHandles(keep => {
    const map = keep(new odr.OpenDriveMap());
    assert.deepEqual(map.load(fixture).errors, []);
    const roads = keep(map.id_to_road);
    assert.ok(roads.size() > 10);
    const road = keep(roads.get('17'));
    const link = keep(road.successor);
    assert.ok(link.id.length > 0);
    const graph = keep(map.get_routing_graph());
    const from = keep(new odr.LaneKey('43', 0, 1));
    const to = keep(new odr.LaneKey('41', 0, 1));
    const path = keep(graph.shortest_path(from, to));
    assert.equal(path.size(), 15);
    assert.equal(keep(path.get(0)).to_string(), from.to_string());
    assert.equal(keep(path.get(14)).to_string(), to.to_string());
    assert.ok(keep(graph.get_lane_successors(from)).size() > 0);
    const edges = keep(graph.edges);
    assert.ok(edges.size() > 0);
    const edgeValues = edges.values();
    edgeValues.forEach(keep);
    assert.equal(edgeValues.length, edges.size());
    assert.ok(edges.has(edgeValues[0]));
    const successors = keep(graph.lane_key_to_successors);
    const keys = successors.keys();
    keys.forEach(keep);
    assert.ok(keys.length > 0);
    const weighted = keep(successors.get(keys[0])).values();
    weighted.forEach(keep);
    assert.ok(weighted.length > 0);
    assert.ok(weighted[0].weight >= 0);
    const diagnostics = map.get_routing_graph_with_diagnostics();
    keep(diagnostics.graph);
    assert.ok(Array.isArray(diagnostics.errors));
    // Returned routing data must remain usable after its original map is reset.
    map.reset();
    assert.equal(keep(graph.shortest_path(from, to)).size(), 15);
}));

test('parser diagnostics survive input lifetime, recover from exceptions and honor options', () => {
    let errors;
    withHandles(keep => {
        const map = keep(new odr.OpenDriveMap());
        assert.deepEqual(map.load(xml, undefined).errors, []);
        assert.ok(map.load('<OpenDRIVE>').errors.length > 0);
        assert.equal(keep(map.id_to_road).size(), 1);
        assert.ok(map.load('<unrelated/>').errors[0].description.includes('root'));
        errors = map.load('<OpenDRIVE><road id="bad" length="-1" junction="-1"/></OpenDRIVE>').errors;
        assert.ok(errors.length > 0);
        assert.equal(typeof errors[0].xml_offset, 'number');
        // The invalid Road constructor above throws internally; the parser catches it.
        assert.deepEqual(map.load(xml, { with_road_objects: false, with_road_signals: false }).errors, []);
        const road = keep(keep(map.id_to_road).get('r'));
        assert.equal(keep(road.id_to_object).size(), 0);
        assert.equal(keep(road.id_to_signal).size(), 0);
        assert.deepEqual(map.load(xml).errors, []);
        assert.equal(keep(map.id_to_road).size(), 1);
    });
    assert.ok(errors[0].description.length > 0);
});
