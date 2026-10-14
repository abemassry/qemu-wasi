/*
 * WASI-GFX Display Backend for QEMU — Simple Host-Extern API
 *
 * Instead of calling the many wasi-gfx WIT functions from the guest, this
 * backend hands frames to the host through a few plain extern functions
 * (imports from "env").  The host (m-browser) shows them on a wasi-gfx
 * surface (wasi-surface-wasmtime), currently by blitting with softbuffer.
 *
 * Architecture:
 *   QEMU VGA emulation → DisplaySurface (pixel buffer in guest memory)
 *     → This backend sends framebuffer data to host via host_gfx_*()
 *       → Host presents it on its wasi-gfx surface
 *
 * Input events flow in reverse:
 *   Host surface events (pointer, keyboard)
 *     → host_gfx_poll_events() returns events to guest
 *       → This backend injects into QEMU input subsystem
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>

#include "wasi_gfx_display.h"

#ifdef QEMU_BUILD
#include "qemu/osdep.h"
#include "ui/console.h"
#include "ui/input.h"
#else
/* Standalone stubs for testing outside QEMU tree */
static void qemu_input_queue_btn(void *src, int btn, bool down)
{
    (void)src; (void)btn; (void)down;
}
static void qemu_input_queue_abs(void *src, int axis, int value,
                                 int min, int max)
{
    (void)src; (void)axis; (void)value; (void)min; (void)max;
}
static void qemu_input_event_sync(void) {}
#endif /* QEMU_BUILD */

/* ================================================================
 * Host-provided extern functions (implemented by m-browser in Rust)
 * ================================================================
 *
 * These are imported from the "env" module (plain C externs).
 * The host reads/writes guest linear memory at the pointers provided.
 */

/*
 * Initialize the host display backend.
 * The host creates a GPU device, surface, render pipeline, etc.
 */
extern void host_gfx_init(int32_t width, int32_t height);

/*
 * Send framebuffer data to the host for rendering.
 *
 * @data_ptr:   Pointer into guest linear memory with BGRA8 pixel data
 * @data_len:   Total bytes (width * height * 4)
 * @width:      Framebuffer width in pixels
 * @height:     Framebuffer height in pixels
 * @stride:     Bytes per row (may include padding)
 *
 * The host reads `data_len` bytes from guest memory at `data_ptr`,
 * uploads to a GPU texture, and renders to the surface.
 */
extern void host_gfx_update_framebuffer(
    const uint8_t *data_ptr,
    int32_t data_len,
    int32_t width,
    int32_t height,
    int32_t stride);

/*
 * Notify the host that the framebuffer size has changed.
 * The host should resize its surface/texture accordingly.
 */
extern void host_gfx_resize(int32_t width, int32_t height);

/*
 * Poll for input events from the host.
 *
 * @event_buf:  Pointer to guest memory buffer for receiving events.
 *              Each event is 3 x int32 = 12 bytes:
 *                [type, code, value]
 *              Event types:
 *                1 = pointer_move  (code=0:x 1:y, value=position)
 *                2 = pointer_btn   (code=button_id, value=0:up 1:down)
 *                3 = key           (code=keycode, value=0:up 1:down)
 *                4 = resize        (code=width, value=height)
 * @max_events: Maximum number of events the buffer can hold
 *
 * Returns: number of events written to event_buf
 */
extern int32_t host_gfx_poll_events(
    int32_t *event_buf,
    int32_t max_events);

/* ================================================================
 * Local display state
 * ================================================================ */

typedef struct WasiGfxDisplay {
    int       fb_width;
    int       fb_height;
    uint8_t  *fb_data;     /* local copy of guest framebuffer (BGRA8) */
    int       scale;       /* integer magnification factor for output */
    uint8_t  *out_data;    /* fb_data scaled up; what the host receives */
    bool      initialized;
} WasiGfxDisplay;

static WasiGfxDisplay wasi_display;

/*
 * Set when fb_data changed since the last frame was sent to the host.  The
 * guest screen is usually static, and scaling plus handing a frame to the
 * host costs several milliseconds on the thread that also runs the guest
 * CPU, so unchanged frames are not sent (except once a second, in case the
 * host needs to redraw its window).
 */
static bool frame_dirty = true;
#define FRAME_RESEND_INTERVAL 30    /* refreshes, ~1 s at 30 Hz */

/*
 * Magnification factor for the displayed image.
 *
 * QEMU's text console is 80x24 characters of an 8x16 font, i.e. 640x384
 * pixels, which is tiny on a modern display.  Magnify by a whole number of
 * pixels (nearest neighbour) so glyph edges stay sharp.  Override with
 * WASI_GFX_SCALE=<n>; 1 disables scaling.
 */
static int wasi_gfx_scale(void)
{
    static int scale = -1;

    if (scale < 0) {
        const char *env = getenv("WASI_GFX_SCALE");
        scale = env ? atoi(env) : 2;
        if (scale < 1) {
            scale = 1;
        } else if (scale > 8) {
            scale = 8;
        }
    }
    return scale;
}

/*
 * Magnification for a width x height image: the configured factor, reduced
 * so the result stays within 1600x1200.  Text mode (720x400) is doubled;
 * a 1280x800 framebuffer console is shown 1:1 (doubling it would also make
 * every frame four times as expensive to produce).
 */
static int wasi_gfx_scale_for(int width, int height)
{
    int scale = wasi_gfx_scale();

    while (scale > 1 && (width * scale > 1600 || height * scale > 1200)) {
        scale--;
    }
    return scale;
}

/*
 * (Re)allocate the scaled output buffer for the current framebuffer size.
 * On allocation failure scaling is disabled rather than failing the frame.
 */
static void wasi_gfx_alloc_out(void)
{
    size_t bytes;

    free(wasi_display.out_data);
    wasi_display.out_data = NULL;

    if (wasi_display.scale <= 1) {
        return;
    }

    bytes = (size_t)wasi_display.fb_width * wasi_display.scale *
            (size_t)wasi_display.fb_height * wasi_display.scale * 4;
    wasi_display.out_data = (uint8_t *)calloc(bytes, 1);
    if (!wasi_display.out_data) {
        fprintf(stderr, "wasi-gfx: scale buffer alloc failed, falling back to 1x\n");
        wasi_display.scale = 1;
    }
}

/* ================================================================
 * Initialization
 * ================================================================ */

int wasi_gfx_display_init(int width, int height)
{
    wasi_display.scale = wasi_gfx_scale_for(width, height);

    printf("wasi-gfx: initializing display %dx%d (scale %dx -> %dx%d)\n",
           width, height, wasi_display.scale,
           width * wasi_display.scale, height * wasi_display.scale);

    wasi_display.fb_width  = width;
    wasi_display.fb_height = height;
    wasi_display.fb_data   = (uint8_t *)calloc((size_t)width * height * 4, 1);
    frame_dirty = true;
    if (!wasi_display.fb_data) {
        printf("wasi-gfx: failed to allocate framebuffer\n");
        return -1;
    }
    wasi_gfx_alloc_out();

    /* Tell the host to set up its GPU display at the magnified size */
    host_gfx_init(width * wasi_display.scale, height * wasi_display.scale);

    wasi_display.initialized = true;
    printf("wasi-gfx: initialization complete\n");
    return 0;
}

/* ================================================================
 * Framebuffer Update
 * ================================================================ */

void wasi_gfx_display_update(DisplaySurface *surface,
                              int x, int y, int w, int h)
{
    static int update_count = 0;
    if (update_count < 10 || update_count % 60 == 0) {
        WASI_LOG("wasi-gfx: update #%d region=(%d,%d %dx%d)\n",
                update_count, x, y, w, h);
    }
    update_count++;
    if (!wasi_display.fb_data || !surface) {
        return;
    }

#ifdef QEMU_BUILD
    uint8_t *src_data = (uint8_t *)surface_data(surface);
    int src_linesize = surface_stride(surface);
#else
    uint8_t *src_data = surface->data;
    int src_linesize = surface->linesize;
#endif

    if (!src_data) {
        return;
    }

    /* Copy dirty region from QEMU's surface to our local buffer */
    for (int row = y; row < y + h && row < wasi_display.fb_height; row++) {
        int src_offset = row * src_linesize + x * 4;
        int dst_offset = row * wasi_display.fb_width * 4 + x * 4;
        int copy_len   = w * 4;

        if (x + w > wasi_display.fb_width) {
            copy_len = (wasi_display.fb_width - x) * 4;
        }
        /*
         * Compare first: VGA text mode reports the cursor cell as updated
         * on every refresh, changed or not, and sending a frame costs far
         * more than this comparison.
         */
        if (copy_len > 0 &&
            memcmp(wasi_display.fb_data + dst_offset, src_data + src_offset,
                   copy_len) != 0) {
            memcpy(wasi_display.fb_data + dst_offset,
                   src_data + src_offset,
                   copy_len);
            frame_dirty = true;
        }
    }
}

/* ================================================================
 * Full-surface blit — bypasses dirty-region tracking
 * ================================================================
 *
 * Reads the entire console DisplaySurface into our local framebuffer
 * each frame.  This does NOT rely on dpy_gfx_update / dirty-page
 * tracking (which is unreliable under the WASI TCG backend).  The
 * caller is expected to force a full VGA redraw via
 * graphic_hw_invalidate() before reading the surface.
 */
void wasi_gfx_display_blit(const uint8_t *src_data, int src_w, int src_h,
                           int src_stride)
{
    static int blit_count = 0;

    if (!src_data || src_w <= 0 || src_h <= 0) {
        if (blit_count < 5) {
            WASI_LOG("wasi-gfx: blit skipped (data=%p %dx%d)\n",
                    (const void *)src_data, src_w, src_h);
        }
        blit_count++;
        return;
    }

    /* Resize local framebuffer to match the surface if needed. */
    if (!wasi_display.fb_data ||
        src_w != wasi_display.fb_width ||
        src_h != wasi_display.fb_height) {
        free(wasi_display.fb_data);
        wasi_display.scale     = wasi_gfx_scale_for(src_w, src_h);
        wasi_display.fb_width  = src_w;
        wasi_display.fb_height = src_h;
        wasi_display.fb_data   = (uint8_t *)calloc((size_t)src_w * src_h * 4, 1);
        frame_dirty = true;
        if (!wasi_display.fb_data) {
            WASI_LOG("wasi-gfx: blit realloc failed (%dx%d)\n",
                    src_w, src_h);
            return;
        }
        /* Keep the scaled output buffer and the host surface in step. */
        wasi_gfx_alloc_out();
        host_gfx_resize(src_w * wasi_display.scale,
                        src_h * wasi_display.scale);
        WASI_LOG("wasi-gfx: blit resized fb to %dx%d (scale %dx)\n",
                src_w, src_h, wasi_display.scale);
    }

    int dst_stride = wasi_display.fb_width * 4;
    for (int row = 0; row < src_h; row++) {
        const uint8_t *s = src_data + (size_t)row * src_stride;
        uint8_t *d = wasi_display.fb_data + (size_t)row * dst_stride;
        int copy_len = src_w * 4;
        if (copy_len > dst_stride) {
            copy_len = dst_stride;
        }
        if (memcmp(d, s, copy_len) != 0) {
            memcpy(d, s, copy_len);
            frame_dirty = true;
        }
    }

    /* Diagnostic: sample the source surface so we can tell whether VGA
     * actually rendered anything (sum>0) or the surface is all-black. */
    if (wasi_debug_level() >= 1 && (blit_count < 5 || blit_count % 120 == 0)) {
        unsigned long long sum = 0;
        size_t total = (size_t)src_w * src_h * 4;
        for (size_t i = 0; i < total; i += 101) {
            sum += src_data[i];
        }
        fprintf(stderr,
                "wasi-gfx: blit #%d %dx%d stride=%d sample_sum=%llu\n",
                blit_count, src_w, src_h, src_stride, sum);
    }
    blit_count++;
}

/* ================================================================
 * Rendering — delegates to host
 * ================================================================ */

void wasi_gfx_display_render(void)
{
    static int render_count = 0;
    if (render_count < 5 || render_count % 60 == 0) {
        WASI_LOG("wasi-gfx: render #%d (init=%d, fb=%p)\n",
                render_count, wasi_display.initialized,
                (void*)wasi_display.fb_data);
    }
    render_count++;
    if (!wasi_display.fb_data || !wasi_display.initialized) {
        return;
    }
    {
        static int unchanged_frames;

        if (!frame_dirty && ++unchanged_frames < FRAME_RESEND_INTERVAL) {
            return;
        }
        unchanged_frames = 0;
        frame_dirty = false;
    }

    int scale = wasi_display.scale;

    if (scale <= 1 || !wasi_display.out_data) {
        host_gfx_update_framebuffer(
            wasi_display.fb_data,
            wasi_display.fb_width * wasi_display.fb_height * 4,
            wasi_display.fb_width,
            wasi_display.fb_height,
            wasi_display.fb_width * 4);
        return;
    }

    /*
     * Nearest-neighbour magnification.  Expand one source row into a single
     * destination row, then copy that row scale times; this keeps the inner
     * loop simple and touches each output pixel exactly once.
     */
    int src_w      = wasi_display.fb_width;
    int src_h      = wasi_display.fb_height;
    int dst_w      = src_w * scale;
    int dst_h      = src_h * scale;
    int src_stride = src_w * 4;
    int dst_stride = dst_w * 4;

    for (int y = 0; y < src_h; y++) {
        const uint32_t *s =
            (const uint32_t *)(wasi_display.fb_data + (size_t)y * src_stride);
        uint8_t *first_row =
            wasi_display.out_data + (size_t)y * scale * dst_stride;
        uint32_t *d = (uint32_t *)first_row;

        for (int x = 0; x < src_w; x++) {
            uint32_t px = s[x];
            for (int k = 0; k < scale; k++) {
                *d++ = px;
            }
        }
        for (int k = 1; k < scale; k++) {
            memcpy(first_row + (size_t)k * dst_stride, first_row, dst_stride);
        }
    }

    host_gfx_update_framebuffer(
        wasi_display.out_data,
        dst_w * dst_h * 4,
        dst_w,
        dst_h,
        dst_stride);
}

/* ================================================================
 * Input Handling — polls events from host
 * ================================================================ */

/*
 * Input events fetched from the host, delivered in order by
 * wasi_gfx_display_poll_events() on each display refresh (~30 Hz).
 *
 * Keyboard input is paced.  Input devices buffer very little and drop the
 * rest: a PS/2 keyboard holds 16 bytes, a USB keyboard 16 events and QEMU's
 * text consoles 16 characters.  The main loop can stall for a moment (e.g.
 * while the host moves QEMU to a fresh store), and everything typed in the
 * meantime would then arrive in one burst and be partly lost.  So at most
 * KEYS_PER_POLL key and text events are delivered per refresh, which leaves
 * the guest time to drain its device in between.  Events behind them, of any
 * type, wait for the next refresh so that keys and pointer events stay in
 * order.
 */
#define MAX_EVENTS 128
#define KEYS_PER_POLL 6
static int32_t pending_events[MAX_EVENTS * 3];
static int32_t pending_count;

/*
 * Console that input should be delivered to.  Set by the DCL when it picks a
 * console, so keys follow whatever is actually on screen instead of QEMU's
 * notion of the "active" console.
 */
#ifdef QEMU_BUILD
static QemuConsole *wasi_input_console;

void wasi_gfx_display_set_input_console(QemuConsole *con)
{
    wasi_input_console = con;
}
#endif

/* Ctrl state, tracked from key events so text input can form control chars. */
static bool wasi_ctrl_held;

/*
 * Set when a Ctrl+Alt+<n> console-switch hotkey is consumed, so that the
 * text event the host sends for the same key press is not typed into the
 * newly selected console.  Cleared by the next key press.
 */
static bool wasi_swallow_text;

/*
 * Deliver a printable character to QEMU's text console.
 *
 * Text consoles translate only a few navigation keys from key codes; ordinary
 * characters have to arrive as text.  Mirrors qemu_text_console_put_string()
 * as used by ui/gtk.c.
 */
static void wasi_gfx_handle_text(int codepoint, bool ctrl_held)
{
#ifdef QEMU_BUILD
    int keysym = codepoint;

    if (wasi_swallow_text) {
        wasi_swallow_text = false;
        return;
    }

    if (!wasi_input_console || !QEMU_IS_TEXT_CONSOLE(wasi_input_console)) {
        return;
    }

    /*
     * Turn Ctrl+letter into the matching control character, which is what
     * the monitor's readline expects (Ctrl-A start of line, Ctrl-U kill, ...).
     */
    if (ctrl_held) {
        if (keysym >= 'a' && keysym <= 'z') {
            keysym = keysym - 'a' + 1;
        } else if (keysym >= 'A' && keysym <= 'Z') {
            keysym = keysym - 'A' + 1;
        }
    }

    WASI_LOG("wasi-gfx: text codepoint=%d ctrl=%d -> keysym=%d\n",
            codepoint, (int)ctrl_held, keysym);

    qemu_text_console_put_keysym(QEMU_TEXT_CONSOLE(wasi_input_console), keysym);
#else
    (void)codepoint;
    (void)ctrl_held;
#endif
}

/*
 * Deliver a key press/release to QEMU.
 *
 * @evdev: Linux evdev key code, as sent by the host.
 * @down:  true for press, false for release.
 */
static void wasi_gfx_handle_key(int evdev, bool down)
{
#ifdef QEMU_BUILD
    static bool alt_held;
    static int swallow_release = -1;
    int qcode;

    /* Tracked so that text events can be folded into control characters. */
    if (evdev == 29 /* KEY_LEFTCTRL */ || evdev == 97 /* KEY_RIGHTCTRL */) {
        wasi_ctrl_held = down;
    }
    if (evdev == 56 /* KEY_LEFTALT */ || evdev == 100 /* KEY_RIGHTALT */) {
        alt_held = down;
    }
    if (down) {
        wasi_swallow_text = false;
    }

    /*
     * Ctrl+Alt+<n> selects console n-1, as in QEMU's GTK and SDL front ends:
     * 1 is the guest display, then the monitor and serial "vc" consoles.
     * The hotkey is consumed rather than passed on.
     */
    if (down && wasi_ctrl_held && alt_held &&
        evdev >= 2 /* KEY_1 */ && evdev <= 10 /* KEY_9 */) {
        QemuConsole *old = wasi_input_console;

        if (old && qemu_console_is_graphic(old)) {
            /* The guest saw Ctrl and Alt go down; don't leave them stuck. */
            qemu_input_event_send_key_qcode(old, Q_KEY_CODE_CTRL, false);
            qemu_input_event_send_key_qcode(old, Q_KEY_CODE_CTRL_R, false);
            qemu_input_event_send_key_qcode(old, Q_KEY_CODE_ALT, false);
            qemu_input_event_send_key_qcode(old, Q_KEY_CODE_ALT_R, false);
        }
        wasi_gfx_display_switch_console(evdev - 2);
        swallow_release = evdev;
        wasi_swallow_text = true;
        return;
    }
    if (!down && evdev == swallow_release) {
        swallow_release = -1;
        return;
    }

    qcode = qemu_input_linux_to_qcode(evdev);
    WASI_LOG("wasi-gfx: key evdev=%d down=%d qcode=%d console=%p text=%d\n",
            evdev, (int)down, qcode, (void *)wasi_input_console,
            wasi_input_console ? !!QEMU_IS_TEXT_CONSOLE(wasi_input_console) : -1);

    if (qcode <= 0) {
        fprintf(stderr, "wasi-gfx: no qcode for evdev %d\n", evdev);
        return;
    }

    if (!wasi_input_console) {
        WASI_LOG("wasi-gfx: key dropped, no input console selected\n");
        return;
    }

    if (QEMU_IS_TEXT_CONSOLE(wasi_input_console)) {
        /*
         * Text consoles consume characters, not press/release pairs, so act
         * on the press only. put_qcode maps cursor keys, Tab, Backspace and
         * the Ctrl combinations to the terminal keysyms the monitor expects.
         */
        if (down) {
            qemu_text_console_put_qcode(QEMU_TEXT_CONSOLE(wasi_input_console),
                                        qcode, wasi_ctrl_held);
        }
    } else {
        qemu_input_event_send_key_qcode(wasi_input_console, qcode, down);
    }
#else
    (void)evdev;
    (void)down;
#endif
}

void wasi_gfx_display_poll_events(void)
{
    int32_t done;
    int keys = 0;

    if (!wasi_display.initialized) {
        return;
    }

    /*
     * Each event: [type, code, value] = 3 ints.  Top up from the host;
     * events that don't fit stay queued on the host side.
     */
    if (pending_count < MAX_EVENTS) {
        pending_count += host_gfx_poll_events(pending_events + pending_count * 3,
                                              MAX_EVENTS - pending_count);
    }

    for (done = 0; done < pending_count; done++) {
        int32_t type  = pending_events[done * 3 + 0];
        int32_t code  = pending_events[done * 3 + 1];
        int32_t value = pending_events[done * 3 + 2];

        if (type == 3 || type == 5) {
            if (keys == KEYS_PER_POLL) {
                break;      /* the rest waits for the next refresh */
            }
            keys++;
        }

        switch (type) {
        case 1: { /* pointer_move: position in host window pixels */
            /*
             * The host shows the frame magnified by wasi_display.scale, so
             * window coordinates span the scaled size.  These go to an
             * absolute pointing device (e.g. -device usb-tablet).
             */
            int span = (code == 0 ? wasi_display.fb_width : wasi_display.fb_height)
                       * wasi_display.scale;

            if (span <= 1) {
                break;
            }
            value = value < 0 ? 0 : value >= span ? span - 1 : value;
            qemu_input_queue_abs(NULL, code == 0 ? 0 /* INPUT_AXIS_X */
                                                 : 1 /* INPUT_AXIS_Y */,
                                 value, 0, span - 1);
            break;
        }

        case 2: /* pointer_btn */
            qemu_input_queue_btn(NULL, code, value != 0);
            /*
             * Report each button change separately.  Pointing devices
             * build one report per sync, so a press and release delivered
             * in the same batch (a quick click between two polls) would
             * otherwise cancel out and the click would be lost.
             */
            qemu_input_event_sync();
            break;

        case 3: /* key */
            wasi_gfx_handle_key(code, value != 0);
            break;

        case 5: /* text — a Unicode codepoint produced by a key press */
            wasi_gfx_handle_text(code, wasi_ctrl_held);
            break;

        case 4: /* resize */
            /*
             * The host reports its window size, which is the magnified size.
             * Feeding that back in as a new framebuffer size would compound
             * the scale factor on every event.  QEMU owns the console
             * dimensions (via dpy_gfx_switch), so just note the event.
             */
            printf("wasi-gfx: host window resize %d x %d (ignored)\n",
                   code, value);
            break;

        default:
            break;
        }
    }

    pending_count -= done;
    memmove(pending_events, pending_events + done * 3,
            (size_t)pending_count * 3 * sizeof(pending_events[0]));

    qemu_input_event_sync();
}

/* ================================================================
 * Resize
 * ================================================================ */

void wasi_gfx_display_resize(int width, int height)
{
    int scale = wasi_gfx_scale_for(width, height);

    /*
     * QEMU replaces the display surface for reasons other than a size
     * change (e.g. VGA page flips), so this is called often: only
     * reallocate when the size actually changes.
     */
    frame_dirty = true;
    if (wasi_display.fb_data && width == wasi_display.fb_width &&
        height == wasi_display.fb_height && scale == wasi_display.scale) {
        return;
    }
    wasi_display.scale = scale;
    printf("wasi-gfx: resize to %dx%d (scale %dx)\n",
           width, height, wasi_display.scale);

    free(wasi_display.fb_data);
    wasi_display.fb_width  = width;
    wasi_display.fb_height = height;
    wasi_display.fb_data   = (uint8_t *)calloc((size_t)width * height * 4, 1);
    frame_dirty = true;
    wasi_gfx_alloc_out();

    host_gfx_resize(width * wasi_display.scale, height * wasi_display.scale);
}

/* ================================================================
 * Cleanup
 * ================================================================ */

void wasi_gfx_display_shutdown(void)
{
    free(wasi_display.fb_data);
    wasi_display.fb_data = NULL;
    free(wasi_display.out_data);
    wasi_display.out_data = NULL;
    wasi_display.initialized = false;
}
