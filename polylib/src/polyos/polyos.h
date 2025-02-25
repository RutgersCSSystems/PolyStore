#ifndef POLYOS_H
#define POLYOS_H

#include <linux/hashtable.h>
#include <linux/interval_tree.h>
#include <linux/list.h>
#include <linux/types.h>

#include "../polystore_def.h"

/* Poly-index (Range tree) node entry (In-memory and kernel space) */
struct it_node_entry {
        int idx_no;                     /* node index number on disk */
        int placement;                  /* Placement */
        size_t size;                    /* Node size */
        loff_t offset;                  /* Offset in underlying real file */
        int ref_count;                  /* Reference 2-bit status counter */
        struct interval_tree_node it;   /* Interval tree node */
};

/* Poly-inode (In-memory and kernel space) */
struct poly_inode {
        /* First cache line */
        int path_hash;                  /* inode canonical path hash value */
        int ino;                        /* inode number */
        mode_t type;                    /* inode type */
        int placement;                  /* Data block placement */
        size_t size;                    /* inode size */
        struct list_head lru;           /* LRU list */
        int refcount;                   /* Reference count */
        int reclaim;                    /* Reclaim flag */
        void *index;                    /* kernel address for poly_index */
        void *data;                     /* Reserved for now */

        /* Second cache line */
        struct rb_root it_tree;         /* interval tree for Poly-index */
        int next_avail_it_node_no;      /* Next available iterval tree node number */
        int it_tree_fd;                 /* file descriptor for interval tree file */
        struct d_it_node_entry 
                        *it_tree_addr;  /* On-disk poly-index memory addr */
        struct file *index_filp;        /* internal file pointer for the index file */
        unsigned long it_root_idx;      /* Root node num in the interval tree */
        int spinlock;                   /* inode meta-data lock */
};

/* Poly-index (Range tree) (In-memory and kernel space) */
struct poly_index {
        char mem[1];                    /* dummy for now */
};

extern struct g_config_var *g_config_var;
extern struct g_control_var *g_control_var;

extern uint32_t next_avail_inode_no;
extern uint32_t next_avail_task_id;

/* mm.c */
void mm_write_lock(struct mm_struct *mm);
void mm_write_unlock(struct mm_struct *mm);
int check_vaddr_mapped(unsigned long addr);

/* inode.c */
struct poly_index* alloc_poly_index(void);
struct poly_inode* alloc_poly_inode(uint32_t path_hash, mode_t type);
void free_poly_inode(struct poly_inode *inode);
void free_poly_index(struct poly_inode *inode);
int i_hashtable_insert(uint32_t path_hash, struct poly_inode *inode);
struct poly_inode* i_hashtable_search(uint32_t path_hash);
void i_hashtable_delete(uint32_t path_hash);
struct poly_inode* i_hashtable_search_and_insert(uint32_t path_hash,
						 mode_t type);
int i_hashtable_search_and_rename(uint32_t old_path_hash, 
                                  uint32_t new_path_hash);
void clean_up_poly_inode_index(void);

/* taskctx.c */
struct g_task_ctx* task_ctx_alloc(void);
void task_ctx_free(struct g_task_ctx *task_ctx);
int task_ctx_hashtable_insert(uint32_t task_id, struct g_task_ctx *task_ctx);
struct g_task_ctx* task_ctx_hashtable_search(uint32_t task_id);
void task_ctx_hashtable_delete(uint32_t task_id);

/* operation.c */
long polyos_client_init(unsigned long arg);
long polyos_client_exit(unsigned long arg);
long polyos_task_ctx_reg(unsigned long arg);
long polyos_task_ctx_delete(unsigned long arg);
long polyos_open(unsigned long arg);
long polyos_close(unsigned long arg);
long polyos_filerw(unsigned long arg);
long polyos_rename(unsigned long arg);
long polyos_unlink(unsigned long arg);


/* Utilities */

#define POLYOS_INFO(fmt, ...) \
        printk(KERN_INFO "PolyOS: " fmt "\n", ##__VA_ARGS__)

#define POLYOS_DEBUG(fmt, ...) \
        printk(KERN_INFO "PolyOS: " fmt " [%s:%d] \n", ##__VA_ARGS__, __FUNCTION__, __LINE__)

#define POLYOS_WARN(fmt, ...) \
        printk(KERN_WARN "PolyOS: " fmt " [%s:%d] \n", ##__VA_ARGS__, __FUNCTION__, __LINE__)

#define POLYOS_ERROR(fmt, ...) \
        printk(KERN_ERR "PolyOS: " fmt " [%s:%d] \n", ##__VA_ARGS__, __FUNCTION__, __LINE__)


#endif
