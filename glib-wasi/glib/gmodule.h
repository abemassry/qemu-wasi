/*
 * Minimal GModule shim for QEMU wasm32-wasi builds.
 *
 * WASI doesn't support dynamic loading (dlopen/dlsym), so all
 * GModule operations are stubs that return failure.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef GLIB_WASI_GMODULE_H
#define GLIB_WASI_GMODULE_H

#include "glib.h"

typedef struct _GModule GModule;

typedef enum {
    G_MODULE_BIND_LAZY  = 1 << 0,
    G_MODULE_BIND_LOCAL = 1 << 1,
    G_MODULE_BIND_MASK  = 0x03
} GModuleFlags;

static inline gboolean g_module_supported(void) { return FALSE; }

static inline GModule *g_module_open(const gchar *file_name, GModuleFlags flags)
{
    (void)file_name; (void)flags;
    return NULL;
}

static inline gboolean g_module_close(GModule *module)
{
    (void)module;
    return TRUE;
}

static inline gboolean g_module_symbol(GModule *module, const gchar *symbol_name,
                                       gpointer *symbol)
{
    (void)module; (void)symbol_name;
    if (symbol) *symbol = NULL;
    return FALSE;
}

static inline const gchar *g_module_error(void)
{
    return "GModule not supported on WASI";
}

static inline const gchar *g_module_name(GModule *module)
{
    (void)module;
    return "";
}

#endif /* GLIB_WASI_GMODULE_H */
