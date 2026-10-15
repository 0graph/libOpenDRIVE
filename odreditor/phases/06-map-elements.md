# Phase 06 Map element tools

Status: planned. Dependencies: phases 02 to 05.

## Deliverable

Focused controls for the modeled map elements, with generic editing available for everything else.

## Coverage

| Area | Controls and operations |
| --- | --- |
| Lane sections | Add/delete/move boundaries; section records. |
| Lanes | Insert/delete/reorder; width/type/level; links and validity-aware corrections. |
| Profiles | Width, lane offset, elevation, superelevation, crossfall, and height records. |
| Markings | Placement, styles, type/line records, dimensions, and patterns. |
| Objects | s/t/height, orientation, dimensions, repeat records, outlines, corners. |
| Signals | Placement, orientation, attributes, validity, supported references. |
| Junctions | Connections, lane mappings, priorities, controller references. |
| Other records | Header, road types/speeds, extensions, unsupported nodes/attributes. |

## Work

1. Build specialized panels from shared property and record controls.
2. Reuse one profile-graph interaction where its semantics fit.
3. Integrate ImGuizmo through the existing camera/coordinate conventions.
4. Implement explicit Connect/Disconnect operations and relationship overlays.
5. Extend reference resolution for any newly editable typed relationship.
6. Mark render/correction support accurately without hiding generic XML access.

## Behavior

- Automatic corrections follow [Edit rules](../EDIT_RULES.md).
- Numeric edits, tree actions, and viewport tools share commands.
- Moving road-relative elements updates their selected s/t coordinates.
- Changing metadata does not regenerate unaffected meshes.
- No arbitrary values are clamped silently by an inspector.
- Native support gaps appear as localized diagnostics, not deleted content.
- Do not imply that an unmodeled controller or junction type has working native simulation/rendering.

## Acceptance

- Each listed category supports add/change/delete and save/reopen.
- Outline edits maintain supported corner references.
- Lane insert/delete uses phase 03 remapping without duplicated logic.
- Profiles retain clear road-relative versus section-relative positions.
- All controls use the shared light theme, spacing, fonts, and components.
- No long explanations or new card layouts appear inside property panels.

## Verification

Reuse the existing correction and serialization suites.
Add durable tests only for new interpolation, coordinate conversion, reference resolution, or a fragile native integration.
Use temporary UI checks for panels and property wiring.

## Out of scope

Traffic-intent inference, full support for every schema's renderer, and network-wide geometry repair.
