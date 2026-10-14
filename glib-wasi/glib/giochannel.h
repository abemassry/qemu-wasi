/*
 * Minimal GIOChannel for WASI - glib-wasi shim
 *
 * Provides the GIOChannel type, GIOStatus enum, GIOFlags,
 * and basic function declarations needed by QEMU.
 *
 * On WASI, GIOChannel is stubbed — QEMU's char-fd.c uses
 * GIOChannel* only as a callback parameter type, and the
 * QIOChannel abstraction handles actual I/O.
 */

#ifndef __G_IO_CHANNEL_H__
#define __G_IO_CHANNEL_H__

#include <stddef.h>

/* Forward declaration — GIOCondition is in glib.h */

/*
 * GIOChannel — opaque struct.
 * We provide just enough fields for code that accesses them directly.
 */
typedef struct _GIOChannel GIOChannel;

struct _GIOChannel {
    int fd;
    int ref_count;
    unsigned int is_readable : 1;
    unsigned int is_writeable : 1;
    unsigned int is_seekable : 1;
    unsigned int close_on_unref : 1;
    unsigned int use_buffer : 1;
};

/*
 * GIOStatus — return status for I/O operations
 */
typedef enum {
    G_IO_STATUS_ERROR  = 0,
    G_IO_STATUS_NORMAL = 1,
    G_IO_STATUS_EOF    = 2,
    G_IO_STATUS_AGAIN  = 3
} GIOStatus;

/*
 * GIOFlags
 */
typedef enum {
    G_IO_FLAG_APPEND       = 1 << 0,
    G_IO_FLAG_NONBLOCK     = 1 << 1,
    G_IO_FLAG_IS_READABLE  = 1 << 2,
    G_IO_FLAG_IS_WRITABLE  = 1 << 3,
    G_IO_FLAG_IS_WRITEABLE = 1 << 3,  /* alias */
    G_IO_FLAG_IS_SEEKABLE  = 1 << 4,
    G_IO_FLAG_MASK         = (1 << 5) - 1,
    G_IO_FLAG_GET_MASK     = G_IO_FLAG_MASK,
    G_IO_FLAG_SET_MASK     = G_IO_FLAG_APPEND | G_IO_FLAG_NONBLOCK
} GIOFlags;

/*
 * GIOError — legacy error type (deprecated in real GLib)
 */
typedef enum {
    G_IO_ERROR_NONE    = 0,
    G_IO_ERROR_AGAIN   = 1,
    G_IO_ERROR_INVAL   = 2,
    G_IO_ERROR_UNKNOWN = 3
} GIOError;

/* Callback type for g_io_add_watch */
typedef gboolean (*GIOFunc)(GIOChannel *source, GIOCondition condition,
                            gpointer data);

/*
 * GIOChannel function stubs.
 * All are implemented as static inline no-ops or minimal wrappers
 * since WASI doesn't have real GIOChannel support.
 */

static inline GIOChannel *g_io_channel_unix_new(int fd)
{
    GIOChannel *ch = (GIOChannel *)g_malloc0(sizeof(GIOChannel));
    ch->fd = fd;
    ch->ref_count = 1;
    ch->is_readable = 1;
    ch->is_writeable = 1;
    return ch;
}

static inline int g_io_channel_unix_get_fd(GIOChannel *channel)
{
    return channel ? channel->fd : -1;
}

static inline GIOChannel *g_io_channel_ref(GIOChannel *channel)
{
    if (channel) {
        channel->ref_count++;
    }
    return channel;
}

static inline void g_io_channel_unref(GIOChannel *channel)
{
    if (channel) {
        channel->ref_count--;
        if (channel->ref_count <= 0) {
            g_free(channel);
        }
    }
}

static inline GIOStatus g_io_channel_read_chars(GIOChannel *channel,
                                                  char *buf, gsize count,
                                                  gsize *bytes_read,
                                                  GError **error)
{
    (void)channel; (void)buf; (void)count;
    if (bytes_read) *bytes_read = 0;
    return G_IO_STATUS_EOF;
}

static inline GIOStatus g_io_channel_write_chars(GIOChannel *channel,
                                                   const char *buf,
                                                   gssize count,
                                                   gsize *bytes_written,
                                                   GError **error)
{
    (void)channel; (void)buf; (void)count;
    if (bytes_written) *bytes_written = 0;
    return G_IO_STATUS_ERROR;
}

static inline GIOStatus g_io_channel_flush(GIOChannel *channel,
                                            GError **error)
{
    (void)channel; (void)error;
    return G_IO_STATUS_NORMAL;
}

static inline GIOStatus g_io_channel_shutdown(GIOChannel *channel,
                                               gboolean flush_pending,
                                               GError **error)
{
    (void)channel; (void)flush_pending; (void)error;
    return G_IO_STATUS_NORMAL;
}

static inline GIOStatus g_io_channel_set_encoding(GIOChannel *channel,
                                                    const char *encoding,
                                                    GError **error)
{
    (void)channel; (void)encoding; (void)error;
    return G_IO_STATUS_NORMAL;
}

static inline void g_io_channel_set_buffered(GIOChannel *channel,
                                              gboolean buffered)
{
    if (channel) {
        channel->use_buffer = buffered ? 1 : 0;
    }
}

static inline GIOStatus g_io_channel_set_flags(GIOChannel *channel,
                                                 GIOFlags flags,
                                                 GError **error)
{
    (void)channel; (void)flags; (void)error;
    return G_IO_STATUS_NORMAL;
}

static inline void g_io_channel_set_close_on_unref(GIOChannel *channel,
                                                     gboolean do_close)
{
    if (channel) {
        channel->close_on_unref = do_close ? 1 : 0;
    }
}

static inline gboolean g_io_channel_get_close_on_unref(GIOChannel *channel)
{
    return channel ? channel->close_on_unref : FALSE;
}

static inline guint g_io_add_watch(GIOChannel *channel, GIOCondition condition,
                                    GIOFunc func, gpointer user_data)
{
    (void)channel; (void)condition; (void)func; (void)user_data;
    /* Can't add watches on WASI — return 0 (invalid source ID) */
    return 0;
}

#endif /* __G_IO_CHANNEL_H__ */
