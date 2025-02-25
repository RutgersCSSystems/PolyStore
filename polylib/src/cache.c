/*
 * cache.c
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
#include "thpool.h"

#define DEFAULT_CACHE_FLUSHING_THREAD_NR 2
//#define DEFAULT_CACHE_FLUSHING_START_THRES (1UL << 30)
//#define DEFAULT_CACHE_FLUSHING_END_THRES (1UL << 29)
#define DEFAULT_CACHE_FLUSHING_START_THRES (1UL << 35)
#define DEFAULT_CACHE_FLUSHING_END_THRES (1UL << 34)

/* Total cache size */
unsigned long poly_cache_size = 0;

/* Cache flushing threshold */
unsigned long poly_cache_flushing_begin = 
                        DEFAULT_CACHE_FLUSHING_START_THRES;
unsigned long poly_cache_flushing_end = 
                        DEFAULT_CACHE_FLUSHING_END_THRES;

/* Cache policy */
cache_policy_t poly_cache_policy = CACHE_POLICY_EQUAL;

/* Background cache flushing thread pool */
threadpool poly_cache_thpool;

/* Background cache flushing thread indicator */
int poly_cache_flushing_indicator;

/* Background cache flushing worker thread nr */
int poly_cache_flushing_thread_nr = 
                        DEFAULT_CACHE_FLUSHING_THREAD_NR;

/* Interval node queue for each flushing worker thread */
struct list_head *poly_cache_flushing_queue;
pthread_mutex_t *poly_cache_flushing_queue_mutex;

unsigned long cache_access_cnt = 0;
unsigned long cache_hit_cnt = 0;
unsigned long cache_miss_cnt = 0;

/* LRU/2 list for interval tree cache buffer */
struct list_head interval_hot_list = LIST_HEAD_INIT(interval_hot_list);
struct list_head interval_cold_list = LIST_HEAD_INIT(interval_cold_list);

/* Spin locks to protect LRU list modification */
pthread_spinlock_t lru_hot_list_lock;
pthread_spinlock_t lru_cold_list_lock;

/* LRU list function helper */
void inline poly_cache_lru_list_add(struct list_head *node, 
                                    struct list_head *target) {
        list_add_tail(node, target);
}

void inline poly_cache_lru_list_del(struct list_head *node) {
        list_del(node);
}

int inline poly_cache_lru_list_check_linked(struct list_head *node) {
        return ((node->next == LIST_POISON1) &&
                (node->prev == LIST_POISON2)) ? 0 : 1;
}

void inline poly_cache_lru_list_move(struct list_head *node, 
                                     struct list_head *target) {
        list_del(node);
        list_add_tail(node, target);
}

void poly_cache_add_lru_cold_list(struct it_node_entry *it_entry) {

        pthread_spin_lock(&lru_cold_list_lock);
        poly_cache_lru_list_add(&it_entry->cache_lru, 
                                &interval_cold_list);
        pthread_spin_unlock(&lru_cold_list_lock);
}


/* Cache size update function */
static void inline poly_cache_size_add(size_t *size, size_t value) {
        __sync_add_and_fetch(size, value);
}

static void inline poly_cache_size_sub(size_t *size, size_t value) {
        __sync_sub_and_fetch(size, value);
}

static void inline poly_cache_size_set(size_t *size, size_t value) {
        __sync_lock_test_and_set(size, value);
}

static size_t inline poly_cache_size_get(size_t *size) {
        return __sync_fetch_and_add(size, 0);
}

/* Cache admission */
int poly_cache_admit(struct poly_file *file, struct it_node_entry *it_entry,
                                        off_t start, off_t end, int rw) {
        struct poly_inode *inode = file->inode;     
        int ret = 0, rwfd = 0;
        int size = end - start + 1, iter = 0;
        int idx = start >> PAGE_SIZE_SHIFT;
        off_t offset = it_entry->offset + (idx << PAGE_SIZE_SHIFT);
        void *buf = it_entry->buf_addr + (idx << PAGE_SIZE_SHIFT);

        /* No cache admission for PMEM read */
        if (rw == READ_IO && it_entry->placement == BLK_PLACEMENT_FAST &&
                        inode->cache_policy ==
                        CACHE_POLICY_FAST_READ_NOT_ADMIT) {
                return ret;        
        }

        /* Reset inode pointer if does not match */
        if (it_entry->inode != inode) {
                ERROR_T("inode pointer mismatch in index entry!");
                it_entry->inode = inode;
        }

        /* Set rwfd */
        if (it_entry->placement == BLK_PLACEMENT_FAST) {
                rwfd = POLYSTORE_FAST_FD(file->polyfd);
        } else if (it_entry->placement == BLK_PLACEMENT_SLOW) {
                rwfd = POLYSTORE_SLOW_FD(file->polyfd);
        }

#ifndef _HETERO_CACHE_PG
        /* Interval-level Cache admission */

        if (it_entry->present == 0) {
                ret = pread(rwfd, it_entry->buf_addr,
                                it_entry->size, it_entry->offset);
                if (ret < 0) {
                        ERROR_T("failed to admit to cache, get %d\n", ret);
                }
                it_entry->present = 1;
        }

        /* Special logics for write operation */
        if (rw == WRITE_IO) {
                /* Set interval dirty flag */
                it_entry->dirty = 1;

                /* Update it_entry size */
                it_entry->size = (end + 1 > it_entry->size) ?
                                                end + 1 : it_entry->size;
        }

        if (poly_cache_size_get(&it_entry->cache_size) == 0) {
                /* Update per-interval poly cache size */
                poly_cache_size_set(&it_entry->cache_size, POLY_INDEX_NODE_SIZE);

                /* Update system-wide hetero cache size */
                poly_cache_size_add(&poly_cache_size, POLY_INDEX_NODE_SIZE);
        }

#else
        /* Page-level Cache admission */

        /* Special logics for write operation */
        if (rw == WRITE_IO) {
                /* Set interval dirty flag */
                it_entry->dirty = 1;

                /* Set pg dirty bit */
                poly_cache_pg_setdirty(it_entry, start, end);

                /* Caculate size to load from storage in case of append operation */
                if (start >= it_entry->size) {
                        /* Pure append operation, no need to load from storage */

                        /* Update interval cache size for append operation */
                        poly_cache_size_add(&it_entry->cache_size, size);

                        /* Update hetero cache size for append operation */
                        poly_cache_size_add(&poly_cache_size, size);

                        /* Set present bit for appended pages */
                        poly_cache_pg_setpresent(it_entry, start, end);

                        /* Update it_entry size */
                        it_entry->size = end + 1;

                        return ret;
                } else if (end > it_entry->size - 1) {
                        /* Partial append operation, only need to load accordingly */
                        size = it_entry->size - start;

                        /* Update interval cache size for append operation */
                        poly_cache_size_add(&it_entry->cache_size,
                                                        end - it_entry->size + 1);

                        /* Update hetero cache size for append operation */
                        poly_cache_size_add(&poly_cache_size,
                                                        end - it_entry->size + 1);

                        /* Set present bit for appended pages */
                        poly_cache_pg_setpresent(it_entry, it_entry->size, end);

                        /* Update it_entry size */
                        it_entry->size = end + 1;
                }
        }

        /* Calulate number of pages to load into cache */
        iter = ((size & (PAGE_SIZE - 1)) == 0) ?
                (size >> PAGE_SIZE_SHIFT) : ((size >> PAGE_SIZE_SHIFT) + 1);

        for (int i = 0; i < iter; ++i, ++idx) {
                if (get_bitmap(it_entry->pg_present_bitmap, idx) == 0) {
                        /* Load from storage to cache if necessary */
                        ret = pread(rwfd, buf, PAGE_SIZE, offset);
                        if (ret < 0) {
                                ERROR_T("failed to admit to cache get %d,"
                                        " offset %ld, iter = %d, rw = %d\n",
                                                ret, offset, iter, rw);
                                return ret;
                        }
                        set_bitmap(it_entry->pg_present_bitmap, idx);

                        /* Update per-interval poly cache size */
                        poly_cache_size_add(&it_entry->cache_size, PAGE_SIZE);

                        /* Update system-wide poly cache size */
                        poly_cache_size_add(&_cache_size, PAGE_SIZE);
                }
                buf += PAGE_SIZE;
                offset += PAGE_SIZE;
        }
#endif

        return ret;
}

/* Poly cache eviction */
int poly_cache_evict(struct poly_inode *inode, struct it_node_entry *it_entry,
                                        off_t start, off_t end) {
        char realpath[PATH_MAX];
        int ret = 0, rwfd = 0;
        int size = end - start + 1, iter = 0;
        int idx = start >> PAGE_SIZE_SHIFT;
        off_t offset = it_entry->offset + (idx << PAGE_SIZE_SHIFT);
        void *buf = it_entry->buf_addr + (idx << PAGE_SIZE_SHIFT);
        void *buf_cond = buf;
        off_t offset_cond = offset;
        int len = 0;

        /* Open rwfd if not opened */
        if (it_entry->placement == BLK_PLACEMENT_FAST) {
                if (inode->cache_rwfd_fast == -1) {
                        util_get_fastpath(inode->pathname, realpath);
                        //ret = open(realpath, O_DIRECT | O_RDWR, 0755);
                        ret = open(realpath, O_RDWR, 0755);
                        if (ret < 0) {
                                ERROR_T("failed to open %s, get %d\n", 
                                                realpath, ret);
                                return ret;
                        }
                        inode->cache_rwfd_fast = ret;
                }
                rwfd = inode->cache_rwfd_fast;
        } else if (it_entry->placement == BLK_PLACEMENT_SLOW) {
                if (inode->cache_rwfd_slow == -1) {
                        util_get_slowpath(inode->pathname, realpath);
                        //ret = open(realpath, O_DIRECT | O_RDWR, 0755);
                        ret = open(realpath, O_RDWR, 0755);
                        if (ret < 0) {
                                ERROR_T("failed to open %s, get %d\n", 
                                                realpath, ret);
                                return ret;
                        }
                        inode->cache_rwfd_slow = ret;
                }
                rwfd = inode->cache_rwfd_slow;
        }

#ifndef _HETERO_CACHE_PG
        /* Interval-level Cache eviction */

        if (it_entry->dirty == 1) {
                if (rwfd < 0)
                        printf("rwfd invalid!\n");
                ret = pwrite(rwfd, it_entry->buf_addr, size, it_entry->offset);
                if (ret < 0) {
                        ERROR_T("failed to write back to device, get %d\n", ret);
                }
        }

#else
        /* Page-level Cache eviction */

        /* Calulate number of pages to evict */
        iter = ((size & (PAGE_SIZE - 1)) == 0) ?
                (size >> PAGE_SIZE_SHIFT) : ((size >> PAGE_SIZE_SHIFT) + 1);

        /* Combine consecutive pages into one write */
        for (int i = 0; i < iter; ++i, ++idx) {
                if (get_bitmap(it_entry->pg_dirty_bitmap, idx) == 1) {
                        ++len;
                        if (idx > 0 && get_bitmap(
                                        it_entry->pg_dirty_bitmap, idx-1) == 0) {
                                buf_cond = buf + idx * PAGE_SIZE;
                                offset_cond = offset + idx * PAGE_SIZE;
                        }
                } else {
                        if (len > 0) {
                                ret = pwrite(rwfd, buf_cond, 
                                                PAGE_SIZE*len, offset_cond);
                                if (ret <= 0) {
                                        ERROR_T("failed to evict, get %d\n", ret);
                                        return ret;
                                }
                                len = 0;
                        }
                }
        }
        if (len > 0) {
                ret = pwrite(rwfd, buf_cond, PAGE_SIZE*len, offset_cond);
                if (ret <= 0) {
                        ERROR_T("failed to evict, get %d\n", ret);
                        return ret;
                }
        }
        for (int i = 0, idx = start >> PAGE_SIZE_SHIFT; i < iter; ++i, ++idx) {
                unset_bitmap(it_entry->pg_dirty_bitmap, idx);
                unset_bitmap(it_entry->pg_present_bitmap, idx);
        }
#endif

        return 0;
}

#ifdef _HETERO_CACHE_PG
void poly_cache_pg_setpresent(struct it_node_entry *it_entry,
                                off_t start, off_t end) {
        int iter = (end - start + 1) >> PAGE_SIZE_SHIFT;
        int idx = start >> PAGE_SIZE_SHIFT;
        for (int i = 0; i < iter; ++i, ++idx) {
                set_bitmap(it_entry->pg_present_bitmap, idx);
        }
}

void poly_cache_pg_setdirty(struct it_node_entry *it_entry,
                                off_t start, off_t end) {
        int iter = (end - start + 1) >> PAGE_SIZE_SHIFT;
        int idx = start >> PAGE_SIZE_SHIFT;
        for (int i = 0; i < iter; ++i, ++idx) {
                set_bitmap(it_entry->pg_dirty_bitmap, idx);
        }
}
#endif

/* Cache flushing and eviction worker function */
size_t poly_cache_flushing_worker(void *arg) {
        int queue_id = *(int*)arg;
        struct it_node_entry *victim = NULL, *tmp = NULL;
        struct poly_inode *victim_inode = NULL;
        size_t written = 0, evicted = 0;
        char relapath[PATH_MAX];
        char realpath[PATH_MAX];

#ifdef POLYSTORE_POLYCACHE_DEBUG
        printf("tid %ld flushing thread %d started\n", 
                        syscall(SYS_gettid), queue_id);
#endif

        pthread_mutex_lock(&poly_cache_flushing_queue_mutex[queue_id]);
        list_for_each_entry_safe(victim, tmp, 
                        &poly_cache_flushing_queue[queue_id], cache_lru) {
                /* Remove from list if the file is deleted */
                if (victim->removed == 1) {
                        poly_cache_lru_list_del(&victim->cache_lru);
                        continue;
                }
                victim_inode = victim->inode;
                if (!victim_inode) {
                        ERROR_T("Failed to find h_inode from interval!\n");
                        break;
                }

                /* Lock this interval as it is being flushed */
                pthread_mutex_lock(&victim->lock);

                victim_inode = victim->inode;

                /* Write back to storage if dirty */
                poly_cache_evict(victim_inode, victim, 0, victim->size - 1);

                /* Clear per-interval dirty flag */
                victim->dirty = 0;

                /* Clear per-interval present flag */
                victim->present = 0;

                /* free cache buffer */
                if (victim->buf_addr) {
                        //free(victim->buf_addr);
                        poly_cache_mm_free(victim->buf_addr);
                        victim->buf_addr = NULL;
                }

                /* reset per-interval cache size */
                victim->cache_size = 0;

                /* clear ref count */
                victim->ref_count = 0;
                
                /* remove the evicted node from dispatch list */
                poly_cache_lru_list_del(&victim->cache_lru);

                /* Unlock this interval after it is being flushed */
                pthread_mutex_unlock(&victim->lock);

                evicted += written;
        }
        pthread_mutex_unlock(&poly_cache_flushing_queue_mutex[queue_id]);

#ifdef POLYSTORE_POLYCACHE_DEBUG
        printf("tid %ld flushing thread %d finished\n", 
                        syscall(SYS_gettid), queue_id);
#endif

        return evicted;
}


/* Cache flushing and eviction dispatch function */
size_t poly_cache_flushing_dispatch() {
        struct it_node_entry *victim = NULL, *tmp = NULL;
        uint32_t rr = 0, i = 0, queue_id = 0;
        size_t cache_size_snapshot = poly_cache_size;
        size_t cache_size_before = poly_cache_size;
        size_t dispatched = 0;
        int *worker_thread_arg = malloc(poly_cache_flushing_thread_nr*sizeof(int));
        
        /* Dispatching intervals to evict to the per flushing thread queue */
        pthread_spin_lock(&lru_cold_list_lock);
        list_for_each_entry_safe(victim, tmp, &interval_cold_list, cache_lru) {
                /* Start flushing an interval from the cold list */
                if (&victim->cache_lru == &interval_cold_list) {
                        ERROR_T("LRU cold list empty\n");
                        break;                        
                }

                /* Remove from list if the file is deleted */
                if (victim->removed == 1) {
                        poly_cache_lru_list_del(&victim->cache_lru);
                        continue;
                }

                /* 
                 * Move the evicted interval node from LRU list 
                 * to worker thread queue
                 */
                poly_cache_lru_list_del(&victim->cache_lru);
                queue_id = (rr++) & (poly_cache_flushing_thread_nr - 1);
                pthread_mutex_lock(&poly_cache_flushing_queue_mutex[queue_id]);
                list_add_tail(&victim->cache_lru, 
                                &poly_cache_flushing_queue[queue_id]);
                pthread_mutex_unlock(&poly_cache_flushing_queue_mutex[queue_id]);

                /* Update system-wide hetero cache size */
                cache_size_snapshot -= victim->cache_size;
                if (cache_size_snapshot < poly_cache_flushing_end) {
                        /* End dispatching when threshold reached */
                        break;
                }
        }
        pthread_spin_unlock(&lru_cold_list_lock);

#if 0 // TODO: Keep this block for now for more testing without just removing
        /* Dispatching intervals to evict to the per flushing thread queue */
        pthread_spin_lock(&lru_hot_list_lock);
        list_for_each_entry_safe(victim, tmp, &interval_hot_list, 
                        cache_lru) {
                /* Start flushing an interval from the hot list */
                if (&victim->cache_lru == &interval_hot_list) {
                        ERROR_T("LRU hot list empty\n");
                        break;                        
                }

                /* Remove from list if the file is deleted */
                if (victim->removed == 1) {
                        poly_cache_lru_list_del(&victim->cache_lru);
                        continue;
                }

                /* 
                 * Move the evicted interval node from LRU list 
                 * to worker thread queue
                 */
                poly_cache_lru_list_del(&victim->cache_lru);
                queue_id = (rr++) & (poly_cache_flushing_thread_nr - 1);
                pthread_mutex_lock(&poly_cache_flushing_queue_mutex[queue_id]);
                list_add_tail(&victim->cache_lru, 
                                &poly_cache_flushing_queue[queue_id]);
                pthread_mutex_unlock(&poly_cache_flushing_queue_mutex[queue_id]);

                /* 
                 * Move the evicted interval node from LRU list 
                 * to worker thread queue 
                 */
                /* Update system-wide hetero cache size */
                cache_size_snapshot -= victim->cache_size;
                if (cache_size_snapshot < poly_cache_flushing_end_threshold) {
                        /* End dispatching when threshold reached */
                        break;
                }
        }
        pthread_spin_unlock(&lru_hot_list_lock);
#endif

        /* Reset system-wide hetero cache size */
        poly_cache_size_set(&poly_cache_size, poly_cache_flushing_end);

        /* Launch cache flushing worker threads */
        for (i = 0; i < poly_cache_flushing_thread_nr; ++i) {
                worker_thread_arg[i] = i;
                thpool_add_work(poly_cache_thpool, 
                                (void*)poly_cache_flushing_worker, 
                                (void*)&worker_thread_arg[i]);
        }

        /* release flushing indicator */
        __sync_lock_release(&poly_cache_flushing_indicator);

        //free(worker_thread_arg);
        return dispatched;
}


/* Poly cache drop */
void poly_cache_drop(struct poly_inode *inode) {
        struct it_node_entry *it_entry = NULL;
        off_t start, end;

        start = 0;
        end = inode->size - 1;
      
#ifdef POLYSTORE_POLYCACHE_DEBUG
        //printf("drop file %s start\n", inode->pathname);
#endif

        pthread_rwlock_wrlock(&inode->it_tree_lock);

        it_entry = polystore_index_lookup(inode, start, end);
        while (it_entry) {
                pthread_mutex_lock(&it_entry->lock);

                if (it_entry->placement == BLK_PLACEMENT_FAST && 
                                inode->cache_policy == 
                                CACHE_POLICY_FAST_READ_NOT_ADMIT) {
                        /* Unmap from the file when not admitted into cache */
                        if (it_entry->buf_addr)
                                munmap(it_entry->buf_addr, it_entry->size);
                } else if (it_entry->buf_addr) {
                        /* Free the cache buffer when dropped */ 
                        //free(it_entry->buf_addr);
                        poly_cache_mm_free(it_entry->buf_addr);
                        it_entry->buf_addr = NULL;
                }
                it_entry->removed = 1;

                pthread_mutex_unlock(&it_entry->lock);

                it_entry = polystore_index_lookup_next(it_entry, start, end);
        }

        pthread_rwlock_unlock(&inode->it_tree_lock);

        /* Close and reset rw fd */
        if (inode->cache_rwfd_fast > 0) {
                close(inode->cache_rwfd_fast);
                inode->cache_rwfd_fast = -1;
        }

        if (inode->cache_rwfd_slow > 0) {
                close(inode->cache_rwfd_slow);
                inode->cache_rwfd_slow = -1;
        }

#ifdef POLYSTORE_POLYCACHE_DEBUG
        //printf("drop file %s finish\n", inode->pathname);
#endif
}

/* Poly cache free when applications finish */
void poly_cache_free(struct poly_inode *inode) {
        struct it_node_entry *it_entry = NULL;
        off_t start, end;
        size_t written = 0;
        char relapath[PATH_MAX];
        char fastpath[PATH_MAX];
        char slowpath[PATH_MAX];

        start = 0;
        end = inode->size - 1;

        pthread_rwlock_wrlock(&inode->it_tree_lock);

        it_entry = polystore_index_lookup(inode, start, end);
        while (it_entry) {
        
                //pthread_mutex_lock(&it_entry->lock);

                if (!it_entry->removed) {
                        /* Write dirty interval buffer to storage */
                        poly_cache_evict(inode, it_entry, 0, it_entry->size - 1);

                        /* Clear per-interval dirty flag */
                        it_entry->dirty = 0;

                        /* Clear per-interval present flag */
                        it_entry->present = 0;

                        /* Reset per-interval cache size */
                        it_entry->cache_size = 0;

                        if (it_entry->placement == BLK_PLACEMENT_FAST && 
                                        inode->cache_policy == 
                                        CACHE_POLICY_FAST_READ_NOT_ADMIT) {
                                /* Unmap from the file when not admitted into cache */
                                if (it_entry->buf_addr)
                                        munmap(it_entry->buf_addr, it_entry->size);
                        } else if (it_entry->buf_addr) {
                                /* Free the cache buffer when dropped */ 
                                //free(it_entry->buf_addr);
                                poly_cache_mm_free(it_entry->buf_addr);

                                /* remove the evicted interval from lru list */
                                poly_cache_lru_list_del(&it_entry->cache_lru);
                        }
                }
                it_entry->buf_addr = NULL;

                //pthread_mutex_unlock(&it_entry->lock);

                it_entry = polystore_index_lookup_next(it_entry, start, end);
        }

        pthread_rwlock_unlock(&inode->it_tree_lock);

        /* Close and reset rw fd */
        if (inode->cache_rwfd_fast > 0) {
                close(inode->cache_rwfd_fast);
                inode->cache_rwfd_fast = -1;
        }

        if (inode->cache_rwfd_slow > 0) {
                close(inode->cache_rwfd_slow);
                inode->cache_rwfd_slow = -1;
        }

        if (inode->cache_mmapfd > 0) {
                close(inode->cache_mmapfd);
                inode->cache_mmapfd = -1;
        }
}

/* Poly cache fsync */
int poly_cache_fsync(struct poly_inode *inode, int fastfd, int slowfd) {
        struct it_node_entry *it_entry = NULL;
        off_t start = 0, end = inode->size - 1;
        off_t node_start = 0, node_end = 0;
        int ret = 0;

        /* Start interating the intervals */
        pthread_rwlock_rdlock(&inode->it_tree_lock);

        it_entry = polystore_index_lookup(inode, start, end);
        while (it_entry) {
                node_start = it_entry->it.start;
                node_end = it_entry->it.last;

                /* Only sync the dirty data to storage */
                if (it_entry->dirty == 1) {
                        /* 
                         * Lock the interval to prevent being 
                         * flushed by background thread
                         */
                        pthread_mutex_lock(&it_entry->lock);

                        if (it_entry->placement == BLK_PLACEMENT_FAST) {
                                ret = pwrite(fastfd, it_entry->buf_addr, 
                                                it_entry->size, it_entry->offset);
                        } else {
                                ret = pwrite(slowfd, it_entry->buf_addr, 
                                                it_entry->size, it_entry->offset);
                        }
                        if (ret != it_entry->size) {
                                ERROR_T("Failed to flush data in fsync,"
                                                "get %d\n", ret);
                        }

                        /* Clear the dirty flag */
                        it_entry->dirty = 0;

                        /* Release the interval lock */
                        pthread_mutex_unlock(&it_entry->lock);
                }
      
                /* Iterate through the next interval tree node */
                it_entry = polystore_index_lookup_next(it_entry, start, end);
        }

        pthread_rwlock_unlock(&inode->it_tree_lock);

        /* Calling real fsync after flusing all the cached data */
        ret = fsync(fastfd) | fsync(slowfd);

        return ret;
}

void poly_cache_initialize(void) {
        int i = 0;
        const char *shell_poly_cache_flushing_begin = NULL;
        const char *shell_poly_cache_flushing_end = NULL;
        const char *shell_poly_cache_policy = NULL;

        /* Initialize cache flushing threshold */
        shell_poly_cache_flushing_begin = getenv("POLYCACHE_FLUSHING_BEGIN");
        if (shell_poly_cache_flushing_begin) {
                poly_cache_flushing_begin =
                        capacity_stoul((char*)shell_poly_cache_flushing_begin);
        }

        shell_poly_cache_flushing_end = getenv("POLYCACHE_FLUSHING_END");
        if (shell_poly_cache_flushing_end) {
                poly_cache_flushing_end =
                        capacity_stoul((char*)shell_poly_cache_flushing_end);
        }

        shell_poly_cache_policy = getenv("POLYCACHE_POLICY");
        if (shell_poly_cache_policy) {
                poly_cache_policy = atoi((char*)shell_poly_cache_policy);
        }

        /* Initialize background cache flushing thread pool */
        poly_cache_thpool = thpool_init(poly_cache_flushing_thread_nr);
        //poly_cache_thpool = thpool_init(1);

        /* Initialize background cache flushing thread worker queue */
        poly_cache_flushing_queue = malloc(poly_cache_flushing_thread_nr *
                                        sizeof(struct list_head));
        poly_cache_flushing_queue_mutex = malloc(poly_cache_flushing_thread_nr *
                                        sizeof(pthread_mutex_t));
        for (i = 0; i < poly_cache_flushing_thread_nr; ++i) {
                INIT_LIST_HEAD(&poly_cache_flushing_queue[i]);
                pthread_mutex_init(&poly_cache_flushing_queue_mutex[i], NULL);
        }

        pthread_spin_init(&lru_hot_list_lock, 0);
        pthread_spin_init(&lru_cold_list_lock, 0);

        /* Initialize shared memory */
        poly_cache_mm_init();
}

void poly_cache_exit(void) {
        INFO_T("access cnt = %ld", cache_access_cnt);
        INFO_T("miss cnt = %ld", cache_miss_cnt);

        /* Wait until the background flushing thread finish */
        thpool_wait(poly_cache_thpool);

        /* Reclaim per-inode interval-tree and cache buffer */
#ifndef POLYSTORE_POLYCACHE_SHM
        polystore_inode_hashmap_iterate(poly_cache_free);
#endif

        /* Release background cache flushing thread worker queue */
        free(poly_cache_flushing_queue);
        free(poly_cache_flushing_queue_mutex);

        /* Free shared memory */
        poly_cache_mm_exit();
}
