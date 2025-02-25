/*
 * context.c
 */

#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <sys/ioctl.h>

#include "debug.h"
#include "polystore.h"
#include "uthash.h"

/* PolyStore Task Context */
__thread struct l_task_ctx tls_task_ctx;
int task_ctx_active_cnt = 0;

struct list_head task_ctx_list = LIST_HEAD_INIT(task_ctx_list); 
pthread_rwlock_t task_ctx_list_lock = PTHREAD_RWLOCK_INITIALIZER;

int polystore_task_ctx_register(int type) {
        int ret = 0;
        struct polyos_task_ctx_reg_cmd cmd;
        struct g_task_ctx *task_ctx = NULL;

        /* Register and get task context from PolyOS */
        cmd.argsz = sizeof(cmd);
        cmd.type = type;
        ret = ioctl(polyos_dev, POLYOS_TASK_CTX_REG_CMD, &cmd);
        if (ret < 0) {
                ERROR_T("Failed to register task with PolyOS, err %d", ret);
                ret = -EFAULT;
                goto err;
        }

        /* Store as TLS and add to task context list */
        task_ctx = (struct g_task_ctx*)cmd.task_ctx_addr;
        if (!task_ctx) {
                ERROR_T("Failed to get task ctx descriptor, err %d", ret);
                ret = -EFAULT;
                goto err;
        }

        /* Initialize task context */
        task_ctx->throughput = 0;
        task_ctx->throughput_read = 0;
        task_ctx->throughput_write = 0;
        task_ctx->last_throughput = 0;
        task_ctx->low_throughput_tic = 0;

        /* Static initial placement */
        if (__sync_fetch_and_add(&task_ctx_active_cnt, 1) < sched_split_point) {
                task_ctx->placement = BLK_PLACEMENT_FAST;
                __sync_fetch_and_add(&sched_fastdev_usage, 1);
        } else {
                task_ctx->placement = BLK_PLACEMENT_SLOW;
        }

        tls_task_ctx.task_ctx = task_ctx;
        tls_task_ctx.task_id = task_ctx->task_id;
        tls_task_ctx.cpu_id = sched_getcpu();

        INIT_LIST_HEAD(&tls_task_ctx.list); 

        pthread_rwlock_wrlock(&task_ctx_list_lock);
        list_add_tail(&tls_task_ctx.list, &task_ctx_list); 
        pthread_rwlock_unlock(&task_ctx_list_lock);

err:
        return ret;
}

int polystore_task_ctx_delete() {
        int ret = 0;
        struct polyos_task_ctx_delete_cmd cmd;

        /* Check TLS task context descriptor */
        if (!tls_task_ctx.task_ctx) {
                ERROR_T("Failed to get task ctx descriptor, err %d", ret);
                ret = -EFAULT;
                goto err;
        }

        /* Reset TLS and remove from task context list */
        pthread_rwlock_wrlock(&task_ctx_list_lock);
        list_del(&tls_task_ctx.list); 
        pthread_rwlock_unlock(&task_ctx_list_lock);

        if (tls_task_ctx.task_ctx->placement == BLK_PLACEMENT_FAST)
                __sync_fetch_and_sub(&sched_fastdev_usage, 1);

        __sync_fetch_and_sub(&task_ctx_active_cnt, 1);

        tls_task_ctx.task_ctx = NULL;

        /* Unregeister task context in PolyOS */
        cmd.argsz = sizeof(cmd);
        cmd.task_id = tls_task_ctx.task_id;
        ret = ioctl(polyos_dev, POLYOS_TASK_CTX_DELETE_CMD, &cmd);
        if (ret < 0) {
                ERROR_T("Failed to delete task with PolyOS, err %d", ret);
                ret = -EFAULT;
        }

err:
        return ret;
}

/* PolyStore file pointer (Poly-file) Hashmap */
struct poly_file_hash_node {
        int polyfd;                     /* PolyStore file descriptor */
        struct poly_file *file;         /* Pointer to the Poly-file */

        UT_hash_handle hh;              /* uthash table handle */
};

struct poly_file_hash_node *poly_file_hashmap = NULL;
pthread_rwlock_t poly_file_hashmap_rwlock = PTHREAD_RWLOCK_INITIALIZER;

struct poly_file* polystore_file_create(int polyfd, struct poly_inode *inode) {
        struct poly_file *file = NULL;
        struct poly_file_hash_node *hash_node = NULL;

        /* Allocate and initialize Poly-file */
        file = (struct poly_file*)malloc(sizeof(struct poly_file));
        if (!file) {
                ERROR_T("Failed to allocate poly-file"); 
                goto err2;
        }
        file->polyfd = polyfd;
        file->off = 0;
        file->inode = inode;

        /* Allocate Poly-file hashmap node */
        hash_node = (struct poly_file_hash_node*)
                        malloc(sizeof(struct poly_file_hash_node));
        if (!hash_node) {
                ERROR_T("Failed to allocate poly-file hashmap node"); 
                goto err1;
        }
        hash_node->polyfd = polyfd;
        hash_node->file = file;

        /* Insert Poly-file to hashmap */
        if (pthread_rwlock_wrlock(&poly_file_hashmap_rwlock) != 0) {
                ERROR_T("Failed to aquire wlock for poly_file hashmap"); 
        }
        HASH_ADD_INT(poly_file_hashmap, polyfd, hash_node);
        pthread_rwlock_unlock(&poly_file_hashmap_rwlock);
        
        return file;
err1:
        free(file);
        file = NULL;
err2:
        return file;
}

struct poly_file* polystore_file_find(int polyfd) {
        struct poly_file_hash_node *hash_node = NULL;

        /* Lookup Poly-file hashmap */
        if (pthread_rwlock_rdlock(&poly_file_hashmap_rwlock) != 0) {
                ERROR_T("Failed to aquire rlock for poly_file hashmap"); 
                return NULL;
        }
        HASH_FIND_INT(poly_file_hashmap, &polyfd, hash_node);
        pthread_rwlock_unlock(&poly_file_hashmap_rwlock);

        if (!hash_node)
                return NULL;
        return hash_node->file;
}

int polystore_file_delete(int polyfd) {
        struct poly_file *file = NULL;
        struct poly_file_hash_node *hash_node = NULL;

        /* Remove from Poly-file hashmap and free hashmap node */
        if (pthread_rwlock_wrlock(&poly_file_hashmap_rwlock) != 0) {
                ERROR_T("Failed to aquire wlock for poly_file hashmap"); 
                return -EFAULT;
        }
        HASH_FIND_INT(poly_file_hashmap, &polyfd, hash_node);
        if (hash_node) {
                file = hash_node->file;
                HASH_DEL(poly_file_hashmap, hash_node);
                free(hash_node);
        }
        pthread_rwlock_unlock(&poly_file_hashmap_rwlock);

        /* Free the poly-file struct */
        if (file)
                free(file);

        return 0;
}


/* PolyStore inode (Poly-inode) Hashmap */
struct poly_inode_hash_node {
        int path_hash;                  /* Path hash value */ 
        void* inode;                    /* PolyStore poly-inode */
        UT_hash_handle hh;              /* uthash table handle */
};

struct poly_inode_hash_node *poly_inode_hashmap = NULL;
pthread_rwlock_t poly_inode_hashmap_rwlock = PTHREAD_RWLOCK_INITIALIZER;

int polystore_inode_hashmap_add(struct poly_inode *inode) {
        int ret = 0;
        int path_hash = inode->path_hash;
        struct poly_inode_hash_node *hash_node = NULL, *replaced = NULL;

        /* Allocate Poly-inode hashmap node */
        hash_node = (struct poly_inode_hash_node*)
                        malloc(sizeof(struct poly_inode_hash_node));
        if (!hash_node) {
                ERROR_T("Failed to allocate poly-inode hashmap node"); 
                ret = -ENOMEM;
                goto err;
        }
        hash_node->path_hash = path_hash;
        hash_node->inode = inode;

        /* Insert Poly-inode to hashmap */
        if (pthread_rwlock_wrlock(&poly_inode_hashmap_rwlock) != 0) {
                ERROR_T("Failed to aquire wlock for poly-inode hashmap"); 
        }
        HASH_REPLACE_INT(poly_inode_hashmap, path_hash, hash_node, replaced);
        //HASH_ADD_INT(poly_inode_hashmap, path_hash, hash_node);
        pthread_rwlock_unlock(&poly_inode_hashmap_rwlock);
        
err:
        return ret;
}

int polystore_inode_hashmap_delete(struct poly_inode *inode) {
        int ret = 0;
        int path_hash = inode->path_hash;
        struct poly_inode_hash_node *hash_node = NULL;

        /* Remove from Poly-file hashmap and free hashmap node */
        if (pthread_rwlock_wrlock(&poly_inode_hashmap_rwlock) != 0) {
                ERROR_T("Failed to aquire wlock for poly-inode hashmap"); 
                return -EFAULT;
        }
        HASH_FIND_INT(poly_inode_hashmap, &path_hash, hash_node);
        if (hash_node) {
                HASH_DEL(poly_inode_hashmap, hash_node);
                free(hash_node);
        }
        pthread_rwlock_unlock(&poly_inode_hashmap_rwlock);

        return 0;
}

int polystore_inode_hashmap_iterate(poly_inode_hashmap_iter_op func) {
        struct poly_inode *inode = NULL;
        struct poly_inode_hash_node *s = NULL, *tmp = NULL;

printf("clean iterate start...\n");
        HASH_ITER(hh, poly_inode_hashmap, s, tmp) {
                inode = (struct poly_inode*)s->inode; 
                if (inode->ino < 0)
                        continue;
                func(inode);
        }
printf("clean iterate end...\n");

        return 0;
}

struct poly_inode* polystore_inode_hashmap_find(int path_hash) {
        int ret = 0;
        struct poly_inode *inode = NULL;
        struct poly_inode_hash_node *hash_node = NULL;

        /* Find Poly-inode from inode hashmap */
        if (pthread_rwlock_wrlock(&poly_inode_hashmap_rwlock) != 0) {
                ERROR_T("Failed to aquire wlock for poly-inode hashmap"); 
                return NULL;
        }
        HASH_FIND_INT(poly_inode_hashmap, &path_hash, hash_node);
        if (hash_node) {
                inode = hash_node->inode;
        }
        pthread_rwlock_unlock(&poly_inode_hashmap_rwlock);

        return inode;
}

int polystore_inode_hashmap_rename(const char *oldname, 
                                   const char *newname) {
        int ret = 0;
        int path_hash_old = (int)util_get_path_hash(oldname);
        int path_hash_new = (int)util_get_path_hash(newname);
        struct poly_inode *inode;
        struct poly_inode_hash_node *hash_node = NULL, *replaced = NULL;

        /* Find Poly-inode from inode hashmap */
        if (pthread_rwlock_wrlock(&poly_inode_hashmap_rwlock) != 0) {
                ERROR_T("Failed to aquire wlock for poly-inode hashmap"); 
                ret =  -EFAULT;
                goto err;
        }
        HASH_FIND_INT(poly_inode_hashmap, &path_hash_old, hash_node);
        if (!hash_node) {
                ret =  -EFAULT;
                goto err;
        }

        /* Get inode */
        inode = hash_node->inode;
        if (!inode) {
                ERROR_T("inode is NULL"); 
                ret =  -EFAULT;
                goto err;
        }
        memset(inode->pathname, 0, strlen(inode->pathname));
        strcpy(inode->pathname, newname);

        /* Remove old entry in Poly-inode hashmap */
        HASH_DEL(poly_inode_hashmap, hash_node);
        hash_node = NULL;

        /* Allocate Poly-inode hashmap node */
        hash_node = (struct poly_inode_hash_node*)
                        malloc(sizeof(struct poly_inode_hash_node));
        if (!hash_node) {
                ERROR_T("Failed to allocate poly-inode hashmap node"); 
                ret = -ENOMEM;
                goto err;
        }
        hash_node->path_hash = path_hash_new;
        hash_node->inode = inode;

        HASH_REPLACE_INT(poly_inode_hashmap, path_hash, hash_node, replaced);
        
err:
        pthread_rwlock_unlock(&poly_inode_hashmap_rwlock);
        return ret;
}

