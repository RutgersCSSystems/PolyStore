/*
 * io_direct.c
 */

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>

#include "polystore.h"
#include "debug.h"

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
ssize_t polystore_read_iter(struct poly_file *file, void *buf, 
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
        uint64_t node_ptr = 0;

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

                if (it_entry->placement == BLK_PLACEMENT_FAST) {
                        ret = pread(fastfd, buf + copied, it_buf_count, 
                                        it_entry->offset + it_buf_start);
                } else if (it_entry->placement == BLK_PLACEMENT_SLOW) {
                        ret = pread(slowfd, buf + copied, it_buf_count, 
                                        it_entry->offset + it_buf_start);
                } else {
                        ERROR_T("read iter: Poly index placement unknown, placement %d", it_entry->placement); 
                        break;
                }

                if (ret < 0) {
                        ERROR_T("read fail with %ld", ret);
                        break;
                } 

                if (offset < 0) {
                        file->off += ret;
                }
                copied += ret;
                start = (node_end < end) ? node_end + 1 : end + 1;

                /* Iterate through the next Poly-index node */
                it_entry = polystore_index_lookup_next(it_entry, start, end);
        }

        pthread_rwlock_unlock(&inode->it_tree_lock);

        /* Update statistics */
        task_ctx->throughput += copied;
        task_ctx->throughput_read += copied;

        return copied;
}

/* PolyStore append function iterator according to placement */
ssize_t polystore_append_iter(struct poly_file *file, const void *buf, 
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
        uint64_t node_ptr = 0;

        /* Start iterating the Poly-index */
        pthread_rwlock_wrlock(&inode->it_tree_lock);

        /* Set correct range for this I/O op */
        polystore_io_set_range(file, inode->size, count, &start, &end);

        it_entry = polystore_index_lookup(inode, start, end);
        while (it_entry) {
                node_start = it_entry->it.start;
                node_end = it_entry->it.last;

                it_buf_start = start - node_start;
                it_buf_count = (node_end < end) ? 
                                node_end - start + 1 : 
                                end - start + 1;

                if (it_entry->placement == BLK_PLACEMENT_FAST) {
                        ret = pwrite(fastfd, buf + copied, it_buf_count, 
                                        it_entry->offset + it_buf_start);
                } else if (it_entry->placement == BLK_PLACEMENT_SLOW) {
                        ret = pwrite(slowfd, buf + copied, it_buf_count, 
                                        it_entry->offset + it_buf_start);
                } else {
                        ERROR_T("write iter: Poly index placement unknown"); 
                        break;
                }

                if (ret != it_buf_count) {
                        ERROR_T("write fail with %ld, expected", 
                                                ret, it_buf_count);
                        break;
                } 

                /* update interval size if it is an append */
                if (end <= node_end && end - node_start > it_entry->size)
                        it_entry->size = end + 1;

                if (offset < 0) {
                        file->off += ret;
                }
                copied += ret;
                start = (node_end < end) ? node_end + 1 : end + 1;

                /* Iterate through the next Poly-index node */
                it_entry = polystore_index_lookup_next(it_entry, start, end);
        }

        /* 
         * If a hole still exist at the end of range [start, end],
         * meaning more data are written to the file, then create 
         * new Poly-index nodes and insert
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
                //new_it_entry->size = 
                //              new_it_entry->it.last - new_it_entry->it.start + 1;
                new_it_entry->size = (end < start + POLY_INDEX_NODE_SIZE) ? 
                                                end + 1 : POLY_INDEX_NODE_SIZE;
                new_it_entry->inode = inode;
                new_it_entry->ref_count = 1;

                new_it_entry->placement = task_ctx->placement;

                /* Need to hold rwlock_wrlock here if kernel inode rwsem is disabled */
                //pthread_rwlock_wrlock(&inode->it_tree_lock); 

                /* Determine the offset in corresponding physical files */
                if (new_it_entry->placement == BLK_PLACEMENT_FAST) {
                        real_pos = lseek(fastfd, 0, SEEK_END);
                } else {
                        real_pos = lseek(slowfd, 0, SEEK_END);
                }

                if ((real_pos & (POLY_INDEX_NODE_SIZE - 1)) == 0) {
                        /* aligned pos */
                        new_it_entry->offset = real_pos;
                } else {
                        /* unaligned pos, do ceiling */
                        new_it_entry->offset = 
                                ((real_pos >> POLY_INDEX_NODE_SIZE_SHIFT) + 1) 
                                        << POLY_INDEX_NODE_SIZE_SHIFT;
                }

                /* Write to corresponding physical files */
                if (new_it_entry->placement == BLK_PLACEMENT_FAST) {
                        ret = pwrite(fastfd, buf + copied, 
                                        it_buf_count, new_it_entry->offset);
                } else {
                        ret = pwrite(slowfd, buf + copied, 
                                        it_buf_count, new_it_entry->offset);
                }

                if (ret != it_buf_count) {
                        ERROR_T("write fail with %ld, expected", 
                                                ret, it_buf_count);
                }

                /* Insert the new interval to the interval tree */
                polystore_index_insert(inode, new_it_entry);

                if (offset < 0) {
                        file->off += it_buf_count;
                }
                copied += it_buf_count;
                start += it_buf_count;
        }


        /* Update poly_inode size */
        // TODO
        inode->size = (end+1) > inode->size ? end+1 : inode->size;

        pthread_rwlock_unlock(&inode->it_tree_lock);

        /* Update statistics */
        task_ctx->throughput += copied;
        task_ctx->throughput_write += copied;

        return copied;
}


/* PolyStore write function iterator according to placement */
ssize_t polystore_write_iter(struct poly_file *file, const void *buf, 
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
        uint64_t node_ptr = 0;

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

                if (it_entry->placement == BLK_PLACEMENT_FAST) {
                        ret = pwrite(fastfd, buf + copied, it_buf_count, 
                                        it_entry->offset + it_buf_start);
                } else if (it_entry->placement == BLK_PLACEMENT_SLOW) {
                        ret = pwrite(slowfd, buf + copied, it_buf_count, 
                                        it_entry->offset + it_buf_start);
                } else {
                        ERROR_T("write iter: Poly index placement unknown"); 
                        break;
                }

                if (ret != it_buf_count) {
                        ERROR_T("write fail with %ld, expected", 
                                                ret, it_buf_count);
                        break;
                } 

                /* update interval size if it is an append */
                if (end <= node_end && end - node_start > it_entry->size)
                        it_entry->size = end + 1;

                if (offset < 0) {
                        file->off += ret;
                }
                copied += ret;
                start = (node_end < end) ? node_end + 1 : end + 1;

                /* Iterate through the next Poly-index node */
                it_entry = polystore_index_lookup_next(it_entry, start, end);
        }

        pthread_rwlock_unlock(&inode->it_tree_lock);

        /* 
         * If a hole still exist at the end of range [start, end],
         * meaning more data are written to the file, then create 
         * new Poly-index nodes and insert
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
                //new_it_entry->size = 
                //              new_it_entry->it.last - new_it_entry->it.start + 1;
                new_it_entry->size = (end < start + POLY_INDEX_NODE_SIZE) ? 
                                                end + 1 : POLY_INDEX_NODE_SIZE;
                new_it_entry->inode = inode;
                new_it_entry->ref_count = 1;

                new_it_entry->placement = task_ctx->placement;

                /* Need to hold rwlock_wrlock here if kernel inode rwsem is disabled */
                //pthread_rwlock_wrlock(&inode->it_tree_lock); 

                /* Determine the offset in corresponding physical files */
                if (new_it_entry->placement == BLK_PLACEMENT_FAST) {
                        real_pos = lseek(fastfd, 0, SEEK_END);
                } else {
                        real_pos = lseek(slowfd, 0, SEEK_END);
                }

                if ((real_pos & (POLY_INDEX_NODE_SIZE - 1)) == 0) {
                        /* aligned pos */
                        new_it_entry->offset = real_pos;
                } else {
                        /* unaligned pos, do ceiling */
                        new_it_entry->offset = 
                                ((real_pos >> POLY_INDEX_NODE_SIZE_SHIFT) + 1) 
                                        << POLY_INDEX_NODE_SIZE_SHIFT;
                }

                /* Write to corresponding physical files */
                if (new_it_entry->placement == BLK_PLACEMENT_FAST) {
                        ret = pwrite(fastfd, buf + copied, 
                                        it_buf_count, new_it_entry->offset);
                } else {
                        ret = pwrite(slowfd, buf + copied, 
                                        it_buf_count, new_it_entry->offset);
                }

                if (ret != it_buf_count) {
                        ERROR_T("write fail with %ld, expected", 
                                                ret, it_buf_count);
                }

                /* Insert the new interval to the interval tree */
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
        // TODO
        inode->size = (end+1) > inode->size ? end+1 : inode->size;

        /* Update statistics */
        task_ctx->throughput += copied;
        task_ctx->throughput_write += copied;

        return copied;
}
