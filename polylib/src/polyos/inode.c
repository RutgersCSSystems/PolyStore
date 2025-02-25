/*
 * inode.c
 */

#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>

#include "polyos.h"

#define POLY_INODE_HASHTABLE_ORDER 14

uint32_t next_avail_inode_no = 0;

DEFINE_RWLOCK(i_hashtable_lock);
DEFINE_HASHTABLE(i_hashtable, POLY_INODE_HASHTABLE_ORDER);

/* Poly-inode hash table entry */
struct i_hash_node {
        uint32_t path_hash;
        struct poly_inode *inode;
        struct hlist_node hash_node;
};     

/*
 * Allocate a new in-memory Poly-index memory region
 */
struct poly_index* alloc_poly_index(void) {
        struct poly_index *index = NULL;
        index = vmalloc(POLY_INDEX_MAX_MEM_SIZE);
        if (!index) { 
                POLYOS_ERROR("Failed to allocate Poly-index!");
                goto out;
        } 
        memset((void*)index, 0, POLY_INDEX_MAX_MEM_SIZE);
out:
        return index;
}

/*
 * Allocate a new in-memory Poly-inode (align to page size)
 */
struct poly_inode* alloc_poly_inode(uint32_t path_hash, mode_t type) {
        int ino = 0;
        struct poly_inode *inode = NULL; 

        /* Get the next available ino */
        ino = __sync_fetch_and_add(&next_avail_inode_no, 1);
        if (ino > MAX_POLY_INODE_NR) {
                POLYOS_ERROR("Exceeding max available Poly-inode!");
                goto out;
        }

        /* Allocate the in-memory Poly-inode struct */
        inode = vmalloc(PAGE_SIZE);
        if (!inode) { 
                POLYOS_ERROR("Failed to allocate Poly-inode!");
                goto out;
        } 
        memset((void*)inode, 0, PAGE_SIZE);

        /* Initialize in-memory h_inode struct */
        inode->path_hash = path_hash;
        inode->ino = ino;
        inode->type = type;
        inode->placement = BLK_PLACEMENT_TBD;
        inode->size = 0UL;
        inode->refcount = 0;
        inode->reclaim = 0;

        inode->it_root_idx = 0;
        inode->it_tree = RB_ROOT;
        inode->it_tree_fd = -1;
        inode->next_avail_it_node_no = 1;

        inode->spinlock = 0;

out:
        return inode;
}

/*
 * free a new in-memory Poly-index memory region
 */
void free_poly_index(struct poly_inode *inode) {
        unsigned long index_addr = 0;
        unsigned long pace = 0;
        struct vm_area_struct *vma = NULL;

        if (!inode || !inode->index) {
                POLYOS_ERROR("Poly-inode or Poly-index NULL!");
                return;
        }

        index_addr = GET_POLY_INDEX_VADDR(inode->ino);

        vma = find_vma(current->mm, (unsigned long)index_addr);
        if (!vma || vma->vm_start != POLY_INDEX_VADDR_BASE ||
                    vma->vm_end != POLY_INDEX_VADDR_BASE +
                                   POLY_INDEX_VADDR_RANGE) {
                POLYOS_ERROR("Find VMA failed!");
                return;
        }

        mm_write_lock(current->mm);
        while (pace < POLY_INDEX_MAX_MEM_SIZE) {
                zap_vma_ptes(vma, index_addr + pace, PAGE_SIZE);
                pace += PAGE_SIZE;
        }
        mm_write_unlock(current->mm);

        vfree(inode->index);
}

/*
 * free a new in-memory Poly-inode memory region
 */
void free_poly_inode(struct poly_inode *inode) {
        unsigned long inode_addr = 0;
        struct vm_area_struct *vma = NULL;

        if (!inode) {
                POLYOS_ERROR("Poly-inode or Poly-index NULL!");
                return;
        }

        inode_addr = GET_POLY_INODE_VADDR(inode->ino);

        vma = find_vma(current->mm, (unsigned long)inode_addr);
        if (!vma || vma->vm_start != POLY_INODE_VADDR_BASE ||
                    vma->vm_end != POLY_INODE_VADDR_BASE +
                                   POLY_INODE_VADDR_RANGE) {
                POLYOS_ERROR("Find VMA failed!");
                return;
        }

        mm_write_lock(current->mm);
        zap_vma_ptes(vma, inode_addr, PAGE_SIZE);
        mm_write_unlock(current->mm);

        vfree(inode);
}


/*
 * Poly-inode hashtable utility
 */
static inline struct i_hash_node* i_hash_node_create(uint32_t path_hash, 
                                             struct poly_inode *inode) {
        struct i_hash_node *new_node = 
                        kmalloc(sizeof(struct i_hash_node), GFP_KERNEL);
        if (new_node) {
                new_node->path_hash = path_hash;
                new_node->inode = inode;
                INIT_HLIST_NODE(&new_node->hash_node);
        }
        return new_node;
}

static inline void i_hash_node_free(struct i_hash_node *node) {
        if (node){
                kfree(node);
        }
}

int i_hashtable_insert(uint32_t path_hash, struct poly_inode *inode) {
        struct i_hash_node *new_node = i_hash_node_create(path_hash, inode);
        if (!new_node){
                POLYOS_ERROR("Failed to allocate memory for new node");
                return -ENOMEM;
        }
        write_lock(&i_hashtable_lock);
        hash_add(i_hashtable, &new_node->hash_node, path_hash);
        write_unlock(&i_hashtable_lock);
        return 0;
}

struct poly_inode* i_hashtable_search(uint32_t path_hash) {
        struct poly_inode *inode = NULL;
        struct i_hash_node *entry = NULL;
        read_lock(&i_hashtable_lock);
        hash_for_each_possible(i_hashtable, entry, hash_node, path_hash) {
                if (entry->path_hash == path_hash) {
                        inode = entry->inode;
                        break;
                }
        }
        read_unlock(&i_hashtable_lock);
        return inode;
}

void i_hashtable_delete(uint32_t path_hash) {
        struct i_hash_node *entry = NULL;
        write_lock(&i_hashtable_lock);
        hash_for_each_possible(i_hashtable, entry, hash_node, path_hash) {
                if (entry->path_hash == path_hash){
                        break;
                }
        }
        if (entry) {
                hash_del(&entry->hash_node);
                i_hash_node_free(entry);
        }
        write_unlock(&i_hashtable_lock);
}

#if 0
struct poly_inode* i_hashtable_search_and_insert(uint32_t path_hash, mode_t type) {
        struct poly_inode *inode = NULL;
        struct poly_index *index = NULL;
        struct i_hash_node *entry = NULL;

        write_lock(&i_hashtable_lock);
        hash_for_each_possible(i_hashtable, entry, hash_node, path_hash) {
                if (entry->path_hash == path_hash) {
                        inode = entry->inode;
                        goto out;
                }
        }

        index = alloc_poly_index();
        if (!index){
                POLYOS_ERROR("Failed to allocate memory for new index");
                inode = ERR_PTR(-ENOMEM);
                goto out;
        }

        inode = alloc_poly_inode(path_hash, type);
        if (!inode){
                POLYOS_ERROR("Failed to allocate memory for new inode");
                inode = ERR_PTR(-ENOMEM);
                goto out;
        }
        inode->index = (void*)index;

        entry = i_hash_node_create(path_hash, inode);
        hash_add(i_hashtable, &entry->hash_node, path_hash);
out:
        write_unlock(&i_hashtable_lock);
        return inode;
}
#endif

struct poly_inode* i_hashtable_search_and_insert(uint32_t path_hash, mode_t type) {
        struct poly_inode *inode = NULL;
        struct poly_index *index = NULL;
        struct i_hash_node *entry = NULL;

        index = alloc_poly_index();
        if (!index){
                POLYOS_ERROR("Failed to allocate memory for new index");
                inode = ERR_PTR(-ENOMEM);
                goto out;
        }

        inode = alloc_poly_inode(path_hash, type);
        if (!inode){
                POLYOS_ERROR("Failed to allocate memory for new inode");
                inode = ERR_PTR(-ENOMEM);
                goto out;
        }
        inode->index = (void*)index;

        write_lock(&i_hashtable_lock);
        hash_for_each_possible(i_hashtable, entry, hash_node, path_hash) {
                if (entry->path_hash == path_hash) {
                        vfree(inode->index);
                        vfree(inode);
                        inode = entry->inode;
                        write_unlock(&i_hashtable_lock);
                        goto out;
                }
        }

        entry = i_hash_node_create(path_hash, inode);
        hash_add(i_hashtable, &entry->hash_node, path_hash);
        write_unlock(&i_hashtable_lock);
out:
        return inode;
}

int i_hashtable_search_and_rename(uint32_t old_path_hash, 
                                  uint32_t new_path_hash) {
        int ret = 0;
        struct poly_inode *inode = NULL;
        struct i_hash_node *entry = NULL;

        write_lock(&i_hashtable_lock);
        hash_for_each_possible(i_hashtable, entry, hash_node, old_path_hash) {
                if (entry->path_hash == old_path_hash) {
                        inode = entry->inode;
                        break;
                }
        }

        if (!inode){
                POLYOS_ERROR("Failed to find inode");
                ret = -ENOENT;
                goto out;
        }

        hash_del(&entry->hash_node);
        i_hash_node_free(entry);

        inode->path_hash = new_path_hash;
        entry = i_hash_node_create(new_path_hash, inode);
        hash_add(i_hashtable, &entry->hash_node, new_path_hash);
out:
        write_unlock(&i_hashtable_lock);
        return ret;
}

void clean_up_poly_inode_index(void) {
        int i = 0;
        struct poly_inode *inode = NULL;
        struct i_hash_node *entry = NULL;

        write_lock(&i_hashtable_lock);
        hash_for_each(i_hashtable, i, entry, hash_node) {
                inode = entry->inode;
                if (inode) {
                        vfree(inode->index);
                        vfree(inode);
                }
        }
        write_unlock(&i_hashtable_lock);
}
