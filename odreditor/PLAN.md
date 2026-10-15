# OpenDRIVE editor plan

Status: phases 01–03 implemented; phases 04–08 remain planned.

## Product contract

| Requirement | Decision |
| --- | --- |
| Purpose | Visual editing of the underlying XODR document. |
| Authority | Persistent XML document; previews never replace it. |
| Editing coverage | Add, remove, change, duplicate, and move any XML element or attribute. |
| Normal commands | Apply the requested edit plus deterministic corrections defined in EDIT_RULES. |
| Raw XML editing | Apply the supplied XML without automatic semantic corrections. |
| Compatibility | Preserve incompatible documents; report problems without blocking save. |
| Implementation | C++ document, commands, UI, geometry, and rendering; minimal browser glue. |
| UI | Compact, cohesive, light Dear ImGui style, following the supplied image. |
| Distribution | One self-contained HTML file with embedded code and assets. |
| Upstream isolation | Build around libOpenDRIVE; editor-owned files live under this directory. |

## Specification sheets

| Document | Covers |
| --- | --- |
| [Architecture](ARCHITECTURE.md) | Ownership, components, data flow, preview, upstream boundary. |
| [Edit rules](EDIT_RULES.md) | Automatic corrections, explicit operations, deferred repairs, lane remapping. |
| [Interface](UI.md) | Layout, style tokens, controls, picking, concise feedback. |
| [Build](BUILD.md) | C++ dependencies, standalone CMake, single HTML packaging. |
| [Engineering](ENGINEERING.md) | Naming, comments, short-circuit logic, temporary files, minimal testing. |

## Implementation phases

| Phase | Deliverable | Completion gate |
| --- | --- | --- |
| [01 App foundation](phases/01-app-foundation.md) | C++ ImGui shell and standalone build. | One HTML opens with the intended light interface and no runtime asset fetches. |
| [02 Document editing](phases/02-document-editing.md) | XML ownership, tree, inspector, save, history. | Unknown content survives edits and undo/redo. |
| [03 Deterministic corrections](phases/03-deterministic-corrections.md) | Lane compaction and scoped reference maintenance. | Renumbering preserves identity across sections, road ends, and junctions. |
| [04 Preview and selection](phases/04-preview-selection.md) | libOpenDRIVE adapter, meshes, picking, diagnostics. | Supported geometry follows edits; failures remain local to the preview. |
| [05 Road geometry tools](phases/05-road-geometry.md) | Line, arc, spiral, and parametric cubic tools. | Draw, edit, delete, and save geometry with correct parameter conversion. |
| [06 Map element tools](phases/06-map-elements.md) | Lanes, profiles, markings, objects, signals, junctions. | Common elements have focused visual controls and complete property access. |
| [07 Geometry attachment](phases/07-geometry-attachment.md) | Endpoint alignment and scoped fitting. | Selected geometry attaches within tolerance, with one reversible edit. |
| [08 Integration and release](phases/08-integration-release.md) | Performance, compatibility checks, final packaging. | Representative files work offline in the single HTML build. |

Dependencies: 01 -> 02 -> 03 -> 04 -> 05 -> 06 -> 07 -> 08.
Packaging is established in phase 01 and verified throughout; it is not postponed to release.

## Correction policy

- Automatic now: preserve identities, compact lane IDs, update resolved references, remove references with a defined deletion rule, rebuild calculated preview data.
- Explicit later: align endpoints, rebuild a selected geometry chain, fit connecting curves, split/merge/reverse roads.
- Deferred: infer traffic intent, redesign junctions, reroute an entire network, or repair unknown extension semantics.
- Corrections belong to the same undo transaction as the triggering edit.
- Ambiguous corrections fail the normal command atomically with a concise diagnostic. Raw XML remains available.
- Existing unrelated incompatibilities do not prevent ordinary editing.

## Scope limits

- Complete XML editing does not imply complete rendering of every OpenDRIVE version.
- Start typed tools with the V1.0 library's modeled features; retain all other content.
- Add format-specific reference rules and renderers explicitly, without changing the document core.
- Preserve content and child order; exact original formatting after an edit is not required.
- No automatic OpenDRIVE version upgrade or coordinate-system conversion.

## Delivery constraints

- Do not edit existing README files, source formatting, or unrelated project configuration.
- No native model-wide setter rewrite, reflection framework, or duplicate OpenDRIVE serializer.
- Temporary tests, notes, scripts, captures, and builds belong under repository-root `.codex/`.
- Permanent tests are limited to algorithms, meaningful integrations, and regression-prone behavior.
- This directory contains the requested permanent specifications; phase sheets give implementation details.
