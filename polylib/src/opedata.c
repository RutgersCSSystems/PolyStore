/*
 * opedata.c
 */

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>

#include "polystore.h"
#include "debug.h"


/* Generic read function */
static ssize_t polystore_read_generic(int polyfd, void *buf, 
                                      size_t count, off_t offset) {
        int fastfd = POLYSTORE_FAST_FD(polyfd);
        int slowfd = POLYSTORE_SLOW_FD(polyfd);
        ssize_t ret = 0;
        struct poly_inode *inode = NULL;
        struct poly_file *file = NULL;

        /* Get Poly-file from poly-fd */
        file = polystore_file_find(polyfd); 
        if (!file) {
                ERROR_T("Failed to find Poly-file of fd %d", polyfd);
                ret = -ENOENT;
                goto out;
        }

        /* Get Poly-inode from Poly-file */
        inode = file->inode;
        if (!inode) {
                ERROR_T("Failed to find Poly-file of fd %d", polyfd);
                ret = -ENOENT;
                goto out;
        }

        //while (__sync_lock_test_and_set(&inode->rw_lock, 1));

        if (inode->placement == BLK_PLACEMENT_FAST) {
                /* Do POSIX read on fast device */
                ret = (offset == -1) ? 
                                read(fastfd, buf, count) : 
                                pread(fastfd, buf, count, offset);
        } else if (inode->placement == BLK_PLACEMENT_SLOW) {
                /* Do POSIX read on slow device */
                ret = (offset == -1) ? 
                                read(slowfd, buf, count) : 
                                pread(slowfd, buf, count, offset);
        } else if (inode->placement == BLK_PLACEMENT_BOTH) {
                // Iterate through the Poly-index and perform read accordingly
#ifndef POLYSTORE_POLYCACHE
                ret = polystore_read_iter(file, buf, count, offset);
#else
                ret = polystore_read_iter_polycache(file, buf, count, offset);
#endif
        } else {
                ERROR_T("Poly inode placement unknown for %d, placement %d\n", 
                                inode->ino, inode->placement);
                ret = 0;
        }

        //__sync_lock_release(&inode->rw_lock);
out:
        return ret;
}

/* Generic write function */
static ssize_t polystore_write_generic(int polyfd, const void *buf, 
                                      size_t count, off_t offset) {
        int fastfd = POLYSTORE_FAST_FD(polyfd);
        int slowfd = POLYSTORE_SLOW_FD(polyfd);
        ssize_t ret = 0;
        struct poly_inode *inode = NULL;
        struct poly_file *file = NULL;

        /* Get Poly-file from poly-fd */
        file = polystore_file_find(polyfd); 
        if (!file) {
                ERROR_T("Failed to find Poly-file of fd %d", polyfd);
                ret = -ENOENT;
                goto out;
        }

        /* Get Poly-inode from Poly-file */
        inode = file->inode;
        if (!inode) {
                ERROR_T("Failed to find Poly-file of fd %d", polyfd);
                ret = -ENOENT;
                goto out;
        }

        //while (__sync_lock_test_and_set(&inode->spinlock, 1));

        if (inode->placement == BLK_PLACEMENT_TBD) {
                inode->placement = BLK_PLACEMENT_BOTH;
                if (file->flags & O_APPEND) {
#ifndef POLYSTORE_POLYCACHE
                        ret = polystore_append_iter(file, buf, count, offset);
#else
                        ret = polystore_append_iter_polycache(file, buf, count, offset);
#endif
                } else {
#ifndef POLYSTORE_POLYCACHE
                        ret = polystore_write_iter(file, buf, count, offset);
#else
                        ret = polystore_write_iter_polycache(file, buf, count, offset);
#endif
                }
        } else if (inode->placement == BLK_PLACEMENT_FAST) {
                /* Do POSIX write on fast device */
                ret = (offset == -1) ? 
                                write(fastfd, buf, count) : 
                                pwrite(fastfd, buf, count, offset);
        } else if (inode->placement == BLK_PLACEMENT_SLOW) {
                /* Do POSIX write on slow device */
                ret = (offset == -1) ? 
                                write(slowfd, buf, count) : 
                                pwrite(slowfd, buf, count, offset);
        } else if (inode->placement == BLK_PLACEMENT_BOTH) {
                // Iterate through the Poly-index and perform write accordingly
                if (file->flags & O_APPEND) {
#ifndef POLYSTORE_POLYCACHE
                        ret = polystore_append_iter(file, buf, count, offset);
#else
                        ret = polystore_append_iter_polycache(file, buf, count, offset);
#endif
                } else {
#ifndef POLYSTORE_POLYCACHE
                        ret = polystore_write_iter(file, buf, count, offset);
#else
                        ret = polystore_write_iter_polycache(file, buf, count, offset);
#endif
                }

        } else {
                ERROR_T("Poly inode corrupted\n");
                ret = -EINVAL;
        }

        //__sync_lock_release(&inode->spinlock);
out:
        return ret;
}

int polystore_ftruncate_iter(int fd, struct poly_file *file, off_t length) {

        int fastfd = POLYSTORE_FAST_FD(fd);
        int slowfd = POLYSTORE_SLOW_FD(fd);
        int ret = 0;
        ssize_t wret = 0;
        size_t count = 0, remain = 0;
        off_t offset = 0;
        struct poly_inode *inode = file->inode;
        struct it_node_entry *it_entry = NULL;
        char buf[4096];

        /* Check the truncate direction */
        if (length > inode->size) {
                memset(buf, 4096, '0');
                remain = length - inode->size;
                while (remain > 0) {
                        offset = inode->size;
                        count = remain > 4096 ? 4096 : remain;
                        wret = polystore_write_iter(file, buf, count, offset);               
                        if (wret != count) {
                                ERROR_T("PolyStore truncate iter failed\n");
                                ret = -EDQUOT;
                        }
                        offset += wret;
                        remain -= count;
                }
                printf("truncate enlarge ino %d, inode->placement %d, current %ld, target %ld\n", inode->ino, inode->placement, inode->size, length);
        } else if (length < inode->size) {
                // TODO This is a temporary fix for RocksDB now
                //printf("truncate, before %ld, new %ld\n", inode->size, length);
                it_entry = polystore_index_lookup(inode, length, inode->size - 1);
                if (it_entry) {
                        if (it_entry->placement == BLK_PLACEMENT_FAST) {
                                ftruncate(fastfd, length);
                        } else {
                                ftruncate(slowfd, length);
                        }
                        it_entry->size -= (inode->size - length);
                }     
                printf("truncate shrink ino %d, inode->placement %d, current %ld, target %ld\n", inode->ino, inode->placement, inode->size, length);
        }

        return ret;
}


/* PolyStore data-plane operations */
int polystore_fallocate(int fd, int mode, off_t offset, off_t len) {
        DEBUG_T("fallocate | fd = %d, mode = %d, offset = %lu, len = %lu", 
                        fd, mode, offset, len);

        int fastfd = POLYSTORE_FAST_FD(fd);
        int slowfd = POLYSTORE_SLOW_FD(fd);
        ssize_t ret = 0;
        struct poly_inode *inode = NULL;
        struct poly_file *file = NULL;

        /* Get Poly-file from poly-fd */
        file = polystore_file_find(fd); 
        if (!file) {
                ERROR_T("Failed to find Poly-file of fd %d", fd);
                ret = -ENOENT;
                goto out;
        }

        /* Get Poly-inode from Poly-file */
        inode = file->inode;
        if (!inode) {
                ERROR_T("Failed to find Poly-file of fd %d", fd);
                ret = -ENOENT;
                goto out;
        }

        if (inode->placement == BLK_PLACEMENT_FAST) {
                /* Invoke fallocte() on fast device */
                ret = fallocate(fastfd, mode, offset, len);
        } else if (inode->placement == BLK_PLACEMENT_SLOW) {
                /* Invoke fallocte() on slow device */
                ret = fallocate(slowfd, mode, offset, len);
        } else  {
                /* 
                 * fallocate() does not have any semantic meaning in 
                 * PolyStore because the placement is determined 
                 * dynamically
                 */
                ret = 0;
        } 

out:
        return ret;
}

int polystore_ftruncate(int fd, off_t length) {
        DEBUG_T("ftruncate | fd = %d, length = %lu", fd, length);

        int fastfd = POLYSTORE_FAST_FD(fd);
        int slowfd = POLYSTORE_SLOW_FD(fd);
        int ret = 0;
        struct poly_inode *inode = NULL;
        struct poly_file *file = NULL;

        /* Get Poly-file from poly-fd */
        file = polystore_file_find(fd); 
        if (!file) {
                ERROR_T("Failed to find Poly-file of fd %d", fd);
                ret = -ENOENT;
                goto out;
        }

        /* Get Poly-inode from Poly-file */
        inode = file->inode;
        if (!inode) {
                ERROR_T("Failed to find Poly-file of fd %d", fd);
                ret = -ENOENT;
                goto out;
        }

        if (inode->placement == BLK_PLACEMENT_FAST) {
                /* Invoke ftruncate() on fast device */
                ret = ftruncate(fastfd, length);
        } else if (inode->placement == BLK_PLACEMENT_SLOW) {
                /* Invoke ftruncate() on slow device */
                ret = ftruncate(slowfd, length);
        } else  {
                /* Invoke polystore_ftruncate_iter() */
                ret = polystore_ftruncate_iter(fd, file, length);
        } 

out:
        return ret;
}

off_t polystore_lseek(int fd, off_t offset, int whence) {
        DEBUG_T("lseek | fd = %d, offset = %lu, whence = %d", 
                                                        fd, offset, whence);

        off_t ret = -1;
        struct poly_file *file = NULL;
        struct poly_inode *inode = NULL;

        /* Get file pointer (poly-file) */
        file = polystore_file_find(fd);
        if (!file) {
                ERROR_T("Failed to get file poiner of fd %d, ret %d", fd, ret);
                return ret;
        }

        /* Get poly-inode */
        inode = file->inode;
        if (!inode) {
                ERROR_T("Failed to get inode of fd %d", fd);
                return ret;
        }
       
        /* Set offset to poly-file */
        switch (whence) {
                default:
                case SEEK_SET:
                        file->off = offset;
                        ret = file->off;
                        break;

                case SEEK_CUR:
                        file->off += offset;
                        ret = file->off;
                        break;

                case SEEK_END:
                        file->off = inode->size + offset;
                        ret = file->off;
                        break;
        }

        return ret;
}

ssize_t polystore_read(int fd, void *buf, size_t count) {
        DEBUG_T("read | fd = %d, count = %ld", fd, count);

        return polystore_read_generic(fd, buf, count, -1);
}

ssize_t polystore_pread(int fd, void *buf, size_t count, off_t offset) {
        DEBUG_T("pread | fd = %d, count = %ld, offset = %lu", fd, count, offset);
        //printf("pread | fd = %d, count = %ld, offset = %lu\n", fd, count, offset);

        return polystore_read_generic(fd, buf, count, offset);
}


ssize_t polystore_write(int fd, const void *buf, size_t count) {
        DEBUG_T("write | fd = %d, count = %ld", fd, count);

        return polystore_write_generic(fd, buf, count, -1);
}

ssize_t polystore_pwrite(int fd, const void *buf, size_t count, off_t offset) {
        DEBUG_T("pwrite | fd = %d, count = %ld, offset = %lu", fd, count, offset);
        //printf("pwrite | fd = %d, count = %ld, offset = %lu\n", fd, count, offset);

        return polystore_write_generic(fd, buf, count, offset);
}

int polystore_fsync(int fd) {
        DEBUG_T("fsync | fd = %d", fd);
        return 0;
}

int polystore_fdatasync(int fd) {
        DEBUG_T("fdatasync | fd = %d", fd);
        return 0;
}

