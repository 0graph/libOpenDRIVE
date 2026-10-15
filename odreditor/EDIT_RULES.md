# Edit corrections

## Rule classes

| Class | Behavior | Delivery |
| --- | --- | --- |
| Automatic | Deterministic structural/reference correction in the same transaction. | Phases 02 to 06. |
| Calculated | Refresh native caches, meshes, bounds, picking, and diagnostics; do not write results into XML. | Phase 04 onward. |
| Explicit tool | Modify a selected scope using a named operation with a preview. | Phases 05 to 07. |
| Deferred | Infer intent or solve a wider network problem. | Outside the initial plan. |
| Raw XML | Apply source edits as supplied; rebuild lookup and preview, without semantic correction. | Phase 02 onward. |

Normal commands maintain supported references. Raw XML provides an escape hatch for intentional incompatibilities.
Save is allowed with semantic errors. A failed normal command leaves the document unchanged.

## Lane identity and numbering

| Edit | Automatic correction | Do not do |
| --- | --- | --- |
| Delete a non-center lane | Remove its subtree; compact surviving IDs on that side of that section; remap references to survivors. | Reconnect the deleted lane's neighbors or modify other lane sections' numbering. |
| Insert a lane | Assign the chosen side/position; shift outer lane IDs; remap references to shifted lanes. | Invent predecessor/successor connections for the new lane. |
| Reorder lanes on one side | Assign consecutive signed IDs in the chosen order; retain each lane's subtree and identity. | Swap unrelated widths, markings, or properties between lane identities. |
| Change a lane ID directly | Require a unique target ID for the normal rename command; rewrite references to that lane. | Renumber a different lane to hide a collision. |
| Move a lane to the other side | Explicit move command; renumber affected sides and remap supported references. | Mirror markings, offsets, travel direction, or connectivity by guesswork. |
| Delete lane 0 | Explicit deletion; retain other IDs and report the missing center lane. | Promote another lane to the center or fabricate a replacement. |
| Edit lane width/type/level | Change requested properties and rebuild affected surfaces. | Alter lane connections or change widths of neighboring lanes. |

Compaction preserves order moving outward from the center. Left remains positive; right remains negative; the surviving center remains zero. On a pre-gapped side, compaction makes that affected side consecutive.

### Reference semantics

- Resolve the entire affected reference set before renumbering.
- A surviving lane keeps its internal NodeId.
- Apply all old-ID-to-new-ID mappings simultaneously.
- Change references that point to that identity, even when the reference lives on another road.
- A lane's own predecessor/successor fields describe other lanes. Change them only if their targets were renumbered or removed.
- References to a deleted lane are removed where the format defines a removable link; they must not target the survivor that inherits its number.
- Never use a document-wide integer substitution.

### Worked deletion

| Item | Before deleting lane -1 in section B | After |
| --- | --- | --- |
| Deleted lane D | B/-1 | Removed. |
| Surviving lane L | B/-2 | B/-1, same NodeId. |
| Outer lane M | B/-3 | B/-2, same NodeId. |
| A lane in preceding section A targets L | successor=-2 | successor=-1. |
| A lane in following section C targets L | predecessor=-2 | predecessor=-1. |
| L targets an unchanged lane in A | predecessor=-2 | Remains -2. |
| Another lane targets D | target=-1 | Remove that link; do not redirect it to L. |

This distinction applies equally when neighboring sections belong to different roads.

## Lane reference coverage

| Reference | Resolution rule |
| --- | --- |
| Lane predecessor/successor within a road | Resolve the adjoining lane section in reference-line direction. |
| Lane predecessor/successor across roads | Resolve the linked road and its start/end contact point; never assume matching lane signs. |
| Junction laneLink from | Resolve the relevant incoming-road boundary for that connection. |
| Junction laneLink to | Resolve the connecting-road boundary selected by the connection contact point. |
| Direct-junction or newer reference variants | Use an explicit format-specific resolver before enabling automatic correction. |
| Signal/object validity ranges | Resolve the lanes actually covered by the element's position/span and format semantics. |
| Extension-specific lane references | Preserve untouched unless a registered resolver defines their meaning. |

Lane links are based on reference-line direction and contact points, not merely driving direction. [ASAM lane linkage](https://publications.pages.asam.net/standards/ASAM_OpenDRIVE/ASAM_OpenDRIVE_Specification/v1.8.1/specification/11_lanes/11_05_lane_link.html)

### Validity ranges

- Resolve each range to lane identities, remove deleted identities, then remap survivors.
- Re-encode contiguous ranges; use multiple validity elements only where the format permits.
- Do not update only the two endpoints when the interior membership changes.
- If a span covers several sections, verify that one resulting definition preserves its meaning across them.
- An empty range cannot always be represented by deleting the validity element: absence may broaden applicability.
- If the supported XML form cannot express the result, stop the normal command with a focused diagnostic. Raw deletion or an explicit edit to the owning element remains available.

## Other identities and topology

| Edit | Automatic correction | Explicit or deferred behavior |
| --- | --- | --- |
| Rename a road | Update recognized road links, junction incoming/connecting road fields, priorities, and other supported road references. | Do not move roads or infer missing links. |
| Rename a junction | Update supported road junction membership and junction references. | Do not change connecting-road geometry. |
| Rename signal/object/controller | Update supported references within the format-defined ID scope. | Do not replace matching strings in names, text, or unknown extensions. |
| Rename a connection, outline, or corner | Update supported references in that element's scope. | Do not assume these IDs are globally unique. |
| Delete a road | Remove its subtree, incoming road-link elements, and junction connections that explicitly require it. Remove applicable reference-only records. | Do not join the remaining roads or delete unrelated junctions/controllers. |
| Delete a junction | Remove its reference links; set affected road membership to the format's no-junction value. | Preserve connecting-road documents; do not reconnect them automatically. |
| Delete a signal/object | Remove known reference-only records and dependencies targeting it when removal has defined semantics. | Do not delete other signals/objects merely because they referenced it. |
| Delete controller | Remove known references to that controller. | Do not redistribute its signals or traffic phases. |
| Delete connection/laneLink | Remove exactly that connection or mapping. | Do not infer a replacement maneuver. |
| Edit one link in inspector | Update that selected relationship. | No silent reciprocal-link creation. |
| Use Connect/Disconnect tool | Apply the explicitly selected road/lane link pair as one transaction. | No geometric fitting unless the attachment tool is selected. |
| Duplicate a subtree | Assign fresh known IDs where needed; remap references wholly inside the duplicate. | Do not rewrite outside references to point to the copy. |
| Move a scoped element | Re-resolve supported internal/outer references and check destination ID collisions. | No implicit coordinate conversion or connection inference. |

Removing a required reference can leave an incomplete owner. Retain the owner with a diagnostic when its semantics remain explicit; if removal would silently change applicability, stop the normal command instead.
Existing unresolved references are never guessed. An ambiguity that prevents a correct remap stops that command; unrelated errors do not.

## Positions, profiles, and records

| Edit | Automatic/calculated behavior | Explicit or deferred behavior |
| --- | --- | --- |
| Change a record start s/sOffset | Reorder records within their known XML group; rebuild lookup and preview. | Do not alter coefficients or move other records. |
| Move a lane-section boundary | Change its s; rebuild derived absolute positions from section-relative records. | Do not rewrite links because a different section becomes adjacent; diagnose them. |
| Add/remove a lane section | Update section ordering and calculated extents; clear supported links to deleted identities. | Lane continuity, copied lanes, and reconnection require explicit commands. |
| Insert/remove a width, elevation, offset, banking, or height record | Update group order and preview using the remaining piecewise definitions. | No automatic smoothing or profile fitting. |
| Move an object/signal in s/t | Update selected coordinates; recalculate its displayed world pose. | No automatic lane reassignment or validity expansion. |
| Delete/rename outline corner | Update supported corner references; remove reference-only records targeting deleted corners. | Do not invent a replacement outline or repair polygon intersections. |
| Edit repeat length/distance | Rebuild repeated preview instances. | Do not create or delete unrelated objects. |
| Change geoReference/header offset | Store the requested metadata; refresh supported transforms. | Reprojecting coordinates is a separate explicit operation. |
| Change format version | Change version metadata and diagnostic rule selection. | Do not migrate elements or discard unsupported attributes. |

Positions and IDs are not editor identity. Duplicate record starts remain representable through raw XML even when the native map cannot distinguish them.

## Geometry and attachment

| Edit/tool | Immediate behavior | Later correction |
| --- | --- | --- |
| Change geometry parameters | Reconstruct cached native geometry; refresh surfaces and endpoint diagnostics. | No unsolicited change to neighboring segment coordinates. |
| Add a road using drawing tools | Generate its selected geometry, stations, and road length as one construction operation. | No network-wide link or lane inference. |
| Append a drawn segment | Initialize it at the chosen chain endpoint and calculate its parameters. | Existing independent roads remain fixed. |
| Insert/delete/reorder using a geometry-chain tool | Recalculate stations and total length for that explicitly edited chain. | Leave world placement unchanged until an alignment/rebuild operation is selected. |
| Directly edit road length | Change the attribute and show disagreement with geometry extent. | Do not stretch geometry or relocate s-based features. |
| Translate/rotate a selected road | Transform that road's selected geometry consistently; attached road-relative content follows its coordinate system. | Connected roads stay fixed. Handle owned inertial-position records through an explicit transform scope. |
| Snap endpoint position | Phase 07: align selected geometry to a chosen endpoint. | No traffic-link creation by visual proximity. |
| Align endpoint position and tangent | Phase 07: rigidly transform the selected road/chain while preserving shape and length. | No curvature or elevation matching unless requested. |
| Rebuild selected reference-line chain | Phase 07: propagate segment start pose and stations; update chain length. | No automatic movement of neighboring roads. |
| Fit a selected connector | Phase 07: solve supported curve parameters against declared endpoint constraints. | No promise that every pair of constraints has a valid single-curve solution. |
| Match elevation/slope | Phase 07: explicit vertical-profile operation with stated affected interval. | No network-wide terrain fitting. |
| Split/merge/reverse roads | Later explicit operations with interval/profile/reference transformations. | Never approximate with XML reordering or sign replacement alone. |

Rigid endpoint alignment is the first attachment feature. It is a small transform when one side is free.
Keeping both endpoints fixed while changing shape needs a fitting algorithm; curvature-continuous and vertical fitting follow separately.

## Attachment contract for phase 07

- Select the anchor endpoint and the movable road/chain explicitly.
- Account for start/start, start/end, end/start, and end/end tangent conventions.
- G0 means shared position; G1 adds compatible tangent direction; G2 adds curvature compatibility.
- Distinguish XY-only alignment from elevation and slope matching.
- Use documented position/angle tolerances and finite-value checks.
- Preview the entire affected scope; commit once; undo restores all values and identities.
- Reject an impossible fit without altering the document.
- Do not repeatedly move a connected network in search of an unspecified solution.

## Always refresh after relevant edits

Native caches, lane borders, mesh parts, picking, spatial bounds, reference overlays, diagnostics, and any previously generated routing graph.
These are derived outputs. Their refresh does not authorize additional XML edits.

## Deferred automatic behavior

- Choosing which traffic lanes should connect.
- Bypassing a deleted lane or road to create new routes.
- Junction generation, lane-count reconciliation across junctions, and intersection redesign.
- Automatically moving every connected road after one endpoint changes.
- Reparameterizing all signs, objects, and profile records after a road-length edit.
- Guessing the meaning of vendor extensions or converting file versions.
- Repairing every pre-existing error on file load.

## Algorithm acceptance cases

- Compact either side without changing the other side or section.
- Resolve cross-road links correctly for all contact-point combinations.
- Remap junction from/to fields in their actual lane scopes.
- Preserve survivor targets while removing links to a deleted lane that shares its new number.
- Rename several IDs without sequential replacement collisions.
- Preserve validity membership or report that it is not representable.
- Undo the initial edit and all automatic corrections together.
- Preserve unrelated and unknown XML exactly in meaning.
