# 17. The movies' artefact: what it is, and everything that did not explain it

Open item. In the opening movie the GLES renderer shows a rectangle of wrong pixels in the picture's top-right corner,
present in some frames and not others, so it reads as blinking. The software renderer is clean on the same frame - the
movie was tuned against it early on, so this is not an old defect that nobody noticed.

## What the game does (from `src/stdwtitl/stdwtitl_80082D70.c`)

The movie screen is `STDWTITL` (`stdwtitl_movie_screen`), and it does not draw into one place:

```c
id = stdwtitl_decoder.imgid;
snap_rect = stdwtitl_decoder.slice;
stdwtitl_decoder.imgid = stdwtitl_decoder.imgid ? 0 : 1;   /* two image buffers, alternating */
stdwtitl_decoder.slice.x += stdwtitl_decoder.slice.w;
if (stdwtitl_decoder.rectid != 0) snap_rect.x += 0x1E0;    /* the second VRAM area, 480 px along */
snap_rect.y = 0x24;                                        /* 36 */
...
DrawSync(0);
LoadImage(&snap_rect, stdwtitl_decoder.imgbuf[id]);
```

So a frame arrives as **slices of 24x416** written by `LoadImage` into **two alternating VRAM areas** (x and x+480, y=36
for both), and the display points at one of them. `DW3_LOAD_LOG` confirms it on the console: the transfers are
`24x416` at x = 72, 96, 120 … 936, 132 times each, plus small `16x1`/`8x32` ones.

There is also a sprite drawn every frame while the movie plays - a small Digimon mark near the top of the picture, above
the film's own window. It is not part of the film: it sits outside the picture the film draws.

## Every hypothesis that was tested, and what it showed

| hypothesis | how it was tested | result |
|---|---|---|
| The 24-bit display path (three bytes per VRAM word, converted from the shim's array) | `DW3_PRESENT_LOG` on the movie | **not it**: the movie's presents are `15-bit`, the 24-bit branch never runs |
| The GPU's drawing missing from a 24-bit frame (a primitive the renderer swallows while the CPU array is what a 24-bit present reads) | `gpu_display_24bit()` in `psyq/gpu.c`, making the CPU rasterise while a 24-bit display is up | **no effect**, reverted: the branch is unreachable on this content |
| The PBO ring of the present, which lands the picture two vsyncs late | `DW3_GLES_PRESENT_NOW=1` switched the present to the blocking read | **no change**, switch removed |
| The present reading the GPU's target instead of the CPU's VRAM | `DW3_GLES_MOVIE_VRAM=1` presented displays taller than 256 lines from `psyq_gpu_vram()` | **same artefact**, switch removed. This is the informative failure: the CPU's array is *also* wrong in that area under `REPLACE=1`, which means the movie's own primitives (the mark, the window) are drawn only into the target |
| The VRAM uploads not reaching the target | `DW3_GLES_CHECK` compares target against the shim's array | **inconclusive by construction**: with `DW3_GLES_REPLACE=1` the array only holds the transfers, so any primitive is reported as a difference (34.4% during the movie, `sw 0x0000 hw 0x3406` - the array is the empty side). With `REPLACE=0` both sides are drawn by the CPU and the check says `exact`, which says nothing about the uploads either |
| The whole GPU path being at fault | `DW3_GLES_REPLACE=0` (the CPU also draws, both sides get everything) | **the artefact disappears** - and the frame time doubles, which is why the picture looks right and the movie feels slow |

## What that leaves

The artefact lives where the movie's **transfers** and the movie screen's **primitives** meet: the sprite and the film's
window are drawn into the GPU's target only, the film's pixels arrive by `LoadImage`, and something in the order between
the last slices of a frame and the present leaves that region showing the other VRAM area's content. Two facts point at
the right edge of the picture specifically: the slices are written left to right, so the rightmost slices are the last
to be uploaded in a vsync, and the artefact sits exactly there.

The next session should start by logging, per vsync, **the order of the last `LoadImage` of the movie and the present**
(`DW3_LOAD_LOG` now carries the frame number, which is what that comparison needs), and by checking whether the display
area's `x` alternates between the two movie areas while the mark is drawn at a fixed `x` - if it does, the mark and the
film are not in the same buffer on every frame, and that is the bug.

## What is *not* worth doing

Transcoding the 14 movies to H.264 to play them through the RK3326's hardware decoder (`/dev/mpp_service`, `mpv` and
`ffmpeg` are both on the device) would remove the artefact by removing the path, at the cost of a conversion step, a
player hand-off synchronised with the game (which waits for the movie's end and accepts a skip) and the port no longer
reading the disc's own movie data. Days of work for a cosmetic defect in the opening films.
