#ifndef POLYSTORE_DEF_H
#define POLYSTORE_DEF_H

/* PolyStore IO path begin with /mnt/polystore */
#define POLYSTORE_PATH_PREFIX (char *)"/mnt/polystore"
#define POLYSTORE_PATH_PREFIX_LEN 14

#define POLYSTORE_PATH_CHECK(PATH) \
        (!strncmp(PATH, POLYSTORE_PATH_PREFIX, POLYSTORE_PATH_PREFIX_LEN))
#define ABSOLUTE_PATH_CHECK(PATH) \
        (PATH[0] == '/')


/*
 * PolyStore IO file descriptor
 * Assume open_file_max less than 4096
 *
 * bit 00 - 11 -> FAST fd
 * bit 12 - 23 -> SLOW fd
 * bit 24 -> PolyStore fd indicator
 */
#define POLYSTORE_FD_PREFIX 0x1000000
#define POLYSTORE_FAST_FD_MASK 0xFFF
#define POLYSTORE_SLOW_FD_MASK 0xFFF000

#define POLYSTORE_FD_CHECK(FD) \
        (FD & POLYSTORE_FD_PREFIX) 
#define POLYSTORE_FAST_FD(FD) \
        (FD & POLYSTORE_FAST_FD_MASK) 
#define POLYSTORE_SLOW_FD(FD) \
        ((FD & POLYSTORE_SLOW_FD_MASK) >> 12) 
#define POLYSTORE_GEN_FD(FAST_FD, SLOW_FD) \
        (FAST_FD | (SLOW_FD << 12) | POLYSTORE_FD_PREFIX)

/* Poly-index */
#define POLY_INDEX_NODE_SIZE_SHIFT 21
#define POLY_INDEX_NODE_SIZE (1 << POLY_INDEX_NODE_SIZE_SHIFT)
#define POLY_INDEX_NODE_SIZE_MASK (~(POLY_INDEX_NODE_SIZE - 1))
#define POLY_INDEX_NODE_OFFSET_MASK (POLY_INDEX_NODE_SIZE - 1)
#define POLY_INDEX_PG_NUM (1 << (POLY_INDEX_NODE_SIZE_SHIFT - PAGE_SIZE_SHIFT))

#define POLY_INDEX_STRUCT_SIZE_SHIFT 8
#define POLY_INDEX_STRUCT_SIZE \
                (1 << POLY_INDEX_STRUCT_SIZE_SHIFT)
#ifndef POLYSTORE_LARGE_INDEX
#define POLY_INDEX_MAX_MEM_SIZE_SHIFT 20
#else
#define POLY_INDEX_MAX_MEM_SIZE_SHIFT 24
#endif
#define POLY_INDEX_MAX_MEM_SIZE \
                (1 << POLY_INDEX_MAX_MEM_SIZE_SHIFT)

/* PolyStore on-disk metadata related */
#define myrb_parent(pc)    ((struct rb_node *)(pc & ~3))
#define my__rb_color(pc)     ((pc) & 1)
#define myrb_color(rb)       my__rb_color((rb)->__rb_parent_color)

#define POLYSTORE_METADATA_FOLDER ".persist"
#define POLYSTORE_POLY_INODE_PREFIX "inodes"
#define PATHSEP "/"
#define META_INODE_NO 0
#define MAX_POLY_INODE_NR 32768
#define MAX_MOUNT_POINT_LEN 64
#define MAX_POLY_INDEX_NODE_NR 65536

/* PolyStore in-memory metadata mapping */
#define POLY_INODE_VADDR_BASE		0x1000000000UL
#define POLY_INDEX_VADDR_BASE		0x2000000000UL
#define POLYSTORE_TASK_CTX_VADDR_BASE	0x3000000000UL
#define POLYSTORE_CONFIG_VAR_VADDR	0x3001000000UL
#define POLYSTORE_CONTROL_VAR_VADDR	0x3002000000UL
#define POLY_CACHE_VADDR_BASE           0x4000000000UL

#define POLY_INODE_VADDR_RANGE          0x100000000UL
#define POLY_INDEX_VADDR_RANGE          0x1000000000UL
#define POLYSTORE_TASK_CTX_VADDR_RANGE  0x1000000UL
#define POLYSTORE_CONFIG_VAR_SIZE       0x10000UL 
#define POLYSTORE_CONTROL_VAR_SIZE      0x10000UL 
#define POLY_CACHE_VADDR_RANGE          0x8000000000UL

#define GET_TASK_CONTEXT_VADDR(TASK_ID) \
               (POLYSTORE_TASK_CTX_VADDR_BASE + ((unsigned long)(TASK_ID) << PAGE_SHIFT)) 

#define GET_POLY_INODE_VADDR(INO) \
               (POLY_INODE_VADDR_BASE + ((unsigned long)(INO) << PAGE_SHIFT)) 

#define GET_POLY_INDEX_VADDR(INO) \
               (POLY_INDEX_VADDR_BASE + ((unsigned long)(INO) << POLY_INDEX_MAX_MEM_SIZE_SHIFT)) 

/* Task context */
#define POLYSTORE_MAX_TASK_NR 1024

/* PolyStore kernel module (PolyOS) */
#define POLYSTORE_DEVICE_NAME "polyos"
#define POLYSTORE_DEVICE_PATH "/dev/polyos"

/* Placement enum type */
typedef enum placement {
        BLK_PLACEMENT_TBD = 1,
        BLK_PLACEMENT_FAST,
        BLK_PLACEMENT_SLOW,
        BLK_PLACEMENT_BOTH
} placement_t;

/* Poly cache policy */
typedef enum cache_policy {
        CACHE_POLICY_EQUAL = 1,
        CACHE_POLICY_FAST_READ_NOT_ADMIT
} cache_policy_t;


/* I/O direction */
typedef enum io_direction {
       READ_IO = 1,
       WRITE_IO
} io_direction_t;

/* Bitmap and Bitmap pointer */
typedef unsigned char bitmap_t;
typedef uint64_t bitmap_ptr_t;


/* System-wide config variables in the shm (rd-only for PolyLib clients) */
struct g_config_var {
        uint32_t max_inode_nr; 
        uint32_t max_it_node_nr; 
        uint64_t cache_flush_start;
        uint64_t cache_flush_end;
        uint16_t cache_flush_thread_nr;
        uint16_t resvd1_16;
        uint32_t resvd1_32;
        uint64_t resvd1_64;
        uint64_t resvd2_64;
        char fast_dir[MAX_MOUNT_POINT_LEN];
        char slow_dir[MAX_MOUNT_POINT_LEN];
} __attribute__((aligned(64)));

/* System-wide control variables in the shm (rw for PolyLib clients) */
struct g_control_var {
        unsigned char poly_cache_free_list[64][512];
        uint32_t poly_cache_free_list_lock[64];
} __attribute__((aligned(64)));


/* System-wide task context */
#define POLYSTORE_TASK_MAIN 0
#define POLYSTORE_TASK_WORK 1

struct g_task_ctx {
        uint32_t task_id;
        uint32_t epoch_tic;
        uint64_t throughput;
        uint64_t throughput_read;
        uint64_t throughput_write;
        uint64_t last_throughput;
        uint32_t low_throughput_tic;
        placement_t placement;

        /* User space fields defined below (Should NOT be touched in kernel) */
        void *dump_filp;
        void *journal;
};


/*
 * On-disk persistent h_inode structure
 * 48 bytes that fits into 64-byte cache line size log entry
 */
struct d_poly_inode {
        uint32_t path_hash;     /* inode canonical path hash value */
        uint32_t ino;           /* inode number */
        uint32_t type;          /* inode type */
        uint32_t placement;     /* inode placement */
        uint64_t size;          /* inode size */
        uint64_t it_root_idx;   /* root node index in indexing (it) file */
        uint64_t it_node_nr;    /* number of indexing (it) nodes */
        uint32_t rsvd32_1;      /* reserved dword 1 */
        uint32_t rsvd32_2;      /* reserved dword 2 */
};

/*
 * On-disk persistent it_node_entry structure
 * 96 bytes that fits into 2 log entries
 * 
 * The first 48 bytes is the struct rb_node
 *
 * The second 48 bytes is the struct interval_tree_node
 * plus the placement and offset info
 */
struct d_poly_it_node {
        struct interval_tree_node it; /* interval node */
        uint64_t offset;        /* offset in underlying data file */
        uint32_t placement;     /* placement of this interval */
        uint32_t size;          /* interval size */
        uint32_t idx_nr;        /* interval node index */
        uint32_t rsvd32_1;      /* reserved dword 1 */
};


#endif
