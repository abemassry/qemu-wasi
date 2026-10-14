/*
 * WASI-GFX Display Backend - Header
 *
 * When building inside QEMU (QEMU_BUILD defined), QEMU provides its own
 * DisplaySurface via ui/console.h.  For standalone compilation or unit
 * testing we fall back to a minimal compatible definition.
 */

#ifndef WASI_GFX_DISPLAY_H
#define WASI_GFX_DISPLAY_H

#include <stdint.h>
#include <stdbool.h>

#ifdef QEMU_BUILD
/*
 * QEMU's own headers define DisplaySurface, QemuConsole, etc.
 * We only need a forward declaration here; the real definition
 * comes from "ui/console.h" which the .c file includes.
 */
typedef struct DisplaySurface DisplaySurface;
typedef struct QemuConsole QemuConsole;

/* Select the console that polled input events are delivered to.  Called by
 * the DCL once it has chosen which console to display, so keys follow what
 * is actually on screen rather than QEMU's notion of the active console. */
void wasi_gfx_display_set_input_console(QemuConsole *con);

/* Show console <index> (0-based) and route input to it (Ctrl+Alt+<n>). */
void wasi_gfx_display_switch_console(int index);
#else
/*
 * Standalone / test build: provide a minimal compatible struct so the
 * backend can be compiled and tested outside the QEMU tree.
 */
typedef struct DisplaySurface {
    int width;
    int height;
    int linesize;         /* bytes per row (may include padding) */
    uint8_t *data;        /* BGRA8 pixel data */
} DisplaySurface;
#endif /* QEMU_BUILD */

/* Initialize the wasi-gfx display with given dimensions.
 * Creates GPU device, surface, graphics context, blit pipeline, sampler,
 * and subscribes to input/frame events.
 * Returns 0 on success, -1 on failure. */
int wasi_gfx_display_init(int width, int height);

/* Copy a dirty region from QEMU's DisplaySurface into our local framebuffer.
 * Does NOT trigger a render -- rendering is driven by frame events. */
void wasi_gfx_display_update(DisplaySurface *surface,
                              int x, int y, int w, int h);

/* Copy a full DisplaySurface (raw BGRA8 pixel data) into the local
 * framebuffer, resizing it if necessary.  Bypasses dirty-region tracking;
 * the caller should force a full VGA redraw before reading the surface. */
void wasi_gfx_display_blit(const uint8_t *src_data, int width, int height,
                           int stride);

/* Render the current framebuffer to the wasi-gfx surface.
 * Called internally when a frame event fires. */
void wasi_gfx_display_render(void);

/* Poll for input events (pointer, keyboard, frame, resize) and inject
 * them into QEMU's input subsystem.  Should be called from QEMU's main
 * loop or a timer callback. */
void wasi_gfx_display_poll_events(void);

/* Handle surface resize -- reallocates the local framebuffer copy. */
void wasi_gfx_display_resize(int width, int height);

/* Release all GPU and wasi-gfx resources. */
void wasi_gfx_display_shutdown(void);

#endif /* WASI_GFX_DISPLAY_H */
