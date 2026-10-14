/*
 * WASI-GFX Display Change Listener - QEMU integration glue
 *
 * Registers the wasi-gfx display backend with QEMU's display subsystem.
 * This file bridges QEMU's DisplayChangeListener interface with our
 * wasi_gfx_display.c implementation.
 *
 * Build: Compiled separately and linked into the QEMU binary.
 *        Requires -DQEMU_BUILD and QEMU include paths.
 */

#include "qemu/osdep.h"
#include "qemu/timer.h"
#include "ui/console.h"
#include "ui/surface.h"

/* Our display backend API */
#include "wasi_gfx_display.h"

/* ---- DisplayChangeListener callbacks ---- */

static DisplayChangeListener wasi_gfx_dcl;

/* Refreshes between forced full redraws (see wasi_gfx_dpy_refresh). */
#define FULL_REFRESH_EVERY 60

/*
 * Time spent refreshing the display, which runs on the same (only) thread
 * as the guest CPU.  QEMU_DISPLAY_STATS=1 prints a breakdown every ~20 s.
 */
static struct {
    uint64_t frames, hw_ns, blit_ns, render_ns, total_ns;
    int64_t window_start_ns;
} refresh_stats;

static void refresh_stats_report(int64_t now)
{
    static int enabled = -1;
    double wall_ms, n;

    if (enabled < 0) {
        enabled = getenv("QEMU_DISPLAY_STATS") != NULL;
        refresh_stats.window_start_ns = now;
    }
    if (!enabled || now - refresh_stats.window_start_ns < 20 * NANOSECONDS_PER_SECOND) {
        return;
    }
    wall_ms = (now - refresh_stats.window_start_ns) / 1e6;
    n = refresh_stats.frames ? refresh_stats.frames : 1;
    fprintf(stderr, "wasi-gfx: %.0f refreshes in %.0f ms: %.2f ms each "
            "(hw %.2f, blit %.2f, render %.2f) = %.1f%% of the time\n",
            (double)refresh_stats.frames, wall_ms, refresh_stats.total_ns / 1e6 / n,
            refresh_stats.hw_ns / 1e6 / n, refresh_stats.blit_ns / 1e6 / n,
            refresh_stats.render_ns / 1e6 / n,
            100.0 * refresh_stats.total_ns / 1e6 / wall_ms);
    memset(&refresh_stats, 0, sizeof(refresh_stats));
    refresh_stats.window_start_ns = now;
}

/*
 * Called by QEMU's display refresh timer (~30 Hz).
 * Polls for wasi-gfx input events and triggers HW display update.
 */
static void wasi_gfx_dpy_refresh(DisplayChangeListener *dcl)
{
    static int refresh_count = 0;
    int64_t t0 = qemu_clock_get_ns(QEMU_CLOCK_REALTIME), t1, t2, t3;

    if (refresh_count < 5 || refresh_count % 60 == 0) {
        WASI_LOG("wasi-gfx: dpy_refresh #%d\n", refresh_count);
    }
    refresh_count++;
    wasi_gfx_display_poll_events();

    /*
     * Changes normally arrive through QEMU's dirty tracking: VGA redraws
     * what the guest changed and reports it via dpy_gfx_update (and text
     * consoles do the same as characters arrive).  As a safety net, every
     * FULL_REFRESH_EVERY frames (or every frame with WASI_GFX_FULL_REFRESH=1)
     * force a complete redraw and copy.
     *
     * Forcing a full redraw on every frame used to be the only mechanism,
     * but in graphics modes VGA answers an invalidate by building a new
     * display surface, so each frame became a reallocation; at 1280x800
     * that took over 30% of the CPU.
     *
     * Text consoles have no hardware behind them: ui/console-vc.c draws
     * their surface as characters arrive, so these calls are for graphic
     * consoles only.
     */
    static int always_full = -1;
    bool full;

    if (always_full < 0) {
        always_full = getenv("WASI_GFX_FULL_REFRESH") != NULL;
    }
    full = always_full || refresh_count % FULL_REFRESH_EVERY == 1;

    if (qemu_console_is_graphic(dcl->con)) {
        if (full) {
            graphic_hw_invalidate(dcl->con);
        }
        graphic_hw_update(dcl->con);
    }
    t1 = qemu_clock_get_ns(QEMU_CLOCK_REALTIME);

    DisplaySurface *surf = full ? qemu_console_surface(dcl->con) : NULL;
    if (surf) {
        wasi_gfx_display_blit((const uint8_t *)surface_data(surf),
                              surface_width(surf),
                              surface_height(surf),
                              surface_stride(surf));
    }
    t2 = qemu_clock_get_ns(QEMU_CLOCK_REALTIME);

    wasi_gfx_display_render();
    t3 = qemu_clock_get_ns(QEMU_CLOCK_REALTIME);

    refresh_stats.frames++;
    refresh_stats.hw_ns += t1 - t0;
    refresh_stats.blit_ns += t2 - t1;
    refresh_stats.render_ns += t3 - t2;
    refresh_stats.total_ns += t3 - t0;
    refresh_stats_report(t3);
}

/*
 * Called when QEMU's VGA emulation updates a rectangular region.
 * We copy the dirty pixels from QEMU's DisplaySurface into our
 * local framebuffer for later rendering on the next frame event.
 */
static void wasi_gfx_dpy_update(DisplayChangeListener *dcl,
                                 int x, int y, int w, int h)
{
    DisplaySurface *surf = qemu_console_surface(dcl->con);
    if (!surf) {
        return;
    }

    /*
     * Pass QEMU's real DisplaySurface straight through.  Under QEMU_BUILD
     * wasi_gfx_display_update reads it with surface_data()/surface_stride(),
     * so it must be the genuine object, not a look-alike struct.
     */
    wasi_gfx_display_update(surf, x, y, w, h);
}

/*
 * Called when the display surface changes (resize, format change).
 * We resize our local framebuffer to match.
 */
static void wasi_gfx_dpy_switch(DisplayChangeListener *dcl,
                                 DisplaySurface *new_surface)
{
    if (new_surface) {
        wasi_gfx_display_resize(surface_width(new_surface),
                                surface_height(new_surface));
        /* A new surface can have entirely new contents: copy all of it. */
        wasi_gfx_display_blit((const uint8_t *)surface_data(new_surface),
                              surface_width(new_surface),
                              surface_height(new_surface),
                              surface_stride(new_surface));
    }
}

static const DisplayChangeListenerOps wasi_gfx_dcl_ops = {
    .dpy_name       = "wasi-gfx",
    .dpy_refresh    = wasi_gfx_dpy_refresh,
    .dpy_gfx_update = wasi_gfx_dpy_update,
    .dpy_gfx_switch = wasi_gfx_dpy_switch,
};

/* ---- Initialization ---- */

/*
 * wasi_gfx_display_register_dcl - Register the wasi-gfx DCL with QEMU.
 *
 * This should be called after QEMU's display state is initialized.
 * It picks a console, initializes the wasi-gfx backend, and registers
 * our DisplayChangeListener.
 *
 * Console choice: if WASI_GFX_CONSOLE names an index, use it.  Otherwise
 * show the guest's display (the first graphic console).  Other consoles,
 * such as the monitor and serial "vc" consoles, are reachable at runtime
 * with Ctrl+Alt+<n>, as in QEMU's GTK and SDL front ends.
 *
 * Returns 0 on success, -1 on failure.
 */
static QemuConsole *wasi_gfx_pick_console(void)
{
    const char *env = getenv("WASI_GFX_CONSOLE");
    QemuConsole *con;
    QemuConsole *first_text = NULL;
    int i;

    if (env && *env) {
        int idx = atoi(env);
        con = qemu_console_lookup_by_index(idx);
        if (con) {
            fprintf(stderr, "wasi-gfx: using console %d (WASI_GFX_CONSOLE)\n",
                    idx);
            return con;
        }
        fprintf(stderr, "wasi-gfx: WASI_GFX_CONSOLE=%s not found, ignoring\n",
                env);
    }

    for (i = 0; (con = qemu_console_lookup_by_index(i)) != NULL; i++) {
        bool graphic = qemu_console_is_graphic(con);

        fprintf(stderr, "wasi-gfx: console %d is %s (Ctrl+Alt+%d)\n",
                i, graphic ? "graphic" : "text", i + 1);
        if (graphic) {
            fprintf(stderr, "wasi-gfx: selecting graphic console %d\n", i);
            return con;
        }
        if (!first_text) {
            first_text = con;
        }
    }

    return first_text;
}

/*
 * Show console <index> (0-based) and send input to it.  Bound to
 * Ctrl+Alt+<index+1>.  Re-registering the listener makes QEMU switch to
 * the new console's surface, which resizes the host display.
 */
void wasi_gfx_display_switch_console(int index)
{
    QemuConsole *con = qemu_console_lookup_by_index(index);

    if (!con) {
        fprintf(stderr, "wasi-gfx: no console %d\n", index);
        return;
    }
    if (con == wasi_gfx_dcl.con) {
        return;
    }

    fprintf(stderr, "wasi-gfx: switching to %s console %d\n",
            qemu_console_is_graphic(con) ? "graphic" : "text", index);
    unregister_displaychangelistener(&wasi_gfx_dcl);
    wasi_gfx_dcl.con = con;
    wasi_gfx_display_set_input_console(con);
    register_displaychangelistener(&wasi_gfx_dcl);
}

int wasi_gfx_display_register_dcl(void)
{
    QemuConsole *con;
    DisplaySurface *surf;
    int width = 1024;
    int height = 768;

    con = wasi_gfx_pick_console();
    if (!con) {
        fprintf(stderr, "wasi-gfx: no console found, deferring init\n");
        return -1;
    }

    /* Get current surface dimensions if available */
    surf = qemu_console_surface(con);
    if (surf) {
        width = surface_width(surf);
        height = surface_height(surf);
    }

    /* Initialize the wasi-gfx display backend */
    if (wasi_gfx_display_init(width, height) < 0) {
        fprintf(stderr, "wasi-gfx: failed to initialize display\n");
        return -1;
    }

    /* Register the DCL */
    memset(&wasi_gfx_dcl, 0, sizeof(wasi_gfx_dcl));
    wasi_gfx_dcl.ops = &wasi_gfx_dcl_ops;
    wasi_gfx_dcl.con = con;
    wasi_gfx_display_set_input_console(con);
    register_displaychangelistener(&wasi_gfx_dcl);

    fprintf(stderr, "wasi-gfx: display registered (%dx%d)\n", width, height);
    return 0;
}

/*
 * We use QEMU's type_init / module_init to auto-register on startup.
 * The QemuDisplay mechanism requires a DisplayType enum value which
 * we don't have (would need QAPI changes). Instead we use a
 * constructor that hooks in via QEMU's module_init(MODULE_INIT_QOM).
 *
 * However, for WASI we can't rely on constructors running at the
 * right time. So we also provide the explicit registration function
 * above that can be called from a patched main() or from a
 * -display none fallback path.
 */

/*
 * Auto-init approach: Override qemu_display_init() behavior.
 * When -display none is used (or no display specified), QEMU
 * skips display init. We hook in by having our main() wrapper
 * call wasi_gfx_display_register_dcl() after machine init.
 *
 * For now, we'll call this from an __attribute__((constructor))
 * or from a patched vl.c. The simplest approach is to have
 * the WASI _start (crt1) call it.
 *
 * Actually, since we're compiling this separately and linking
 * it in, we can just call it from our custom WASI main wrapper.
 */
