# Phase 08 Integration and release

Status: planned. Dependencies: phases 01 to 07.

## Deliverable

A verified single HTML editor with the documented correction scope and a narrow upstream dependency boundary.

## Work

1. Profile representative small, large, curved, and extension-bearing files.
2. Optimize actual bottlenecks: dirty previews, sampling, picking, tree virtualization, or worker execution.
3. Verify document preservation after mixed edits and dependent corrections.
4. Exercise repeated open/close, undo/redo, deletion while selected, and preview failures.
5. Build against the pinned library baseline and a deliberately chosen newer upstream revision.
6. Resolve adapter incompatibilities in editor-owned code first.
7. Produce the embedded release HTML and include required dependency notices.
8. Verify the light UI at normal/high DPI and with lengthy properties/diagnostics.

## Release gates

| Area | Required result |
| --- | --- |
| Packaging | One HTML; no required adjacent or remote application resources. |
| Browser launch | Tested file:// and static-hosted operation in the selected supported browsers. |
| File workflow | Open/drop -> edit -> save -> reopen works offline. |
| Corrections | Lane/reference identity is preserved and every correction is undoable. |
| Compatibility | Invalid or unsupported XML remains editable and saveable. |
| Preview | Current revision is identifiable; stale results cannot overwrite newer edits. |
| Upstream changes | Integration assumptions remain isolated in OdrAdapter/build glue. |
| UI | Cohesive light theme and shared components; no explanatory clutter. |
| Repository | No unrelated source, README, or formatting changes. |

## Verification

- Run the small algorithm and integration suite established by earlier phases.
- Keep one end-to-end offline packaging integration rather than multiple overlapping smoke tests.
- Use sanitizers when ownership or invalidation behavior changed.
- Store temporary fixtures, profiling output, logs, and captures under repository-root `.codex/`.
- Do not add permanent performance or UI snapshot suites without a demonstrated regression need.

## Completion record

Record supported toolchain/dependency pins, tested browsers, format-specific correction coverage, and known preview gaps in editor-owned configuration/specification material.
Update these phase/spec documents as decisions change; do not create or edit README files.

## Future scope

Additional schema renderers, semantic validation tools, road split/merge/reversal, advanced fitting, and opt-in network repair.
The first release does not silently infer traffic intent or rewrite unknown extensions.
