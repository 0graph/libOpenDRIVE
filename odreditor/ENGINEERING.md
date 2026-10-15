# Engineering requirements

## Scope and maintenance

- Build around libOpenDRIVE and keep editor changes under `odreditor/`.
- Do not edit README files or unrelated documentation/configuration.
- Do not reformat existing upstream files or alter their naming conventions.
- Keep upstream adaptation in OdrAdapter.
- Prefer a narrow public API improvement over a broad native rewrite when a change is unavoidable.
- Keep such a native change separate and independently reviewable.
- Do not introduce reflection, a framework, or a generic abstraction merely to shorten declarations.

## C++ style

| Topic | Requirement |
| --- | --- |
| Helper names | UpperCamelCase, following the requested examples: GetPredecessorLane, RebuildReferenceLine. |
| Name meaning | State the operation and subject; avoid vague helpers such as ProcessData or HandleThing. |
| Control flow | Prefer short-circuit checks, early returns, and shallow nesting. |
| Boolean expressions | Keep evaluation order obvious; do not hide mutations inside clever expressions. |
| Comments | At most 1-2 lines, only for genuinely unclear behavior or constraints. |
| Block comments | No long explanatory blocks; put design material in these specifications. |
| Ownership | RAII, explicit owners, values/NodeIds at UI boundaries. |
| Mutations | Commands and transactions; no panel-specific mutation paths. |
| Errors | Clear results/diagnostics; catch exceptions at the native adapter boundary. |
| Numeric data | Preserve source strings until edited; finite checks and explicit unit conversion for calculations. |

Use ordinary C++ syntax and focused functions. Reuse working library facilities.
Existing libOpenDRIVE API names remain unchanged when called from the adapter.

## UI implementation

- Follow [UI](UI.md).
- Centralize colors, sizes, spacing, and typography.
- Reuse shared controls and existing ImGui components.
- No long paragraphs, redundant blurbs, arbitrary dividers, nested cards, or inconsistent inline controls.
- Keep technical details in optional diagnostics, not routine user flows.
- Numeric fields and viewport tools must use the same commands.

## Temporary files

| Material | Location |
| --- | --- |
| Requested permanent specs and phase sheets | `odreditor/` |
| Permanent editor code and necessary assets | `odreditor/` |
| Temporary tests and generated fixtures | Repository-root `.codex/tests/` |
| Research notes, scratch docs, scripts | Repository-root `.codex/research/` and `.codex/scripts/` |
| Captures and temporary logs | Repository-root `.codex/captures/` and `.codex/logs/` |
| Build/dependency experiments | Repository-root `.codex/build/` |

Do not scatter scratch files in the repository root, source tree, or unrelated temporary directories.
Do not commit temporary material. Follow the active filesystem permissions for `.codex/`.
The user-supplied image under `odreditor/reference/` is a permanent design reference.

## Permanent test policy

Keep tests few and behavior-focused. A durable test must cover an algorithm, an integration boundary, or a failure likely to return after other changes.

| Area | Durable coverage worth keeping |
| --- | --- |
| Lane remapping | Signed IDs, scoped targets, contact points, deleted-target aliasing, validity ranges. |
| Document/history | Preservation, atomic failure, undo of dependent changes, identity lifetime. |
| Native adapter | Cache reconstruction, node/mesh association, incompatible-input containment. |
| Geometry algorithms | Known analytic results, transform invariants, solver constraints, parameter conversions. |
| Packaging | Single-file offline open/edit/save integration. |

- Use table-driven cases where they express the same behavior across inputs.
- Assert independently specified outcomes; do not calculate expected results with the code under test.
- Include failure, boundary, and nontrivial inputs; do not tune fixtures merely to pass.
- Check that unrelated XML is unchanged in meaning.
- Reuse fixtures and setup; avoid repeating the same assertions in several suites.
- Use tolerances justified by units and algorithms; avoid broad tolerances that hide failures.
- Add a regression case for a real defect only when it protects a meaningful contract.
- Use temporary checks for simple control wiring, labels, styling, and reversible low-risk edits.
- No permanent generic smoke-test file or snapshot suite for every UI panel.

## Verification by phase

- Each phase sheet names the small set of checks needed for its behavior.
- Reuse earlier integration cases instead of adding equivalent tests in each phase.
- Run relevant native and WASM checks when an adapter or algorithm changes.
- Use sanitizers for meaningful ownership or invalidation changes.
- Inspect UI changes in the browser; temporary captures stay in `.codex/`.
- After relevant checks pass, broaden testing only for a new failure or unresolved concern.

## Upstream update procedure

1. Update the chosen libOpenDRIVE revision deliberately.
2. Build the unchanged upstream library and standalone editor.
3. Run the existing adapter contract checks and one representative browser integration.
4. Adapt editor-owned integration code first.
5. Document an actual incompatibility near its adapter code with a short comment if needed.

Do not maintain speculative version branches or compatibility shims for versions never supported.
