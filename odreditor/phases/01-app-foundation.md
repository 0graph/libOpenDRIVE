# Phase 01 App foundation

Status: planned. Dependencies: none.

## Deliverable

A standalone C++ application shell that builds into one HTML and establishes the light ImGui design.

## Work

1. Create editor-owned CMake targets and a dependency-pin manifest.
2. Integrate the parent libOpenDRIVE public target without changing its source or root build.
3. Add Dear ImGui, SDL2/OpenGL3 browser backend, and the minimal application loop.
4. Add a UTF-8 HTML shell with embedded WASM, font, and initial resources.
5. Define the shared theme and basic menu/tree/viewport/inspector/status layout.
6. Implement file-open/drop and download bridge stubs with explicit byte ownership.
7. Verify input capture, resize, DPI scaling, and context setup.

## Decisions

- C++17 initially.
- Use the established SDL2/OpenGL3 Emscripten example as the integration reference.
- Fixed split panes first; no mandatory docking or desktop multi-window support.
- No JavaScript UI framework or separate JavaScript document model.
- No mandatory worker or threading requirement for the shell.
- Resolve and pin actual dependency/toolchain versions here.

## Acceptance

- Build from `odreditor/` using a separate `.codex/build/` directory.
- Open the resulting HTML directly and through static hosting.
- With networking disabled, the UI and fonts still load without adjacent assets.
- The interface matches the density and palette of [UI](../UI.md).
- Pointer/keyboard input in a panel does not activate the viewport.
- Existing native and WASM targets remain available without source edits.

## Verification

One temporary browser/packaging probe under `.codex/`; inspect the runtime request list.
Record the tested toolchain in editor build configuration.
Promote only the useful offline packaging integration into the small release check later.

## References

[Build](../BUILD.md) and [Engineering](../ENGINEERING.md).
