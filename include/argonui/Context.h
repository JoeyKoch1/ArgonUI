// ArgonUI - Context.h
//
// The single object an application talks to. Owns the frame's DrawList, the
// input snapshot, the id stack, the layout cursor, the style stack, the
// persistent widget state map, the animation store and the font family.
//
// Widgets are free functions in argon::ui that operate on the *current*
// Context (like every immediate-mode GUI people already know how to use), but
// everything is also reachable through an explicit `Context&` so two contexts
// can coexist - which matters for overlay code that renders both a standalone
// menu window and an in-game HUD.
#pragma once

#include <cstdarg>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "Anim/Anim.h"
#include "Backend/Backend.h"
#include "Core/Log.h"
#include "Core/Rect.h"
#include "Draw/DrawList.h"
#include "Input/Input.h"
#include "Text/Font.h"
#include "Theme/Style.h"
#include "Theme/Theme.h"

namespace argon::ui {
enum class ItemFlags : u32 {
    None = 0,
    /// Don't register hover/active for this item (decorative geometry).
    NoInteraction = 1u << 0,
    /// Item is not clipped by the current clip rect.
    NoClip = 1u << 1,
    /// Keep the previous hovered id alive even though this item is hovered.
    /// (Sliders: the rail and the thumb must not fight over the hot id.)
    AllowOverlap = 1u << 2,
    /// Don't clear the active id on release (drags that survive release).
    KeepsActive = 1u << 3,
};

/// Outcome of registering an item's bounds with the immediate-mode system.
struct ItemResult {
    Rect bounds;
    bool hovered = false;      ///< Pointer is over this item *and* nothing is above it.
    bool hoveredExact = false; ///< Pointer is over this item, ignoring overlap rules.
    bool held = false;         ///< Mouse is down and this item owns it.
    bool pressed = false;      ///< Went down this frame on this item.
    bool released = false;     ///< Came up this frame.
    bool clicked = false;      ///< pressed && released within bounds.
    bool doubleClicked = false;
    bool dragging = false;     ///< held and the pointer moved past the threshold.
    Vec2 dragDelta{0.0f, 0.0f};   ///< Movement since the previous frame, while dragging.
    Vec2 dragTotal{0.0f, 0.0f};   ///< Total movement since press.
    Vec2 pressPos{0.0f, 0.0f};    ///< Where the press happened (sliders jump-to-click).
    f32 heldDuration = 0.0f;
};

/// Per-widget persistent state. ArgonUI keys this by the widget's id, so it
/// survives across frames without the application storing anything.
struct WidgetState {
    Rect lastBounds;
    bool hoveredLast = false;
    bool heldLast = false;
    bool opened = false;   ///< combo open, section expanded, context menu visible
    bool flag = false;     ///< secondary bool (toggle value, checkbox value)
    f32 value = 0.0f;      ///< primary float (slider, scroll offset, progress)
    f32 value2 = 0.0f;     ///< secondary float (tab indicator, drag origin)
    Vec2 vec;              ///< primary vec2 (scroll velocity, indicator centre)
    Vec2 vec2;             ///< secondary vec2 (indicator extent)
    u32 payload = 0;       ///< int payload (selection index, item count)
    f32 timer = 0.0f;      ///< seconds spent in the current state
};

struct ContextConfig {
    f32 dpiScale = 1.0f;
    /// Directories to search for TTFs, e.g. "C:/Assets/Fonts".
    const char* fontFamilyDir = nullptr;
    /// Upload a frame's DrawList to the backend inside Render().
    bool submitAutomatically = true;
    /// Soft caps; the draw lists reserve based on them.
    u32 vertexCapacity = 262144;
    u32 indexCapacity = 393216;
    /// Draw ArgonUI's built-in diagnostics overlay on top of everything.
    bool showMetrics = false;
    bool verboseLogging = false;
};

class Context {
public:
    Context();
    ~Context();
    Context(const Context&) = delete;
    Context& operator=(const Context&) = delete;

    // -- Current instance --------------------------------------------------
    /// Thread-local current context. Widget free functions use this.
    static Context* Current();
    static void SetCurrent(Context* ctx);
    /// Asserts a current context exists. Returns a null-safe singleton in
    /// release builds so a stray widget call can never crash the host.
    static Context& Get();

    // -- Lifetime ----------------------------------------------------------
    /// `backend` must outlive the context (or be transferred via SetBackend).
    bool Initialize(IArgonBackend* backend, const ContextConfig& config = {});
    void Shutdown();

    /// Hand ownership of the backend to the context. Null detaches.
    void SetBackend(IArgonBackend* backend);
    IArgonBackend* Backend() const { return backend_; }

    // -- Frame -------------------------------------------------------------
    /// Must be called once per frame before any widget.
    void NewFrame(const FrameState& state);
    /// Finalises the draw list and, when configured, submits it.
    void Render();
    const DrawList& GetDrawList() const { return drawList_; }
    DrawList& MutableDrawList() { return drawList_; }

    /// Index checkpoint for DrawList::InsertBackgroundBox. Capture it in a
    /// Begin function and use it in the matching End function to paint a
    /// container's background behind content whose height is only known then.
    u32 DrawCheckpoint() const { return drawList_.IndexCheckpoint(); }

    const FrameState& Frame() const { return frame_; }
    f32 DeltaTime() const { return frame_.deltaTime; }
    f32 Time() const { return frame_.time; }
    u32 FrameIndex() const { return frame_.frameIndex; }

    // -- Input -------------------------------------------------------------
    InputState& Input() { return input_; }
    const InputState& Input() const { return input_; }
    void SetInput(const InputState& state) { input_ = state; }

    bool IsMouseHoveringRect(const Rect& r) const;
    bool IsMouseHoveringRect(const Rect& r, bool clipped) const;
    bool IsAnyMouseClicked() const;
    bool IsMouseDragging() const { return dragDistance_ > dragThreshold_; }
    bool IsMouseDragging(MouseButton b) const {
        return activeMouseButton_ == b && IsMouseDragging();
    }
    f32 MouseDragThreshold() const { return dragThreshold_; }

    /// True when the pointer is over an ArgonUI window and no popup is blocking.
    bool WantsCaptureMouse() const;
    /// True when a text field owns the keyboard.
    bool WantsCaptureKeyboard() const;

    // -- Ids ---------------------------------------------------------------
    void PushID(const char* id);
    void PushID(const std::string& id);
    void PushID(int id);
    void PushID(const void* ptr);
    void PopID();
    int IDStackDepth() const { return static_cast<int>(idStack_.size()); }

    /// Stable hash of `str` within the current id scope.
    u32 GetID(const char* str) const;
    u32 GetID(const char* str, const char* suffix) const;
    u32 GetID(int i) const;

    u32 HoveredId() const { return hoveredId_; }
    u32 ActiveId() const { return activeId_; }
    u32 FocusId() const { return focusId_; }
    void SetActiveId(u32 id) { activeId_ = id; }
    void ClearActiveId() { activeId_ = 0; }
    void SetFocusId(u32 id) { focusId_ = id; }
    void ClearFocusId() { focusId_ = 0; }
    /// Requests keyboard focus for whatever item declares its id this frame.
    void SetKeyboardFocusHere();
    bool WantsFocusRequest() const { return wantsFocus_; }
    bool IsItemFocused() const { return lastItemId_ != 0 && lastItemId_ == focusId_; }

    // -- Layout ------------------------------------------------------------

    /// Pushes a layout region (absolute rect, full content region, or
    /// content-region minus a size) and returns its index.
    void BeginLayout(const Rect& contentRect);
    void BeginLayoutInContent(f32 width, f32 height);
    void EndLayout();

    Rect ContentRegion() const;
    Rect ContentRegionAvail() const;
    f32 ContentWidth() const { return ContentRegionAvail().Width(); }
    f32 ContentHeight() const { return ContentRegionAvail().Height(); }
    f32 ContentWidthMax() const { return ContentRegion().Width(); }
    f32 ContentHeightMax() const { return ContentRegion().Height(); }
    Vec2 ContentRegionCenter() const { return ContentRegion().GetCenter(); }

    void SetCursorPos(const Vec2& p);
    void SetCursorPosX(f32 x);
    void SetCursorPosY(f32 y);
    Vec2 CursorPos() const;
    f32 CursorPosX() const { return CursorPos().x; }
    f32 CursorPosY() const { return CursorPos().y; }
    /// Cursor position relative to the innermost layout origin.
    Vec2 CursorPosLocal() const;

    void SameLine(f32 offsetFromStartX = 0.0f, f32 spacing = -1.0f);
    void NewLine();
    void Spacing();
    void Spacing(f32 count);
    void Dummy(const Vec2& size);

    /// Full cursor state, including line bookkeeping.
    /// Rows and split layouts use this to draw an inline control and then put
    /// the cursor back exactly where it was.
    struct CursorState {
        Vec2 cursor;
        f32 lineStartX, lineY, lineHeight, prevItemEndX, prevItemHeight;
        bool hasPrevItem;
    };
    CursorState SaveCursor() const;
    void RestoreCursor(const CursorState& s);
    void Indent(f32 indent = 0.0f);
    void Unindent(f32 indent = 0.0f);
    f32 IndentX() const;
    void Separator();
    void SeparatorText(const char* label);

    /// Reserves horizontal space so the *next* SameLine'd item lines up.
    void DummySameLine(const Vec2& size);

    // -- Items -------------------------------------------------------------
    /// Registers `bounds` under `id` and resolves hover/press/release.
    ///
    /// Hover is resolved by draw order and layer: a widget in a higher layer
    /// (a popup, a modal) always wins over whatever was under it, and the
    /// widget drawn first on a layer keeps the hover. `AllowOverlap` opts out
    /// of the exclusivity rule - the slider rail and its thumb need it.
    ItemResult ItemBehavior(u32 id, const Rect& bounds, ItemFlags flags = ItemFlags::None);
    /// Moves the cursor past `size` (leaves the x cursor untouched).
    void ItemSize(const Vec2& size, f32 textBaselineOffsetY = 0.0f);
    /// Sets the layout cursor as if a zero-size item were placed there.
    /// Used by split layouts to pin a control to the right edge of a row.
    void SetItemCursorScreenPos(const Vec2& p);

    bool IsItemHovered() const { return lastItem_.hovered; }
    bool IsItemActive() const { return lastItem_.held; }
    bool IsItemClicked() const { return lastItem_.clicked; }

    // -- Z-order -----------------------------------------------------------
    /// Opens a higher draw layer. Popups, context menus and modals call this
    /// so their widgets win the hover/click test over the content beneath.
    u32 PushZ();
    void PopZ();
    u32 CurrentZ() const { return z_; }

    // -- State store -------------------------------------------------------
    WidgetState& State(u32 id);
    bool HasState(u32 id) const;
    f32 StateF(u32 id, f32 fallback = 0.0f);
    void SetStateF(u32 id, f32 v);
    bool StateB(u32 id, bool fallback = false);
    void SetStateB(u32 id, bool v);
    int StateI(u32 id, int fallback = 0);
    void SetStateI(u32 id, int v);
    Vec2 StateV(u32 id);
    void SetStateV(u32 id, const Vec2& v);
    /// Seconds the item has been hovered continuously (0 when not hovered).
    f32 HoverDuration(u32 id) const;

    /// Animated value that springs toward `target`. Creates the slot on first
    /// use, seeded at `target` so nothing pops on the first frame.
    f32 AnimFloat(u32 id, f32 target, f32 rate);
    /// 0 -> 1 helper. `duration` wins when > 0, otherwise `rate` is used.
    f32 AnimTo(u32 id, f32 target, f32 duration);
    Vec2 AnimVec2(u32 id, const Vec2& target, f32 rate);
    /// Current animated value without advancing it.
    f32 AnimValue(u32 id) const;

    // -- Clipping ----------------------------------------------------------
    void PushClipRect(const Rect& rect);
    void PushClipRectFullscreen();   ///< Clip to the current layout region.
    void PopClipRect();
    const Rect& ClipRect() const { return drawList_.CurrentClip(); }

    // -- Style -------------------------------------------------------------
    Style& GetStyle() { return style_; }
    const Style& GetStyle() const { return style_; }
    const Theme& GetTheme() const { return style_.Theme(); }
    void SetTheme(const Theme& theme);
    /// Installs a preset: "dark", "light", "midnight".
    void SetThemePreset(const char* name);

    void PushStyleColor(StyleColorId id, const Color& c);
    void PushStyleVar(StyleVarId id, f32 v);
    void PopStyleColor(int count = 1);
    void PopStyleVar(int count = 1);
    Color Color(StyleColorId id) const { return style_.GetColor(id); }
    f32 Var(StyleVarId id) const { return style_.GetVar(id); }
    f32 Alpha() const { return style_.Alpha(); }

    // -- Text --------------------------------------------------------------
    FontManager& Fonts() { return fonts_; }
    Font* FontAt(f32 pixelSize);
    Font* SemiboldFontAt(f32 pixelSize);
    Font* MonoFontAt(f32 pixelSize);
    Font* CurrentFont() { return currentFont_; }
    void SetCurrentFont(Font* font) { currentFont_ = font; }

    /// Width/height of `text` in the given font (defaults to CurrentFont).
    Vec2 CalcTextSize(const char* text, Font* font = nullptr) const;
    f32 CalcTextSizeX(const char* text, Font* font = nullptr) const;
    f32 CalcTextHeight(const char* text, Font* font = nullptr) const;

    /// Draws `text` with its top-left at `pos`. Returns the advance.
    f32 AddText(const Vec2& pos, const char* text, const Color& color,
                Font* font = nullptr);
    /// Draws `text` centred in `bounds` (both axes). Honours style alpha.
    f32 AddTextCentered(const Rect& bounds, const char* text, const Color& color,
                       Font* font = nullptr, bool centerVertically = true);
    /// Draws `text` right-aligned to `bounds.max.x`.
    f32 AddTextRightAligned(const Rect& bounds, const char* text, const Color& color,
                            Font* font = nullptr, bool centerVertically = true);

    void Text(const char* fmt, ...);
    void TextV(const char* fmt, va_list args);
    void TextColored(const Color& c, const char* fmt, ...);
    void TextDisabled(const char* fmt, ...);
    void TextWrapped(const char* text);

    // -- Primitives --------------------------------------------------------
    void AddBox(const Rect& r, const Color& fill, const CornerRadius& radius = CornerRadius(),
                f32 borderWidth = 0.0f, const Color& borderColor = Color(0, 0, 0, 0),
                f32 glowWidth = 0.0f, const Color& glowColor = Color(0, 0, 0, 0),
                u32 vertexFlags = VF_FLAT);
    void AddLine(const Vec2& a, const Vec2& b, f32 thickness, const Color& c);
    void AddCircle(const Rect& r, const Color& fill);
    void AddImage(const Rect& r, u32 textureId, const Color& tint = Color(1, 1, 1, 1),
                  const CornerRadius& radius = CornerRadius());

    // -- Textures ----------------------------------------------------------
    /// Uploads a texture and returns a handle usable with AddImage.
    u32 RegisterTexture(const TextureDesc& desc, const void* pixels);
    void UpdateTexture(u32 handle, const TextureDesc& desc, const void* pixels);
    void UnregisterTexture(u32 handle);
    /// Registers a texture from raw RGB(A) bytes, premultiplying nothing.
    u32 RegisterTextureFromPixels(u32 width, u32 height, const u8* rgba,
                                  TextureFormat format = TextureFormat::RGBA8);

    // -- Metrics / diagnostics --------------------------------------------
    const DrawStats& DrawStatsSnapshot() const { return drawList_.Stats(); }
    f32 CpuFrameMs() const { return cpuFrameMs_; }

    /// Turns the built-in diagnostics overlay on or off at runtime.
    void SetMetricsVisible(bool visible) { metricsVisible_ = visible; }
    bool MetricsVisible() const { return metricsVisible_ || config_.showMetrics; }

    /// Injected by Backends/Null and the built-in metrics overlay.
    void SetCpuFrameMs(f32 ms) { cpuFrameMs_ = ms; }

private:
    struct LayoutEntry {
        Rect content;          ///< Absolute rect this layout stacks items in.
        Vec2 cursor;           ///< Where the next item goes.
        f32 lineStartX;        ///< Left edge of the current line.
        f32 lineY;             ///< Top of the current line.
        f32 lineHeight;        ///< Tallest item on the current line so far.
        f32 prevItemEndX;      ///< X just past the previous item on this line.
        f32 prevItemHeight;    ///< Height of the previous item on this line.
        bool hasPrevItem;
        f32 itemSpacingX;
        f32 itemSpacingY;
    };

    void ComputeEdgeStates();
    void UpdateDurations(f32 dt);
    static u32 Fnv1a(const char* str, u32 seed);
    void RenderMetricsOverlay();

    // -- Owned state -------------------------------------------------------
    IArgonBackend* backend_ = nullptr;
    bool ownsBackend_ = false;
    ContextConfig config_;
    FrameState frame_;

    DrawList drawList_;
    InputState input_;
    Style style_;
    FontManager fonts_;
    Font* currentFont_ = nullptr;

    std::vector<u32> idStack_;
    std::vector<LayoutEntry> layoutStack_;
    std::vector<Rect> clipStack_;
    std::unordered_map<u32, WidgetState> states_;
    std::unordered_map<u32, AnimEntry> anims_;
    std::unordered_map<u32, AnimVec2Entry> animVec2_;
    std::vector<u32> ownedTextures_;

    // -- Per-frame ---------------------------------------------------------
    u32 hoveredId_ = 0;
    u32 hoveredIdPrev_ = 0;
    u32 activeId_ = 0;
    u32 focusId_ = 0;
    u32 lastItemId_ = 0;
    u32 lastItemIdPrev_ = 0;
    bool wantsFocus_ = false;

    /// Current draw layer and the layer that won the hover this frame.
    u32 z_ = 0;
    u32 hoverWinnerZ_ = 0;
    std::vector<u32> zStack_;

    u32 activeMouseButton_ = Mouse_Left;
    f32 dragDistance_ = 0.0f;
    Vec2 dragStartPos_{0.0f, 0.0f};
    Vec2 dragPrevPos_{0.0f, 0.0f};
    f32 dragThreshold_ = 4.0f;
    bool mouseDownPrev_[Mouse_COUNT] = {false, false, false, false, false};
    f32 mouseDownTime_[Mouse_COUNT] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    f32 lastClickTime_[Mouse_COUNT] = {-10.0f, -10.0f, -10.0f, -10.0f, -10.0f};
    Vec2 lastClickPos_[Mouse_COUNT];

    f32 cpuFrameMs_ = 0.0f;
    bool metricsVisible_ = false;

    ItemResult lastItem_;
    ItemResult itemPrev_;

    /// Blocks popups/menus from stealing input (an open modal, a drag in
    /// owns the pointer).
    int inputLock_ = 0;

    Rect popupCapture_{};         ///< Published by an open popup, last frame.
    Rect nextPopupCapture_{};     ///< Being published by an open popup, this frame.
    bool hasPopupCapture_ = false;
    bool hasNextPopupCapture_ = false;
    bool popupDismiss_ = false;

public:
    void PushInputLock() { ++inputLock_; }
    void PopInputLock() { if (inputLock_ > 0) --inputLock_; }
    bool IsInputLocked() const { return inputLock_ > 0; }

    // -- Popup dismissal --------------------------------------------------
    //
    // Popups cannot resolve "clicked outside?" in the frame they open, because
    // everything under them has already processed input by then. So an open
    // popup publishes its bounds, and the *next* NewFrame turns a click
    // outside those bounds into a global press suppression before a single
    // widget runs. The visible effect is that clicking off a combo closes it
    // without also pressing the widget underneath.
    //
    // Call once per frame while a popup is open. The last call wins, which is
    // correct for the single-popup-at-a-time model ArgonUI uses.
    void SetPopupCapture(const Rect& bounds) {
        nextPopupCapture_ = bounds;
        hasNextPopupCapture_ = true;
    }

    /// True when the click this frame landed outside the popup that was open
    /// last frame. Widgets that own a popup close it; every widget suppresses
    /// its press.
    bool PopupDismissRequested() const { return popupDismiss_; }

    /// True while any popup was open at the start of this frame.
    bool PopupWasOpen() const { return hasPopupCapture_; }
};

} // namespace argon::ui
