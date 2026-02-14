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
    size_t *offset_table;
    unsigned char *string_data;
    size_t table_size;
    size_t data_size;
} loc_file;

LOCAPI loc_file loc_load(const char *file_path);
LOCAPI const char *loc_get_string(loc_file *loc, const char *english_key);
LOCAPI void loc_free(loc_file *loc);

#ifdef __cplusplus
}
#endif

#endif /* LOC_H */

#ifdef LOC_IMPLEMENTATION

#include <stdlib.h>

#if defined(_WIN32) || defined(_WIN64)
    #include <windows.h>
#else
    #include <sys/stat.h>
    #include <unistd.h>
    #include <fcntl.h>
#endif

/* FNV-1a hash function - must match the one used during generation */
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
    if (!loc.file_buffer || file_size < sizeof(size_t)) {
        return loc;
    }
    
    /* Parse the file structure */
    size_t *offsets = (size_t *)loc.file_buffer;
    
    /* Find table size by finding first non-zero offset */
    size_t first_data_offset = 0;
    size_t max_scan = file_size / sizeof(size_t);
    
    for(size_t i = 0; i < max_scan; i++) {
        if(offsets[i] != 0) {
            first_data_offset = offsets[i];
            break;
        }
    }
    
    if(first_data_offset > 0) {
        loc.table_size = first_data_offset / sizeof(size_t);
    } else {
        free(loc.file_buffer);
        loc.file_buffer = NULL;
        return loc;
    }
    
    size_t header_size = loc.table_size * sizeof(size_t);
    
    if(file_size < header_size) {
        free(loc.file_buffer);
        loc.file_buffer = NULL;
        loc.table_size = 0;
        return loc;
    }
    
    loc.offset_table = (size_t *)loc.file_buffer;
    loc.string_data = loc.file_buffer + header_size;
    loc.data_size = file_size - header_size;
    
    return loc;
}

LOCAPI const char *loc_get_string(loc_file *loc, const char *english_key) {
    if(!loc || !loc->offset_table || !loc->string_data || loc->table_size == 0) {
        return NULL;
    }
    
    uint32_t hash = loc_hash_string(english_key);
    size_t index = hash % loc->table_size;
    size_t offset = loc->offset_table[index];
    
    /* Offset is relative to string_data */
    if(offset >= loc->data_size) {
        return NULL;
    }
    
    return (const char *)(loc->string_data + offset);
}

LOCAPI void loc_free(loc_file *loc) {
    if(loc && loc->file_buffer) {
        free(loc->file_buffer);
        loc->file_buffer = NULL;
        loc->offset_table = NULL;
        loc->string_data = NULL;
        loc->table_size = 0;
        loc->data_size = 0;
    }
}

#endif /* LOC_IMPLEMENTATION */
