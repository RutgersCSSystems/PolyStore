/*
 * opemetadata.c
 */

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>

#include "polystore.h"
#include "debug.h"


int polystore_open(const char *pathname, int flags, mode_t mode) {
        DEBUG_T("open | path = %s, flags = %d, mode = %d", pathname, flags, mode);
        //printf("open %s, thread id = %d\n", pathname, tls_task_ctx.task_id);
        
        int ret = 0, fd = 0;
        char fastpath[PATH_MAX];
        char slowpath[PATH_MAX];
        struct poly_inode *inode = NULL;
        struct poly_index *index = NULL;
        struct poly_file *file = NULL;
        struct polyos_open_cmd cmd;

        /* Split a PolyStore pathname to correspondance in FAST and SLOW */        
        util_get_physpaths(pathname, fastpath, slowpath);

        /* Build PolyStore open() command */
        cmd.argsz = sizeof(cmd);
        strcpy(cmd.fastpath, fastpath);
        strcpy(cmd.slowpath, slowpath);
        cmd.path_hash = (int)util_get_path_hash(pathname);
        cmd.mode = mode;
        cmd.flags = flags;
        cmd.inode_addr = 0;
        cmd.index_addr = 0;

        /* Send open() command */
        ret = ioctl(polyos_dev, POLYOS_OPEN_CMD, &cmd);
        fd = cmd.fd;
        if (ret < 0) {
                ERROR_T("Failed to open file %s, %d", pathname, ret);
                goto out;
        }

        /* Get poly-inode, poly-index, and polyfd */
        inode = (struct poly_inode*)cmd.inode_addr;
        if (!inode) {
                ERROR_T("Failed to get inode of %s, %d", pathname, ret);
                goto out;
        }

        index = (struct poly_index*)cmd.index_addr;
        if (!index) {
                ERROR_T("Failed to get index of %s, %d", pathname, ret);
                goto out;
        }
        inode->index = (void*)index;

        /* Create file pointer (poly-file) */
        file = polystore_file_create(fd, inode);
        if (!file) {
                ERROR_T("Failed to create file pointer of %s, %d", 
                                                        pathname, ret);
        }
        file->flags = flags;

        /* Initialize poly-index rwlock */
        while(__sync_lock_test_and_set(&inode->spinlock, 1));
        if (inode->refcount++ == 0) {
                /* Initialize Poly-index lock */
                pthread_rwlock_init(&inode->it_tree_lock, NULL);

#ifdef POLYSTORE_POLYCACHE
                /* Add to Poly-inode hashmap */
                polystore_inode_hashmap_add(inode);

                /* Setup Poly cache policy */
                inode->cache_policy = poly_cache_policy;

                /* Store file path to inode for Poly Cache */
                strcpy(inode->pathname, pathname);

                /* Reset rwfds for Poly cache */
                inode->cache_rwfd_fast = -1;
                inode->cache_rwfd_slow = -1;
                inode->cache_mmapfd = -1;
#endif
        }
        __sync_lock_release(&inode->spinlock);

out:
        //printf("open %s, thread id = %d, fd = %d\n", pathname, tls_task_ctx.task_id, fd);
        return fd;
}

int polystore_creat(const char *pathname, mode_t mode) {
        DEBUG_T("creat | path = %s, mode = %d", pathname, mode);
        return polystore_open(pathname, O_WRONLY | O_CREAT | O_TRUNC, mode);
}

int polystore_close(int fd) {
        DEBUG_T("close | fd = %d", fd);

        int ret = 0;
        struct polyos_close_cmd cmd;
        struct poly_inode *inode = NULL;
        struct poly_file *file = NULL;

        /* Get the file pointer (poly-file) */
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

#ifdef POLYSTORE_POLYCACHE
        /* Drop Poly cache buffers */
        if (inode->refcount == 1 && inode->reclaim == 1) {
                poly_cache_drop(inode);
                polystore_inode_hashmap_delete(inode);
        }
#endif

        /* Delete file pointer (poly-file) */
        ret = polystore_file_delete(fd);
        if (ret) {
                 ERROR_T("Failed to delete file pointer with %d", ret);
        }

        /* Build PolyStore close() command */
        cmd.argsz = sizeof(cmd);
        cmd.fd = fd;
        cmd.path_hash = inode->path_hash;

        /* Send close() command */
        ret = ioctl(polyos_dev, POLYOS_CLOSE_CMD, &cmd);
        if (ret < 0) {
                ERROR_T("Failed to close file of fd %d, ret %d", fd, ret);
        }

out:
        return ret;
}

DIR* polystore_opendir(const char *pathname) {
        DEBUG_T("opendir | path = %s", pathname);

        char fastpath[PATH_MAX];
        char slowpath[PATH_MAX];
        DIR *fastdir = NULL;

        /* Split a PolyStore pathname to correspondance in FAST and SLOW */        
        util_get_physpaths(pathname, fastpath, slowpath);

        /* Open corresponding files in FAST and SLOW */
        if ((fastdir = real_opendir(fastpath)) == NULL) {
                ERROR_T("Failed to opendir in FAST %s", fastpath);
        }

        return fastdir;
}

struct dirent* polystore_readdir(DIR *dirp) {
        DEBUG_T("readdir");
        return real_readdir(dirp);
}

int polystore_closedir(DIR *dirp) {
        DEBUG_T("closedir");
        return real_closedir(dirp);
}

int polystore_mkdir(const char *pathname, mode_t mode) {
        DEBUG_T("mkdir | path = %s, mode = %d", pathname, mode);

        int ret = 0;
        char fastpath[PATH_MAX];
        char slowpath[PATH_MAX];

        /* Split a PolyStore pathname to correspondance in FAST and SLOW */        
        util_get_physpaths(pathname, fastpath, slowpath);

        /* Invoke mkdir() in FAST and SLOW */
        ret = mkdir(fastpath, mode);
        if (ret < 0) {
                goto out;
        }

        ret = mkdir(slowpath, mode);

out:
        return ret;
}

int polystore_rmdir(const char *pathname) {
        DEBUG_T("rmdir | path = %s", pathname);

        int ret = 0;
        char fastpath[PATH_MAX];
        char slowpath[PATH_MAX];

        /* Split a PolyStore pathname to correspondance in FAST and SLOW */        
        util_get_physpaths(pathname, fastpath, slowpath);

        /* Invoke rmdir() in FAST and SLOW */
        ret = rmdir(fastpath);
        if (ret < 0) {
                goto out;
        }

        ret = rmdir(slowpath);

out:
        return ret;
}

int polystore_rename(const char *oldname, const char *newname) {
        DEBUG_T("rename | oldname = %s, newname = %s", oldname, newname);

        int ret = 0;
        char fastoldpath[PATH_MAX];
        char fastnewpath[PATH_MAX];
        char slowoldpath[PATH_MAX];
        char slownewpath[PATH_MAX];
        struct polyos_rename_cmd cmd;

        /* Split a PolyStore pathname to correspondance in FAST and SLOW */        
        util_get_physpaths(oldname, fastoldpath, slowoldpath);
        util_get_physpaths(newname, fastnewpath, slownewpath);

        /* Invoke rename() in FAST and SLOW */
        ret = rename(fastoldpath, fastnewpath);
        if (ret < 0) {
                goto out;
        }

        ret = rename(slowoldpath, slownewpath);


        /* Build PolyStore rename() command */
        cmd.argsz = sizeof(cmd);
        cmd.old_path_hash = (int)util_get_path_hash(oldname);
        cmd.new_path_hash = (int)util_get_path_hash(newname);

        /* Send rename() command */
        ret = ioctl(polyos_dev, POLYOS_RENAME_CMD, &cmd);
        if (ret < 0) {
                ERROR_T("Failed to rename file %s to %s, %d", 
                                                oldname, newname, ret);
        }

#ifdef POLYSTORE_POLYCACHE
        /* Update Poly-inode hashmap */
        polystore_inode_hashmap_rename(oldname, newname);  
#endif

out:
        return ret;
}

int polystore_stat(const char *pathname, struct stat *statbuf) {
        DEBUG_T("stat | pathname = %s", pathname);

        int ret = 0;
        char fastpath[PATH_MAX];
        char slowpath[PATH_MAX];
        struct stat statbuf_fast;
        struct stat statbuf_slow;

        /* Split a PolyStore pathname to correspondance in FAST and SLOW */        
        util_get_physpaths(pathname, fastpath, slowpath);

        /* Invoke stat() in FAST and SLOW */
        ret = stat(fastpath, &statbuf_fast);
        if (ret < 0) {
                goto out;
        }

        ret = stat(slowpath, &statbuf_slow);
        if (ret < 0) {
                goto out;
        }

        /* Copy to the target statbuf */
        if (statbuf_fast.st_size != 0 &&
            statbuf_slow.st_size == 0) {
                memcpy(statbuf, &statbuf_fast, sizeof(struct stat));
        } else if (statbuf_fast.st_size == 0 &&
                   statbuf_slow.st_size != 0) {
                memcpy(statbuf, &statbuf_slow, sizeof(struct stat));
        } else {
                memcpy(statbuf, &statbuf_fast, sizeof(struct stat));
                statbuf->st_size += statbuf_slow.st_size;
        }

out:
        return ret;
}

int polystore_lstat(const char *pathname, struct stat *statbuf) {
        DEBUG_T("lstat | pathname = %s", pathname);
        // Not supported for now
        return 0;
}

int polystore_fstat(int fd, struct stat *statbuf) {
        DEBUG_T("fstat | fd = %d", fd);

        int fastfd = POLYSTORE_FAST_FD(fd);
        int slowfd = POLYSTORE_SLOW_FD(fd);
        ssize_t ret = 0;
        struct stat statbuf_fast;
        struct stat statbuf_slow;
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
                /* Invoke fstat() on fast device */
                ret = fstat(fastfd, statbuf);
        } else if (inode->placement == BLK_PLACEMENT_SLOW) {
                /* Invoke fstat() on slow device */
                ret = fstat(slowfd, statbuf);
        } else {
                /* Invoke stat() in FAST and SLOW */
                ret = fstat(fastfd, &statbuf_fast);
                if (ret < 0) {
                        goto out;
                }

                ret = fstat(slowfd, &statbuf_slow);
                if (ret < 0) {
                        goto out;
                }

                memcpy(statbuf, &statbuf_fast, sizeof(struct stat));
                statbuf->st_size += statbuf_slow.st_size;
        } 

out:
        return ret;
}

int polystore_truncate(const char *pathname, off_t length) {
        DEBUG_T("truncate | pathname = %s, length = %lu", pathname, length);
        
        // FIXME
        int fd = polystore_open(pathname, O_WRONLY | O_TRUNC, 0666); 
        polystore_ftruncate(fd, length);
        close(fd);

        return 0;
}

int polystore_unlink(const char *pathname) {
        DEBUG_T("unlink | path = %s", pathname);

        int ret = 0;
        char fastpath[PATH_MAX];
        char slowpath[PATH_MAX];
        struct polyos_unlink_cmd cmd;
        struct poly_inode *inode = NULL;

        /* Split a PolyStore pathname to correspondance in FAST and SLOW */        
        util_get_physpaths(pathname, fastpath, slowpath);

#ifdef POLYSTORE_POLYCACHE
        /* Find poly-inode */
        inode = polystore_inode_hashmap_find((int)util_get_path_hash(pathname));
        if (inode && inode->refcount == 0) {
                /* Drop Poly cache buffers */
                poly_cache_drop(inode);
                polystore_inode_hashmap_delete(inode);
        }
#endif

        /* Invoke unlink() in FAST and SLOW */
        ret = unlink(fastpath);
        if (ret < 0) {
                ERROR_T("Failed to unlink %s", fastpath);
        }

        ret = unlink(slowpath);
        if (ret < 0) {
                ERROR_T("Failed to unlink %s", slowpath);
        }

        /* Build PolyStore unlink() command */
        cmd.argsz = sizeof(cmd);
        cmd.path_hash = (int)util_get_path_hash(pathname);

        /* Send unlink() command */
        ret = ioctl(polyos_dev, POLYOS_UNLINK_CMD, &cmd);
        if (ret < 0) {
                ERROR_T("Failed to unlink file %s, %d", pathname, ret);
                goto out;
        }

out:
        return ret;
}

int polystore_symlink(const char *target, const char *linkpath) {
        DEBUG_T("symlink | target = %s, linkpath = %s", target, linkpath);
        // Not supported for now
        return 0;
}

int polystore_access(const char *pathname, int mode) {
        DEBUG_T("access | pathname = %s, mode = %d", pathname, mode);

        int ret = 0;
        char fastpath[PATH_MAX];
        char slowpath[PATH_MAX];

        /* Split a PolyStore pathname to correspondance in FAST and SLOW */        
        util_get_physpaths(pathname, fastpath, slowpath);

        return access(fastpath, mode);
}

int polystore_fcntl(int fd, int cmd, void *arg) {
        DEBUG_T("fcntl | fd = %d, cmd = %d", fd, cmd);

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
                /* Invoke fcntl() on fast device */
                ret = fcntl(fastfd, cmd, arg);
        } else if (inode->placement == BLK_PLACEMENT_SLOW) {
                /* Invoke fcntl() on slow device */
                ret = fcntl(slowfd, cmd, arg);
        } else  {
                // TODO
        } 

out:
        return ret;

}
