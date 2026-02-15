#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <limits.h>

#if defined(_WIN32) || defined(_WIN64)
    #include <windows.h>
#else
    #include <sys/stat.h>
    #include <unistd.h>
    #include <fcntl.h>
#endif

#define LOC_ARENA_PUSH_STRUCT(arena, T) (T*)loc_arena_push(arena, sizeof(T), 0)
#define LOC_ARENA_PUSH_ARRAY(arena, T, n) (T*)loc_arena_push(arena, sizeof(T) * n, 0)
#define LOC_ARENA_PUSH_STRUCT_ZERO(arena, T) (T*)loc_arena_push(arena, sizeof(T), 1)
#define LOC_ARENA_PUSH_ARRAY_ZERO(arena, T, n) (T*)loc_arena_push(arena, sizeof(T) * n, 1)

typedef struct {
    size_t pos;
    size_t committed_size;
    size_t page_size;
    size_t reserved_size;
} loc_mem_arena;

typedef struct {
    loc_mem_arena *arena;
    size_t start_pos;
}loc_arena_temp;

typedef int loc_arena_bool;

static loc_mem_arena *loc_arena_init(size_t size);
static void *loc_arena_push(loc_mem_arena *arena, size_t size, loc_arena_bool zero_out_the_memory);
static void loc_arena_destroy(loc_mem_arena *arena);

#ifndef LOC_ARENA_ALIGNMENT
#define LOC_ARENA_ALIGNMENT 16
#endif

#define ALIGN_UP(x, a) (((x) + ((a) - 1)) & ~((a) - 1))

#define LOC_ARENA_MIN(a, b) (((a) < (b)) ? (a) : (b))
#define LOC_ARENA_MAX(a, b) (((a) > (b)) ? (a) : (b))

#define LOC_ARENA_BASE_POS sizeof(loc_mem_arena)

static void *loc_arena_memset(void *buf, int value, size_t count) {
    unsigned char *p = (unsigned char *)buf;
    unsigned char v = (unsigned char)value;
	size_t i = 0;

    for (; i < count; i++) {
        p[i] = v;
    }

    return buf;
}

#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>

loc_mem_arena *loc_arena_init(size_t size) {
    SYSTEM_INFO sys_info;
    GetSystemInfo(&sys_info);
    size_t page_size = sys_info.dwPageSize;
    size = ALIGN_UP(size, page_size);

    loc_mem_arena *arena = (loc_mem_arena*)VirtualAlloc(NULL, size, MEM_RESERVE, PAGE_READWRITE);
    if (!arena) {
        fprintf(stderr, "VirtualAlloc reserve failed: %lu\n", GetLastError());
        exit(EXIT_FAILURE);
    }

    size_t initial_commit = LOC_ARENA_MAX(page_size, loc_arena_BASE_POS);
    initial_commit = ALIGN_UP(initial_commit, page_size);
    
    void *commit = VirtualAlloc(arena, initial_commit, MEM_COMMIT, PAGE_READWRITE);
    if (!commit) {
        fprintf(stderr, "VirtualAlloc initial commit failed: %lu\n", GetLastError());
        VirtualFree(arena, 0, MEM_RELEASE);
        exit(EXIT_FAILURE);
    }

    arena->page_size = page_size;
    arena->reserved_size = size;
    arena->pos = LOC_ARENA_BASE_POS;
    arena->committed_size = initial_commit;

    return arena;
}

void* loc_arena_push(loc_mem_arena* arena, size_t size, loc_arena_bool zero_out_the_memory) {
    size_t base = (size_t)arena + arena->pos;
    uintptr_t aligned = ALIGN_UP(base, LOC_ARENA_ALIGNMENT);
    size_t padding = aligned - base;
    size_t total_size = padding + size;
    size_t required = arena->pos + total_size;

    if (required > arena->reserved_size) {
        fprintf(stderr, "ERROR: Allocation exceeds arena reserved_size!\n");
        return NULL;
    }

    if (required > arena->committed_size) {
        size_t new_commit_end = ALIGN_UP(required, arena->page_size);
        size_t commit_amount = new_commit_end - arena->committed_size;

        void* result = VirtualAlloc((char*)arena + arena->committed_size,
                                    commit_amount, MEM_COMMIT, PAGE_READWRITE);
        if (!result) {
            fprintf(stderr, "VirtualAlloc commit failed: %lu\n", GetLastError());
            exit(EXIT_FAILURE);
        }

        arena->committed_size = new_commit_end;
    }

    arena->pos += total_size;
    if(zero_out_the_memory) {
        loc_arena_memset((void*)aligned, 0, total_size);
    }
    return (void*)aligned;
}

void loc_arena_destroy(loc_mem_arena *arena) {
    if (arena) {
        arena->pos = 0;
        arena->committed_size = 0;
        arena->reserved_size = 0;
        arena->page_size = 0;
        VirtualFree(arena, 0, MEM_RELEASE);
    }
}

#else

#include <sys/mman.h>
#include <unistd.h>

loc_mem_arena *loc_arena_init(size_t size) {
    size_t page_size = (size_t)sysconf(_SC_PAGESIZE);
    size = ALIGN_UP(size, page_size);

    loc_mem_arena *arena = (loc_mem_arena*)mmap(NULL, size, PROT_READ | PROT_WRITE,
                                         MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (arena == MAP_FAILED) {
        perror("mmap failed");
        exit(EXIT_FAILURE);
    }

    arena->page_size = page_size;
    arena->reserved_size = size;
    arena->pos = LOC_ARENA_BASE_POS;
    arena->committed_size = size; // mmap commits all at once on most systems

    return arena;
}

void* loc_arena_push(loc_mem_arena* arena, size_t size, loc_arena_bool zero_out_the_memory) {
    size_t base = (size_t)arena + arena->pos;
    uintptr_t aligned = ALIGN_UP(base, LOC_ARENA_ALIGNMENT);
    size_t padding = aligned - base;
    size_t total_size = padding + size;
    size_t required = arena->pos + total_size;

    if (required > arena->reserved_size) {
        fprintf(stderr, "ERROR: Allocation exceeds arena reserved_size!\n");
        return NULL;
    }

    arena->pos += total_size;

    if(zero_out_the_memory) {
        loc_arena_memset((void*)aligned, 0, total_size);
    }
    return (void*)aligned;
}

void loc_arena_destroy(loc_mem_arena *arena) {
    if (arena) {
        munmap(arena, arena->reserved_size);
    }
}

#endif // _WIN32 || _WIN64
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
#if UCHAR_MAX == 0xFF
typedef unsigned char  uint8_t;
typedef signed   char  int8_t;
#else
#error "No 8-bit type available on this platform."
#endif

/* 16-bit type */
#if USHRT_MAX == 0xFFFF
typedef unsigned short uint16_t;
typedef signed   short int16_t;
#elif UINT_MAX == 0xFFFF
typedef unsigned int   uint16_t;
typedef signed   int   int16_t;
#else
#error "No 16-bit type available on this platform."
#endif

/* 32-bit type */
#if UINT_MAX == 0xFFFFFFFFUL
typedef unsigned int uint32_t;
typedef signed   int int32_t;
#elif ULONG_MAX == 0xFFFFFFFFUL
typedef unsigned long  uint32_t;
typedef signed   long  int32_t;
#else
#error "No 32-bit type available on this platform."
#endif

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

typedef struct {
    size_t len;
    unsigned char *value;
} string;

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
    } else {
        return 0;
    }
#endif
}

static unsigned char *loc_read_entire_file(loc_mem_arena *arena, const char *file_path, size_t *bytes_read) {
    size_t file_size = loc_get_file_size(file_path);
    if (file_size == 0) {
        return NULL;
    }

    unsigned char *file = LOC_ARENA_PUSH_ARRAY(arena, unsigned char, file_size + 1);

#if defined(_WIN32) || defined(_WIN64)
    HANDLE hFile = CreateFileA(file_path, GENERIC_READ, FILE_SHARE_READ, NULL,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        return NULL;
    }

    size_t total_read = 0;
    while (total_read < file_size) {
        DWORD to_read = (DWORD)((file_size - total_read) > 0xFFFFFFFFUL ? 0xFFFFFFFFUL : (file_size - total_read));
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

static loc_bool loc_write_entire_file(const char *file_path, size_t file_size, char *buffer) {
#if defined(_WIN32) || defined(_WIN64)
    HANDLE hFile = CreateFileA(file_path, GENERIC_WRITE, 0, NULL,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        return loc_false;
    }

    size_t total_written = 0;
    while (total_written < file_size) {
        DWORD to_write = (DWORD)((file_size - total_written) > 0xFFFFFFFFUL ? 0xFFFFFFFFUL : (file_size - total_written));
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
    uint32_t hash = 2166136261;
    for(size_t i = 0; i < string.len; i++) {
        hash = hash ^ string.value[i];
        hash = hash * 16777619;
    }
    return hash;
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

typedef struct {
    size_t count;
    size_t *offsets;
} bucket;

int main(int argc, char **argv) {
    size_t input_size = 0;
    unsigned char *input, *at, *end;
    int language_count = 0;
    loc_mem_arena *arena;

    if(argc < 3) {
        printf("Invalid Usage.\n");
        printf("Usage: loc [input_file_path] [lang1] [lang2] [lang3] ...\n");
        printf("Input file format: pipe-delimited (|) with optional whitespace around pipes\n");
        printf("Use || to include a literal pipe character in a string\n");
        printf("Example: loc strings.txt en fr jp\n");
        printf("  Produces: strings.en.loc, strings.fr.loc, strings.jp.loc\n");
        return -1;
    }

    language_count = argc - 2;
    
    // Reserve 1GB of virtual memory
    arena = loc_arena_init(1024 * 1024 * 1024);

    input = loc_read_entire_file(arena, argv[1], &input_size);
    if(!input) {
        printf("Failed to read file: %s\n", argv[1]);
        loc_arena_destroy(arena);
        return -1;
    }

    // First pass: count rows
    at = input;
    end = input + input_size;
    size_t row_count = 0;
    
    while(at < end) {
        string first_value = consume_value(&at, end);
        if(first_value.len == 0) break;
        
        for(int i = 1; i < language_count; i++) {
            consume_value(&at, end);
        }
        row_count++;
    }

    printf("Found %zu strings\n", row_count);

    // Allocate bucket offset table (one entry per string)
    size_t bucket_table_size = row_count;
    
    // Allocate string buffers for each language
    typedef struct {
        unsigned char *data;
        size_t size;
        size_t capacity;
    } language_buffer;
    
    language_buffer *lang_buffers = LOC_ARENA_PUSH_ARRAY_ZERO(arena, language_buffer, language_count);
    for(int i = 0; i < language_count; i++) {
        lang_buffers[i].capacity = input_size;
        lang_buffers[i].data = LOC_ARENA_PUSH_ARRAY(arena, unsigned char, lang_buffers[i].capacity);
        lang_buffers[i].size = 0;
    }

    // Allocate buckets for each language
    bucket **lang_buckets = LOC_ARENA_PUSH_ARRAY(arena, bucket*, language_count);
    for(int i = 0; i < language_count; i++) {
        lang_buckets[i] = LOC_ARENA_PUSH_ARRAY_ZERO(arena, bucket, bucket_table_size);
    }

    // Second pass: build buckets and strings
    at = input;
    
    while(at < end) {
        string values[32];
        if(language_count > 32) {
            printf("Error: Too many languages (max 32)\n");
            loc_arena_destroy(arena);
            return -1;
        }
        
        for(int i = 0; i < language_count; i++) {
            values[i] = consume_value(&at, end);
        }
        
        if(values[0].len == 0) break;
        
        // Hash the English string
        uint32_t hash = fnv1a_hash(values[0]);
        uint32_t bucket_index = hash % bucket_table_size;
        
        // For each language, add offset to bucket and store string
        for(int lang_idx = 0; lang_idx < language_count; lang_idx++) {
            bucket *b = &lang_buckets[lang_idx][bucket_index];
            
            // Expand bucket offsets array
            if(b->count == 0) {
                b->offsets = LOC_ARENA_PUSH_ARRAY(arena, size_t, 1);
            } else {
                size_t *new_offsets = LOC_ARENA_PUSH_ARRAY(arena, size_t, b->count + 1);
                for(size_t i = 0; i < b->count; i++) {
                    new_offsets[i] = b->offsets[i];
                }
                b->offsets = new_offsets;
            }
            
            // Store offset to this string entry
            b->offsets[b->count] = lang_buffers[lang_idx].size;
            b->count++;
            
            // Store format: [english_key:null-terminated][localized_string:null-terminated]
            // Write English key first (for verification)
            unescape_and_copy(lang_buffers[lang_idx].data, &lang_buffers[lang_idx].size,
                            values[0].value, values[0].len);
            lang_buffers[lang_idx].data[lang_buffers[lang_idx].size++] = '\0';
            
            // Then write localized string
            unescape_and_copy(lang_buffers[lang_idx].data, &lang_buffers[lang_idx].size,
                            values[lang_idx].value, values[lang_idx].len);
            
            // Add null terminator
            lang_buffers[lang_idx].data[lang_buffers[lang_idx].size++] = '\0';
        }
    }

    // Write output files for each language
    for(int lang_idx = 0; lang_idx < language_count; lang_idx++) {
        char output_path[512];
        const char *input_path = argv[1];
        const char *lang_code = argv[2 + lang_idx];
        
        // Create output filename
        const char *dot = input_path;
        const char *last_dot = NULL;
        while(*dot) {
            if(*dot == '.') last_dot = dot;
            dot++;
        }
        
        if(last_dot) {
            size_t prefix_len = last_dot - input_path;
            loc_memcpy(output_path, input_path, prefix_len);
            output_path[prefix_len] = '.';
            size_t lang_len = loc_strlen(lang_code);
            loc_memcpy(output_path + prefix_len + 1, lang_code, lang_len);
            loc_memcpy(output_path + prefix_len + 1 + lang_len, ".loc", 5);
        } else {
            size_t path_len = loc_strlen(input_path);
            loc_memcpy(output_path, input_path, path_len);
            output_path[path_len] = '.';
            size_t lang_len = loc_strlen(lang_code);
            loc_memcpy(output_path + path_len + 1, lang_code, lang_len);
            loc_memcpy(output_path + path_len + 1 + lang_len, ".loc", 5);
        }
        
        // Calculate sizes for each chunk
        // Format: [bucket_offset_table_size][bucket_offset_table][bucket_list_size][bucket_list][strings_size][strings]
        
        size_t bucket_offset_table_size = bucket_table_size * sizeof(size_t);
        
        // Calculate bucket list size
        size_t bucket_list_size = 0;
        for(size_t i = 0; i < bucket_table_size; i++) {
            bucket *b = &lang_buckets[lang_idx][i];
            bucket_list_size += sizeof(size_t) + (b->count * sizeof(size_t));
        }
        
        size_t strings_size = lang_buffers[lang_idx].size;
        
        size_t total_size = sizeof(size_t) + bucket_offset_table_size +
                           sizeof(size_t) + bucket_list_size +
                           sizeof(size_t) + strings_size;
        
        // Build output buffer
        unsigned char *output = LOC_ARENA_PUSH_ARRAY(arena, unsigned char, total_size);
        size_t output_pos = 0;
        
        // Write bucket offset table size
        *((size_t*)(output + output_pos)) = bucket_offset_table_size;
        output_pos += sizeof(size_t);
        
        // Write bucket offset table
        size_t *bucket_offsets = (size_t*)(output + output_pos);
        size_t current_bucket_offset = 0;
        
        for(size_t i = 0; i < bucket_table_size; i++) {
            bucket_offsets[i] = current_bucket_offset;
            bucket *b = &lang_buckets[lang_idx][i];
            current_bucket_offset += sizeof(size_t) + (b->count * sizeof(size_t));
        }
        output_pos += bucket_offset_table_size;
        
        // Write bucket list size
        *((size_t*)(output + output_pos)) = bucket_list_size;
        output_pos += sizeof(size_t);
        
        // Write bucket list
        for(size_t i = 0; i < bucket_table_size; i++) {
            bucket *b = &lang_buckets[lang_idx][i];
            
            // Write count
            *((size_t*)(output + output_pos)) = b->count;
            output_pos += sizeof(size_t);
            
            // Write offsets
            for(size_t j = 0; j < b->count; j++) {
                *((size_t*)(output + output_pos)) = b->offsets[j];
                output_pos += sizeof(size_t);
            }
        }
        
        // Write strings size
        *((size_t*)(output + output_pos)) = strings_size;
        output_pos += sizeof(size_t);
        
        // Write strings
        loc_memcpy(output + output_pos, lang_buffers[lang_idx].data, strings_size);
        
        if(!loc_write_entire_file(output_path, total_size, (char*)output)) {
            printf("Failed to write output file: %s\n", output_path);
            loc_arena_destroy(arena);
            return -1;
        }
        
        printf("Successfully created %s (%zu strings, %zu bytes)\n",
               output_path, row_count, total_size);
    }
    
    loc_arena_destroy(arena);
    return 0;
}
