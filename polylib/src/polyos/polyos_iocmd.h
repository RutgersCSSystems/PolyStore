#ifndef IOCMD_H
#define IOCMD_H

#include <linux/types.h>
#include <linux/ioctl.h>

/* IOCTL commands */
#define POLYOS_IOC_MAGIC 'k'

/* POLYOS_CLIENT_INIT_CMD */
struct polyos_client_init_cmd {
        int argsz;                      /* arg size */
        unsigned long config_var_addr;  /* shared memory of global config var (in) */
        unsigned long control_var_addr; /* shared memory of global control var (in) */ 
};
#define POLYOS_CLIENT_INIT_CMD _IOR(POLYOS_IOC_MAGIC, 1, char *)


/* POLYOS_CLIENT_EXIT_CMD */
struct polyos_client_exit_cmd {
        int argsz;                      /* arg size */
        unsigned long config_var_addr;  /* shared memory of global config var (in) */
        unsigned long control_var_addr; /* shared memory of global control var (in) */ 
        // TODO
};
#define POLYOS_CLIENT_EXIT_CMD _IOR(POLYOS_IOC_MAGIC, 2, char *)


/* POLYOS_TASK_CTX_REG_CMD */
struct polyos_task_ctx_reg_cmd {
        int argsz;                      /* arg size */
        int type;                       /* task type - main or worker (in) */
        int task_id;                    /* task id for task context (out) */ 
        unsigned long task_ctx_addr;    /* shared memory for task context (out) */ 
};
#define POLYOS_TASK_CTX_REG_CMD _IOR(POLYOS_IOC_MAGIC, 3, char *)


/* POLYOS_TASK_CTX_DELETE_CMD */
struct polyos_task_ctx_delete_cmd {
        int argsz;                      /* arg size */
        int task_id;                    /* task id for task context (in) */ 
};
#define POLYOS_TASK_CTX_DELETE_CMD _IOR(POLYOS_IOC_MAGIC, 4, char *)


/* POLYOS_INDEX_ALLOC_CMD */
struct polyos_index_alloc_cmd {
        int argsz;                      /* arg size */
        int inode_no;                   /* Poly-inode no (in) */ 
        int index_no;                   /* Poly-index no (in) */ 
};
#define POLYOS_INDEX_ALLOC_CMD _IOR(POLYOS_IOC_MAGIC, 5, char *)


/* POLYOS_OPEN_CMD */
struct polyos_open_cmd {
        int argsz;                      /* arg size */
        char fastpath[256];             /* file path in fast storage (in) */
        char slowpath[256];             /* file path in slow storage (in) */
        int path_hash;                  /* file canonical path hash value (in) */ 
        int mode;                       /* mode (in) */ 
        int flags;                      /* flags (in) */ 
        int fd;                         /* file descriptor (out) */
        unsigned long inode_addr;       /* shared memory of poly-inode (out) */
        unsigned long index_addr;       /* shared memory of poly-index (out) */
};
#define POLYOS_OPEN_CMD _IOR(POLYOS_IOC_MAGIC, 8, char *)

/* POLYOS_RW_CMD */
struct polyos_rw_cmd {
        int argsz;                      /* arg size */
        int fd;                         /* file descriptor (in) */
        unsigned long buf;              /* I/O user space buffer (in) */
        size_t len;                     /* I/O size (in) */ 
        loff_t off;                     /* I/O offset (in) */ 
        int type;                       /* I/O type r/w (int) */
        ssize_t ret;                    /* I/O result (out) */
};
#define POLYOS_RW_CMD _IOR(POLYOS_IOC_MAGIC, 9, char *)


/* POLYOS_CLOSE_CMD */
struct polyos_close_cmd {
        int argsz;                      /* arg size */
        int fd;                         /* file descriptor (in) */
        int path_hash;                  /* file canonical path hash value (in) */ 
};
#define POLYOS_CLOSE_CMD _IOR(POLYOS_IOC_MAGIC, 10, char *)


/* POLYOS_RENAME_CMD */
struct polyos_rename_cmd {
        int argsz;                      /* arg size */
        int old_path_hash;              /* old canonical path hash value (in) */ 
        int new_path_hash;              /* new canonical path hash value (in) */ 
};
#define POLYOS_RENAME_CMD _IOR(POLYOS_IOC_MAGIC, 11, char *)


/* POLYOS_UNLINK_CMD */
struct polyos_unlink_cmd {
        int argsz;                      /* arg size */
        int path_hash;                  /* canonical path hash value (in) */ 
};
#define POLYOS_UNLINK_CMD _IOR(POLYOS_IOC_MAGIC, 12, char *)



#endif
