/*
 * Minimal gstdio.h for WASI - glib-wasi shim
 *
 * Provides GLib's wrapped POSIX file I/O functions.
 * On WASI these are simple wrappers around standard POSIX calls.
 */

#ifndef __G_STDIO_H__
#define __G_STDIO_H__

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

/*
 * g_open — wrapper around open()
 */
static inline int g_open(const char *filename, int flags, int mode)
{
    return open(filename, flags, mode);
}

/*
 * g_fopen — wrapper around fopen()
 */
static inline FILE *g_fopen(const char *filename, const char *mode)
{
    return fopen(filename, mode);
}

/*
 * g_rename — wrapper around rename()
 */
static inline int g_rename(const char *oldfilename, const char *newfilename)
{
    return rename(oldfilename, newfilename);
}

/*
 * g_remove — wrapper around remove()
 */
static inline int g_remove(const char *filename)
{
    return remove(filename);
}

/*
 * g_unlink — wrapper around unlink()
 */
static inline int g_unlink(const char *filename)
{
    return unlink(filename);
}

/*
 * g_mkdir — wrapper around mkdir()
 */
static inline int g_mkdir(const char *filename, int mode)
{
    return mkdir(filename, mode);
}

/*
 * g_mkdir_with_parents — create directory and all parent directories
 */
static inline int g_mkdir_with_parents(const char *pathname, int mode)
{
    char *path, *p;
    int ret;

    if (pathname == NULL || pathname[0] == '\0') {
        errno = EINVAL;
        return -1;
    }

    path = strdup(pathname);
    if (!path) return -1;

    /* Iterate through path components */
    for (p = path + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            ret = mkdir(path, mode);
            if (ret != 0 && errno != EEXIST) {
                free(path);
                return -1;
            }
            *p = '/';
        }
    }
    ret = mkdir(path, mode);
    free(path);
    if (ret != 0 && errno != EEXIST) {
        return -1;
    }
    return 0;
}

/*
 * g_stat — wrapper around stat()
 */
static inline int g_stat(const char *filename, struct stat *buf)
{
    return stat(filename, buf);
}

/*
 * g_lstat — wrapper around lstat() (same as stat on WASI)
 */
static inline int g_lstat(const char *filename, struct stat *buf)
{
    /* WASI doesn't distinguish symlinks */
    return stat(filename, buf);
}

/*
 * g_access — wrapper around access()
 */
static inline int g_access(const char *filename, int mode)
{
    return access(filename, mode);
}

/*
 * g_chmod — wrapper around chmod()
 * WASI may not fully support chmod, stub it.
 */
static inline int g_chmod(const char *filename, int mode)
{
    (void)filename; (void)mode;
    /* WASI has limited permission support */
    return 0;
}

/*
 * g_mkstemp — wrapper around mkstemp()
 */
static inline int g_mkstemp(char *tmpl)
{
    /* WASI SDK may or may not have mkstemp.
     * Provide a simple fallback. */
#ifdef __wasi__
    /* Simple implementation: try to create a unique file */
    size_t len = strlen(tmpl);
    static unsigned int counter = 0;
    int fd;

    if (len < 6) {
        errno = EINVAL;
        return -1;
    }

    /* Replace trailing XXXXXX */
    for (int attempts = 0; attempts < 100; attempts++) {
        unsigned int val = counter++;
        for (int i = 0; i < 6; i++) {
            tmpl[len - 6 + i] = 'a' + (val % 26);
            val /= 26;
        }
        fd = open(tmpl, O_RDWR | O_CREAT | O_EXCL, 0600);
        if (fd >= 0) return fd;
        if (errno != EEXIST) return -1;
    }
    errno = EEXIST;
    return -1;
#else
    return mkstemp(tmpl);
#endif
}

#endif /* __G_STDIO_H__ */
