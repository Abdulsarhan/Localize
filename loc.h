/* loc_runtime.h - v0.2 - localization runtime loader
 *
 * USAGE:
 *   #define LOC_RUNTIME_IMPLEMENTATION
 *   #include "loc_runtime.h"
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
 *   [bucket_offset_table_size] - (size_t) size of bucket_offset_table in bytes.
 *   [bucket_offset_table]      - (size_t array), one offset per bucket. Offsets are relative to start of bucket_list.
 *   [bucket_list_size]         - (size_t) size of bucket_list in bytes.
 *   [bucket_list]              - each bucket is: offset_count (size_t) + offsets_to_strings (count * size_t) offsets are relative to start of strings.
 *   [strings_size]             - (size_t) size of the strings section in bytes.
 *   [strings]                  - each entry is: english_key (null-terminated) + localized_string (null-terminated)
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
    #if defined(ARENA_BUILD_DLL)
        #define LOCAPI __declspec(dllexport)
    #elif defined(ARENA_USE_DLL)
        #define LOCAPI __declspec(dllimport)
    #else
        #define LOCAPI extern
    #endif
#else
    #define LOCAPI extern
#endif // LOCAPI


#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    unsigned char *file_buffer;
    size_t *bucket_offset_table;
    unsigned char *bucket_list;
    unsigned char *strings;
    size_t bucket_count;
    size_t bucket_list_size;
    size_t strings_size;
} loc_file;

LOCAPI loc_file loc_load(const char *file_path);
LOCAPI const char *loc_get_string(loc_file *loc, const char *english_key);
LOCAPI void loc_free(loc_file *loc);

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

static uint32_t loc_hash_string(const char *str) {
    uint32_t hash = 2166136261u;
    const unsigned char *s = (const unsigned char *)str;
    
    while(*s) {
        hash = hash ^ (*s);
        hash = hash * 16777619u;
        s++;
    }
    
    return hash;
}

static size_t loc_strlen(const char *str) {
    const char *s = str;
    while (*s) s++;
    return s - str;
}

static int loc_strcmp(const char *s1, const char *s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(unsigned char *)s1 - *(unsigned char *)s2;
}

static size_t loc_get_file_size(const char *file_path) {
#if defined(_WIN32) || defined(_WIN64)
    WIN32_FILE_ATTRIBUTE_DATA file_info;
    if (GetFileAttributesExA(file_path, GetFileExInfoStandard, &file_info)) {
        LARGE_INTEGER size;
        size.LowPart = file_info.nFileSizeLow;
        size.HighPart = file_info.nFileSizeHigh;
        return (size_t)size.QuadPart;
    }
    return 0;
#else
    struct stat fstat;
    int err = stat(file_path, &fstat);
    if(!err) {
        return fstat.st_size;
    }
    return 0;
#endif
}

static unsigned char *loc_read_entire_file(const char *file_path, size_t *bytes_read) {
    size_t file_size = loc_get_file_size(file_path);
    if (file_size == 0) {
        return NULL;
    }

    unsigned char *file = (unsigned char *)malloc(file_size + 1);
    if (!file) {
        return NULL;
    }

#if defined(_WIN32) || defined(_WIN64)
    HANDLE hFile = CreateFileA(file_path, GENERIC_READ, FILE_SHARE_READ, NULL,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        free(file);
        return NULL;
    }

    size_t total_read = 0;
    while (total_read < file_size) {
        DWORD to_read = (DWORD)((file_size - total_read) > 0xFFFFFFFFUL ? 0xFFFFFFFFUL : (file_size - total_read));
        DWORD bytes_read_chunk = 0;
        
        if (!ReadFile(hFile, file + total_read, to_read, &bytes_read_chunk, NULL) || bytes_read_chunk == 0) {
            CloseHandle(hFile);
            free(file);
            return NULL;
        }
        total_read += bytes_read_chunk;
    }

    CloseHandle(hFile);
#else
    int fd = open(file_path, O_RDONLY);
    if (fd == -1) {
        free(file);
        return NULL;
    }

    size_t total_read = 0;
    while (total_read < file_size) {
        ssize_t result = read(fd, file + total_read, file_size - total_read);
        if (result <= 0) {
            close(fd);
            free(file);
            return NULL;
        }
        total_read += result;
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
    
    loc.file_buffer = loc_read_entire_file(file_path, &file_size);
    if (!loc.file_buffer || file_size < sizeof(size_t) * 3) {
        return loc;
    }
    
    unsigned char *ptr = loc.file_buffer;
    
    size_t bucket_offset_table_size = *((size_t *)ptr);
    ptr += sizeof(size_t);
    
    loc.bucket_offset_table = (size_t *)ptr;
    loc.bucket_count = bucket_offset_table_size / sizeof(size_t);
    ptr += bucket_offset_table_size;
    
    loc.bucket_list_size = *((size_t *)ptr);
    ptr += sizeof(size_t);
    
    loc.bucket_list = ptr;
    ptr += loc.bucket_list_size;
    
    loc.strings_size = *((size_t *)ptr);
    ptr += sizeof(size_t);
    
    loc.strings = ptr;
    
    return loc;
}

LOCAPI const char *loc_get_string(loc_file *loc, const char *english_key) {
    if(!loc || !loc->bucket_offset_table || !loc->strings || loc->bucket_count == 0) {
        return NULL;
    }
    
    uint32_t hash = loc_hash_string(english_key);
    size_t bucket_index = hash % loc->bucket_count;
    
    size_t bucket_offset = loc->bucket_offset_table[bucket_index];
    unsigned char *bucket_ptr = loc->bucket_list + bucket_offset;
    
    // Read bucket: count followed by offsets
    size_t count = *((size_t *)bucket_ptr);
    bucket_ptr += sizeof(size_t);
    size_t *offsets = (size_t *)bucket_ptr;
    
    for(size_t i = 0; i < count; i++) {
        size_t string_offset = offsets[i];
        if(string_offset >= loc->strings_size) {
            continue;  // Invalid offset
        }
        
        // Format: [english_key:null-terminated][localized_string:null-terminated]
        const char *stored_english = (const char *)(loc->strings + string_offset);
        
        if(loc_strcmp(stored_english, english_key) == 0) {
            // Found a match! Skip past the English key to get the localized string
            size_t english_len = loc_strlen(stored_english);
            return stored_english + english_len + 1;  // +1 for null terminator
        }
    }
    
    return NULL;  // Not found
}

LOCAPI void loc_free(loc_file *loc) {
    if(loc && loc->file_buffer) {
        free(loc->file_buffer);
        loc->file_buffer = NULL;
        loc->bucket_offset_table = NULL;
        loc->bucket_list = NULL;
        loc->strings = NULL;
        loc->bucket_count = 0;
        loc->bucket_list_size = 0;
        loc->strings_size = 0;
    }
}

#endif /* LOC_IMPLEMENTATION */
