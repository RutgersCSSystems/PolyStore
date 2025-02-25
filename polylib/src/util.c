/*
 * util.c
 */

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "polystore.h"

/* Path utility functions */
void util_flatten_path(char *path) {
        size_t i = 0, len = strlen(path);
        for (int i = 0; i < len; ++i) {
                if (path[i] == '/')
                        path[i] = '#';
        }
}

void util_get_fullpath(const char *path, char *fullpath) {
        /* Get file full path */
        memset(fullpath, 0, PATH_MAX);
        if (path[0] == '/') {
                strcpy(fullpath, path);
        } else {
                getcwd(fullpath, PATH_MAX);
                strcat(fullpath, "/");
                strcat(fullpath, path);
        }
}

void util_get_fastpath(const char *path, char *fastpath) {
        char relapath[PATH_MAX];
        strcpy(relapath, path + POLYSTORE_PATH_PREFIX_LEN + 1);
        sprintf(fastpath, "%s/%s", FAST_DIR, relapath);
}

void util_get_slowpath(const char *path, char *slowpath) {
        char relapath[PATH_MAX];
        strcpy(relapath, path + POLYSTORE_PATH_PREFIX_LEN + 1);
        //flatten_path(relapath);
        sprintf(slowpath, "%s/%s", SLOW_DIR, relapath);
}

void util_get_physpaths(const char *path, 
                        char *fastpath, char *slowpath) {
        util_get_fastpath(path, fastpath);
        util_get_slowpath(path, slowpath);
}

#define HASH_SEED 0x12345678
uint32_t util_get_path_hash(const char *path) {
        /* Murmur's mix
         * 
         * https://github.com/aappleby/smhasher/blob/master/src/Hashes.cpp
         */
        uint32_t h = HASH_SEED;

        for (; *path; ++path) {
                h ^= *path;
                h *= 0x5bd1e995;
                h ^= h >> 15;
        }
        return h;
}


/*
 * Misc
 */
/* Helper function to build capacity from string */
unsigned long capacity_stoul(char* origin) {
        char *str = malloc(strlen(origin) + 1);
        strcpy(str, origin);

        /* magnitude is last character of size */
        char size_magnitude = str[strlen(str)-1];

        /* erase magnitude char */
        str[strlen(str)-1] = 0;

        unsigned long file_size_bytes = strtoul(str, NULL, 0);

        switch(size_magnitude) {
                case 'g':
                case 'G':
                        file_size_bytes *= 1024;
                case 'm':
                case 'M':
                        file_size_bytes *= 1024;
                case '\0':
                case 'k':
                case 'K':
                        file_size_bytes *= 1024;
                        break;
                case 'p':
                case 'P':
                        file_size_bytes *= 4;
                        break;
                case 'b':
                case 'B':
                        break;
                default:
                        printf("incorrect size format\n");
                        break;
        }
        free(str);
        return file_size_bytes;
}

/* Bitmap operations */
void inline set_bitmap(bitmap_t *b, int i) {
        b[i >> 3] |= 1 << (i & 7);
}

void inline unset_bitmap(bitmap_t *b, int i) {
        b[i >> 3] &= ~(1 << (i & 7));
}

uint8_t inline get_bitmap(bitmap_t *b, int i) {
        return b[i >> 3] & (1 << (i & 7)) ? 1 : 0;
}
