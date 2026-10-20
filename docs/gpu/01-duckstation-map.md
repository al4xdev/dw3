# 01 — Map of the reference, and the path of one polygon

## Sizes, to see what is actually big

```
gpu.cpp                 4294   PS1 GPU state machine (registers, drawing area, polygon parsing)
gpu_backend.cpp         1033   command queue from the CPU thread to the GPU thread
gpu_backend.h            199   the backend interface
gpu_hw.cpp              4887   hardware backend: batching, pipelines, VRAM as textures, present
gpu_hw_shadergen.cpp    4111   generates every shader (batch, VRAM write/copy/fill, present, downsample)
gpu_hw_texture_cache.cpp3924   texture cache (optional; only pays off with upscaling)
gpu_sw.cpp               481   software backend (the class)
gpu_sw_rasterizer.inl   2050   software rasteriser (templated, span based - close to ours)
gpu_sw_rasterizer.cpp    129   instantiates the templates
util/gpu_device.h       1078   D3D11/D3D12/Vulkan/GL/GLES/Metal abstraction
```

Against ours: `psxstack/psyq/gpu.c` holds the state machine *and* the rasteriser in one file, and `render_gpu.c`
(1560) is our SDL_GPU backend. The reference splits the three: state (`gpu.cpp`), backend (`gpu_sw.*`, `gpu_hw.*`)
and shader generation (`gpu_hw_shadergen.cpp`).

## Path of one polygon

```
gpu.cpp                    reads register 0x1F801810, builds a GPUBackendDrawPolygonCommand
   |                       (up to 4 vertices, colour, texpage, clut)
   v
gpu_backend.cpp            PushCommand -> queue -> video thread (the reference runs the GPU on its own thread)
   v
gpu_hw.cpp DrawPolygon()   picks the pipeline for the current state, appends BatchVertex
   |                       (x,y,z,w,colour,texpage,u,v), and only FLUSHES when the state changes
   v
DrawBatchVertices()        one glDrawElements for N triangles
   v
gpu_hw_shadergen.cpp       the vertex/fragment shader that resolves texture, colour, semi-transparency,
                           the mask bit and dithering
```

Three architectural decisions worth copying:

1. **Batching by state.** Flush only when texpage, CLUT, transparency mode, dithering, texture window or a mask
   flag changes (`gpu_hw.cpp:4280-4289`). Our software path has no flush: it draws triangle by triangle. The measured
   price of *not* batching is in `07-cost-model.md`.
2. **One shader generator, many `#define`s.** Every combination (textured, 4/8-bit palette, semi-transparency,
   dithering, mask, interlace) comes out of `GenerateBatchFragmentShader` (`gpu_hw_shadergen.cpp:2722-2770`).
3. **VRAM lives on the GPU; the CPU adapts.** The opposite of our stack today.

## The detail that matters most

`gpu_hw.cpp:4410` is the no-fragment-fetch fallback:

```cpp
DrawBatchVertices(BatchRenderMode::OnlyOpaque, index_count, base_index, base_vertex, texture);
DrawBatchVertices(BatchRenderMode::OnlyTransparent, index_count, base_index, base_vertex, texture);
```

The same geometry is drawn twice, one pass discarding the transparent texels and one discarding the opaque ones.
That is plain GLES2 running PS1 semantics. On our device we do not need it (see `04`), but it is the floor of what
is possible.
