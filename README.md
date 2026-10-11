# your-sloppy-viewer

A STEP and DXF viewer for Linux. GTK 4 / libadwaita, native Wayland.

![Shaded view](docs/screenshot-shaded.png)

![Wireframe view with orientation cube and dimensions](docs/screenshot-wireframe.png)

## Features

- Opens `.step` / `.stp` models, with colours; open by file picker, drag-and-drop or command line
- Opens 2D `.dxf` drawings (lines, arcs, circles, polylines, text, layer colours) in a flat top view
- Render styles: shaded with edges, shaded, wireframe
- Orthographic and perspective projection
- Orbit (left drag), pan (middle or right drag), zoom toward the cursor (wheel or pinch)
- Standard views, fit to window, clickable orientation cube
- Overall dimensions readout
- Loads in the background with a progress bar; idle when nothing changes

Keyboard shortcuts are listed in the app under Main Menu → Keyboard Shortcuts.

## Build and install

Dependencies (Fedora):

    sudo dnf install gcc-c++ make cmake ninja-build opencascade-devel gtk4-devel libadwaita-devel libepoxy-devel

Build and run without installing:

    make build
    ./build/your-sloppy-viewer model.step

Install for the current user (into `~/.local`) and make it the default app for STEP and DXF files:

    make install

`make uninstall` removes it. `make test` runs the camera checks.

## License

MIT
