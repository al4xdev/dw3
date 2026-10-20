# 03 — CLUT without a CLUT: the palette is one more row of VRAM

What our `gpu.c` calls "the CLUT on the CPU" (decode the palette into a 16- or 256-entry table and index it) does
**not exist** in the reference's hardware path. The palette lives in VRAM, and the lookup is two samples of the same
texture.

## The code (`gpu_hw_shadergen.cpp:2881-2919`)

```glsl
#elif PALETTE
  uint2 icoord = ApplyTextureWindow(FloatToIntegerCoords(...));

  uint2 vicoord;
  #if PALETTE_4_BIT
    // 4bit will never wrap, since it's in the last texpage row.
    vicoord = uint2(texpage.x + (icoord.x / 4u), texpage.y + icoord.y);
  #elif PALETTE_8_BIT
    // 8bit can wrap in the X direction.
    vicoord = uint2((texpage.x + (icoord.x / 2u)) & 0x3FFu, texpage.y + icoord.y);
  #endif

  // load colour/palette
  // use texelFetch()/load for native resolution to work around point sampling precision
  // in some drivers, such as older AMD and Mali Midgard
  #if !UPSCALED
    float4 texel = LOAD_TEXTURE(samp0, int2(vicoord), 0);
  #else
    float4 texel = SAMPLE_TEXTURE_LEVEL(samp0, float2(vicoord) * RCP_VRAM_SIZE, 0.0);
  #endif
  uint vram_value = RGBA8ToRGBA5551(texel);

  // apply palette
  #if PALETTE_4_BIT
    uint subpixel = icoord.x & 3u;
    uint palette_index = (vram_value >> (subpixel * 4u)) & 0x0Fu;
    uint2 palette_icoord = uint2((texpage.z + palette_index), texpage.w);
  #elif PALETTE_8_BIT
    uint subpixel = icoord.x & 1u;
    uint palette_index = (vram_value >> (subpixel * 8u)) & 0xFFu;
    uint2 palette_icoord = uint2(((texpage.z + palette_index) & 0x3FFu), texpage.w);
  #endif

  return LOAD_TEXTURE(samp0, int2(palette_icoord), 0);
```

Two dependent reads per pixel, no table, no invalidation:

1. read the packed texel and extract the index (`>> (subpixel*4) & 15`);
2. read the colour at `(palette_x + index, palette_y)`.

The `texpage` arrives as a `uint4` (base_x, base_y, palette_x, palette_y) computed in the vertex shader from the
register bits (`gpu_hw_shadergen.cpp:212-219`):

```glsl
v_texpage.x = (a_texpage & 15u) * 64u;
v_texpage.y = ((a_texpage >> 4) & 1u) * 256u;
v_texpage.z = ((a_texpage >> 16) & 63u) * 16u;   // palette: X in steps of 16
v_texpage.w = ((a_texpage >> 22) & 511u);        // palette: Y
```

The PS1 texture window (`(x & and) | or`) becomes an AND/OR on **integer** coordinates
(`gpu_hw_shadergen.cpp:2831-2836`).

## What this teaches

- The CPU-side CLUT cache is an accelerator for a software rasteriser, not a property of the problem. On the GPU the
  indirection costs one extra sample of a texture that is already hot.
- **The emulator never learns that the palette changed.** There is no `UpdateCLUT` on the hardware path: writing to
  the palette area is just a VRAM write, and the dirty rectangle already covers the read copy. That removes a whole
  class of invalidation bugs (which the software rasteriser's `g_gpu_clut` still needs, `gpu_hw.cpp:717`).

## Precision: the only part that still worries me

`FloatToIntegerCoords` (`gpu_hw_shadergen.cpp:2838-2852`) rounds with `roundEven` at 1x and floors when upscaling,
and the vertex shader offsets positions by half a texel (`gpu_hw_shadergen.cpp:175-182`):

```glsl
// Offset the vertex position by 0.5 to ensure correct interpolation of texture coordinates
// at 1x resolution scale.
float vertex_offset = (UPSCALED == 0) ? 0.5 : 0.0;
float pos_x = ((a_pos.x + vertex_offset) / 512.0) - 1.0;
float pos_y = ((a_pos.y + vertex_offset) / -256.0) + 1.0;
```

The interpolated `v_tex0` **is the integer texel coordinate** (not normalised: the attribute is unpacked in the
vertex shader, `gpu_hw_shadergen.cpp:207`), and the fragment shader rounds it. At 1x the interpolation only has to be
right to within half a texel.

On this device the answer is measured, not assumed: a `texture2D` with NEAREST at `(texel + 0.5) / 1024` returned the
exact palette byte (`06-what-the-device-answered.md`), with `precision highp float` giving 23 bits. Without the
explicit `highp`, `mediump` is 10 bits and the 0..1023 coordinates break.
