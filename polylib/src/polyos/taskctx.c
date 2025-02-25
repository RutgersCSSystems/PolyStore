/*
 * taskctx.c
 */

#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>

#include "polyos.h"

#define POLYSTORE_TASKCTX_HASHTABLE_ORDER 8

uint32_t next_avail_task_id = 0;

DEFINE_RWLOCK(task_ctx_hashtable_lock);
DEFINE_HASHTABLE(task_ctx_hashtable, POLYSTORE_TASKCTX_HASHTABLE_ORDER);

/* PolyOS task context hash table entry */
struct task_ctx_hash_node {
        uint32_t task_id;
        struct g_task_ctx *task_ctx;
        struct hlist_node hash_node;
};

/*
 * Allocate a new PolyStore task context descriptor
 */
struct g_task_ctx* task_ctx_alloc(void) {
        int task_id = 0, ret = 0;
        unsigned long task_ctx_addr = 0;
        struct g_task_ctx *task_ctx = NULL; 
        struct vm_area_struct *vma = NULL;

        /* Get the next available task_id */
        task_id = __sync_fetch_and_add(&next_avail_task_id, 1);
        if (task_id > POLYSTORE_MAX_TASK_NR) {
                POLYOS_ERROR("Exceeding max task id!");
                goto out;
        }

        /* Allocate the in-memory g_task_ctx struct */
        task_ctx = vmalloc(PAGE_SIZE);
        if (!task_ctx) { 
                POLYOS_ERROR("Failed to allocate task_ctx struct!");
                goto out;
        }

        /* Initialize in-memory t_task_ctx struct */
	task_ctx->task_id = task_id;
        task_ctx->epoch_tic = 0;
        task_ctx->throughput = 0;
        task_ctx->throughput_read = 0;
        task_ctx->throughput_write = 0;
        task_ctx->last_throughput = 0;
        task_ctx->low_throughput_tic = 0;
        task_ctx->placement = 0;        // TODO
        task_ctx->journal = NULL;

        task_ctx_addr = GET_TASK_CONTEXT_VADDR(task_id);

        /* Map the task_ctx page to the user space process/thread */
        vma = find_vma(current->mm, (unsigned long)task_ctx_addr);
        if (!vma || vma->vm_start != POLYSTORE_TASK_CTX_VADDR_BASE ||
                    vma->vm_end != POLYSTORE_TASK_CTX_VADDR_BASE +
                                   POLYSTORE_TASK_CTX_VADDR_RANGE) {
                POLYOS_ERROR("Find VMA failed!");
                goto out;
        }

        mm_write_lock(current->mm);
        ret = remap_pfn_range(vma, task_ctx_addr, vmalloc_to_pfn(task_ctx),
                              PAGE_SIZE, vma->vm_page_prot);
        mm_write_unlock(current->mm);
        if (ret) {
                POLYOS_ERROR("Faild to map to user addr 0x%lx!", task_ctx_addr);
                goto out;
        }

out:
        return task_ctx;
}

/*
 * Deallocate a new PolyStore task context descriptor
 */
void task_ctx_free(struct g_task_ctx *task_ctx) {
        unsigned long task_ctx_addr = 0;
        struct vm_area_struct *vma = NULL;

        task_ctx_addr = GET_TASK_CONTEXT_VADDR(task_ctx->task_id);

        /* Map the task_ctx page to the user space process/thread */
        vma = find_vma(current->mm, (unsigned long)task_ctx_addr);
        if (!vma || vma->vm_start != POLYSTORE_TASK_CTX_VADDR_BASE ||
                    vma->vm_end != POLYSTORE_TASK_CTX_VADDR_BASE +
                                   POLYSTORE_TASK_CTX_VADDR_RANGE) {
                POLYOS_ERROR("Find VMA failed!");
        }

        mm_write_lock(current->mm);
        zap_vma_ptes(vma, task_ctx_addr, PAGE_SIZE);
        mm_write_unlock(current->mm);

        return vfree(task_ctx);
}

/*
 * PolyOS task context hashtable utility
 */
static inline struct task_ctx_hash_node* task_ctx_hash_node_create(uint32_t task_id, 
                                             struct g_task_ctx *task_ctx) {
        struct task_ctx_hash_node *new_node = 
                        kmalloc(sizeof(struct task_ctx_hash_node), GFP_KERNEL);
        if (new_node) {
                new_node->task_id = task_id;
                new_node->task_ctx = task_ctx;
                INIT_HLIST_NODE(&new_node->hash_node);
        }
        return new_node;
}

static inline void task_ctx_hash_node_free(struct task_ctx_hash_node *node) {
        if (node){
                kfree(node);
        }
}

int task_ctx_hashtable_insert(uint32_t task_id, struct g_task_ctx *task_ctx) {
        struct task_ctx_hash_node *new_node = 
                        task_ctx_hash_node_create(task_id, task_ctx);
        if (!new_node){
                printk(KERN_ERR "Failed to allocate memory for new node");
                return -ENOMEM;
        }
        write_lock(&task_ctx_hashtable_lock);
        hash_add(task_ctx_hashtable, &new_node->hash_node, task_id);
        write_unlock(&task_ctx_hashtable_lock);
        return 0;
}

struct g_task_ctx* task_ctx_hashtable_search(uint32_t task_id) {
        struct task_ctx_hash_node *entry = NULL;
        read_lock(&task_ctx_hashtable_lock);
        hash_for_each_possible(task_ctx_hashtable, entry, hash_node, task_id) {
                if (entry->task_id == task_id) {
                        read_unlock(&task_ctx_hashtable_lock);
                        return entry->task_ctx;
                }
        }
        read_unlock(&task_ctx_hashtable_lock);
        return NULL;
}

void task_ctx_hashtable_delete(uint32_t task_id) {
        struct task_ctx_hash_node *entry = NULL;
        write_lock(&task_ctx_hashtable_lock);
        hash_for_each_possible(task_ctx_hashtable, entry, hash_node, task_id) {
                if (entry->task_id == task_id){
                        hash_del(&entry->hash_node);
                        task_ctx_hash_node_free(entry);
                        break;
                }
        }
        write_unlock(&task_ctx_hashtable_lock);
}

