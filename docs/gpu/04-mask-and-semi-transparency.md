# 04 — Mask bit and semi-transparency: the six strategies

This is where our software rasteriser does per pixel, on the CPU, something a GPU pipeline has to be redesigned around.

## The problem in two lines

1. **Per-pixel semi-transparency.** The PS1 decides whether a pixel blends from the sampled texel's alpha bit (bit
   15), not from a polygon state.
2. **Per-pixel mask.** A marked pixel must not be overwritten by a later polygon that asks to check the mask.

Both are "read the destination and decide", which on a modern rasteriser means reading the framebuffer you are
writing.

## What the reference detects at startup (`gpu_hw.cpp:1330-1346`)

```cpp
m_allow_shader_blend = features.framebuffer_fetch ||
                       ((features.feedback_loops || features.raster_order_views) && ...);
m_prefer_shader_blend = (m_allow_shader_blend && g_gpu_settings.IsUsingShaderBlending());
m_use_rov_for_shader_blend = (m_allow_shader_blend && !features.framebuffer_fetch &&
                              features.raster_order_views && ...);
m_write_mask_as_depth = (!m_pgxp_depth_buffer && !features.framebuffer_fetch && !m_prefer_shader_blend);
```

| strategy | requirement | accuracy | cost |
|---|---|---|---|
| **A. fragment fetch** | `EXT_shader_framebuffer_fetch` / `GL_ARM_shader_framebuffer_fetch` | exact | free, no extra pass |
| **B. ROV** | `EXT_rasterizer_order_resources` (desktop) | exact | cheaper than a barrier |
| **C. feedback loop + barrier** | texture bound while it is the render target | exact | one barrier per draw ("Yucky", `gpu_hw.cpp:2559`) |
| **D. dual-source blend** | `SRC_ALPHA_SATURATE`-style second source | approximate, per primitive | free |
| **E. mask in the depth buffer** | any GLES with depth16 | mask exact (colour is not read) | free |
| **F. two passes** | nothing, plain GLES2 | approximate in ordering | 2x vertices, 2x fragments |

## A: fragment fetch, the path the Mali likes

`gpu_hw_shadergen.cpp:3102-3108` (the non-ROV variant):

```glsl
float4 bg_col = LAST_FRAG_COLOR;          // what the framebuffer already holds for this pixel
#if CHECK_MASK_BIT
  if (bg_col.a != 0.0)
    discard;
#endif
```

and the four PS1 modes in normalised space (`gpu_hw_shadergen.cpp:3133-3143`):

```glsl
#if TRANSPARENCY_MODE == 0   // half background + half foreground
  o_col0.rgb = (bg_col.rgb * 0.5) + (fg_col.rgb * 0.5);
#elif TRANSPARENCY_MODE == 1 // background + foreground
  o_col0.rgb = bg_col.rgb + fg_col.rgb;
#elif TRANSPARENCY_MODE == 2 // background - foreground
  o_col0.rgb = bg_col.rgb - fg_col.rgb;
#elif TRANSPARENCY_MODE == 3 // background + a quarter of the foreground
  o_col0.rgb = bg_col.rgb + (fg_col.rgb * 0.25);
#endif
```

The author's own note about Mali (`gpu_hw.cpp:1780-1790`):

```cpp
// TODO: This isn't entirely accurate, 127.5 versus 128.
// But if we use fbfetch on Mali, it doesn't matter.
```

Without fragment fetch, "half background" has to be approximated with a constant-colour blend (`0x808080`); with it,
the maths is exact. On our device this is the verified path (see `06`).

`LAST_FRAG_COLOR` is `o_col0` (`inout`) with `EXT_shader_framebuffer_fetch`, but `gl_LastFragColorARM` when only the
ARM extension is present (`util/shadergen.cpp:671-679`) - and the ARM variant needs no `inout` rewrite, which is what
makes it usable from a GLSL ES 1.00 shader.

## E: the mask bit in the depth buffer, for GLES2

Without fragment fetch or ROV the reference does not read colour to test the mask; it keeps the mask bit **in the
depth buffer**:

- every draw that asks to check the mask gets a unique, decreasing `z` (`m_current_depth++` at
  `gpu_hw.cpp:4357-4358`; `GetCurrentNormalizedVertexDepth` = `1 - depth/65535`, `gpu_hw.cpp:3742-3745`);
- the fragment shader writes `o_depth = oalpha * v_pos.z` (`gpu_hw_shadergen.cpp:3200`, `3217`, `3244`): an unmarked
  pixel gets depth 0 (far), a marked one gets the `z` of the moment it was drawn;
- later draws use `depth_func = GreaterEqual` (`gpu_hw.cpp:1721`): since each new draw has a *smaller* `z` than any
  earlier one, a marked pixel fails the test and is discarded, while an unmarked one (depth 0) passes;
- when the counter wraps, the depth buffer is rebuilt from the alpha channel
  (`UpdateDepthBufferFromMaskBit`, `gpu_hw.cpp:2387-2403`).

`check-mask` therefore costs a free `discard` and never reads colour. This works on **plain GLES2 with
`GL_DEPTH_COMPONENT16`**, no extension at all.

## F: two passes, the honest fallback

`NeedsTwoPassRendering` (`gpu_hw.cpp:3656-3664`):

```cpp
return (m_batch.texture_mode != BatchTextureMode::Disabled &&
        (m_batch.transparency_mode == GPUTransparencyMode::BackgroundMinusForeground ||
         (!m_supports_dual_source_blend && m_batch.transparency_mode != GPUTransparencyMode::Disabled)));
```

and in `FlushRender` (`gpu_hw.cpp:4408-4411`) the two draws. The opaque pass discards semi-transparent pixels and
the transparent pass does the opposite (`gpu_hw_shadergen.cpp:3203-3205`, `3220-3222`). Cost: 2x vertices and 2x
fragments, no destination read.

Its assumed limitation (`gpu_hw.cpp:4274-4275`):

```cpp
// Reverse blending breaks with mixed transparent and opaque pixels, so we have to do one draw per polygon.
// If we have fbfetch, we don't need to draw it in two passes. Test case: Suikoden 2 shadows.
```

For "background - foreground" the batch cannot accumulate: one flush per polygon. That is the worst draw-call case,
and the main reason to want fragment fetch.

## Alpha is always the mask bit

In both paths `o_col0.a = oalpha` (`gpu_hw_shadergen.cpp:3069`):

```glsl
oalpha = float(u_set_mask_while_drawing ? 1 : int(semitransparent));
```

- textured: `semitransparent = (texcol.a >= 0.5)` (the texel's bit 15);
- untextured: `semitransparent = true` (a raw polygon is always semi-transparent);
- `u_set_mask_while_drawing` forces alpha 1 (mark), and is a per-batch uniform.

Our `gpu.c` does the same with a `uint16_t` and bit masks; the difference is that there the `if` costs A35 cycles
and here it becomes a `discard`.

## Reading for us

- **Mask**: exact on GLES2 via strategy E. Not the problem.
- **Semi-transparency**: either fragment fetch (exact, free), two passes (approximate, 2x fragments) or a
  premultiplied per-primitive blend (approximate, free). This game uses a lot of transparency (shadows, water, battle
  effects), so it decides the backend's cost.
