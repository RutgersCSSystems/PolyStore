/*
 * io_buffered.c
 */

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>

#include "polystore.h"
#include "debug.h"


/* Set up mmap for PMEM reads without Poly cache admission */
static void* mmap_pmem_cache_file(char *pathname, struct it_node_entry *it_entry, 
                                struct poly_inode *inode) {
        char realpath[PATH_MAX];
        util_get_fastpath(pathname, realpath);

        if (inode->cache_mmapfd <= 0) {
                inode->cache_mmapfd = open(realpath, O_RDWR, 0755);
                if (inode->cache_mmapfd < 0) {
                        fprintf(stderr, "failed to open formmap, get %d\n", 
                                inode->cache_mmapfd);
                }
        }

        /* Memory-map the file to this interval entry */
        it_entry->buf_addr = mmap(NULL, it_entry->size,
                        PROT_READ | PROT_WRITE, MAP_PRIVATE, 
                        inode->cache_mmapfd, it_entry->offset);
        if (it_entry->buf_addr == MAP_FAILED) {
                fprintf(stderr, "mmap failed, errno = %d\n", errno);
        }

        return it_entry->buf_addr;
}


/* Offset related functions */
static void inline polystore_io_set_range(struct poly_file *file, 
                                          off_t offset, size_t count,
                                          off_t *start, off_t *end) {
        if (offset < 0) {
                /* sequential read/write ops */
                *start = file->off;
        } else {
                /* positioned read/write ops */
                *start = offset;
        }

        *end = *start + count - 1;
}

/* PolyStore read function iterator according to placement */
ssize_t polystore_read_iter_polycache(struct poly_file *file, void *buf, 
                                   size_t count, off_t offset) {
        int polyfd = file->polyfd;
        int fastfd = POLYSTORE_FAST_FD(polyfd);
        int slowfd = POLYSTORE_SLOW_FD(polyfd);
        struct poly_inode *inode = file->inode;
        struct g_task_ctx *task_ctx = tls_task_ctx.task_ctx;

        struct it_node_entry *it_entry = NULL, *new_it_entry = NULL;
        off_t start = 0, end = 0;
        off_t node_start = 0, node_end = 0, it_buf_start = 0, real_pos = 0;
        size_t it_buf_count = 0;
        ssize_t copied = 0, ret = 0;
        int ref_count = 0;
        uint64_t node_ptr = 0;

        /* Directly return if I/O size is zero */
        if (count == 0) {
                return copied;
        }

        /* Set correct range for this I/O op */
        polystore_io_set_range(file, offset, count, &start, &end);

        /* Start iterating the Poly-index */
        pthread_rwlock_rdlock(&inode->it_tree_lock);
        
        it_entry = polystore_index_lookup(inode, start, end);
        while (it_entry) {
                node_start = it_entry->it.start;
                node_end = it_entry->it.last;

                it_buf_start = start - node_start;
                it_buf_count = (node_end < end) ? 
                                node_end - start + 1 : 
                                end - start + 1;

                /* 
                 * Check if this interval to read is currently
                 * the last interval of this file
                 */
                if (it_entry->size < POLY_INDEX_NODE_SIZE) {
                        it_buf_count = (end - start + 1 < it_entry->size) ? 
                                end - start + 1 : it_entry->size - it_buf_start;
                }

                /* check if the start offset to read is beyond legal offset */
                if (it_buf_start >= it_entry->size) {
                        //start = end;
                        break;
                }

                /* Lock the interval to prevent flushed by background thread */
                pthread_mutex_lock(&it_entry->lock);

                if (!it_entry->buf_addr) {
                        if (it_entry->placement == BLK_PLACEMENT_FAST &&
                                        inode->cache_policy == 
                                        CACHE_POLICY_FAST_READ_NOT_ADMIT) {

                                /* Do not admit to hetero cache for PMEM */
                                it_entry->buf_addr = 
                                        mmap_pmem_cache_file(inode->pathname, 
                                                        it_entry, inode);
                                if (!it_entry->buf_addr) {
                                        fprintf(stderr, "%s: %d, "
                                                "mmap_pmem_cache_file failed \n",
                                                        __func__, __LINE__);
                                        exit(-1);
                                }
                        } else {
                                /* allocate cache buffer if interval not in cache */
                                /*posix_memalign((void**)&it_entry->buf_addr,
                                        PAGE_SIZE, POLY_INDEX_NODE_SIZE);*/
                                it_entry->buf_addr = poly_cache_mm_alloc();

                                /* Insert new interval to cache cold lru list */
                                poly_cache_add_lru_cold_list(it_entry);
                        }
                }

                /* Admit to hetero cache */
                poly_cache_admit(file, it_entry, it_buf_start,
                                        it_buf_start + it_buf_count - 1, READ_IO);

                /* copy data from hetero cache buf to user buf */
                if (it_buf_start + it_buf_count > POLY_INDEX_NODE_SIZE)
                        printf("!!!!!!!!!!!!!!! read overflow, start %lu, count %lu\n", it_buf_start, it_buf_count);
                memcpy(buf + copied, it_entry->buf_addr + it_buf_start,
                                it_buf_count);

                ref_count = __sync_fetch_and_add(&it_entry->ref_count, 1);
#if 0
                if (ref_count == 0) {
                        /* add to cold list if the interval is just loaded from storage */
                        pthread_spin_lock(&lru_cold_list_lock);
                        poly_cache_lru_list_add(&it_entry->cache_lru_list, &interval_cold_list);
                        pthread_spin_unlock(&lru_cold_list_lock);
                } else if (ref_count == 1) {
                        /* If the interval is accessed more than once, move to hot list */
                        pthread_spin_lock(&lru_cold_list_lock);
                        poly_cache_lru_list_del(&it_entry->cache_lru_list);
                        pthread_spin_unlock(&lru_cold_list_lock);

                        pthread_spin_lock(&lru_hot_list_lock);
                        poly_cache_lru_list_add(&it_entry->cache_lru_list, &interval_hot_list);
                        pthread_spin_unlock(&lru_hot_list_lock);
                }
#endif
                /* Release the interval lock */
                pthread_mutex_unlock(&it_entry->lock);

                if (offset < 0) {
                        file->off += it_buf_count;
                }
                copied += it_buf_count;
                start = (node_end < end) ? node_end + 1 : end + 1;

                /* Iterate through the next Poly-index node */
                it_entry = polystore_index_lookup_next(it_entry, start, end);
        }

        pthread_rwlock_unlock(&inode->it_tree_lock);

        /* Update statistics */
        task_ctx->throughput += copied;
        task_ctx->throughput_read += copied;

        __sync_fetch_and_add(&cache_access_cnt, 1);

        return copied;
}

/* PolyStore append function iterator according to placement */
ssize_t polystore_append_iter_polycache(struct poly_file *file, const void *buf, 
                                    size_t count, off_t offset) {
        int polyfd = file->polyfd;
        int fastfd = POLYSTORE_FAST_FD(polyfd);
        int slowfd = POLYSTORE_SLOW_FD(polyfd);
        struct poly_inode *inode = file->inode;
        struct g_task_ctx *task_ctx = tls_task_ctx.task_ctx;

        struct it_node_entry *it_entry = NULL, *new_it_entry = NULL;
        off_t start = 0, end = 0;
        off_t node_start = 0, node_end = 0, it_buf_start = 0, real_pos = 0;
        size_t it_buf_count = 0;
        ssize_t copied = 0, ret = 0;
        int ref_count = 0;
        uint64_t node_ptr = 0;

        /* Directly return if I/O size is zero */
        if (count == 0) {
                return copied;
        }

        /* Start iterating the Poly-index */
        pthread_rwlock_wrlock(&inode->it_tree_lock);

        /* Set correct range for this I/O op */
        polystore_io_set_range(file, offset, count, &start, &end);
        
        it_entry = polystore_index_lookup(inode, start, end);
        while (it_entry) {
                node_start = it_entry->it.start;
                node_end = it_entry->it.last;

                it_buf_start = start - node_start;
                it_buf_count = (node_end < end) ? 
                                node_end - start + 1 : 
                                end - start + 1;

                /* Lock the interval to prevent flushed by background thread */
                pthread_mutex_lock(&it_entry->lock);

                if (!it_entry->buf_addr) {
                        /* Allocate cache buffer */
                        /*posix_memalign((void**)&it_entry->buf_addr,
                                PAGE_SIZE, POLY_INDEX_NODE_SIZE);*/
                        it_entry->buf_addr = poly_cache_mm_alloc();

                        /* Insert new interval to cache cold lru list */
                        poly_cache_add_lru_cold_list(it_entry);
                }

                /* Admit to hetero cache */
                poly_cache_admit(file, it_entry, it_buf_start,
                                        it_buf_start + it_buf_count - 1, WRITE_IO);

                /* Copy data from user buf to hetero cache buf */
                memcpy(it_entry->buf_addr + it_buf_start, buf + copied,
                                it_buf_count);

                ref_count = __sync_fetch_and_add(&it_entry->ref_count, 1);
#if 0
                if (ref_count == 0) {
                        /* add to cold list if the interval is just loaded from storage */
                        pthread_spin_lock(&lru_cold_list_lock);
                        poly_cache_lru_list_add(&it_entry->cache_lru_list, &interval_cold_list);
                        pthread_spin_unlock(&lru_cold_list_lock);
                } else if (ref_count == 1) {
                        /* If the interval is accessed more than once, move to hot list */
                        pthread_spin_lock(&lru_cold_list_lock);
                        poly_cache_lru_list_del(&it_entry->cache_lru_list);
                        pthread_spin_unlock(&lru_cold_list_lock);

                        pthread_spin_lock(&lru_hot_list_lock);
                        poly_cache_lru_list_add(&it_entry->cache_lru_list, &interval_hot_list);
                        pthread_spin_unlock(&lru_hot_list_lock);
                }
#endif
                /* Release the interval lock */
                pthread_mutex_unlock(&it_entry->lock);

                if (offset < 0) {
                        file->off += it_buf_count;
                }
                copied += it_buf_count;
                start = (node_end < end) ? node_end + 1 : end + 1;

                /* Iterate through the next Poly-index node */
                it_entry = polystore_index_lookup_next(it_entry, start, end);
        }

        /* 
         * If a hole still exist at the end of range [start, end],
         * meaning more data are written to the file, then create 
         * new interval tree nodes and insert
         */
        while (start < end) {
                it_buf_start = start - (start & POLY_INDEX_NODE_SIZE_MASK);
                it_buf_count = end - start + 1;
                it_buf_count = it_buf_count > POLY_INDEX_NODE_SIZE ? 
                                        POLY_INDEX_NODE_SIZE : it_buf_count;

                /* Allocate new Poly-index node */
                new_it_entry = polystore_index_entry_allocate(inode);
                if (!new_it_entry) {
                        ERROR_T("failed to allocate poly-index entry");
                        break;
                }

                new_it_entry->it.start = 
                                start & POLY_INDEX_NODE_SIZE_MASK;
                new_it_entry->it.last = 
                                new_it_entry->it.start + POLY_INDEX_NODE_SIZE - 1;
                new_it_entry->size = 0;
                new_it_entry->cache_size = 0;
                new_it_entry->inode = inode;
                new_it_entry->ref_count = 1;
                new_it_entry->dirty = 1;
                new_it_entry->present = 1;
                new_it_entry->removed = 0;
                //posix_memalign((void**)&new_it_entry->buf_addr, PAGE_SIZE, POLY_INDEX_NODE_SIZE); 
                new_it_entry->buf_addr = poly_cache_mm_alloc();
                memset(new_it_entry->buf_addr, 0, POLY_INDEX_NODE_SIZE);
                pthread_mutex_init(&new_it_entry->lock, NULL);

                new_it_entry->placement = task_ctx->placement;

                /* Admit to hetero cache */
                poly_cache_admit(file, new_it_entry, it_buf_start,
                                        it_buf_start + it_buf_count - 1, WRITE_IO);

                /* copy from user buf to hetero cache */
                memcpy(new_it_entry->buf_addr, buf + copied, it_buf_count);

                /* get the offset of the physical file */
                if (new_it_entry->placement == BLK_PLACEMENT_FAST) {
                        new_it_entry->offset = 
                                __sync_fetch_and_add(&inode->cache_fast_offset, 
                                                POLY_INDEX_NODE_SIZE);
                } else {
                        new_it_entry->offset =
                                __sync_fetch_and_add(&inode->cache_slow_offset, 
                                                POLY_INDEX_NODE_SIZE);
                }

                /* Insert new interval to cache cold lru list */
                poly_cache_add_lru_cold_list(new_it_entry);

                /* Insert the new interval to the inode interval tree */
                polystore_index_insert(inode, new_it_entry);

                if (offset < 0) {
                        file->off += it_buf_count;
                }
                copied += it_buf_count;
                start += it_buf_count;
        }

        /* Update poly_inode size */
        inode->size = (end + 1 > inode->size) ? (end + 1) : inode->size;

        pthread_rwlock_unlock(&inode->it_tree_lock);

#ifdef POLYSTORE_POLYCACHE_SENSITIVITY
        /* evict Poly cache if necessary */
        if (poly_cache_size > poly_cache_flushing_begin) {
                if (__sync_lock_test_and_set(&poly_cache_flushing_indicator, 1) == 0) {
                        //thpool_add_work(poly_cache_thpool, (void*)poly_cache_flushing, NULL);
                        poly_cache_flushing_dispatch();
                }
                //thpool_wait(poly_cache_thpool);
        }
#endif

        /* Update statistics */
        task_ctx->throughput += copied;
        task_ctx->throughput_write += copied;

        return copied;
}


/* PolyStore write function iterator according to placement */
ssize_t polystore_write_iter_polycache(struct poly_file *file, const void *buf, 
                                    size_t count, off_t offset) {
        int polyfd = file->polyfd;
        int fastfd = POLYSTORE_FAST_FD(polyfd);
        int slowfd = POLYSTORE_SLOW_FD(polyfd);
        struct poly_inode *inode = file->inode;
        struct g_task_ctx *task_ctx = tls_task_ctx.task_ctx;

        struct it_node_entry *it_entry = NULL, *new_it_entry = NULL;
        off_t start = 0, end = 0;
        off_t node_start = 0, node_end = 0, it_buf_start = 0, real_pos = 0;
        size_t it_buf_count = 0;
        ssize_t copied = 0, ret = 0;
        int ref_count = 0;
        uint64_t node_ptr = 0;

        /* Directly return if I/O size is zero */
        if (count == 0) {
                return copied;
        }

        /* Set correct range for this I/O op */
        polystore_io_set_range(file, offset, count, &start, &end);

        /* Start iterating the Poly-index */
        pthread_rwlock_rdlock(&inode->it_tree_lock);
        
        it_entry = polystore_index_lookup(inode, start, end);
        while (it_entry) {
                node_start = it_entry->it.start;
                node_end = it_entry->it.last;

                it_buf_start = start - node_start;
                it_buf_count = (node_end < end) ? 
                                node_end - start + 1 : 
                                end - start + 1;

                /* Lock the interval to prevent flushed by background thread */
                pthread_mutex_lock(&it_entry->lock);

                if (!it_entry->buf_addr) {
                        /* Allocate cache buffer */
                        /*posix_memalign((void**)&it_entry->buf_addr,
                                PAGE_SIZE, POLY_INDEX_NODE_SIZE);*/
                        it_entry->buf_addr = poly_cache_mm_alloc();

                        /* Insert new interval to cache cold lru list */
                        poly_cache_add_lru_cold_list(it_entry);
                }

                /* Admit to hetero cache */
                poly_cache_admit(file, it_entry, it_buf_start,
                                        it_buf_start + it_buf_count - 1, WRITE_IO);

                /* Copy data from user buf to hetero cache buf */
                if (it_buf_start + it_buf_count > POLY_INDEX_NODE_SIZE)
                        printf("!!!!!!!!!!!!!!! write overflow, start %lu, count %lu\n", it_buf_start, it_buf_count);
                memcpy(it_entry->buf_addr + it_buf_start, buf + copied,
                                it_buf_count);

                ref_count = __sync_fetch_and_add(&it_entry->ref_count, 1);
#if 0
                if (ref_count == 0) {
                        /* add to cold list if the interval is just loaded from storage */
                        pthread_spin_lock(&lru_cold_list_lock);
                        poly_cache_lru_list_add(&it_entry->cache_lru_list, &interval_cold_list);
                        pthread_spin_unlock(&lru_cold_list_lock);
                } else if (ref_count == 1) {
                        /* If the interval is accessed more than once, move to hot list */
                        pthread_spin_lock(&lru_cold_list_lock);
                        poly_cache_lru_list_del(&it_entry->cache_lru_list);
                        pthread_spin_unlock(&lru_cold_list_lock);

                        pthread_spin_lock(&lru_hot_list_lock);
                        poly_cache_lru_list_add(&it_entry->cache_lru_list, &interval_hot_list);
                        pthread_spin_unlock(&lru_hot_list_lock);
                }
#endif
                /* Release the interval lock */
                pthread_mutex_unlock(&it_entry->lock);

                if (offset < 0) {
                        file->off += it_buf_count;
                }
                copied += it_buf_count;
                start = (node_end < end) ? node_end + 1 : end + 1;

                /* Iterate through the next Poly-index node */
                it_entry = polystore_index_lookup_next(it_entry, start, end);
        }

        pthread_rwlock_unlock(&inode->it_tree_lock);

        /* 
         * If a hole still exist at the end of range [start, end],
         * meaning more data are written to the file, then create 
         * new interval tree nodes and insert
         */
        while (start < end) {
                it_buf_start = start - (start & POLY_INDEX_NODE_SIZE_MASK);
                it_buf_count = end - start + 1;
                it_buf_count = it_buf_count > POLY_INDEX_NODE_SIZE ? 
                                        POLY_INDEX_NODE_SIZE : it_buf_count;

                /* Allocate new Poly-index node */
                new_it_entry = polystore_index_entry_allocate(inode);
                if (!new_it_entry) {
                        ERROR_T("failed to allocate poly-index entry");
                        break;
                }

                new_it_entry->it.start = 
                                start & POLY_INDEX_NODE_SIZE_MASK;
                new_it_entry->it.last = 
                                new_it_entry->it.start + POLY_INDEX_NODE_SIZE - 1;
                new_it_entry->size = 0;
                new_it_entry->cache_size = 0;
                new_it_entry->inode = inode;
                new_it_entry->ref_count = 1;
                new_it_entry->dirty = 1;
                new_it_entry->present = 1;
                new_it_entry->removed = 0;
                //posix_memalign((void**)&new_it_entry->buf_addr, PAGE_SIZE, POLY_INDEX_NODE_SIZE); 
                new_it_entry->buf_addr = poly_cache_mm_alloc();
                memset(new_it_entry->buf_addr, 0, POLY_INDEX_NODE_SIZE);
                pthread_mutex_init(&new_it_entry->lock, NULL);

                new_it_entry->placement = task_ctx->placement;

                /* Admit to hetero cache */
                poly_cache_admit(file, new_it_entry, it_buf_start,
                                        it_buf_start + it_buf_count - 1, WRITE_IO);

                /* copy from user buf to hetero cache */
                if (it_buf_start + it_buf_count > POLY_INDEX_NODE_SIZE)
                        printf("!!!!!!!!!!!!!!! write overflow, start %lu, count %lu\n", it_buf_start, it_buf_count);
                memcpy(new_it_entry->buf_addr, buf + copied, it_buf_count);

                /* get the offset of the physical file */
                if (new_it_entry->placement == BLK_PLACEMENT_FAST) {
                        new_it_entry->offset = 
                                __sync_fetch_and_add(&inode->cache_fast_offset, 
                                                POLY_INDEX_NODE_SIZE);
                } else {
                        new_it_entry->offset =
                                __sync_fetch_and_add(&inode->cache_slow_offset, 
                                                POLY_INDEX_NODE_SIZE);
                }

                /* Insert new interval to cache cold lru list */
                poly_cache_add_lru_cold_list(new_it_entry);

                /* Insert the new interval to the inode interval tree */
                pthread_rwlock_wrlock(&inode->it_tree_lock);
                polystore_index_insert(inode, new_it_entry);
                pthread_rwlock_unlock(&inode->it_tree_lock);

                if (offset < 0) {
                        file->off += it_buf_count;
                }
                copied += it_buf_count;
                start += it_buf_count;
        }

        /* Update poly_inode size */
        inode->size = (end + 1 > inode->size) ? (end + 1) : inode->size;

#ifdef POLYSTORE_POLYCACHE_SENSITIVITY
        /* evict Poly cache if necessary */
        if (poly_cache_size > poly_cache_flushing_begin) {
                if (__sync_lock_test_and_set(&poly_cache_flushing_indicator, 1) == 0) {
                        //thpool_add_work(poly_cache_thpool, (void*)poly_cache_flushing, NULL);
                        poly_cache_flushing_dispatch();
                }
                //thpool_wait(poly_cache_thpool);
        }
#endif
        /* Update statistics */
        task_ctx->throughput += copied;
        task_ctx->throughput_write += copied;

        return copied;
}
