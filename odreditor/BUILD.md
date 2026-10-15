# Build and dependencies

## Build contract

| Requirement | Design |
| --- | --- |
| Main implementation | C++17 baseline, compatible with the existing library. |
| Browser target | Emscripten, WebGL 2, embedded WASM. |
| Shipping artifact | One UTF-8 `odreditor.html`. |
| Offline use | No CDN, runtime font fetch, external script, or separate WASM/data file. |
| File access | Browser file selection/drop and download; tiny JS bridge. |
| Native build | Useful for debugging core algorithms; browser remains the delivery target. |
| Build entry | Standalone `odreditor/CMakeLists.txt`. |
| Scratch output | Repository-root `.codex/build/odreditor/`. |

Opening the HTML directly with file:// is a release acceptance target, verified in selected browsers.
HTTPS/static hosting also works. Advanced filesystem APIs are optional conveniences.

## Selected baseline

| Dependency | Use | Adoption rule |
| --- | --- | --- |
| libOpenDRIVE | Geometry, surfaces, sampling, native diagnostics. | Consume existing public target/API through OdrAdapter. |
| pugixml | Authoritative document, XML edits, save. | Reuse the existing dependency; avoid a second XML model. |
| Dear ImGui | C++ application UI. | Use standard demo patterns and centralized light theme. |
| SDL2 plus OpenGL 3 backend | Window/input integration and browser-compatible rendering. | Follow the maintained ImGui Emscripten example. |
| GLM | Camera matrices, transforms, picking mathematics. | Keep XML/geometric calculations in double precision where needed. |
| ImGuizmo | Translation/rotation handles and scene editing. | Integrate when visual tools arrive; adapt float matrices at the viewport boundary. |

Candidate additions only when justified:

- Clothoids or another established fitter for constrained spiral/connector fitting.
- A profile plotting library if native ImGui drawing would duplicate substantial interaction logic.
- A spatial index or broader renderer if measured map sizes exceed the simple renderer's needs.

Pin releases or commits in editor-owned build files. Select actual pins during phase 01; do not track floating branches.
Record required attribution and dependency notices with the app.

## Rendering boundary

- Dear ImGui supplies UI, not a full 3D scene engine.
- Begin with a small C++ renderer for libOpenDRIVE mesh buffers, lines, overlays, and picking.
- Use existing libraries for matrices and gizmos instead of implementing those primitives again.
- Use a local render origin to avoid precision loss with large map coordinates.
- Keep source coordinates unchanged; converting them to GPU float buffers is a display operation.
- A heavier graphics library is acceptable when it removes demonstrated complexity and supports embedded WebAssembly deployment.

## Standalone CMake

- Accept a libOpenDRIVE source path; default to the parent checkout.
- Add that source directory with a separate binary directory and EXCLUDE_FROM_ALL, then link `OpenDrive::OpenDrive`.
- Do not maintain a copied list of upstream source files.
- Build the editor target explicitly; leave unrelated upstream tools/tests out of the requested target.
- Allow an existing compatible installed target as an alternate integration path.
- Keep dependency flags target-scoped and settings editor-prefixed.
- No root CMake, README, or existing workflow changes are required for the initial editor.
- If later CI is useful, add an editor-specific workflow rather than rewriting the native workflow.

Proposed commands after phase 01 implements the target:

```sh
emcmake cmake -S odreditor -B .codex/build/odreditor -DCMAKE_BUILD_TYPE=Release
cmake --build .codex/build/odreditor --target OdrEditor
```

## Single HTML packaging

- Link an HTML executable with `-sSINGLE_FILE=1`.
- Embed shaders, fonts, icons, and required notices.
- Keep the custom HTML shell small and include UTF-8 metadata.
- Use the pinned Emscripten version's supported SDL/WebGL settings.
- Keep memory growth and exception settings consistent with libOpenDRIVE.
- Avoid mandatory pthreads, SharedArrayBuffer, server headers, or a service worker.
- If a preview worker is added, embed its source and payload; test offline startup separately.
- Debug builds may produce extra artifacts under `.codex/`; the release dependency set is one HTML.

Emscripten's SINGLE_FILE setting embeds generated subresources. The app must also embed its own assets and avoid adding external runtime dependencies. [Emscripten settings](https://emscripten.org/docs/tools_reference/settings_reference.html#single-file)

## Browser bridge

| Operation | Bridge responsibility |
| --- | --- |
| Open/drop | Obtain file bytes and pass them into C++; retain ownership rules explicitly. |
| Save | Receive serialized bytes and initiate a download from a user action. |
| Clipboard | Optional interaction initiated by the user, with browser-supported fallback. |
| Resize/DPI | Notify the C++ application of display changes. |

No parallel JavaScript map model, correction engine, or UI framework is planned.

## Packaging acceptance

- Open the HTML with networking disabled and no adjacent app assets.
- Load a local XODR, perform an edit, save, and reopen the download.
- Verify runtime requests contain no required external app resources.
- Test file:// and hosted launch in Chromium and Firefox; document any actual support limits.
- Exercise WebGL context recreation and repeated document open/close.
- Rebuild from the pinned inputs without depending on a developer's global packages.

## Primary references

- [Dear ImGui](https://github.com/ocornut/imgui)
- [ImGui SDL2 OpenGL3 Emscripten example](https://github.com/ocornut/imgui/tree/master/examples/example_sdl2_opengl3)
- [ImGuizmo](https://github.com/CedricGuillemet/ImGuizmo)
- [GLM](https://github.com/g-truc/glm)
- [Clothoids fitting library](https://github.com/ebertolazzi/Clothoids)
