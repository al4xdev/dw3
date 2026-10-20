# 02 — The VRAM is one RGBA8 texture (and the mask bit lives in alpha)

## The format

`gpu_hw.cpp:50`

```cpp
static constexpr GPUTextureFormat VRAM_RT_FORMAT = GPUTextureFormat::RGBA8;
```

The PS1 VRAM (1024x512, 16 bits per texel) becomes **one RGBA8 texture of 1024x512** (times the internal scale).
Not `R16UI`, not two channels: plain, renderable RGBA8.

The packing is in `gpu_hw_shadergen.cpp:68-85`:

```glsl
uint RGBA8ToRGBA5551(float4 v)
{
  uint r = uint(roundEven(v.r * 31.0));
  uint g = uint(roundEven(v.g * 31.0));
  uint b = uint(roundEven(v.b * 31.0));
  uint a = (v.a != 0.0) ? 1u : 0u;
  return (r) | (g << 5) | (b << 10) | (a << 15);
}

float4 RGBA5551ToRGBA8(uint v)
{
  uint r = (v & 31u);
  uint g = ((v >> 5) & 31u);
  uint b = ((v >> 10) & 31u);
  uint a = ((v >> 15) & 1u);
  return float4(float(r) / 31.0, float(g) / 31.0, float(b) / 31.0, float(a));
}
```

Why this loses nothing:

- 5 bits fit exactly in a normalised 8-bit channel (`r/31` is representable, and `roundEven(*31)` recovers the
  integer), so 5551 -> RGBA8 -> 5551 is lossless.
- The **fourth channel holds the PS1 mask bit**, which in our software renderer is bit 15 of each `uint16_t`. The
  PS1's set-mask/check-mask flags then become ordinary fragment operations: alpha 0 or 1, testable with `discard`
  and writable by the shader.

Consequences: hardware blending can use that alpha as the mask, and reading or writing the whole VRAM (LoadImage,
VRAM-to-VRAM, a dump) is reading or writing a texture, with no exotic format anywhere.

## Two textures, not one

`gpu_hw.cpp:977-991`

```
m_vram_texture           RenderTarget RGBA8 1024x512   <- where drawing happens
m_vram_read_texture      Texture      RGBA8 1024x512   <- where sampling happens
m_vram_readback_texture  RenderTarget RGBA8  512x512   <- ReadVRAM/dump only
```

A texture cannot be sampled while it is bound as the framebuffer, and a battle draws into the same page it just used
as a texture. The reference keeps a *read copy* and updates **only the dirty rectangles** before each flush
(`UpdateVRAMReadTexture`, `gpu_hw.cpp:2317-2380`) with `CopyTextureRegion`. Two rectangles track what changed: what
was drawn (`m_vram_dirty_draw_rect`) and what the CPU wrote (`m_vram_dirty_write_rect`).

Measured on our device: a full 1024x512 copy costs 0.42 ms via `glBlitFramebuffer` and 1.77 ms via
`glCopyTexSubImage2D` (the ES2 fallback), so the rectangle, not the whole page, is what makes this free.

## Writing VRAM from the CPU (LoadImage)

Not `glTexSubImage2D`: a **screen quad** with a scissor (`gpu_hw.cpp:3905-3994`) whose shader reads the source from a
texture buffer and writes `RGBA5551ToRGBA8(value | mask)`, replicating each texel `resolution_scale` times when
upscaling. `FillVRAM` (`gpu_hw.cpp:3747-3804`) is the same shape rather than `glClear`, because the PS1 drawing area
can wrap and has interlaced fills (`(fragpos.y & 1) == field -> discard`). `CopyVRAM` (`gpu_hw.cpp:3996+`) picks
between a local texture-cache copy, the software rasteriser (for overlaps hardware cannot resolve in one pass) and a
shader copy over non-overlapping blocks.

## Readback (checkpoint / dump)

`gpu_hw.cpp:3819-3860` renders the region into a **half-width** RGBA8 target, packing two 16-bit pixels into one
32-bit texel:

```
encoded_left  = copy_rect.left / 2;
encoded_width = copy_rect.width() / 2;   // half the texels, same bytes
```

The download then moves half the data it otherwise would. On our device, the matching rule (read back in the format
you stored) is worth 2-3x: 320x240 read as 5-5-5-1 costs 0.73 ms against 1.95 ms read as RGBA8, and reading a
mismatched format cost 28 ms once (`08-format-and-present.md`).
