#ifndef POLYSTORE_H
#define POLYSTORE_H

#include <dirent.h>
#include <pthread.h>
#include <stdint.h>
#include <linux/limits.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "list.h"
#include "interval_tree.h"
#include "polystore_def.h"
#include "polyos/polyos_iocmd.h"

#define PAGE_SIZE_SHIFT 12
#define PAGE_SIZE       4096

/* Forward declaration of glibc structs */
struct stat64;

/* Poly-index (Range tree) node entry (In-memory and user space) */
// TODO
struct it_node_entry {
        int idx_no;                     /* node index number on disk */
        int placement;                  /* Placement */
        size_t size;                    /* Node size */
        loff_t offset;                  /* Offset in underlying real file */
        int ref_count;                  /* Reference 2-bit status counter */
        struct interval_tree_node it;   /* Interval tree node */

        /* User space fields defined below */
        struct poly_inode *inode;       /* Back pointer to h_inode struct */
        pthread_mutex_t lock;           /* interval mutex lock */

        void *buf_addr;                 /* Poly cache buffer */
        size_t cache_size;              /* Poly cache size */
        int dirty;                      /* Poly cache dirty flag */
        int present;                    /* Poly cache present flag */
        int removed;                    /* Poly cache delete flag */
        struct list_head cache_lru;     /* Poly cache LRU list node */
        //bitmap_t pg_dirty_bitmap[POLY_INDEX_PG_NUM];
        //bitmap_t pg_present_bitmap[POLY_INDEX_PG_NUM];
};

/* Poly-inode (In-memory and user space) */
// TODO
struct poly_inode {
        /* First cache line (64 bytes) */
        int path_hash;                  /* inode canonical path hash value */
        int ino;                        /* inode number */
        mode_t type;                    /* inode type */
        int placement;                  /* Data block placement */
        size_t size;                    /* inode size */
        struct list_head lru;           /* LRU list */
        int refcount;                   /* Reference count */
        int reclaim;                    /* Reclaim flag */
        void *dummy;                    /* Dummy pointer */
        void *data;                     /* Reserved for now */

        /* Second cache line (64 bytes) */
        struct rb_root it_tree;         /* interval tree for Poly-index */
        int next_avail_it_node_no;      /* Next available iterval tree node number */
        int it_tree_fd;                 /* file descriptor for interval tree file */
        void *dummy2;                   /* Second dummy pointer */
        void *dummy3;                   /* Third dummy pointer */
        unsigned long it_root_idx;      /* Root node num in the interval tree */
        int spinlock;                   /* inode meta-data lock */

        /* User space fields defined below */
        pthread_rwlock_t it_tree_lock;  /* Interval tree rwlock */
        int cache_policy;               /* Policy for Poly cache */
        int cache_rwfd_fast;            /* File descriptor for Poly cache (fast) */
        int cache_rwfd_slow;            /* File descriptor for Poly cache (slow) */
        int cache_mmapfd;               /* File descriptor for mmap data on PMEM */
        loff_t cache_fast_offset;       /* Offset in the physical file (fast) */
        loff_t cache_slow_offset;       /* Offset in the physical file (slow) */
        char pathname[256];             /* Pathname of the file (for cache_mmapfd) */
        void *index;                    /* User space pointer to Poly-index memory */
};


/* Poly-file (In-memory PolyStore file pointer in user space) */
struct poly_file {
        int polyfd;                     /* Poly fd */
        int flags;                      /* File open flags */
        loff_t off;                     /* File offset */       
        struct poly_inode *inode;       /* Pointer to Poly-inode */ 
        struct poly_inode *index;       /* Pointer to Poly-index */ 
};


/* Task descriptor (user space) */
struct l_task_ctx {
        struct g_task_ctx *task_ctx;
        int task_id;
        int cpu_id;
        struct list_head list;
        FILE *dump_file;
};

/* PolyStore Lib global variables below */
extern struct g_config_var *g_config_var;
extern struct g_control_var *g_control_var;
extern int polyos_dev;
extern const char* FAST_DIR;
extern const char* SLOW_DIR;
/* PolyStore task context below */
extern __thread struct l_task_ctx tls_task_ctx;
extern int task_ctx_active_cnt;
extern struct list_head task_ctx_list;
extern pthread_rwlock_t task_ctx_list_lock;
/* PolyStore placement below */
extern unsigned int sched_split_point;
extern unsigned int sched_epoch_tic;
extern unsigned int sched_fastdev_usage;
extern unsigned long sched_aggre_throughput;
/* PolyStore profiling below */
extern unsigned long profile_fast_placed;
extern unsigned long profile_slow_placed;
extern char profile_g_stat_dump_fname[PATH_MAX];
extern FILE *profile_g_stat_dump_file;
/* Poly cache below */
extern unsigned long poly_cache_size;
extern unsigned long poly_cache_flushing_begin;
extern unsigned long poly_cache_flushing_end;
extern cache_policy_t poly_cache_policy;
extern int poly_cache_flushing_indicator;
extern int poly_cache_flushing_thread_nr;
extern struct list_head *poly_cache_flushing_queue;
extern pthread_mutex_t *poly_cache_flushing_queue_mutex;
extern unsigned long cache_access_cnt;
extern unsigned long cache_hit_cnt;
extern unsigned long cache_miss_cnt;
extern struct list_head interval_hot_list;
extern struct list_head interval_cold_list;
extern pthread_spinlock_t lru_hot_list_lock;
extern pthread_spinlock_t lru_cold_list_lock;            


/* polystore.c */
void polystore_init(void) __attribute__((constructor));
void polystore_exit(void) __attribute__((destructor));

/* shim.c */
DIR* real_opendir(const char *pathname);
struct dirent* real_readdir(DIR *dirp);
int real_closedir(DIR *dirp);

/* opemetadata.c */
int polystore_open(const char *pathname, int flags, mode_t mode);
int polystore_creat(const char *pathname, mode_t mode);
int polystore_close(int fd);
off_t polystore_lseek(int fd, off_t offset, int whence);
DIR* polystore_opendir(const char *pathname);
struct dirent* polystore_readdir(DIR *dirp);
int polystore_closedir(DIR *dirp);
int polystore_mkdir(const char *pathname, mode_t mode);
int polystore_rmdir(const char *pathname);
int polystore_rename(const char *oldname, const char *newname);
int polystore_fallocate(int fd, int mode, off_t offset, off_t len);
int polystore_stat(const char *pathname, struct stat *statbuf);
int polystore_lstat(const char *pathname, struct stat *statbuf);
int polystore_fstat(int fd, struct stat *statbuf);
int polystore_truncate(const char *pathname, off_t length);
int polystore_ftruncate(int fd, off_t length);
int polystore_unlink(const char *pathname);
int polystore_symlink(const char *target, const char *linkpath);
int polystore_access(const char *pathname, int mode);
int polystore_fcntl(int fd, int cmd, void *arg);

/* opedata.c */
ssize_t polystore_read(int fd, void *buf, size_t count);
ssize_t polystore_pread(int fd, void *buf, size_t count, off_t offset);
ssize_t polystore_write(int fd, const void *buf, size_t count);
ssize_t polystore_pwrite(int fd, const void *buf, size_t count, off_t offset);
int polystore_fsync(int fd);
int polystore_fdatasync(int fd);

/* io_direct.c */
ssize_t polystore_read_iter(struct poly_file *file, void *buf, 
                                   size_t count, off_t offset);
ssize_t polystore_append_iter(struct poly_file *file, const void *buf, 
                                    size_t count, off_t offset);
ssize_t polystore_write_iter(struct poly_file *file, const void *buf, 
                                    size_t count, off_t offset);

/* io_buffered.c */
ssize_t polystore_read_iter_polycache(struct poly_file *file, void *buf, 
                                   size_t count, off_t offset);
ssize_t polystore_append_iter_polycache(struct poly_file *file, const void *buf, 
                                    size_t count, off_t offset);
ssize_t polystore_write_iter_polycache(struct poly_file *file, const void *buf, 
                                    size_t count, off_t offset);

/* cache.c */
void poly_cache_initialize(void);
void poly_cache_exit(void);
void poly_cache_lru_list_add(struct list_head *node,
                                    struct list_head *target);
void poly_cache_lru_list_del(struct list_head *node);
void poly_cache_lru_list_move(struct list_head *node,
                                     struct list_head *target);
void poly_cache_add_lru_cold_list(struct it_node_entry *it_entry);
int poly_cache_lru_list_check_linked(struct list_head *node);

int poly_cache_admit(struct poly_file *file, struct it_node_entry *it_entry,
                                        off_t start, off_t end, int rw);
int poly_cache_evict(struct poly_inode *inode, struct it_node_entry *it_entry,
                                        off_t start, off_t end);
void poly_cache_drop(struct poly_inode *inode);
size_t poly_cache_flushing_dispatch();

/* cache_mm.c */
void poly_cache_mm_init(void);
void poly_cache_mm_exit(void);
void* poly_cache_mm_alloc(void);
void poly_cache_mm_free(void *buf);

/* context.c */
int polystore_task_ctx_register(int type);
int polystore_task_ctx_delete(void);
struct poly_file* polystore_file_create(int polyfd, struct poly_inode *inode);
struct poly_file* polystore_file_find(int polyfd);
int polystore_file_delete(int polyfd);
int polystore_inode_hashmap_add(struct poly_inode *inode);
int polystore_inode_hashmap_delete(struct poly_inode *inode);
int polystore_inode_hashmap_rename(const char *oldname, const char *newname);
typedef void (*poly_inode_hashmap_iter_op)(struct poly_inode *inode);
int polystore_inode_hashmap_iterate(poly_inode_hashmap_iter_op func);
struct poly_inode* polystore_inode_hashmap_find(int path_hash);


/* indexing.c */
struct it_node_entry* polystore_index_entry_allocate(struct poly_inode *inode);
void polystore_index_insert(struct poly_inode *inode, 
                            struct it_node_entry *entry);
struct it_node_entry* polystore_index_lookup(struct poly_inode *inode,
                                             unsigned long start, 

                                             unsigned long last);
struct it_node_entry* polystore_index_lookup_next(struct it_node_entry *entry,
                                             unsigned long start, 
                                             unsigned long last);
void polystore_index_remove(struct poly_inode *inode, 
                            struct it_node_entry *entry);
int polystore_index_load(struct poly_inode *inode, 
                         unsigned long start, unsigned long last);
int polystore_index_sync(struct poly_inode *inode, 
                         unsigned long start, unsigned long last);

/* placement.c */
void polystore_placement_static_statonly(void);
void polystore_placement_dynamic(void);

/* util.c */
void util_flatten_path(char *path);
void util_get_fullpath(const char *path, char *fullpath);
void util_get_fastpath(const char *path, char *fastpath);
void util_get_slowpath(const char *path, char *slowpath);
void util_get_physpaths(const char *path, char *fastpath, char *slowpath);
uint32_t util_get_path_hash(const char *path);
void set_bitmap(bitmap_t *b, int i);
void unset_bitmap(bitmap_t *b, int i);
uint8_t get_bitmap(bitmap_t *b, int i);
unsigned long capacity_stoul(char* origin);


#endif
