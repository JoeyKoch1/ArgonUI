# ArgonUI documentation style

Rules every page in `docs/` follows. Keep them consistent so the reference reads
as one document rather than a pile of separate contributions.

## Accuracy is the only hard rule

Every signature must match the header it documents, character for character
including default arguments. Do not invent, rename, reorder or "improve" a
parameter. If you cannot find a symbol, it does not exist.

When a page documents a symbol, link the header it came from so a reader can
check you. GitBook resolves relative paths, so from `docs/api/foo.md` it is
`../../include/argonui/Draw/DrawList.h`.

## Entry format

Every public function, method, struct, enum and constant gets one entry:

````markdown
### `DrawList::AddBox`

```cpp
void AddBox(const Rect& bounds,
            const Color& fill,
            const CornerRadius& radius = CornerRadius(),
            f32 borderWidth = 0.0f,
            const Color& borderColor = Color(0, 0, 0, 0),
            f32 glowWidth = 0.0f,
            const Color& glowColor = Color(0, 0, 0, 0),
            u32 vertexFlags = VF_FLAT);
```

One or two sentences on what it does.

| Parameter | Type | Default | Meaning |
|---|---|---|---|
| `bounds` | `const Rect&` | | Screen-space rectangle in pixels. |
| `fill` | `const Color&` | | Interior colour. |
| `radius` | `const CornerRadius&` | `CornerRadius()` | Per-corner radii, clockwise from top-left. |
| `borderWidth` | `f32` | `0` | Inner border thickness in pixels. 0 draws no border. |
| `glowWidth` | `f32` | `0` | Outer glow spread in pixels. 0 draws no glow. |
| `vertexFlags` | `u32` | `VF_FLAT` | Any of the `VF_*` flags. |

**Returns:** nothing, it appends to the draw list.

{% hint style="info" %}
Glow expands the emitted quad by `glowWidth`, so it is not clipped away by the
caller's clip rect unless you account for it.
{% endhint %}
````

Rules for entries:

- Give defaults in the Parameters table, not just in the signature.
- **Returns:** is mandatory for anything non-void. Write "nothing" for void.
- Add a `{% hint %}` block only when there is a real gotcha: a lifetime rule, a
  pairing requirement, a unit, a performance note. An empty hint is noise.
- Example code goes under a `**Example**` heading or inside the hint. Do not
  put an example on every entry. Roughly one in eight.

## Prose

- Plain ASCII. No em dashes, no smart quotes, no box drawing. Use `-` or
  restructure the sentence.
- No "Note that", no "simply", no "just", no "this is what makes". Those read
  as filler. Write the fact instead.
- Do not restate the signature in prose. Explain what it is for, what it costs,
  and what goes wrong if you misuse it.
- Second person for instructions ("call `RenderTooltips` last"), third person
  for description ("the draw list merges adjacent quads").
- British or American spelling, pick one per page and stay there. Match the
  source: the headers use American ("colour" appears only where a name is
  fixed by an API), so use American.

## Page layout

```markdown
# Page title

One paragraph saying what this covers and who needs it.

## Section

### Symbol or heading

...

## See also

- [Link to the related page](relative/path.md)
```

- Start every page with a `#` H1.
- Always end with `## See also` linking to at least one other page.
- No page should be a stub. If you cannot write a full page, say so in your
  report rather than shipping three paragraphs.

## Cross references

- First mention of a type: `` `Rect` ``. After that, plain `Rect`.
- A method on a class: `Context::NewFrame`, not "NewFrame", unless the page is
  about `Context`.
- Link between API pages by relative path, never by heading anchor. Anchors
  break when someone renames a heading.