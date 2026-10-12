// ArgonUI - ArgonUI.h
// Single include for application code.
//
//     #include "argonui/ArgonUI.h"
//
// The core half of this header is GPU-API free. The backend half is behind
// `argonui/Backend/Registry.h`, which is included on demand.
#pragma once

#include "Core/Color.h"
#include "Core/Log.h"
#include "Core/Rect.h"
#include "Core/Types.h"
#include "Core/Version.h"

#include "Anim/Anim.h"

#include "Draw/DrawList.h"
#include "Draw/Vertex.h"

#include "Input/Input.h"

#include "Text/Font.h"

#include "Theme/Style.h"
#include "Theme/Theme.h"

#include "Widgets/Controls.h"
#include "Widgets/Icons.h"
#include "Widgets/Widgets.h"

#include "Context.h"

namespace argon::ui {

// ---------------------------------------------------------------------------
// Minimal integration sketch (see examples/demo for the full version):
//
//   argon::ui::Context ctx;
//   argon::ui::IArgonBackend* backend = argon::ui::CreateBackend(
//       argon::ui::GraphicsApi::D3D11);
//   ctx.Initialize(backend);
//
//   // each frame
//   argon::ui::FrameState fs;
//   fs.displaySize = { (float)w, (float)h };
//   fs.deltaTime   = dt;
//   ctx.NewFrame(fs);
//   ctx.BeginLayout(argon::ui::Rect(0, 0, (float)w, (float)h));
//   ...draw with the widget functions...
//   argon::ui::RenderTooltips(ctx);
//   ctx.EndLayout();
//   ctx.Render();            // uploads and presents
// ---------------------------------------------------------------------------

} // namespace argon::ui