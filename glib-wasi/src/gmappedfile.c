/*
 * GMappedFile minimal implementation for QEMU wasm32-wasi.
 *
 * Uses read() since WASI mmap is emulated and may not support
 * MAP_PRIVATE with a file descriptor.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

#include "../glib/glib.h"
#include "../glib/gerror.h"

struct _GMappedFile {
    gchar   *contents;
    gsize    length;
    gint     ref_count;
};

GMappedFile *g_mapped_file_new(const gchar *filename, gboolean writable,
                               GError **error)
{
    int fd = open(filename, writable ? O_RDWR : O_RDONLY);
    if (fd < 0) {
        if (error) {
            *error = g_error_new(0, 0, "Failed to open '%s'", filename);
        }
        return NULL;
    }
    GMappedFile *file = g_mapped_file_new_from_fd(fd, writable, error);
    close(fd);
    return file;
}

GMappedFile *g_mapped_file_new_from_fd(gint fd, gboolean writable,
                                       GError **error)
{
    struct stat st;
    if (fstat(fd, &st) < 0) {
        if (error) {
            *error = g_error_new(0, 0, "fstat failed on fd %d", fd);
        }
        return NULL;
    }

    gsize length = (gsize)st.st_size;
    gchar *contents = (gchar *)g_malloc(length + 1);

    /*
     * Read the whole file regardless of the descriptor's current offset,
     * as mmap() would: callers such as QEMU's ELF loader have already
     * read() the headers through the same descriptor.  Reading from the
     * current offset shifted the contents, so every ELF segment and note
     * was taken from the wrong place.
     */
    gsize total_read = 0;
    while (total_read < length) {
        ssize_t n = pread(fd, contents + total_read, length - total_read,
                          (off_t)total_read);
        if (n <= 0) {
            break;
        }
        total_read += (gsize)n;
    }
    contents[total_read] = '\0';

    GMappedFile *file = (GMappedFile *)g_malloc(sizeof(GMappedFile));
    file->contents = contents;
    file->length = total_read;
    file->ref_count = 1;
    return file;
}

gchar *g_mapped_file_get_contents(GMappedFile *file)
{
    if (file == NULL) return NULL;
    return file->contents;
}

gsize g_mapped_file_get_length(GMappedFile *file)
{
    if (file == NULL) return 0;
    return file->length;
}

GMappedFile *g_mapped_file_ref(GMappedFile *file)
{
    if (file == NULL) return NULL;
    file->ref_count++;
    return file;
}

void g_mapped_file_unref(GMappedFile *file)
{
    if (file == NULL) return;
    if (--file->ref_count <= 0) {
        g_free(file->contents);
        g_free(file);
    }
}
