# Editor architecture

## Ownership

| Data | Owner | Lifetime |
| --- | --- | --- |
| Complete XML tree | XodrDocument | Until document close or replacement. |
| Internal NodeId | XodrDocument | Stable through normal edits; distinct from XML IDs. |
| Edits and inverse changes | EditHistory | Bounded undo history for the document. |
| Resolved XML references | ReferenceIndex | Rebuilt or invalidated by document revisions. |
| Native roads and geometry | OdrAdapter | Disposable preview revision. |
| Meshes and picking IDs | SceneRenderer | Until affected preview geometry is replaced. |
| Selection and tool state | EditorApp | NodeIds and values; no borrowed native objects. |

The XML document is authoritative. Commands update XML. libOpenDRIVE calculates geometry from snapshots. Calculated meshes, borders, caches, and native defaults never overwrite XML.

## Proposed source layout

| Location under odreditor | Responsibility |
| --- | --- |
| `CMakeLists.txt`, `cmake/` | Standalone targets and pinned dependencies. |
| `src/document/` | XodrDocument, node lookup, XML preservation. |
| `src/edit/` | Commands, history, reference resolution, corrections. |
| `src/odr/` | OdrAdapter and format-specific translation. |
| `src/geometry/` | Drawing, profile operations, later attachment/fitting. |
| `src/render/` | Cameras, meshes, picking, overlays, GPU lifetime. |
| `src/ui/` | Shared controls, theme, panels, editor tools. |
| `src/platform/`, `web/` | SDL/Emscripten integration, HTML shell, file bridge. |
| `tests/` | Only approved categories of durable tests from ENGINEERING. |

Create files when their responsibilities exist. Do not scaffold unused interfaces or one-class wrappers.

## Document API

| Group | Proposed operations |
| --- | --- |
| Files | LoadDocument, SaveDocument, CreateDocument |
| Inspection | GetNode, GetChildren, GetAttributes, FindNode |
| Raw mutation | SetAttribute, RemoveAttribute, SetText, InsertElement, RemoveElement, MoveElement, ReplaceSubtree |
| Domain commands | DeleteLane, InsertLane, RenameRoad, MoveLaneSection, ReplaceGeometry |
| Transactions | BeginEdit, CommitEdit, CancelEdit, Undo, Redo |

- Raw mutation primitives are internal building blocks and power the explicit XML editor.
- Normal inspector edits use domain commands where correction rules exist.
- NodeIds are not serialized into XODR and are not derived from editable IDs or floating-point positions.
- Deleted IDs cannot resolve to new elements that happen to reuse the same memory or XML ID.
- Undo restores the original node identities and sibling placement.
- A whole-document XML replacement rebuilds identity lookup; selection is remapped only when unambiguous.
- Preserve unknown nodes, attributes, comments, CDATA, and namespace declarations.
- Missing attributes, empty strings, and explicit zero values remain distinct.

## Command execution

1. Resolve selected nodes and references against the pre-edit document.
2. Compute the proposed changes and their exact correction scope.
3. Reject ambiguity affecting those corrections; leave unrelated errors alone.
4. Commit XML changes and inverse records atomically.
5. Update reference lookup and mark affected previews dirty.
6. Publish the new revision and retain one undo entry.

Dragging produces temporary candidate values. Release commits one edit; Escape cancels it.
The document tree, inspector, and viewport all invoke the same commands.

## Reference lookup

- Resolve semantic targets to NodeIds before changing IDs.
- Lane identity includes road, lane section, and signed lane ID.
- Resolve boundary links using road contact points and junction mappings.
- Support format-defined reference fields explicitly; never replace matching text throughout the file.
- Use one rebuildable index first. Add incremental maintenance only when measured.
- References to unknown extensions remain untouched and are reported as outside supported correction coverage.

## Preview adapter

- Link libOpenDRIVE directly into the C++ app.
- The browser editor does not need to call the existing Embind API for every operation.
- Use existing public constructors, loading, sampling, and mesh generation.
- Start with a fresh road-level projection for a changed road; preserve the required header context.
- Use a complete map snapshot when an operation actually needs network-wide interpretation.
- Keep each projection's owner alive until sampling and mesh extraction finish.
- Copy mesh/diagnostic results; do not retain OdrNode or collection references across replacement.
- Associate preview parts with source NodeIds, rather than relying on road IDs being unique.
- If source-to-preview mapping is ambiguous, omit that picking mapping and keep tree access.
- Avoid loading options that discard explicit values when they can be disabled.

Known integration boundaries:

| Native behavior | Editor treatment |
| --- | --- |
| Lane-number gaps can discard a road during load. | Keep XML; show available reference geometry and a localized diagnostic. |
| Lane borders are calculated during loading. | Rebuild the affected road projection after width/offset changes. |
| Spiral and ParamPoly3 cache construction data. | Reconstruct from XML parameters; never mutate cached preview fields. |
| ParamPoly3 can normalize coefficients internally. | Save authoritative XML values; explicitly convert newly generated parameters. |
| Mesh aggregation loses element identity. | Extract selectable parts and attach source NodeIds in the adapter. |
| Native parse diagnostics refer to XML nodes/offsets. | Convert to editor node diagnostics before temporary documents are destroyed. |

## Execution and responsiveness

- Initial runtime: one C++ application loop with dirty-road scheduling.
- Coalesce property changes; avoid whole-map rebuilds on mouse movement.
- Use cheap curve previews while dragging and detailed surfaces on commit.
- Budget work between frames; individual native calls may still exceed that budget.
- Introduce an isolated preview worker when measured workloads require it.
- A worker receives snapshots and returns revision-tagged buffers; it never owns the authoritative document.
- Keep worker source and any additional WASM payload embedded in the single HTML.
- Catch supported native exceptions at the adapter boundary. Preserve the document independently of preview success.

## Upstream compatibility

- Consume public headers and the `OpenDrive::OpenDrive` target.
- Keep all native API assumptions inside `src/odr/`.
- Prefer composition, free functions, and document reconstruction over patching native setters.
- Do not copy the upstream parser or maintain a parallel fork of its model.
- Keep the root build, existing WASM bindings, README, and native source untouched by default.
- If a native fix is necessary, keep it small, independently useful, and separate from editor feature changes.
- Verify the adapter against the pinned baseline and a deliberately chosen newer upstream revision.

## Implementation references

- [Existing model API](../include/libodr/OpenDriveMap.h)
- [Existing parser and derived data](../src/OpenDriveMap.cpp)
- [Existing geometry construction](../src/Geometries/ParamPoly3.cpp)
- [Pugixml parsing, mutation, and serialization](https://pugixml.org/docs/manual.html)
