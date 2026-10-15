# Phase 07 Geometry attachment

Status: planned. Dependencies: phases 03 to 06.

## Deliverable

Attach selected roads/segments through scoped alignment and fitting, with preview and undo.

## Implement in order

| Step | Operation | Constraint |
| --- | --- | --- |
| 1 | Snap endpoint position | Translate selected scope; preserve shape. |
| 2 | Align endpoint and tangent | Rigid XY transform of selected scope; preserve lengths and curvature. |
| 3 | RebuildReferenceLine | Propagate selected chain's segment starts, headings, stations, and total length. |
| 4 | Fit connector | Keep declared endpoints/tangents fixed; solve supported curve parameters. |
| 5 | Match vertical profile | Explicit elevation/slope fit over a selected interval. |
| 6 | Optional attach-on-connect | Opt-in command behavior using the same proven operations. |

Rigid alignment is the small first implementation. Fixed-endpoint fitting and curvature continuity require separate algorithms and failure handling.

## Endpoint convention

For tangent alignment, compare reference-line forward tangents:

| Anchor end | Movable end | Desired tangent relation |
| --- | --- | --- |
| Start | Start | Opposite. |
| Start | End | Same. |
| End | Start | Same. |
| End | End | Opposite. |

Use a shared convention helper; do not scatter sign fixes across UI handlers.

## Command scope

- User chooses the fixed anchor and movable road/chain.
- Transform all selected segment poses consistently.
- Road-relative objects follow the transformed road naturally.
- Include owned inertial-position records only when the selected operation defines that transform.
- Do not move connected neighboring roads automatically.
- State whether s-based records stay at their current s or undergo a requested remapping.
- Geometric attachment and traffic-link creation remain independently selectable.
- A failed fit leaves all XML unchanged.

## Fitting

- Start with line/arc constructions and supported cubic constraints.
- Evaluate an established clothoid library before implementing a solver.
- Expose G0/G1/G2 only when the selected method actually satisfies that condition.
- Bound solver work and reject non-finite, degenerate, or impossible candidates.
- Use documented position, angle, and curvature tolerances with appropriate units.
- Do not present a nearest-fit approximation as exact constraint satisfaction.

## Acceptance

- Rigid alignment preserves lengths, curvature, and relative point distances.
- All four endpoint combinations attach with the intended tangent direction.
- Rebuilding a chain updates only the selected scope.
- Undo restores geometry, profiles, and references changed by the command.
- Solver failure and cancellation are atomic.

## Verification

Keep analytical transform cases and independent constraint/invariant checks for fitting.
Include near-zero curvature, short segments, large coordinates, and infeasible constraints.
Reuse history/adapter tests rather than copying them into the geometry suite.

## Deferred

Network-wide relaxation, junction redesign, and arbitrary road split/merge/reversal until their transformations are specified separately.
