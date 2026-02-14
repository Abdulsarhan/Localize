#include <stdio.h>
#include <stdlib.h>
#include <limits.h>

#if defined(_WIN32) || defined(_WIN64)
    #include <windows.h>
#else
    #include <sys/stat.h>
    #include <unistd.h>
    #include <fcntl.h>
#endif

#define ARENA_IMPLEMENTATION
#include "arena.h"

#define loc_false 0
#define loc_true 1

static size_t loc_strlen(const char *str) {
    const char *s = str;
    while (*s) s++;
    return s - str;
}

static void *loc_memcpy(void *dest, const void *src, size_t n) {
    unsigned char *d = (unsigned char *)dest;
    const unsigned char *s = (const unsigned char *)src;
    for (size_t i = 0; i < n; i++) {
        d[i] = s[i];
    }
    return dest;
}

#ifndef _STDINT_H
/* 8-bit type */
/* according to the c standard, char is guarunteed to be 1 byte. */
#if UCHAR_MAX == 0xFF
typedef unsigned char  uint8_t;
typedef signed   char  int8_t;
#else
#error "No 8-bit type available on this platform."
#endif /* 8-bit type */

/* 16-bit type */
/* according to the c standard, short is guarunteed to be at least 2 bytes, since it has to be at least bigger than a char. */
#if USHRT_MAX == 0xFFFF
typedef unsigned short uint16_t;
typedef signed   short int16_t;
#elif UINT_MAX == 0xFFFF
typedef unsigned int   uint16_t;
typedef signed   int   int16_t;
#else
#error "No 16-bit type available on this platform."
#endif /* 16-bit types */

/* 32-bit type */
#if UINT_MAX == 0xFFFFFFFFUL
typedef unsigned int uint32_t;
typedef signed   int int32_t;
#elif ULONG_MAX == 0xFFFFFFFFUL
typedef unsigned long  uint32_t;
typedef signed   long  int32_t;
#else
#error "No 32-bit type available on this platform."
#endif /* 32 bit types */

#if ULONG_MAX == 0xFFFFFFFFFFFFFFFFUL
typedef unsigned long uint64_t;
typedef signed   long int64_t;
#elif defined(ULLONG_MAX) && ULLONG_MAX == 0xFFFFFFFFFFFFFFFFULL
typedef unsigned long long uint64_t;
typedef signed   long long int64_t;
#else
#error "No 64-bit type available on this platform."
#endif
#endif

typedef int loc_bool;
typedef uint8_t u8;

typedef struct  {
    size_t len;
    unsigned char *value;
} string;

size_t loc_get_file_size(const char *file_path) {
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
    } else {
        return 0;
    }
#endif
}

unsigned char *loc_read_entire_file(mem_arena *arena, const char *file_path, size_t *bytes_read) {
    size_t file_size = loc_get_file_size(file_path);
    if (file_size == 0) {
        return NULL;
    }

    unsigned char *file = ARENA_PUSH_ARRAY(arena, unsigned char, file_size + 1);

#if defined(_WIN32) || defined(_WIN64)
    HANDLE hFile = CreateFileA(file_path, GENERIC_READ, FILE_SHARE_READ, NULL,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        return NULL;
    }

    size_t total_read = 0;
    while (total_read < file_size) {
        DWORD to_read = (DWORD)((file_size - total_read) > MAXDWORD ? MAXDWORD : (file_size - total_read));
        DWORD bytes_read_chunk = 0;
        
        if (!ReadFile(hFile, file + total_read, to_read, &bytes_read_chunk, NULL) || bytes_read_chunk == 0) {
            CloseHandle(hFile);
            return NULL;
        }
        total_read += bytes_read_chunk;
    }

    CloseHandle(hFile);
#else
    int fd = open(file_path, O_RDONLY);
    if (fd == -1) {
        return NULL;
    }

    size_t total_read = 0;
    while (total_read < file_size) {
        ssize_t result = read(fd, file + total_read, file_size - total_read);
        if (result <= 0) {
            close(fd);
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

loc_bool loc_write_entire_file(const char *file_path, size_t file_size, char *buffer) {
#if defined(_WIN32) || defined(_WIN64)
    HANDLE hFile = CreateFileA(file_path, GENERIC_WRITE, 0, NULL,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        return loc_false;
    }

    size_t total_written = 0;
    while (total_written < file_size) {
        DWORD to_write = (DWORD)((file_size - total_written) > MAXDWORD ? MAXDWORD : (file_size - total_written));
        DWORD bytes_written_chunk = 0;
        
        if (!WriteFile(hFile, buffer + total_written, to_write, &bytes_written_chunk, NULL) || bytes_written_chunk == 0) {
            CloseHandle(hFile);
            return loc_false;
        }
        total_written += bytes_written_chunk;
    }

    CloseHandle(hFile);
    return loc_true;
#else
    int fd = open(file_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd == -1) {
        return loc_false;
    }

    size_t total_written = 0;
    while (total_written < file_size) {
        ssize_t result = write(fd, buffer + total_written, file_size - total_written);
        if (result <= 0) {
            close(fd);
            return loc_false;
        }
        total_written += result;
    }

    close(fd);
    return loc_true;
#endif
}

static uint8_t consume_byte(unsigned char **at, unsigned char *end) {
    if(*at + 1 != end) {
        (*at) += 1;
    } else {
        printf("Tried to consume past end!\n");
    }
    return **at;
}

static uint8_t peek_byte(unsigned char **at, unsigned char *end) {
    if(*at + 1 != end) {
        return *((*at) + 1);
    }
    printf("Tried to peek past end!\n");
    return **at;
}

/* consumes a value in the pipe-delimited file */
static string consume_value(unsigned char **at, unsigned char *end) {
    string string = {0};
    
    // Skip leading whitespace
    while(*at != end && (**at == ' ' || **at == '\t')) {
        (*at)++;
    }
    
    string.value = *at;
    
    // Consume until we hit a non-escaped pipe or end of file
    while(*at != end) {
        if(**at == '|') {
            // Check if it's an escaped pipe (||)
            if(*at + 1 != end && *(*at + 1) == '|') {
                // Skip both pipes
                (*at) += 2;
            } else {
                // Single pipe is a delimiter, stop here
                break;
            }
        } else {
            (*at)++;
        }
    }
    
    // Trim trailing whitespace from the value
    unsigned char *value_end = *at;
    while(value_end > string.value && (*(value_end - 1) == ' ' || *(value_end - 1) == '\t')) {
        value_end--;
    }
    
    string.len = value_end - string.value;
    
    // Skip the pipe delimiter if present
    if(*at != end && **at == '|') {
        (*at)++;
    }
    
    return string;
}


static uint32_t fnv1a_hash(string string) {
    uint32_t hash = 2166136261; /* FNV offset basis */
    for(int i = 0; i < string.len; i++) {
        hash = hash ^ string.value[i];
        hash = hash * 16777619;
    }
    return hash;
}

typedef struct {
    size_t offset_count;
    size_t offset_cap;
    size_t *table;  // FIX: Changed to size_t* to store actual offsets
} offset_table;

static offset_table init_offset_table(mem_arena *arena, size_t offset_cap) {
    offset_table table;
    table.table = ARENA_PUSH_ARRAY_ZERO(arena, size_t, offset_cap);
    table.offset_cap = offset_cap;
    table.offset_count = 0;
    return table;
}

/* Unescape pipes (|| -> |) and copy to buffer */
static void unescape_and_copy(unsigned char *dest, size_t *dest_size, unsigned char *src, size_t src_len) {
    for(size_t i = 0; i < src_len; i++) {
        if(src[i] == '|' && i + 1 < src_len && src[i + 1] == '|') {
            // Found ||, write single | and skip next char
            dest[(*dest_size)++] = '|';
            i++;  // Skip the second pipe
        } else {
            dest[(*dest_size)++] = src[i];
        }
    }
}

int main(int argc, char **argv) {
    size_t input_size = 0;
    unsigned char *input, *at, *end;
    int language_count = 0;
    mem_arena *arena;

    if(argc < 3) {
        printf("Invalid Usage.\n");
        printf("Usage: loc [input_file_path] [lang1] [lang2] [lang3] ...\n");
        printf("Input file format: pipe-delimited (|) with optional whitespace around pipes\n");
        printf("Use || to include a literal pipe character in a string\n");
        printf("Example: loc strings.txt en fr jp\n");
        printf("  Produces: strings.en.loc, strings.fr.loc, strings.jp.loc\n");
        return -1;
    }

    language_count = argc - 2;  // First arg is program name, second is file path
    
    // Reserve 1GB of virtual memory - it's just address space
    arena = arena_init(1024 * 1024 * 1024);

    input = loc_read_entire_file(arena, argv[1], &input_size);
    if(!input) {
        printf("Failed to read file: %s\n", argv[1]);
        arena_destroy(arena);
        return -1;
    }

    // First pass: count rows to determine offset table size
    at = input;
    end = input + input_size;
    size_t row_count = 0;
    
    while(at < end) {
        string first_value = consume_value(&at, end);
        if(first_value.len == 0) break;
        
        // Skip remaining values in this row
        for(int i = 1; i < language_count; i++) {
            consume_value(&at, end);
        }
        row_count++;
    }

    // Use 8x size for hash table to minimize collisions
    size_t table_size = 1024;
    while(table_size < row_count * 8) {
        table_size *= 2;
    }

    // Allocate arrays to hold all language strings
    typedef struct {
        unsigned char *data;
        size_t size;
        size_t capacity;
    } language_buffer;
    
    language_buffer *lang_buffers = ARENA_PUSH_ARRAY_ZERO(arena, language_buffer, language_count);
    
    // Initialize buffers for each language
    for(int i = 0; i < language_count; i++) {
        lang_buffers[i].capacity = input_size;  // Conservative estimate
        lang_buffers[i].data = ARENA_PUSH_ARRAY(arena, unsigned char, lang_buffers[i].capacity);
        lang_buffers[i].size = 0;
    }

    // Create offset tables for each language (they'll have the same structure)
    offset_table *tables = ARENA_PUSH_ARRAY(arena, offset_table, language_count);
    for(int i = 0; i < language_count; i++) {
        tables[i] = init_offset_table(arena, table_size);
    }
    
    // Track which hash table slots are used for collision detection
    unsigned char *slot_used = ARENA_PUSH_ARRAY_ZERO(arena, unsigned char, table_size);

    // Second pass: parse and build offset tables + language buffers
    at = input;
    
    while(at < end) {
        string values[32];  // Max 32 languages - should be plenty
        if(language_count > 32) {
            printf("Error: Too many languages (max 32)\n");
            arena_destroy(arena);
            return -1;
        }
        
        // Read all values for this row
        for(int i = 0; i < language_count; i++) {
            values[i] = consume_value(&at, end);
        }
        
        // Skip empty rows
        if(values[0].len == 0) break;
        
        // Hash the English string (first column)
        uint32_t value_hash = fnv1a_hash(values[0]);
        uint32_t table_index = value_hash % table_size;
        
        // Check for collision using the slot_used tracker
        if(slot_used[table_index]) {
            printf("ERROR: Hash collision detected!\n");
            printf("String '%.*s' collides with a previous entry.\n", (int)values[0].len, values[0].value);
            printf("Consider using more strings or the hash table might need adjustment.\n");
            arena_destroy(arena);
            return -1;
        }
        
        // Mark this slot as used
        slot_used[table_index] = 1;
        
        // For each language, store its string and record offset
        for(int lang_idx = 0; lang_idx < language_count; lang_idx++) {
            // Record offset to this string in this language's buffer
            tables[lang_idx].table[table_index] = lang_buffers[lang_idx].size;
            
            // Unescape and copy string to language buffer
            unescape_and_copy(lang_buffers[lang_idx].data, &lang_buffers[lang_idx].size, 
                            values[lang_idx].value, values[lang_idx].len);
            
            // Add null terminator
            lang_buffers[lang_idx].data[lang_buffers[lang_idx].size++] = '\0';
        }
    }

    // Offsets are already relative to string_data, no adjustment needed!
    // (We stored them as offsets into lang_buffers, which become string_data)
    
    // Calculate header size once
    size_t header_size = sizeof(size_t) * table_size;
    
    // Write output files for each language
    for(int lang_idx = 0; lang_idx < language_count; lang_idx++) {
        char output_path[512];
        
        // Create output filename: input.LANG.loc
        const char *input_path = argv[1];
        const char *lang_code = argv[2 + lang_idx];
        
        // Find the last dot in the input path for extension
        const char *dot = input_path;
        const char *last_dot = NULL;
        while(*dot) {
            if(*dot == '.') last_dot = dot;
            dot++;
        }
        
        if(last_dot) {
            // Insert language code before extension
            size_t prefix_len = last_dot - input_path;
            loc_memcpy(output_path, input_path, prefix_len);
            output_path[prefix_len] = '.';
            size_t lang_len = loc_strlen(lang_code);
            loc_memcpy(output_path + prefix_len + 1, lang_code, lang_len);
            loc_memcpy(output_path + prefix_len + 1 + lang_len, ".loc", 5);  // Include null terminator
        } else {
            // No extension, just append .LANG.loc
            size_t path_len = loc_strlen(input_path);
            loc_memcpy(output_path, input_path, path_len);
            output_path[path_len] = '.';
            size_t lang_len = loc_strlen(lang_code);
            loc_memcpy(output_path + path_len + 1, lang_code, lang_len);
            loc_memcpy(output_path + path_len + 1 + lang_len, ".loc", 5);  // Include null terminator
        }
        
        // Calculate total output size
        size_t output_size = header_size + lang_buffers[lang_idx].size;
        unsigned char *output = ARENA_PUSH_ARRAY(arena, unsigned char, output_size);
        
        // Copy offset table
        loc_memcpy(output, tables[lang_idx].table, header_size);
        // Copy language data
        loc_memcpy(output + header_size, lang_buffers[lang_idx].data, lang_buffers[lang_idx].size);
        
        if(!loc_write_entire_file(output_path, output_size, (char*)output)) {
            printf("Failed to write output file: %s\n", output_path);
            arena_destroy(arena);
            return -1;
        }
        
        printf("Successfully created %s (%zu strings, %zu bytes)\n", 
               output_path, row_count, output_size);
    }
    
    // Free everything at once
    arena_destroy(arena);
    return 0;
}
