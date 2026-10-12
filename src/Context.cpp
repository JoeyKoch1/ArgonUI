#include "argonui/Context.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace argon::ui {
namespace {
thread_local Context* g_currentContext = nullptr;

constexpr f32 kMaxDeltaTime = 0.25f;  // 250 ms clamp; a hitch must not teleport animations
constexpr f32 kDoubleClickInterval = 0.35f;
constexpr f32 kDoubleClickRadius = 6.0f;

LayoutEntry MakeLayoutEntry(const Rect& content, f32 spacingX, f32 spacingY) {
    LayoutEntry e{};
    e.content = content;
    e.cursor = content.min;
    e.lineStartX = content.min.x;
    e.lineY = content.min.y;
    e.lineHeight = 0.0f;
    e.prevItemEndX = content.min.x;
    e.prevItemHeight = 0.0f;
    e.hasPrevItem = false;
    e.itemSpacingX = spacingX;
    e.itemSpacingY = spacingY;
    return e;
}
} // namespace

// Current instance

Context* Context::Current() { return g_currentContext; }
void Context::SetCurrent(Context* ctx) { g_currentContext = ctx; }

Context& Context::Get() {
    if (g_currentContext != nullptr) return *g_currentContext;
    // Never crash the host because of a stray widget call. The context is
    // inert (no backend, no frame) but all accessors are safe.
    static Context orphan;
    return orphan;
}

// Lifetime

Context::Context() {
    idStack_.reserve(64);
    layoutStack_.reserve(64);
    states_.reserve(1024);
    anims_.reserve(512);
    drawList_.Reserve(16384, 24576, 256);
    layoutStack_.push_back(MakeLayoutEntry(Rect(0, 0, 1, 1), 8.0f, 5.0f));
    clipStack_.reserve(64);
}

Context::~Context() { Shutdown(); }

bool Context::Initialize(IArgonBackend* backend, const ContextConfig& config) {
    config_ = config;
    backend_ = backend;

    if (!fonts_.Initialize(config.fontFamilyDir, config.dpiScale)) {
        ARGON_WARN("Context", "font initialisation failed; text will be invisible");
    }
    currentFont_ = fonts_.Regular();

    if (config.dpiScale != 1.0f) {
        style_.Reset(Theme::Dark());
    }

    drawList_.Reserve(config.vertexCapacity, config.indexCapacity, 512);
    style_.ClearStack();

    layoutStack_.clear();
    layoutStack_.push_back(MakeLayoutEntry(Rect(0, 0, 1, 1),
                                            style_.GetVar(StyleVarId::ItemSpacingX),
                                            style_.GetVar(StyleVarId::ItemSpacingY)));

    if (g_currentContext == nullptr) g_currentContext = this;
    return true;
}

void Context::Shutdown() {
    // Release GPU-side resources *before* the backend goes away.
    if (backend_ != nullptr) {
        fonts_.ReleaseTextures(*backend_);
        for (u32 tex : ownedTextures_) backend_->DestroyTexture(tex);
    }
    ownedTextures_.clear();

    if (ownsBackend_ && backend_ != nullptr) delete backend_;
    backend_ = nullptr;
    ownsBackend_ = false;
}

void Context::SetBackend(IArgonBackend* backend) {
    if (backend_ != nullptr && backend_ != backend) {
        fonts_.ReleaseTextures(*backend_);
        for (u32 tex : ownedTextures_) backend_->DestroyTexture(tex);
        ownedTextures_.clear();
        if (ownsBackend_) delete backend_;
        backend_ = nullptr;
        ownsBackend_ = false;
    }
    backend_ = backend;
}

// Frame

void Context::NewFrame(const FrameState& state) {
    frame_ = state;
    if (frame_.deltaTime <= 0.0f || frame_.deltaTime > kMaxDeltaTime) {
        frame_.deltaTime = kMaxDeltaTime;
    }

    drawList_.Reset();
    ComputeEdgeStates();
    UpdateDurations(frame_.deltaTime);

    // Resolve popup dismissal before any widget runs this frame.
    popupDismiss_ = false;
    hasNextPopupCapture_ = false;
    if (hasPopupCapture_ && input_.IsMouseClicked(Mouse_Left) &&
        !popupCapture_.Contains(input_.mousePos)) {
        popupDismiss_ = true;
    }

    hoveredIdPrev_ = hoveredId_;
    hoveredId_ = 0;
    hoverWinnerZ_ = 0;
    wantsFocus_ = false;
    lastItem_ = ItemResult{};
    itemPrev_ = lastItem_;
    zStack_.clear();
    z_ = 0;
    inputLock_ = 0;

    // Root layout spans the display.
    layoutStack_.clear();
    layoutStack_.push_back(MakeLayoutEntry(Rect(0, 0, frame_.displaySize.x, frame_.displaySize.y),
                                            style_.GetVar(StyleVarId::ItemSpacingX),
                                            style_.GetVar(StyleVarId::ItemSpacingY)));

    style_.ClearStack();

    fonts_.SetDpiScale(frame_.dpiScale);
    if (backend_ != nullptr) {
        fonts_.SyncTextures(*backend_);
    }
}

void Context::Render() {
    // An open popup publishes its bounds for next frame's dismissal test.
    popupCapture_ = nextPopupCapture_;
    hasPopupCapture_ = hasNextPopupCapture_;

    drawList_.Finalize();

    if (config_.showMetrics || metricsVisible_) {
        RenderMetricsOverlay();
        drawList_.Finalize();
    }

    if (backend_ == nullptr) return;
    if (!config_.submitAutomatically) return;

    if (!backend_->BeginFrame(frame_)) return;
    backend_->Render(drawList_, frame_);
    // Adopted targets return false here: the host presents, we do not.
    backend_->Present();
    backend_->EndFrame();
}

void Context::ComputeEdgeStates() {
    InputState& in = input_;
    const f32 now = frame_.time;

    in.mousePrevPos = in.mousePos;
    in.mouseDelta = Vec2(0.0f, 0.0f);

    // The host fills mousePos/wheel/mouseDown; everything below is derived.
    // (Host contract: set mouseDown[] every frame; do not set the edge flags.)
    in.mouseMoved = (in.mousePos.x != in.mousePrevPos.x) || (in.mousePos.y != in.mousePrevPos.y);

    for (int b = 0; b < Mouse_COUNT; ++b) {
        in.mouseClicked[b] = false;
        in.mouseReleased[b] = false;
        in.mouseDoubleClicked[b] = false;
        in.releasePos[b] = Vec2(0.0f, 0.0f);

        const bool down = in.mouseDown[b];
        const bool wasDown = mouseDownPrev_[b];

        if (down && !wasDown) {
            in.mouseClicked[b] = true;
            in.clickPos[b] = in.mousePos;
            mouseDownTime_[b] = now;
            in.mouseDownDuration[b] = 0.0f;
            if (now - lastClickTime_[b] < kDoubleClickInterval &&
                (in.mousePos - lastClickPos_[b]).Size().x <= kDoubleClickRadius &&
                (in.mousePos - lastClickPos_[b]).Size().y <= kDoubleClickRadius) {
                in.mouseDoubleClicked[b] = true;
                lastClickTime_[b] = now - kDoubleClickInterval;  // no triple-click report
            } else {
                lastClickTime_[b] = now;
            }
            lastClickPos_[b] = in.mousePos;
        } else if (!down && wasDown) {
            in.mouseReleased[b] = true;
            in.releasePos[b] = in.mousePos;
            in.mouseDownDuration[b] = now - mouseDownTime_[b];
        } else if (down) {
            in.mouseDownDuration[b] = now - mouseDownTime_[b];
        }
        mouseDownPrev_[b] = down;
    }

    // Drag bookkeeping.
    if (activeId_ == 0) {
        for (int b = 0; b < Mouse_COUNT; ++b) {
            if (in.mouseClicked[b]) {
                activeMouseButton_ = static_cast<MouseButton>(b);
                dragStartPos_ = in.mousePos;
                dragPrevPos_ = in.mousePos;
                dragDistance_ = 0.0f;
                break;
            }
        }
    } else {
        const Vec2 total = in.mousePos - dragStartPos_;
        dragDistance_ = std::sqrt(total.x * total.x + total.y * total.y);
        dragPrevPos_ = in.mousePos;
    }
}

void Context::UpdateDurations(f32 dt) {
    (void)dt;
    for (auto& kv : states_) {
        kv.second.timer += dt;
    }
}

// Ids

u32 Context::Fnv1a(const char* str, u32 seed) {
    u32 h = seed;
    if (str == nullptr) return h;
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(str); *p != 0; ++p) {
        h ^= static_cast<u32>(*p);
        h *= 16777619u;
    }
    // Zero is reserved for "no id".
    return h == 0u ? 1u : h;
}

void Context::PushID(const char* id) {
    u32 seed = idStack_.empty() ? 0x811C9DC5u : idStack_.back();
    idStack_.push_back(Fnv1a(id, seed));
}
void Context::PushID(const std::string& id) { PushID(id.c_str()); }

void Context::PushID(int id) {
    u32 seed = idStack_.empty() ? 0x811C9DC5u : idStack_.back();
    u32 h = seed ^ 0x9E3779B9u ^ static_cast<u32>(id);
    h *= 16777619u;
    idStack_.push_back(h == 0u ? 1u : h);
}

void Context::PushID(const void* ptr) {
    u32 seed = idStack_.empty() ? 0x811C9DC5u : idStack_.back();
    u32 h = seed ^ static_cast<u32>(reinterpret_cast<uintptr_t>(ptr) & 0xFFFFFFFFu);
    h *= 16777619u;
    idStack_.push_back(h == 0u ? 1u : h);
}

void Context::PopID() {
    if (!idStack_.empty()) idStack_.pop_back();
}

u32 Context::GetID(const char* str) const {
    const u32 seed = idStack_.empty() ? 0x811C9DC5u : idStack_.back();
    return Fnv1a(str, seed);
}

u32 Context::GetID(const char* str, const char* suffix) const {
    u32 h = GetID(str);
    return Fnv1a(suffix, h);
}

u32 Context::GetID(int i) const {
    u32 seed = idStack_.empty() ? 0x811C9DC5u : idStack_.back();
    u32 h = seed ^ 0x9E3779B9u ^ static_cast<u32>(i);
    h *= 16777619u;
    return h == 0u ? 1u : h;
}

// Layout

void Context::BeginLayout(const Rect& contentRect) {
    LayoutEntry& parent = layoutStack_.back();
    const f32 sx = parent.itemSpacingX;
    const f32 sy = parent.itemSpacingY;
    const Rect clipped = IntersectStrict(contentRect, parent.content);
    layoutStack_.push_back(MakeLayoutEntry(clipped, sx, sy));
}

void Context::BeginLayoutInContent(f32 width, f32 height) {
    const Rect avail = ContentRegionAvail();
    const Rect r(avail.min.x, avail.min.y,
                 width > 0.0f ? avail.min.x + width : avail.max.x,
                 height > 0.0f ? avail.min.y + height : avail.max.y);
    BeginLayout(r);
}

void Context::EndLayout() {
    if (layoutStack_.size() <= 1) return;  // never pop the root
    // Copy before popping: taking a reference here would dangle.
    const LayoutEntry done = layoutStack_.back();
    layoutStack_.pop_back();
    LayoutEntry& parent = layoutStack_.back();
    parent.cursor.x = Max(parent.cursor.x, done.content.max.x);
    parent.cursor.y = Max(parent.cursor.y, done.content.max.y);
}

Rect Context::ContentRegion() const { return layoutStack_.back().content; }

Rect Context::ContentRegionAvail() const {
    const LayoutEntry& e = layoutStack_.back();
    return Rect(e.cursor.x, e.lineY, e.content.max.x, e.content.max.y);
}

void Context::SetCursorPos(const Vec2& p) {
    LayoutEntry& e = layoutStack_.back();
    e.cursor = p;
    e.lineStartX = p.x;
    e.lineY = p.y;
    e.lineHeight = 0.0f;
    e.prevItemEndX = p.x;
    e.hasPrevItem = false;
}

void Context::SetCursorPosX(f32 x) {
    Vec2 p = layoutStack_.back().cursor;
    p.x = x;
    SetCursorPos(p);
}

void Context::SetCursorPosY(f32 y) {
    Vec2 p = layoutStack_.back().cursor;
    p.y = y;
    SetCursorPos(p);
}

Vec2 Context::CursorPos() const { return layoutStack_.back().cursor; }

Context::CursorState Context::SaveCursor() const {
    const LayoutEntry& e = layoutStack_.back();
    return CursorState{e.cursor,      e.lineStartX, e.lineY,   e.lineHeight,
                       e.prevItemEndX, e.prevItemHeight, e.hasPrevItem};
}

void Context::RestoreCursor(const CursorState& s) {
    LayoutEntry& e = layoutStack_.back();
    e.cursor = s.cursor;
    e.lineStartX = s.lineStartX;
    e.lineY = s.lineY;
    e.lineHeight = s.lineHeight;
    e.prevItemEndX = s.prevItemEndX;
    e.prevItemHeight = s.prevItemHeight;
    e.hasPrevItem = s.hasPrevItem;
}

Vec2 Context::CursorPosLocal() const {
    const LayoutEntry& e = layoutStack_.back();
    return e.cursor - e.content.min;
}

void Context::SameLine(f32 offsetFromStartX, f32 spacing) {
    LayoutEntry& e = layoutStack_.back();
    if (!e.hasPrevItem) return;
    const f32 s = spacing >= 0.0f ? spacing : e.itemSpacingX;
    e.cursor.x = offsetFromStartX > 0.0f ? (e.lineStartX + offsetFromStartX) : (e.prevItemEndX + s);
    e.cursor.y = e.lineY;
    e.lineHeight = Max(e.lineHeight, e.prevItemHeight);
}

void Context::NewLine() {
    LayoutEntry& e = layoutStack_.back();
    const f32 advance = e.lineHeight > 0.0f ? e.lineHeight + e.itemSpacingY : e.itemSpacingY;
    e.cursor.y = e.lineY + advance;
    e.lineStartX = e.content.min.x;
    e.lineY = e.cursor.y;
    e.cursor.x = e.lineStartX;
    e.lineHeight = 0.0f;
    e.prevItemEndX = e.lineStartX;
    e.prevItemHeight = 0.0f;
    e.hasPrevItem = false;
}

void Context::ItemSize(const Vec2& size, f32) {
    LayoutEntry& e = layoutStack_.back();

    // Auto-wrap when the item would overflow the content width.
    if (e.hasPrevItem && e.cursor.x > e.lineStartX &&
        e.cursor.x + size.x > e.content.max.x + kEpsilon) {
        NewLine();
    }

    e.cursor.x += size.x;
    e.lineHeight = Max(e.lineHeight, size.y);
    e.prevItemEndX = e.cursor.x;
    e.prevItemHeight = size.y;
    e.hasPrevItem = true;
}

void Context::SetItemCursorScreenPos(const Vec2& p) {
    LayoutEntry& e = layoutStack_.back();
    e.cursor = p;
    e.lineStartX = p.x;
    e.lineY = p.y;
    e.lineHeight = 0.0f;
    e.prevItemEndX = p.x;
    e.hasPrevItem = false;
}

void Context::Spacing() { ItemSize(Vec2(0.0f, style_.GetVar(StyleVarId::GapSmall))); NewLine(); }

void Context::Spacing(f32 count) {
    for (f32 i = 0.0f; i < count; ++i) Spacing();
}

void Context::Dummy(const Vec2& size) {
    ItemSize(size);
    NewLine();
}

void Context::DummySameLine(const Vec2& size) {
    LayoutEntry& e = layoutStack_.back();
    if (!e.hasPrevItem) return;
    const f32 target = e.prevItemEndX + e.itemSpacingX + size.x;
    e.cursor.x = target;
    e.lineHeight = Max(e.lineHeight, size.y);
    e.prevItemEndX = target;
    e.prevItemHeight = size.y;
}

void Context::Indent(f32 indent) {
    if (indent <= 0.0f) indent = style_.GetVar(StyleVarId::GapLarge);
    LayoutEntry& e = layoutStack_.back();
    e.cursor.x += indent;
    e.lineStartX += indent;
    e.content.min.x += indent;
}

void Context::Unindent(f32 indent) {
    if (indent <= 0.0f) indent = style_.GetVar(StyleVarId::GapLarge);
    LayoutEntry& e = layoutStack_.back();
    e.cursor.x -= indent;
    e.lineStartX -= indent;
    e.content.min.x -= indent;
}

f32 Context::IndentX() const { return layoutStack_.back().cursor.x - layoutStack_.back().content.min.x; }

void Context::SeparatorText(const char* label) {
    Separator();
    if (label != nullptr && *label != '\0' && currentFont_ != nullptr) {
        const f32 t = style_.GetVar(StyleVarId::SeparatorThickness);
        const f32 y = layoutStack_.back().lineY;
        const Vec2 size = currentFont_->MeasureText(label, std::strlen(label));
        const Rect pill(layoutStack_.back().lineStartX, y + t,
                        layoutStack_.back().lineStartX + size.x + 10.0f,
                        y + t + size.y + 6.0f);
        drawList_.AddBox(pill, style_.GetColor(StyleColorId::WindowBg), CornerRadius(5.0f));
        AddText(Vec2(pill.min.x + 5.0f, pill.min.y + 3.0f + currentFont_->Ascent()), label,
                style_.GetColor(StyleColorId::TextMuted), currentFont_);
        ItemSize(Vec2(pill.Width(), pill.Height()));
        NewLine();
    }
}

void Context::Separator() {
    const LayoutEntry& e = layoutStack_.back();
    const f32 w = e.content.Width();
    const f32 t = style_.GetVar(StyleVarId::SeparatorThickness);
    const Rect r(e.lineStartX, e.lineY + e.itemSpacingY, e.lineStartX + w,
                 e.lineY + e.itemSpacingY + t);
    drawList_.AddLine(Vec2(r.min.x, r.GetCenter().y), Vec2(r.max.x, r.GetCenter().y), t,
                      style_.GetColor(StyleColorId::Separator));
    ItemSize(Vec2(w, t + style_.GetVar(StyleVarId::GapSmall) * 2.0f));
    NewLine();
}

// Items

u32 Context::PushZ() {
    zStack_.push_back(z_);
    ++z_;
    return z_;
}

void Context::PopZ() {
    if (zStack_.empty()) return;
    z_ = zStack_.back();
    zStack_.pop_back();
    if (z_ > 0) --z_;
}

ItemResult Context::ItemBehavior(u32 id, const Rect& bounds, ItemFlags flags) {
    ItemResult r{};
    r.bounds = bounds;

    const bool noInteraction = (flags & ItemFlags::NoInteraction) != 0;
    const bool allowOverlap = (flags & ItemFlags::AllowOverlap) != 0;

    r.hoveredExact = !noInteraction && IsMouseHoveringRect(bounds, (flags & ItemFlags::NoClip) == 0);

    // Hover arbitration: the first claim on a layer wins, but a higher layer
    // always takes over. Popups and modals then feel modal without any
    // special-casing in the widget code: the modal's backdrop simply claims
    // hover at a higher z before its content draws.
    if (r.hoveredExact) {
        const bool canTake = hoveredId_ == 0 || hoveredId_ == id || hoveredId_ == hoveredIdPrev_ ||
                             allowOverlap || z_ > hoverWinnerZ_;
        if (canTake) {
            hoveredId_ = id;
            hoverWinnerZ_ = z_;
            r.hovered = true;
        }
    }

    // An input lock (an open modal, a drag that owns the pointer) or a popup
    // dismissal suppresses presses, never hovers, so the widget under the
    // cursor still reacts visually.
    const bool locked = (inputLock_ > 0 || popupDismiss_) && !allowOverlap;

    if (!noInteraction && !locked && input_.IsMouseClicked(Mouse_Left) && r.hoveredExact) {
        activeId_ = id;
        activeMouseButton_ = Mouse_Left;
        dragStartPos_ = input_.clickPos[Mouse_Left];
        dragPrevPos_ = input_.mousePos;
        dragDistance_ = 0.0f;
    }

    r.pressed = (activeId_ == id) && input_.IsMouseClicked(Mouse_Left);
    r.held = (activeId_ == id) && input_.IsMouseDown(Mouse_Left);
    r.released = (activeId_ == id) && input_.IsMouseReleased(Mouse_Left);
    r.doubleClicked = input_.IsMouseDoubleClicked(Mouse_Left) &&
                      (activeId_ == id || activeId_ == 0) && r.hoveredExact;
    r.heldDuration = r.held ? (frame_.time - mouseDownTime_[Mouse_Left]) : 0.0f;
    r.pressPos = (activeId_ == id) ? input_.clickPos[Mouse_Left] : input_.mousePos;

    r.dragging = r.held && (dragDistance_ > dragThreshold_);
    r.dragDelta = r.dragging ? input_.mouseDelta : Vec2(0.0f, 0.0f);
    r.dragTotal = r.held ? (input_.mousePos - dragStartPos_) : Vec2(0.0f, 0.0f);

    r.clicked = r.released && bounds.Contains(input_.releasePos[Mouse_Left]);

    // Release clears the active id unless the widget asked to keep it.
    if (r.released && (flags & ItemFlags::KeepsActive) == 0) {
        activeId_ = 0;
    }

    lastItem_ = r;
    lastItemId_ = id;
    lastItemIdPrev_ = id;
    return r;
}

bool Context::IsMouseHoveringRect(const Rect& r) const { return IsMouseHoveringRect(r, true); }

bool Context::IsMouseHoveringRect(const Rect& r, bool clipped) const {
    if (r.IsEmpty()) return false;
    Rect test = r;
    if (clipped) {
        test = IntersectStrict(r, drawList_.CurrentClip());
        if (test.Area() <= 0.0f) return false;
    }
    return test.Contains(input_.mousePos);
}

bool Context::IsAnyMouseClicked() const {
    for (int i = 0; i < Mouse_COUNT; ++i) {
        if (input_.mouseClicked[i]) return true;
    }
    return false;
}

bool Context::WantsCaptureMouse() const { return hoveredId_ != 0 || activeId_ != 0; }
bool Context::WantsCaptureKeyboard() const { return focusId_ != 0; }

void Context::SetKeyboardFocusHere() { wantsFocus_ = true; }

// State store

WidgetState& Context::State(u32 id) {
    auto it = states_.find(id);
    if (it == states_.end()) {
        WidgetState s{};
        s.lastBounds = Rect(0, 0, 0, 0);
        it = states_.emplace(id, s).first;
    }
    return it->second;
}

bool Context::HasState(u32 id) const { return states_.find(id) != states_.end(); }

f32 Context::StateF(u32 id, f32 fallback) {
    auto it = states_.find(id);
    return it == states_.end() ? fallback : it->second.value;
}
void Context::SetStateF(u32 id, f32 v) { State(id).value = v; }

bool Context::StateB(u32 id, bool fallback) {
    auto it = states_.find(id);
    return it == states_.end() ? fallback : it->second.flag;
}
void Context::SetStateB(u32 id, bool v) { State(id).flag = v; }

int Context::StateI(u32 id, int fallback) {
    auto it = states_.find(id);
    return it == states_.end() ? fallback : static_cast<int>(it->second.payload);
}
void Context::SetStateI(u32 id, int v) { State(id).payload = static_cast<u32>(v); }

Vec2 Context::StateV(u32 id) {
    auto it = states_.find(id);
    return it == states_.end() ? Vec2(0.0f, 0.0f) : it->second.vec;
}
void Context::SetStateV(u32 id, const Vec2& v) { State(id).vec = v; }

f32 Context::HoverDuration(u32 id) const {
    auto it = states_.find(id);
    if (it == states_.end()) return 0.0f;
    return it->second.hoveredLast ? it->second.timer : 0.0f;
}

f32 Context::AnimFloat(u32 id, f32 target, f32 rate) {
    AnimEntry& a = anims_[id];
    return AdvanceAnim(a, target, frame_.deltaTime, rate, 0.0f, EaseCubicOut);
}

f32 Context::AnimTo(u32 id, f32 target, f32 duration) {
    AnimEntry& a = anims_[id];
    return AdvanceAnim(a, target, frame_.deltaTime, style_.GetVar(StyleVarId::ExpandRate),
                       duration, EaseCubicOut);
}

Vec2 Context::AnimVec2(u32 id, const Vec2& target, f32 rate) {
    AnimVec2Entry& a = animVec2_[id];
    if (!a.seeded) a.Seed(target);
    a.target = target;
    const f32 k = DampFactor(frame_.deltaTime, rate);
    a.value.x = Lerp(a.value.x, target.x, k);
    a.value.y = Lerp(a.value.y, target.y, k);
    if (NearlyEqual(a.value.x, target.x, 0.05f) && NearlyEqual(a.value.y, target.y, 0.05f)) {
        a.value = target;
    }
    return a.value;
}

f32 Context::AnimValue(u32 id) const {
    auto it = anims_.find(id);
    return it == anims_.end() ? 0.0f : it->second.value;
}

// Clipping

void Context::PushClipRect(const Rect& rect) {
    clipStack_.push_back(drawList_.PushClip(rect));
}

void Context::PushClipRectFullscreen() {
    clipStack_.push_back(drawList_.PushClip(layoutStack_.back().content));
}

void Context::PopClipRect() {
    if (!clipStack_.empty()) drawList_.PopClip(clipStack_.back());
    if (!clipStack_.empty()) clipStack_.pop_back();
}

// Style

void Context::SetTheme(const Theme& theme) { style_.Reset(theme); }

void Context::SetThemePreset(const char* name) {
    if (name == nullptr) return;
    if (std::strcmp(name, "light") == 0)      SetTheme(Theme::Light());
    else if (std::strcmp(name, "midnight") == 0) SetTheme(Theme::Midnight());
    else                                      SetTheme(Theme::Dark());
}

void Context::PushStyleColor(StyleColorId id, const Color& c) { style_.PushColor(id, c); }
void Context::PushStyleVar(StyleVarId id, f32 v) { style_.PushVar(id, v); }
void Context::PopStyleColor(int count) { style_.PopColors(count); }
void Context::PopStyleVar(int count) { style_.PopVars(count); }

// Fonts and text

Font* Context::FontAt(f32 pixelSize) { return fonts_.AtSize(pixelSize); }
Font* Context::SemiboldFontAt(f32 pixelSize) { return fonts_.SemiboldAtSize(pixelSize); }
Font* Context::MonoFontAt(f32 pixelSize) { return fonts_.MonoAtSize(pixelSize); }

Vec2 Context::CalcTextSize(const char* text, Font* font) const {
    if (font == nullptr) font = currentFont_;
    if (font == nullptr || text == nullptr) return Vec2(0.0f, 0.0f);
    return font->MeasureText(text, std::strlen(text));
}

f32 Context::CalcTextSizeX(const char* text, Font* font) const {
    return CalcTextSize(text, font).x;
}

f32 Context::CalcTextHeight(const char* text, Font* font) const {
    if (font == nullptr) font = currentFont_;
    return font != nullptr ? font->LineHeight() : 0.0f;
}

f32 Context::AddText(const Vec2& pos, const char* text, const Color& color, Font* font) {
    // Apply the style alpha like the other primitives do. Without this a
    // PushStyleVar(Alpha, ...) fades the shapes around a label but not the
    // label itself, which reads as a rendering bug rather than a fade.
    const Color draw = color.WithAlpha(color.a * Alpha());

    if (font == nullptr) font = currentFont_;
    if (font == nullptr || text == nullptr || *text == '\0') return 0.0f;
    if (draw.a <= 0.0f) return 0.0f;

    const u32 texId = fonts_.TextureIdOf(font);
    if (texId == 0) {
        // Atlas not uploaded yet (headless context, or first frame before the
        // backend existed). Measuring is still correct; drawing is skipped.
        return font->MeasureLine(text, std::strlen(text));
    }

    const DrawList::BatchState saved = drawList_.SaveBatchState();
    drawList_.SetTexture(texId, CmdFlags::FontAtlas);

    const size_t length = std::strlen(text);
    Vec2 pen(pos.x, pos.y);  // pen.y is the baseline

    u32 prevCp = 0;
    size_t i = 0;
    while (i < length) {
        const u32 cp = DecodeUtf8(text, length, i);
        if (prevCp != 0) pen.x += font->Kerning(prevCp, cp);

        const Glyph* g = font->GetGlyph(cp);
        if (g != nullptr) {
            if (!g->isWhitespace && g->width > 0.0f && g->height > 0.0f) {
                // Snap to whole pixels horizontally: the atlas is sampled with
                // a linear filter and a fractional origin would smear it.
                const f32 gx = std::floor(pen.x + g->bearingX + 0.5f);
                const f32 gy = pen.y + g->bearingY;
                drawList_.AddGlyphQuad(Rect(gx, gy, gx + g->width, gy + g->height), g->uv, draw);
            }
            pen.x += g->advanceX;
        }
        prevCp = cp;
    }

    drawList_.RestoreBatchState(saved);
    return pen.x - pos.x;
}

f32 Context::AddTextCentered(const Rect& bounds, const char* text, const Color& color,
                             Font* font, bool centerVertically) {
    if (font == nullptr) font = currentFont_;
    if (font == nullptr || text == nullptr) return 0.0f;
    const Vec2 size = font->MeasureText(text, std::strlen(text));
    const Vec2 p(bounds.GetCenter().x - size.x * 0.5f,
                 bounds.min.y + (centerVertically ? (bounds.Height() - size.y) * 0.5f : 0.0f) +
                     font->Ascent());
    return AddText(p, text, color, font);
}

f32 Context::AddTextRightAligned(const Rect& bounds, const char* text, const Color& color,
                                 Font* font, bool centerVertically) {
    if (font == nullptr) font = currentFont_;
    if (font == nullptr || text == nullptr) return 0.0f;
    const Vec2 size = font->MeasureText(text, std::strlen(text));
    const f32 x = bounds.max.x - size.x;
    const f32 y = bounds.min.y +
                  (centerVertically ? (bounds.Height() - size.y) * 0.5f : 0.0f) + font->Ascent();
    return AddText(Vec2(x, y), text, color, font);
}

void Context::TextV(const char* fmt, va_list args) {
    if (fmt == nullptr) return;
    char stackBuf[1024];
    va_list copy;
    va_copy(copy, args);
    const int needed = std::vsnprintf(stackBuf, sizeof(stackBuf), fmt, copy);
    va_end(copy);
    if (needed < 0) return;

    const char* text = stackBuf;
    std::string heap;
    if (static_cast<size_t>(needed) >= sizeof(stackBuf)) {
        heap.resize(static_cast<size_t>(needed) + 1);
        std::vsnprintf(&heap[0], heap.size(), fmt, args);
        text = heap.c_str();
    }

    Font* font = currentFont_;
    if (font == nullptr) return;
    const Vec2 size = font->MeasureText(text, std::strlen(text));
    LayoutEntry& e = layoutStack_.back();
    const Rect bounds(e.lineStartX, e.lineY, e.lineStartX + size.x, e.lineY + size.y);

    AddText(Vec2(bounds.min.x, bounds.min.y + font->Ascent()), text,
            style_.GetColor(StyleColorId::Text), font);
    ItemSize(size);
}

void Context::Text(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    TextV(fmt, args);
    va_end(args);
}

void Context::TextColored(const Color& c, const char* fmt, ...) {
    PushStyleColor(StyleColorId::Text, c);
    va_list args;
    va_start(args, fmt);
    TextV(fmt, args);
    va_end(args);
    PopStyleColor(1);
}

void Context::TextDisabled(const char* fmt, ...) {
    PushStyleColor(StyleColorId::Text, style_.GetColor(StyleColorId::TextDisabled));
    va_list args;
    va_start(args, fmt);
    TextV(fmt, args);
    va_end(args);
    PopStyleColor(1);
}

void Context::TextWrapped(const char* text) {
    Font* font = currentFont_;
    if (font == nullptr || text == nullptr) return;

    const LayoutEntry& e = layoutStack_.back();
    const f32 width = e.content.max.x - e.lineStartX;
    std::vector<std::pair<size_t, size_t>> lines;
    font->WrapText(text, std::strlen(text), width, lines);

    f32 y = e.lineY;
    for (const auto& line : lines) {
        AddText(Vec2(e.lineStartX, y + font->Ascent()), text + line.first,
                style_.GetColor(StyleColorId::Text), font);
        y += font->LineHeight();
    }
    ItemSize(Vec2(Min(width, font->MeasureLine(text, std::strlen(text))),
                     y - e.lineY));
    NewLine();
}

// Primitives

void Context::AddBox(const Rect& r, const Color& fill, const CornerRadius& radius, f32 borderWidth,
                     const Color& borderColor, f32 glowWidth, const Color& glowColor,
                     u32 vertexFlags) {
    drawList_.AddBox(r, fill.WithAlpha(fill.a * Alpha()), radius, borderWidth,
                     borderColor.WithAlpha(borderColor.a * Alpha()), glowWidth,
                     glowColor.WithAlpha(glowColor.a * Alpha()), vertexFlags);
}

void Context::AddLine(const Vec2& a, const Vec2& b, f32 thickness, const Color& c) {
    drawList_.AddLine(a, b, thickness, c.WithAlpha(c.a * Alpha()));
}

void Context::AddCircle(const Rect& r, const Color& fill) {
    drawList_.AddCircle(r, fill.WithAlpha(fill.a * Alpha()));
}

void Context::AddImage(const Rect& r, u32 textureId, const Color& tint, const CornerRadius& radius) {
    drawList_.AddImage(r, textureId, tint.WithAlpha(tint.a * Alpha()), radius);
}

// Textures

u32 Context::RegisterTexture(const TextureDesc& desc, const void* pixels) {
    if (backend_ == nullptr) return 0;
    const u32 handle = backend_->CreateTexture(desc, pixels);
    if (handle != 0) ownedTextures_.push_back(handle);
    return handle;
}

void Context::UpdateTexture(u32 handle, const TextureDesc& desc, const void* pixels) {
    if (backend_ == nullptr || handle == 0) return;
    backend_->UpdateTexture(handle, desc, pixels);
}

void Context::UnregisterTexture(u32 handle) {
    if (backend_ == nullptr || handle == 0) return;
    backend_->DestroyTexture(handle);
    ownedTextures_.erase(
        std::remove(ownedTextures_.begin(), ownedTextures_.end(), handle),
        ownedTextures_.end());
}

u32 Context::RegisterTextureFromPixels(u32 width, u32 height, const u8* rgba,
                                       TextureFormat format) {
    TextureDesc desc;
    desc.width = width;
    desc.height = height;
    desc.format = format;
    return RegisterTexture(desc, rgba);
}

// Diagnostics

void Context::RenderMetricsOverlay() {
    const DrawStats& s = drawList_.Stats();

    char buf[384];
    std::snprintf(buf, sizeof(buf),
                  "ArgonUI  frame %u  %.2f ms\n"
                  "draw calls %u   quads %u   verts %u\n"
                  "clips %u   merged %u   upload %.1f KB\n"
                  "backend %s   api %s",
                  frame_.frameIndex, cpuFrameMs_, s.drawCalls, s.quads, s.vertices,
                  s.clipRegions, s.batchesMerged,
                  static_cast<double>(drawList_.UploadBytes()) / 1024.0,
                  backend_ != nullptr ? backend_->Name() : "-",
                  backend_ != nullptr ? GraphicsApiName(backend_->Info().api) : "-");

    Font* font = fonts_.MonoAtSize(FontManager::kSizeSmall);
    if (font == nullptr) return;
    const Vec2 size = font->MeasureText(buf, std::strlen(buf));
    const Rect box(frame_.displaySize.x - size.x - 24.0f, frame_.displaySize.y - size.y - 24.0f,
                   frame_.displaySize.x - 8.0f, frame_.displaySize.y - 8.0f);

    drawList_.AddBox(box, Color(0, 0, 0, 0.72f * Alpha()), CornerRadius(6.0f), 1.0f,
                     Color(1, 1, 1, 0.10f * Alpha()));
    AddText(Vec2(box.min.x + 10.0f, box.min.y + 10.0f + font->Ascent()), buf,
            Color(0.4f, 1.0f, 0.55f, Alpha()), font);
}

} // namespace argon::ui
