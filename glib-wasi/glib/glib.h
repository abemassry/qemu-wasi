/*
 * Minimal GLib shim for QEMU compiled to wasm32-wasi.
 *
 * This is NOT the full GLib library. It is a purpose-built minimal
 * replacement that provides just enough types, macros, and function
 * declarations for QEMU to compile against wasm32-wasi.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#ifndef GLIB_H_WASI_SHIM
#define GLIB_H_WASI_SHIM

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <limits.h>
#include <float.h>
#include <sys/types.h>  /* ssize_t */

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/*  Version information                                               */
/* ------------------------------------------------------------------ */

/* Our shim pretends to be glib 2.56 (minimum QEMU requires) */
#define GLIB_MAJOR_VERSION 2
#define GLIB_MINOR_VERSION 56
#define GLIB_MICRO_VERSION 0

#define GLIB_CHECK_VERSION(major, minor, micro)  \
    (GLIB_MAJOR_VERSION > (major) ||             \
     (GLIB_MAJOR_VERSION == (major) && GLIB_MINOR_VERSION > (minor)) || \
     (GLIB_MAJOR_VERSION == (major) && GLIB_MINOR_VERSION == (minor) && \
      GLIB_MICRO_VERSION >= (micro)))

/* ------------------------------------------------------------------ */
/*  Platform size constants (wasm32: pointers and long are 32-bit)    */
/* ------------------------------------------------------------------ */

#define GLIB_SIZEOF_VOID_P  4
#define GLIB_SIZEOF_SIZE_T  4
#define GLIB_SIZEOF_LONG    4

/* ------------------------------------------------------------------ */
/*  Core scalar typedefs                                              */
/* ------------------------------------------------------------------ */

typedef int            gboolean;
typedef char           gchar;
typedef unsigned char  guchar;
typedef short          gshort;
typedef unsigned short gushort;
typedef int            gint;
typedef unsigned int   guint;
typedef long           glong;
typedef unsigned long  gulong;

typedef int8_t         gint8;
typedef uint8_t        guint8;
typedef int16_t        gint16;
typedef uint16_t       guint16;
typedef int32_t        gint32;
typedef uint32_t       guint32;
typedef int64_t        gint64;
typedef uint64_t       guint64;

typedef float          gfloat;
typedef double         gdouble;

typedef size_t         gsize;
typedef ssize_t        gssize;
typedef int64_t        goffset;

typedef void *         gpointer;
typedef const void *   gconstpointer;

typedef uintptr_t      guintptr;
typedef intptr_t       gintptr;

/* ------------------------------------------------------------------ */
/*  GQuark                                                            */
/* ------------------------------------------------------------------ */

typedef guint32 GQuark;

/* ------------------------------------------------------------------ */
/*  Common callback typedefs                                          */
/* ------------------------------------------------------------------ */

typedef void     (*GDestroyNotify) (gpointer data);
typedef void     (*GFunc)          (gpointer data, gpointer user_data);
typedef gint     (*GCompareFunc)   (gconstpointer a, gconstpointer b);
typedef gint     (*GCompareDataFunc)(gconstpointer a, gconstpointer b,
                                     gpointer user_data);
typedef gboolean (*GEqualFunc)     (gconstpointer a, gconstpointer b);
typedef guint    (*GHashFunc)      (gconstpointer key);
typedef void     (*GHFunc)         (gpointer key, gpointer value,
                                    gpointer user_data);
typedef gboolean (*GHRFunc)        (gpointer key, gpointer value,
                                    gpointer user_data);
typedef void     (*GFreeFunc)      (gpointer data);
typedef gpointer (*GCopyFunc)      (gconstpointer src, gpointer user_data);

/* ------------------------------------------------------------------ */
/*  GStrv = NULL-terminated string array                              */
/* ------------------------------------------------------------------ */

typedef gchar **GStrv;

/* ------------------------------------------------------------------ */
/*  GIOCondition (from GLib's GIOChannel)                             */
/* ------------------------------------------------------------------ */

typedef enum {
    G_IO_IN   = 1 << 0,
    G_IO_OUT  = 1 << 2,
    G_IO_PRI  = 1 << 1,
    G_IO_ERR  = 1 << 3,
    G_IO_HUP  = 1 << 4,
    G_IO_NVAL = 1 << 5
} GIOCondition;

/* ------------------------------------------------------------------ */
/*  GHook types (minimal stubs)                                       */
/* ------------------------------------------------------------------ */

typedef void (*GHookFunc)(gpointer data);
typedef struct _GHookList GHookList;
typedef struct _GHook GHook;

/* ------------------------------------------------------------------ */
/*  Boolean / NULL constants                                          */
/* ------------------------------------------------------------------ */

#ifndef TRUE
#define TRUE  1
#endif

#ifndef FALSE
#define FALSE 0
#endif

#ifndef NULL
#define NULL ((void *)0)
#endif

/* ------------------------------------------------------------------ */
/*  MIN / MAX / CLAMP / ABS                                           */
/* ------------------------------------------------------------------ */

#ifndef MIN
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#endif

#ifndef MAX
#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#endif

#ifndef ABS
#define ABS(a) (((a) < 0) ? -(a) : (a))
#endif

#ifndef CLAMP
#define CLAMP(x, lo, hi) (((x) < (lo)) ? (lo) : (((x) > (hi)) ? (hi) : (x)))
#endif

/* ------------------------------------------------------------------ */
/*  Numeric limits                                                    */
/* ------------------------------------------------------------------ */

#define G_MAXINT     INT_MAX
#define G_MAXUINT    UINT_MAX
#define G_MININT     INT_MIN

#define G_MAXINT8    INT8_MAX
#define G_MAXUINT8   UINT8_MAX
#define G_MAXINT16   INT16_MAX
#define G_MAXUINT16  UINT16_MAX
#define G_MAXINT32   INT32_MAX
#define G_MAXUINT32  UINT32_MAX
#define G_MAXINT64   INT64_MAX
#define G_MAXUINT64  UINT64_MAX

#define G_MAXSIZE    SIZE_MAX
#define G_MAXSSIZE   SSIZE_MAX

#define G_MINFLOAT   FLT_MIN
#define G_MAXFLOAT   FLT_MAX
#define G_MINDOUBLE  DBL_MIN
#define G_MAXDOUBLE  DBL_MAX

/* ------------------------------------------------------------------ */
/*  printf format macros                                              */
/* ------------------------------------------------------------------ */

/* wasm32: int64_t is long long, size_t is unsigned long */
#define G_GINT64_FORMAT   PRId64
#define G_GUINT64_FORMAT  PRIu64

/* wasm32-wasi: size_t is 32-bit unsigned (zu works, but glib uses "u") */
#define G_GSIZE_FORMAT    "u"

/* goffset is int64_t */
#define G_GOFFSET_FORMAT  G_GINT64_FORMAT

/* Pull in PRI* macros */
#ifndef __STDC_FORMAT_MACROS
#define __STDC_FORMAT_MACROS
#endif
#include <inttypes.h>

/* ------------------------------------------------------------------ */
/*  Pointer ↔ integer casting macros                                  */
/* ------------------------------------------------------------------ */

#define GINT_TO_POINTER(i)    ((gpointer)(gintptr)(i))
#define GPOINTER_TO_INT(p)    ((gint)(gintptr)(p))
#define GUINT_TO_POINTER(u)   ((gpointer)(guintptr)(u))
#define GPOINTER_TO_UINT(p)   ((guint)(guintptr)(p))
#define GSIZE_TO_POINTER(s)   ((gpointer)(gsize)(s))
#define GPOINTER_TO_SIZE(p)   ((gsize)(p))

/* ------------------------------------------------------------------ */
/*  Compiler hint / attribute macros                                  */
/* ------------------------------------------------------------------ */

#if defined(__GNUC__) || defined(__clang__)
#define G_LIKELY(expr)       __builtin_expect(!!(expr), 1)
#define G_UNLIKELY(expr)     __builtin_expect(!!(expr), 0)
#define G_GNUC_UNUSED        __attribute__((unused))
#define G_GNUC_PRINTF(f, a)  __attribute__((format(printf, f, a)))
#define G_GNUC_NULL_TERMINATED __attribute__((sentinel))
#define G_GNUC_WARN_UNUSED_RESULT __attribute__((warn_unused_result))
#define G_GNUC_NORETURN      __attribute__((__noreturn__))
#define G_NORETURN           _Noreturn
#define G_GNUC_DEPRECATED    __attribute__((__deprecated__))
#define G_GNUC_MALLOC        __attribute__((__malloc__))
#define G_GNUC_CONST         __attribute__((__const__))
#define G_GNUC_PURE          __attribute__((__pure__))
#else
#define G_LIKELY(expr)       (expr)
#define G_UNLIKELY(expr)     (expr)
#define G_GNUC_UNUSED
#define G_GNUC_PRINTF(f, a)
#define G_GNUC_NULL_TERMINATED
#define G_GNUC_WARN_UNUSED_RESULT
#define G_GNUC_NORETURN
#define G_NORETURN
#define G_GNUC_DEPRECATED
#define G_GNUC_MALLOC
#define G_GNUC_CONST
#define G_GNUC_PURE
#endif

/* ------------------------------------------------------------------ */
/*  Static assert / array size                                        */
/* ------------------------------------------------------------------ */

#define G_STATIC_ASSERT(expr)  _Static_assert(expr, "G_STATIC_ASSERT failed: " #expr)

#define G_N_ELEMENTS(arr)  (sizeof(arr) / sizeof((arr)[0]))

/* ------------------------------------------------------------------ */
/*  Statement macros                                                  */
/* ------------------------------------------------------------------ */

#define G_STMT_START  do
#define G_STMT_END    while (0)

/* ------------------------------------------------------------------ */
/*  Source location macros                                             */
/* ------------------------------------------------------------------ */

#define G_STRLOC  __FILE__ ":" G_STRINGIFY(__LINE__)
#define G_STRFUNC ((const char *)(__func__))

/* Stringify helper */
#define G_STRINGIFY_ARG(x) #x
#define G_STRINGIFY(x)     G_STRINGIFY_ARG(x)

/* ------------------------------------------------------------------ */
/*  Memory allocation — function declarations                         */
/*  (implementations live in src/gmem.c)                              */
/* ------------------------------------------------------------------ */

gpointer g_malloc       (gsize n_bytes);
gpointer g_malloc0      (gsize n_bytes);
gpointer g_realloc      (gpointer mem, gsize n_bytes);
void     g_free         (gpointer mem);

gpointer g_try_malloc   (gsize n_bytes);
gpointer g_try_malloc0  (gsize n_bytes);
gpointer g_try_realloc  (gpointer mem, gsize n_bytes);

gpointer g_memdup       (gconstpointer mem, guint byte_size);
gpointer g_memdup2      (gconstpointer mem, gsize byte_size);

/* Overflow-checked array allocators */
gpointer g_malloc_n     (gsize n_blocks, gsize n_block_bytes);
gpointer g_malloc0_n    (gsize n_blocks, gsize n_block_bytes);
gpointer g_realloc_n    (gpointer mem, gsize n_blocks, gsize n_block_bytes);

/* GSlice — thin wrappers around g_malloc / g_free */
gpointer g_slice_alloc  (gsize block_size);
gpointer g_slice_alloc0 (gsize block_size);
void     g_slice_free1  (gsize block_size, gpointer mem_block);

/* ------------------------------------------------------------------ */
/*  Convenience allocation macros                                     */
/* ------------------------------------------------------------------ */

#define g_new(type, count)       ((type *)g_malloc_n((count), sizeof(type)))
#define g_new0(type, count)      ((type *)g_malloc0_n((count), sizeof(type)))
#define g_renew(type, mem, count) \
    ((type *)g_realloc_n((mem), (count), sizeof(type)))

/* try_ variants — return NULL on failure instead of aborting */
gpointer g_try_malloc_n  (gsize n_blocks, gsize n_block_bytes);
gpointer g_try_malloc0_n (gsize n_blocks, gsize n_block_bytes);
gpointer g_try_realloc_n (gpointer mem, gsize n_blocks, gsize n_block_bytes);

#define g_try_new(type, count)   ((type *)g_try_malloc_n((count), sizeof(type)))
#define g_try_new0(type, count)  ((type *)g_try_malloc0_n((count), sizeof(type)))
#define g_try_renew(type, mem, count) \
    ((type *)g_try_realloc_n((mem), (count), sizeof(type)))

gpointer g_slice_copy  (gsize block_size, gconstpointer mem_block);

#define g_slice_new(type)   ((type *)g_slice_alloc(sizeof(type)))
#define g_slice_new0(type)  ((type *)g_slice_alloc0(sizeof(type)))
#define g_slice_free(type, ptr) \
    G_STMT_START { g_slice_free1(sizeof(type), (ptr)); } G_STMT_END
#define g_slice_dup(type, mem) ((type *)g_slice_copy(sizeof(type), (mem)))

/* ------------------------------------------------------------------ */
/*  g_steal_pointer / g_clear_pointer                                 */
/* ------------------------------------------------------------------ */

/*
 * g_steal_pointer: atomically read *pp, set *pp = NULL, return old value.
 * The real GLib version uses _Generic for type safety; we keep it simple.
 */
static inline gpointer
g_steal_pointer_impl(gpointer *pp)
{
    gpointer p = *pp;
    *pp = NULL;
    return p;
}

#define g_steal_pointer(pp) \
    (0 ? (*(pp)) : g_steal_pointer_impl((gpointer *)(pp)))

#define g_clear_pointer(pp, destroy) \
    G_STMT_START {                                       \
        gpointer _cp_temp = *(pp);                       \
        if (_cp_temp != NULL) {                           \
            *(pp) = NULL;                                 \
            ((GDestroyNotify)(destroy))(_cp_temp);        \
        }                                                 \
    } G_STMT_END

/* ------------------------------------------------------------------ */
/*  g_autofree (no-op if compiler lacks cleanup attribute)            */
/* ------------------------------------------------------------------ */

#if defined(__GNUC__) || defined(__clang__)
static inline void g_autoptr_cleanup_generic_gfree(void *p) {
    void **pp = (void **)p;
    g_free(*pp);
}
#define g_autofree __attribute__((cleanup(g_autoptr_cleanup_generic_gfree)))
#else
#define g_autofree
#endif

/* ------------------------------------------------------------------ */
/*  GLib autoptr infrastructure                                       */
/*  QEMU uses G_DEFINE_AUTOPTR_CLEANUP_FUNC extensively in QAPI code  */
/* ------------------------------------------------------------------ */

#if defined(__GNUC__) || defined(__clang__)

#define G_DEFINE_AUTOPTR_CLEANUP_FUNC(TypeName, func) \
    typedef TypeName *_GLIB_AUTOPTR_TYPENAME(TypeName);  \
    static inline void _GLIB_AUTOPTR_CLEAR_FUNC_NAME(TypeName)(TypeName **_ptr) { \
        if (*_ptr) { (func)(*_ptr); *_ptr = NULL; } \
    } \
    static inline void _GLIB_AUTOPTR_CLEANUP_FUNC_NAME(TypeName)(TypeName **_ptr) { \
        _GLIB_AUTOPTR_CLEAR_FUNC_NAME(TypeName)(_ptr); \
    }

#define _GLIB_AUTOPTR_TYPENAME(TypeName) _GLibAutoPtr_##TypeName
#define _GLIB_AUTOPTR_CLEAR_FUNC_NAME(TypeName) _glib_auto_clear_##TypeName
#define _GLIB_AUTOPTR_CLEANUP_FUNC_NAME(TypeName) _glib_auto_cleanup_##TypeName

#define g_autoptr(TypeName) \
    __attribute__((cleanup(_GLIB_AUTOPTR_CLEANUP_FUNC_NAME(TypeName)))) \
    _GLIB_AUTOPTR_TYPENAME(TypeName)

#define G_DEFINE_AUTO_CLEANUP_CLEAR_FUNC(TypeName, func) \
    static inline void _glib_auto_cleanup_clear_##TypeName(TypeName *_ptr) { \
        (func)(_ptr); \
    }

#define g_auto(TypeName) \
    __attribute__((cleanup(_glib_auto_cleanup_clear_##TypeName))) TypeName

#define G_DEFINE_AUTO_CLEANUP_FREE_FUNC(TypeName, func, none) \
    static inline void _glib_auto_cleanup_free_##TypeName(TypeName *_ptr) { \
        if (*_ptr != none) (func)(*_ptr); \
    } \
    static inline void _glib_auto_cleanup_clear_##TypeName(TypeName *_ptr) { \
        if (*_ptr != none) (func)(*_ptr); \
    }

#else /* no cleanup attribute support */

#define G_DEFINE_AUTOPTR_CLEANUP_FUNC(TypeName, func)
#define g_autoptr(TypeName)  TypeName *
#define G_DEFINE_AUTO_CLEANUP_CLEAR_FUNC(TypeName, func)
#define g_auto(TypeName)     TypeName
#define G_DEFINE_AUTO_CLEANUP_FREE_FUNC(TypeName, func, none)

#endif

/* ------------------------------------------------------------------ */
/*  Forward declarations for types defined in sub-headers             */
/* ------------------------------------------------------------------ */

#ifndef GLIB_WASI_GERROR_DEFINED
#define GLIB_WASI_GERROR_DEFINED
typedef struct _GError GError;
#endif

/* Forward declare GHashTable (defined in ghash.h) */
#ifndef GLIB_WASI_GHASH_TABLE_FORWARD_DECLARED
#define GLIB_WASI_GHASH_TABLE_FORWARD_DECLARED
typedef struct _GHashTable GHashTable;
#endif

/* ------------------------------------------------------------------ */
/*  Additional constants                                              */
/* ------------------------------------------------------------------ */

#define G_USEC_PER_SEC      1000000
#define G_SOURCE_CONTINUE   TRUE
#define G_SOURCE_REMOVE     FALSE

/* Time span constants (in microseconds) */
#define G_TIME_SPAN_SECOND  ((gint64) 1000000)
#define G_TIME_SPAN_MINUTE  ((gint64) 60000000)
#define G_TIME_SPAN_HOUR    ((gint64) 3600000000)
#define G_TIME_SPAN_DAY     ((gint64) 86400000000)

#define G_IS_DIR_SEPARATOR(c) ((c) == '/')

/* ------------------------------------------------------------------ */
/*  Additional scalar typedefs                                        */
/* ------------------------------------------------------------------ */

typedef guint32 gunichar;
typedef int     GPid;

/* ------------------------------------------------------------------ */
/*  Opaque type forward declarations                                  */
/* ------------------------------------------------------------------ */

typedef struct _GData GData;

/* ------------------------------------------------------------------ */
/*  GSpawnFlags                                                       */
/* ------------------------------------------------------------------ */

typedef int GSpawnFlags;
#define G_SPAWN_STDOUT_TO_DEV_NULL  8
#define G_SPAWN_CLOEXEC_PIPES       256
#define G_SPAWN_DO_NOT_REAP_CHILD   2
#define G_SPAWN_SEARCH_PATH         4
#define G_SPAWN_STDERR_TO_DEV_NULL  16
#define G_SPAWN_CHILD_INHERITS_STDIN 32
#define G_SPAWN_FILE_AND_ARGV_ZERO  64

/* ------------------------------------------------------------------ */
/*  GDateTime                                                         */
/* ------------------------------------------------------------------ */

typedef struct _GDateTime GDateTime;
void g_date_time_unref(GDateTime *dt);

/* ------------------------------------------------------------------ */
/*  GUri                                                              */
/* ------------------------------------------------------------------ */

typedef struct _GUri GUri;
#define G_URI_FLAGS_NONE  0
#define G_URI_PARAMS_NONE 0

GUri        *g_uri_parse       (const gchar *uri_string, gint flags, GError **error);
const gchar *g_uri_get_scheme  (GUri *uri);
const gchar *g_uri_get_host    (GUri *uri);
const gchar *g_uri_get_path    (GUri *uri);
gint         g_uri_get_port    (GUri *uri);
const gchar *g_uri_get_query   (GUri *uri);
GHashTable  *g_uri_parse_params(const gchar *params, gssize length,
                                const gchar *separators, gint flags,
                                GError **error);
void         g_uri_unref       (GUri *uri);

/* ------------------------------------------------------------------ */
/*  GTreeNode / GTraverseNodeFunc                                     */
/* ------------------------------------------------------------------ */

typedef struct _GTreeNode GTreeNode;
typedef gboolean (*GTraverseNodeFunc)(GTreeNode *node, gpointer data);

/* ------------------------------------------------------------------ */
/*  GChecksum / GChecksumType                                         */
/* ------------------------------------------------------------------ */

typedef enum {
    G_CHECKSUM_MD5    = 0,
    G_CHECKSUM_SHA1   = 1,
    G_CHECKSUM_SHA256 = 2,
    G_CHECKSUM_SHA384 = 3,
    G_CHECKSUM_SHA512 = 4
} GChecksumType;

typedef struct _GChecksum GChecksum;

GChecksum *g_checksum_new           (GChecksumType type);
void       g_checksum_free          (GChecksum *checksum);
void       g_checksum_update        (GChecksum *checksum, const guchar *data,
                                     gssize length);
void       g_checksum_get_digest    (GChecksum *checksum, guint8 *buffer,
                                     gsize *digest_len);
gssize     g_checksum_type_get_length(GChecksumType type);

/* ------------------------------------------------------------------ */
/*  GHmac                                                             */
/* ------------------------------------------------------------------ */

typedef struct _GHmac GHmac;

GHmac *g_hmac_new        (GChecksumType type, const guchar *key, gsize key_len);
void   g_hmac_update     (GHmac *hmac, const guchar *data, gssize length);
void   g_hmac_get_digest (GHmac *hmac, guint8 *buffer, gsize *digest_len);
void   g_hmac_unref      (GHmac *hmac);

/* ------------------------------------------------------------------ */
/*  GTimer                                                            */
/* ------------------------------------------------------------------ */

typedef struct _GTimer GTimer;

GTimer  *g_timer_new     (void);
gdouble  g_timer_elapsed (GTimer *timer, gulong *microseconds);
void     g_timer_destroy (GTimer *timer);

/* ------------------------------------------------------------------ */
/*  GMappedFile                                                       */
/* ------------------------------------------------------------------ */

typedef struct _GMappedFile GMappedFile;

GMappedFile *g_mapped_file_new         (const gchar *filename, gboolean writable,
                                        GError **error);
GMappedFile *g_mapped_file_new_from_fd (gint fd, gboolean writable, GError **error);
gchar       *g_mapped_file_get_contents(GMappedFile *file);
gsize        g_mapped_file_get_length  (GMappedFile *file);
GMappedFile *g_mapped_file_ref         (GMappedFile *file);
void         g_mapped_file_unref       (GMappedFile *file);

/* ------------------------------------------------------------------ */
/*  GSequence / GSequenceIter                                         */
/* ------------------------------------------------------------------ */

typedef struct _GSequence     GSequence;
typedef struct _GSequenceIter GSequenceIter;

GSequence     *g_sequence_new           (GDestroyNotify data_destroy);
GSequenceIter *g_sequence_insert_sorted (GSequence *seq, gpointer data,
                                         GCompareDataFunc cmp_func,
                                         gpointer cmp_data);
GSequenceIter *g_sequence_lookup        (GSequence *seq, gpointer data,
                                         GCompareDataFunc cmp_func,
                                         gpointer cmp_data);
void           g_sequence_remove        (GSequenceIter *iter);

/* ------------------------------------------------------------------ */
/*  GData stubs                                                       */
/* ------------------------------------------------------------------ */

gpointer g_datalist_get_data   (GData **datalist, const gchar *key);
void     g_datalist_set_data   (GData **datalist, const gchar *key,
                                gpointer data);
void     g_datalist_remove_data(GData **datalist, const gchar *key);

/* ------------------------------------------------------------------ */
/*  GOnce init helpers (single-threaded)                              */
/* ------------------------------------------------------------------ */

gboolean g_once_init_enter(volatile gsize *location);
void     g_once_init_leave(volatile gsize *location, gsize result);

/* ------------------------------------------------------------------ */
/*  GDateTime functions                                               */
/* ------------------------------------------------------------------ */

GDateTime *g_date_time_new_now_utc      (void);
GDateTime *g_date_time_new_now_local    (void);
GDateTime *g_date_time_new_from_unix_local(gint64 t);
GDateTime *g_date_time_new_from_unix_utc(gint64 t);
gchar     *g_date_time_format           (GDateTime *dt, const gchar *format);
gchar     *g_date_time_format_iso8601   (GDateTime *dt);
gint64     g_date_time_to_unix          (GDateTime *dt);
gint       g_date_time_get_microsecond  (GDateTime *dt);
gint       g_date_time_get_second       (GDateTime *dt);
gint       g_date_time_get_minute       (GDateTime *dt);
gint       g_date_time_get_hour         (GDateTime *dt);
gint       g_date_time_get_day_of_month (GDateTime *dt);
gint       g_date_time_get_month        (GDateTime *dt);
gint       g_date_time_get_year         (GDateTime *dt);

/* ------------------------------------------------------------------ */
/*  File utilities                                                    */
/* ------------------------------------------------------------------ */

gchar   *g_get_current_dir    (void);
gboolean g_file_get_contents  (const gchar *filename, gchar **contents,
                               gsize *length, GError **error);
gboolean g_file_set_contents  (const gchar *filename, const gchar *contents,
                               gssize length, GError **error);
gint     g_file_open_tmp      (const gchar *tmpl, gchar **name_used,
                               GError **error);
/* g_mkstemp is defined as static inline in gstdio.h */
#include "gstdio.h"

/* ------------------------------------------------------------------ */
/*  Base64 in-place decode                                            */
/* ------------------------------------------------------------------ */

guchar *g_base64_decode_inplace(gchar *text, gsize *out_len);

/* ------------------------------------------------------------------ */
/*  UTF-8 / pattern matching                                          */
/* ------------------------------------------------------------------ */

gboolean g_utf8_validate       (const gchar *str, gssize max_len,
                                const gchar **end);
gboolean g_pattern_match_simple(const gchar *pattern, const gchar *string);

/* ------------------------------------------------------------------ */
/*  Spawn stubs                                                       */
/* ------------------------------------------------------------------ */

gboolean g_spawn_async_with_pipes(const gchar *working_directory,
                                  gchar **argv, gchar **envp,
                                  GSpawnFlags flags,
                                  GFunc child_setup,
                                  gpointer user_data,
                                  GPid *child_pid,
                                  gint *standard_input,
                                  gint *standard_output,
                                  gint *standard_error,
                                  GError **error);
void     g_spawn_close_pid       (GPid pid);

/* ------------------------------------------------------------------ */
/*  GUnix stubs                                                       */
/* ------------------------------------------------------------------ */

gboolean g_unix_open_pipe         (gint *fds, gint flags, GError **error);
gboolean g_unix_set_fd_nonblocking(gint fd, gboolean nonblock,
                                   GError **error);

/* ------------------------------------------------------------------ */
/*  printf helpers (also declared in gprintf.h)                       */
/* ------------------------------------------------------------------ */

gint g_sprintf(gchar *string, const gchar *format, ...) G_GNUC_PRINTF(2, 3);

/* ------------------------------------------------------------------ */
/*  GTest stubs — QEMU's glib-compat.h references g_test_slow()      */
/* ------------------------------------------------------------------ */

static inline gboolean g_test_slow(void) { return FALSE; }
static inline void     g_test_skip(const gchar *msg) { (void)msg; }
static inline void     g_test_queue_destroy(GDestroyNotify destroy_func,
                                            gpointer destroy_data) {
    (void)destroy_func; (void)destroy_data;
}
static inline gboolean g_test_initialized(void) { return FALSE; }
static inline gboolean g_test_subprocess(void) { return FALSE; }
static inline void     g_test_message(const gchar *format, ...) { (void)format; }

#define g_assert_cmpmem(m1, l1, m2, l2) \
    g_assert(memcmp((m1), (m2), (l1)) == 0 && (gsize)(l1) == (gsize)(l2))

/* ------------------------------------------------------------------ */
/*  g_memdup2 (for GLIB_CHECK_VERSION < 2.68 compat in glib-compat.h)*/
/* ------------------------------------------------------------------ */

/* Already declared above; QEMU's glib-compat.h provides inline impl */

/* ------------------------------------------------------------------ */
/*  Include all sub-headers (umbrella header, like real GLib)         */
/* ------------------------------------------------------------------ */

#include "gmessages.h"
#include "gstrfuncs.h"
#include "gstring.h"
#include "garray.h"
#include "glist.h"
#include "ghash.h"
#include "gqueue.h"
#include "gerror.h"
#include "gthread.h"
#include "gatomic.h"
#include "gtree.h"
#include "gutils.h"
#include "grand.h"
#include "gmain.h"
#include "giochannel.h"

/* ------------------------------------------------------------------ */
/*  GMarkup stubs                                                     */
/*  QEMU uses g_markup_printf_escaped / g_markup_vprintf_escaped      */
/* ------------------------------------------------------------------ */

gchar *g_markup_printf_escaped(const gchar *format, ...) G_GNUC_PRINTF(1, 2);
gchar *g_markup_vprintf_escaped(const gchar *format, va_list args);
gchar *g_markup_escape_text(const gchar *text, gssize length);

/* ------------------------------------------------------------------ */
/*  GRegex minimal stub                                               */
/* ------------------------------------------------------------------ */

typedef struct _GRegex GRegex;
typedef struct _GMatchInfo GMatchInfo;

typedef enum {
    G_REGEX_CASELESS         = 1 << 0,
    G_REGEX_MULTILINE        = 1 << 1,
    G_REGEX_DOTALL           = 1 << 2,
    G_REGEX_EXTENDED         = 1 << 3,
    G_REGEX_ANCHORED         = 1 << 4,
    G_REGEX_UNGREEDY         = 1 << 9,
    G_REGEX_RAW              = 1 << 11,
    G_REGEX_OPTIMIZE         = 1 << 13,
    G_REGEX_DUPNAMES         = 1 << 19
} GRegexCompileFlags;

typedef enum {
    G_REGEX_MATCH_ANCHORED   = 1 << 4,
    G_REGEX_MATCH_NOTBOL     = 1 << 7,
    G_REGEX_MATCH_NOTEOL     = 1 << 8,
    G_REGEX_MATCH_NOTEMPTY   = 1 << 10,
    G_REGEX_MATCH_PARTIAL    = 1 << 15,
    G_REGEX_MATCH_NEWLINE_CR = 1 << 20,
    G_REGEX_MATCH_NEWLINE_LF = 1 << 21,
    G_REGEX_MATCH_NEWLINE_CRLF = 1 << 20 | 1 << 21
} GRegexMatchFlags;

/* ------------------------------------------------------------------ */
/*  Autoptr cleanup registrations                                     */
/*  Must come after all type-defining headers are included.           */
/* ------------------------------------------------------------------ */

#if defined(__GNUC__) || defined(__clang__)

/* GString: freed with g_string_free(s, TRUE) → need a wrapper */
static inline void _glib_autoptr_clear_GString(GString *s) {
    if (s) g_string_free(s, TRUE);
}
G_DEFINE_AUTOPTR_CLEANUP_FUNC(GString, _glib_autoptr_clear_GString)

/* GArray: freed with g_array_unref */
G_DEFINE_AUTOPTR_CLEANUP_FUNC(GArray, g_array_unref)

/* GPtrArray: freed with g_ptr_array_unref */
G_DEFINE_AUTOPTR_CLEANUP_FUNC(GPtrArray, g_ptr_array_unref)

/* GByteArray: freed with g_byte_array_unref */
G_DEFINE_AUTOPTR_CLEANUP_FUNC(GByteArray, g_byte_array_unref)

/* GBytes: freed with g_bytes_unref */
G_DEFINE_AUTOPTR_CLEANUP_FUNC(GBytes, g_bytes_unref)

/* GHashTable: freed with g_hash_table_unref */
G_DEFINE_AUTOPTR_CLEANUP_FUNC(GHashTable, g_hash_table_unref)

/* GError: freed with g_error_free */
G_DEFINE_AUTOPTR_CLEANUP_FUNC(GError, g_error_free)

/* GList: freed with g_list_free */
G_DEFINE_AUTOPTR_CLEANUP_FUNC(GList, g_list_free)

/* GSList: freed with g_slist_free */
G_DEFINE_AUTOPTR_CLEANUP_FUNC(GSList, g_slist_free)

/* GRegex: freed with g_regex_unref (stub) */
static inline void g_regex_unref(GRegex *r) { (void)r; }
G_DEFINE_AUTOPTR_CLEANUP_FUNC(GRegex, g_regex_unref)

/* GMatchInfo: freed with g_match_info_unref (stub) */
static inline void g_match_info_unref(GMatchInfo *m) { (void)m; }
G_DEFINE_AUTOPTR_CLEANUP_FUNC(GMatchInfo, g_match_info_unref)

/* gchar* (GStrv): autoptr support for plain strings */
static inline void _glib_autoptr_clear_gchar(gchar *s) {
    g_free(s);
}
G_DEFINE_AUTOPTR_CLEANUP_FUNC(gchar, _glib_autoptr_clear_gchar)

/* GStrv (gchar **): g_auto(GStrv) cleanup */
G_DEFINE_AUTO_CLEANUP_FREE_FUNC(GStrv, g_strfreev, NULL)

/* GDateTime: freed with g_date_time_unref */
G_DEFINE_AUTOPTR_CLEANUP_FUNC(GDateTime, g_date_time_unref)

/* GSource: freed with g_source_unref (declared in gmain.h) */
G_DEFINE_AUTOPTR_CLEANUP_FUNC(GSource, g_source_unref)

/* GUri: freed with g_uri_unref */
G_DEFINE_AUTOPTR_CLEANUP_FUNC(GUri, g_uri_unref)

/* GMainLoop: freed with g_main_loop_unref */
G_DEFINE_AUTOPTR_CLEANUP_FUNC(GMainLoop, g_main_loop_unref)

/* GMainContext: freed with g_main_context_unref */
G_DEFINE_AUTOPTR_CLEANUP_FUNC(GMainContext, g_main_context_unref)

#endif /* __GNUC__ || __clang__ */

#ifdef __cplusplus
}
#endif

#endif /* GLIB_H_WASI_SHIM */
