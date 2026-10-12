# ArgonUI changelog

All notable changes to this project are recorded here.

## [0.1.0] - unreleased

First public shape of the library.

### Added

- Core draw list with greedy batch merging. Consecutive quads that share clip
  rect, texture, blend and flags collapse into a single batch.
- Single-SDF rendering model. One 64-byte vertex and one shader pipeline draw
  every primitive: panels, toggles, sliders, pills, icon strokes, glyphs and
  thumbnails all evaluate the same rounded-box signed distance field.
- `DrawList::InsertBackgroundBox`, which splices geometry into the index buffer
  so containers can paint a background behind content sized at End time.
- Backend abstraction separating device, target and present ownership, with
  Direct3D 11, Direct3D 12, Vulkan and OpenGL implementations plus a headless
  null backend for tests.
- stb_truetype text with per-size R8 glyph atlases baked on demand.
- Immediate-mode context: id stack, layout cursor, style stack, per-id state
  store and frame-rate-independent animation.
- Widget set: buttons, icon buttons, checkboxes, toggle switches, sliders,
  drag fields, text and search fields, combos, segmented controls, progress
  bars, badges, breadcrumbs, collapsible sections, windows, scroll areas, tab
  bars, sidebars, rows, grids, lists, modals, context menus and tooltips.
- Around 60 procedural stroke icons drawn from the same primitives as the rest
  of the UI. No icon font and no SVG rasteriser.
- Dark, light and midnight themes.
- Win32 demo application.
- CMake build with each backend as an independent target.

### Known limitations

- Rectangular clipping only. Windows with large corner radii can show a sliver
  of content at the extreme corners.
- Text input covers caret movement, UTF-8 insertion, backspace, delete and
  click-to-position. No selection, copy/paste or undo.
- `Arc` and the triangle primitives are real geometry rather than SDF
  primitives.
- The code has not been compiled or run yet.