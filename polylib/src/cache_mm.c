/*
 * cache_mm.c
 */

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/stat.h>
#include <unistd.h>

#include "debug.h"
#include "polystore.h"

#define POLY_CACHE_SHM "/poly_cache_shm"
#define CPU_MAX 64
#define POLY_CACHE_PER_CPU_LIST_ENTRY_SZ 4096
#define POLY_CACHE_PER_CPU_LIST_SZ 512

/* Poly cache shm vaddr */
void *poly_cache_shm = NULL;

/* Poly cache shm file descriptor */
int poly_cache_shm_fd = 0;

/* Poly cache per-CPU free list */
bitmap_t *poly_cache_free_list[CPU_MAX];
int* poly_cache_free_list_lock[CPU_MAX];

#define POLY_CACHE_PER_CPU_SIZE_SHIFT 33
#define POLY_CACHE_PER_CPU_SIZE (1 << POLY_CACHE_PER_CPU_SIZE_SHIFT)
#define POLY_CACHE_PER_CPU_SIZE_MASK (~(POLY_CACHE_PER_CPU_SIZE - 1))

#define POLY_CACHE_GET_CPU(vaddr) \
                (((vaddr - POLY_CACHE_VADDR_BASE) >> POLY_CACHE_PER_CPU_SIZE_SHIFT))
#define POLY_CACHE_GET_FREE_LIST_INDEX(vaddr, cpu_id) \
                ((vaddr - POLY_CACHE_VADDR_BASE - (cpu_id << POLY_CACHE_PER_CPU_SIZE_SHIFT)) >> POLY_INDEX_NODE_SIZE_SHIFT)
#define POLY_CACHE_BUF_VADDR(cpu_id, list_id) \
                (POLY_CACHE_VADDR_BASE + (cpu_id << POLY_CACHE_PER_CPU_SIZE_SHIFT) + (list_id << POLY_INDEX_NODE_SIZE_SHIFT))


void poly_cache_mm_init(void) {
#ifdef POLYSTORE_POLYCACHE_SHM
        int i = 0;

        /* Open shared memory */
        poly_cache_shm_fd = shm_open(POLY_CACHE_SHM, O_CREAT | O_RDWR, 0666);
        if (poly_cache_shm_fd == -1) {
                ERROR_T("Failed to open Poly cache shared memory");
                return;
        }

        if (ftruncate(poly_cache_shm_fd, POLY_CACHE_VADDR_RANGE) == -1) {
                ERROR_T("Failed to open Poly cache shared memory");
                return;
        }

        /* mmap shared memory to the application */
        poly_cache_shm = mmap((void*)POLY_CACHE_VADDR_BASE, POLY_CACHE_VADDR_RANGE, 
                              PROT_READ | PROT_WRITE, MAP_SHARED, 
                              poly_cache_shm_fd, 0);
        if ((unsigned long)poly_cache_shm != POLY_CACHE_VADDR_BASE) {
                ERROR_T("Failed to mmap Poly cache shared memory");
                return;
        }

        /* initialize per-CPU bitmap */
        for (i = 0; i < CPU_MAX; ++i) {
                poly_cache_free_list[i] = (bitmap_t*)g_control_var->poly_cache_free_list[i];
                poly_cache_free_list_lock[i] = (int*)&g_control_var->poly_cache_free_list_lock[i];
        }
#endif
}

void poly_cache_mm_exit(void) {
#ifdef POLYSTORE_POLYCACHE_SHM
        /* Free shared memory */
        shm_unlink(POLY_CACHE_SHM);
#endif
}

void* poly_cache_mm_alloc(void) {
        void *buf = NULL;
#ifdef POLYSTORE_POLYCACHE_SHM
        int cpu_id = tls_task_ctx.cpu_id;
        int i = 0;
        bitmap_t *shm_list = poly_cache_free_list[cpu_id];

        while(__sync_lock_test_and_set(poly_cache_free_list_lock[cpu_id], 1));
        for (i = 0; i < POLY_CACHE_PER_CPU_LIST_ENTRY_SZ; ++i) {
                if (get_bitmap(shm_list, i) == 0) {
                        set_bitmap(shm_list, i);
                        break;
                }
        } 
        __sync_lock_release(poly_cache_free_list_lock[cpu_id]);

        if (i < POLY_CACHE_PER_CPU_LIST_ENTRY_SZ) {
                buf = (void*)POLY_CACHE_BUF_VADDR((unsigned long)cpu_id, (unsigned long)i);
        } else {
                ERROR_T("Failed to allocate shm, idx\n", i);
                buf = NULL;
        }
#else
        posix_memalign((void**)&buf, PAGE_SIZE, POLY_INDEX_NODE_SIZE);
#endif

        return buf;
}

void poly_cache_mm_free(void* buf) {
#ifdef POLYSTORE_POLYCACHE_SHM
        int cpu_id = POLY_CACHE_GET_CPU((unsigned long)buf);
        int list_id = POLY_CACHE_GET_FREE_LIST_INDEX((unsigned long)buf, (unsigned long)cpu_id);
        bitmap_t *shm_list = poly_cache_free_list[cpu_id];

        while(__sync_lock_test_and_set(poly_cache_free_list_lock[cpu_id], 1));
        unset_bitmap(shm_list, list_id);
        __sync_lock_release(poly_cache_free_list_lock[cpu_id]);
#else
        free(buf);
#endif
        return;
}
