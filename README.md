# StepView

A minimal, fast native STEP viewer for Linux. C++20, OpenCascade (OCCT) for STEP
import, tessellation and OpenGL rendering, GLFW for the window. No Qt, no web runtime.

## Requirements

Fedora packages (system versions are used as-is; developed against OCCT 7.9.3, GLFW 3.4):

    sudo dnf install gcc-c++ cmake ninja-build opencascade-devel glfw-devel \
                     mesa-libGL-devel libX11-devel

## Build

    cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
    cmake --build build --parallel

## Run

    ./build/stepview /path/to/model.step
    ./build/stepview                      # empty window, then drop a file onto it

`.step` and `.stp` are accepted (case-insensitive). Drop another file onto the window to
replace the current model. Load timings are printed to stdout:

    fixture.step: parse 5.6 ms | transfer 11.9 ms | tessellate 93.0 ms (23600 triangles) | first frame 12.7 ms

## Install

    make install

Builds, then installs into `~/.local` (override with `PREFIX=...`): the binary, the icon,
a `model/step` MIME type for `*.step` / `*.stp`, and a desktop entry, and makes StepView
the default application for STEP files. `make uninstall` removes the installed files.

## Controls

| Input              | Action                              |
| ------------------ | ----------------------------------- |
| Left drag          | Orbit (Z stays up)                  |
| Middle drag        | Pan                                 |
| Wheel              | Zoom toward the cursor              |
| `F`                | Fit model to window                 |
| `E`                | Toggle CAD edges                    |
| `P`                | Toggle perspective / orthographic   |
| `1` / `2` / `3`    | Front / right / top view            |
| `0`                | Isometric view                      |
| View cube          | Click a face, edge or corner        |
| `Esc`              | Quit                                |

Shortcuts use physical key positions, so they work regardless of keyboard layout.

## Test fixture

`tests/fixture.step` is a small coloured part generated with OCCT:

    cmake --build build --target make_fixture
    ./build/make_fixture tests/fixture.step

## Design notes

- `src/App.*` – GLFW window, input, drag-and-drop, event loop.
- `src/StepLoader.*` – STEP reading (`STEPCAFControl_Reader`, with colours) and tessellation.
- `src/Viewer.*` – OCCT viewer, styling, camera (`AIS_ViewController`).
- `src/GlfwOcctWindow.*` – `Aspect_Window` wrapper adapted from OCCT's `samples/glfw`
  (MIT-style notice preserved in those files).

GLFW is forced onto X11 (`GLFW_PLATFORM_X11`) because Fedora's OCCT is a GLX build; on a
GNOME Wayland session the window runs through XWayland.

The loop is event-driven (`glfwWaitEvents`); nothing is drawn while idle. Files load on a
worker thread, so the window stays responsive and keeps showing the previous model until
the new one is ready. The viewer and GL context are created once and reused across files.

Mesh density is chosen per model: linear deflection is 0.04 % of the bounding-box diagonal,
angular deflection ranges from 0.15 rad for small parts to 0.40 rad for models with many
faces, to keep triangle counts bounded on large assemblies.

## Limitations

- Requires X11 or XWayland; there is no native Wayland backend.
- No file picker yet: open files from the command line or by drag-and-drop.
- A load in progress cannot be cancelled; a file dropped meanwhile is opened afterwards.
- Assemblies are shown as a whole: no tree, per-part visibility, selection or measurement.
- STEP colours are applied; names, layers, PMI and materials are ignored.
- Very large files are bounded by OCCT's single-threaded STEP parser and its memory use
  (a 70 MB test file took about 13 s and 1 GB of RAM).
- On large models, finely curved small features can show faceting when zoomed in closely.
