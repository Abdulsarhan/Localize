/* loc_runtime.h - v0.3 - localization runtime loader
 *
 * USAGE:
 *   #define LOC_IMPLEMENTATION
 *   #include "loc.h"
 *
 *   // Load from file
 *   loc_file loc = loc_load("strings.en.loc");
 *
 *   // Get strings by English key
 *   const char *text = loc_get_string(&loc, "hello");
 *
 *   // Clean up when done
 *   loc_free(&loc);
 *
 * FILE FORMAT:
 *   [slot_count]   - (size_t) number of slots in the open-addressing hash map.
 *   [hash_map]     - (size_t array of slot_count entries) each slot holds an offset
 *                    into the strings section, or SIZE_MAX if the slot is empty.
 *                    Collisions are resolved by linear probing.
 *   [strings_size] - (size_t) size of the strings section in bytes.
 *   [strings]      - each entry is: english_key (null-terminated) + localized_string (null-terminated).
 *                    offsets in hash_map point to the start of an entry (the english_key).
 *
 * LICENSE:
 *   MIT.
 */

#ifndef LOC_H
#define LOC_H

#include <stdint.h>
#include <stddef.h>

#if defined(LOC_STATIC)
    #define LOCAPI static
#elif defined(_WIN32) || defined(_WIN64)
    #if defined(LOC_BUILD_DLL)
        #define LOCAPI __declspec(dllexport)
    #elif defined(LOC_USE_DLL)
        #define LOCAPI __declspec(dllimport)
    #else
        #define LOCAPI extern
    #endif
#else
    #define LOCAPI extern
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    unsigned char *file_buffer;
    size_t        *hash_map;
    unsigned char *strings;
    size_t         slot_count;
    size_t         strings_size;
} loc_file;

LOCAPI loc_file    loc_load(const char *file_path);
LOCAPI const char *loc_get_string(loc_file *loc, const char *english_key);
LOCAPI void        loc_free(loc_file *loc);

#ifdef __cplusplus
}
#endif

#endif /* LOC_H */

/* ===========================================================================
 *                          IMPLEMENTATION
 * ===========================================================================
 */
#ifdef LOC_IMPLEMENTATION

#include <stdlib.h>

#if defined(_WIN32) || defined(_WIN64)
    #include <windows.h>
#else
    #include <sys/stat.h>
    #include <unistd.h>
    #include <fcntl.h>
#endif

/* SIZE_MAX sentinel for empty slots */
#ifndef LOC_EMPTY_SLOT
    #define LOC_EMPTY_SLOT ((size_t)-1)
#endif

static uint32_t loc__hash_string(const char *str) {
    uint32_t hash = 2166136261u;
    const unsigned char *s = (const unsigned char *)str;
    while (*s) {
        hash = hash ^ (*s);
        hash = hash * 16777619u;
        s++;
    }
    return hash;
}

static size_t loc__strlen(const char *str) {
    const char *s = str;
    while (*s) s++;
    return s - str;
}

static int loc__strcmp(const char *s1, const char *s2) {
    while (*s1 && (*s1 == *s2)) { s1++; s2++; }
    return *(unsigned char *)s1 - *(unsigned char *)s2;
}

static size_t loc__get_file_size(const char *file_path) {
#if defined(_WIN32) || defined(_WIN64)
    WIN32_FILE_ATTRIBUTE_DATA file_info;
    if (GetFileAttributesExA(file_path, GetFileExInfoStandard, &file_info)) {
        LARGE_INTEGER size;
        size.LowPart  = file_info.nFileSizeLow;
        size.HighPart = file_info.nFileSizeHigh;
        return (size_t)size.QuadPart;
    }
    return 0;
#else
    struct stat st;
    if (stat(file_path, &st) == 0) return (size_t)st.st_size;
    return 0;
#endif
}

static unsigned char *loc__read_entire_file(const char *file_path, size_t *bytes_read) {
    size_t file_size = loc__get_file_size(file_path);
    if (file_size == 0) return NULL;

    unsigned char *file = (unsigned char *)malloc(file_size + 1);
    if (!file) return NULL;

#if defined(_WIN32) || defined(_WIN64)
    HANDLE hFile = CreateFileA(file_path, GENERIC_READ, FILE_SHARE_READ, NULL,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) { free(file); return NULL; }

    size_t total_read = 0;
    while (total_read < file_size) {
        DWORD to_read = (DWORD)((file_size - total_read) > 0xFFFFFFFFUL
                                ? 0xFFFFFFFFUL : (file_size - total_read));
        DWORD chunk = 0;
        if (!ReadFile(hFile, file + total_read, to_read, &chunk, NULL) || chunk == 0) {
            CloseHandle(hFile); free(file); return NULL;
        }
        total_read += chunk;
    }
    CloseHandle(hFile);
#else
    int fd = open(file_path, O_RDONLY);
    if (fd == -1) { free(file); return NULL; }

    size_t total_read = 0;
    while (total_read < file_size) {
        ssize_t result = read(fd, file + total_read, file_size - total_read);
        if (result <= 0) { close(fd); free(file); return NULL; }
        total_read += (size_t)result;
    }
    close(fd);
#endif

    file[file_size] = '\0';
    *bytes_read = file_size;
    return file;
}

LOCAPI loc_file loc_load(const char *file_path) {
    loc_file loc = {0};
    size_t file_size = 0;

    loc.file_buffer = loc__read_entire_file(file_path, &file_size);
    if (!loc.file_buffer || file_size < sizeof(size_t) * 2) return loc;

    unsigned char *ptr = loc.file_buffer;

    /* slot_count */
    loc.slot_count = *((size_t *)ptr);
    ptr += sizeof(size_t);

    /* hash_map: slot_count offsets */
    loc.hash_map = (size_t *)ptr;
    ptr += loc.slot_count * sizeof(size_t);

    /* strings_size + strings */
    loc.strings_size = *((size_t *)ptr);
    ptr += sizeof(size_t);

    loc.strings = ptr;

    return loc;
}

LOCAPI const char *loc_get_string(loc_file *loc, const char *english_key) {
    if (!loc || !loc->hash_map || !loc->strings || loc->slot_count == 0) return NULL;

    uint32_t hash        = loc__hash_string(english_key);
    size_t   slot_index  = hash % loc->slot_count;

    for (size_t probe = 0; probe < loc->slot_count; probe++) {
        size_t slot = loc->hash_map[slot_index];

        if (slot == LOC_EMPTY_SLOT) return NULL; /* empty slot: key not present */

        if (slot < loc->strings_size) {
            const char *stored_key = (const char *)(loc->strings + slot);
            if (loc__strcmp(stored_key, english_key) == 0) {
                /* skip past the English key to get the localized string */
                return stored_key + loc__strlen(stored_key) + 1;
            }
        }

        /* collision: linear probe */
        slot_index = (slot_index + 1) % loc->slot_count;
    }

    return NULL;
}

LOCAPI void loc_free(loc_file *loc) {
    if (loc && loc->file_buffer) {
        free(loc->file_buffer);
        loc->file_buffer = NULL;
        loc->hash_map    = NULL;
        loc->strings     = NULL;
        loc->slot_count  = 0;
        loc->strings_size = 0;
    }
}

#endif /* LOC_IMPLEMENTATION */
