#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/types.h>

#include "../polyos_iocmd.h"

#define DEVICE_FILE "/dev/polyos"

#define PAGE_SIZE 4096

#define POLY_INODE_VADDR_BASE           0x1000000000
#define POLY_INDEX_VADDR_BASE           0x2000000000
#define POLYSTORE_CONFIG_VAR_VADDR      0x5000000000
#define POLYSTORE_CONTROL_VAR_VADDR     0x6000000000

#define POLY_INODE_VADDR_RANGE          0x1000000000
#define POLY_INDEX_VADDR_RANGE          0x1000000000

struct inode {
	int path_hash;
	int ino;
};

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
        char fast_dir[64];
        char slow_dir[64];
};

/* System-wide control variables in the shm (rw for PolyLib clients) */
struct g_control_var {
        uint32_t next_avail_task_id;
};

struct g_config_var *g_config_var = NULL;
struct g_control_var *g_control_var = NULL;

const char *fast_dir_file = "/mnt/fast/test";
const char *slow_dir_file = "/mnt/slow/test";

int polystore_init_client(int devfd) {
	int ret = 0;
	struct polyos_client_init_cmd cmd;
	cmd.argsz = sizeof(cmd);
	cmd.config_var_addr = POLYSTORE_CONFIG_VAR_VADDR;
	cmd.control_var_addr = POLYSTORE_CONTROL_VAR_VADDR;

	g_config_var = mmap((struct g_config_var*)POLYSTORE_CONFIG_VAR_VADDR, 
			    PAGE_SIZE, PROT_READ, MAP_ANONYMOUS | MAP_SHARED, -1, 0);
	g_control_var = mmap((struct g_control_var*)POLYSTORE_CONTROL_VAR_VADDR, 
			    PAGE_SIZE, PROT_READ | PROT_WRITE, 
			    MAP_ANONYMOUS | MAP_SHARED, -1, 0);

	printf("Config variable mapped at %p\n", g_config_var);
	printf("Control variable mapped at %p\n", g_control_var);

	ret = ioctl(devfd, POLYOS_CLIENT_INIT_CMD, &cmd);
	if (ret < 0) {
		printf("Failed to init client with %d\n", ret);
		goto out;
	}

	printf("AFTER ...\n");

	printf("Config variable max inode: %d\n", g_config_var->max_inode_nr);
	printf("Config variable max itnode: %d\n", g_config_var->max_it_node_nr);

	printf("Next avail task id: %d\n", __sync_fetch_and_add(&g_control_var->next_avail_task_id, 1));

out:
	return ret;
}

int polystore_open(int devfd) {
	int ret = 0;
	struct inode *inode = NULL;
	struct polyos_open_cmd cmd;
	cmd.argsz = sizeof(cmd);
	strcpy(cmd.fastpath, fast_dir_file);
	strcpy(cmd.slowpath, slow_dir_file);
	cmd.path_hash = 9;
	cmd.mode = 0666;
	cmd.flags = O_CREAT | O_RDWR;
	cmd.inode_mem = 0;
	cmd.index_mem = 0;

	void *m = mmap((void*)POLY_INODE_VADDR_BASE, POLY_INODE_VADDR_RANGE, 
             PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_SHARED, -1, 0);
	if (m == MAP_FAILED) {
		printf("Failed to map inode\n");
	}

	m = mmap((void*)POLY_INDEX_VADDR_BASE, POLY_INDEX_VADDR_RANGE, 
             PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_SHARED, -1, 0);
	if (m == MAP_FAILED) {
		printf("Failed to map index\n");
	}


	ret = ioctl(devfd, POLYOS_OPEN_CMD, &cmd);
	if (ret < 0) {
		printf("Failed to open file with %d\n", ret);
		goto out;
	}

	printf("AFTER ...\n");

	printf("inode vaddr: 0x%lx\n", cmd.inode_mem);
	printf("index vaddr: 0x%lx\n", cmd.index_mem);
	inode = (struct inode*)cmd.inode_mem;

	printf("inode no: %d\n", inode->ino);
	printf("inode path hash: %d\n", inode->path_hash);

out:
	return ret;
}

int polystore_close(int devfd) {
	int ret = 0;
	struct polyos_close_cmd cmd;
	cmd.argsz = sizeof(cmd);

	// TODO	

	return ret;
}


int main(int argc, char **argv) {
        int ret, devfd, op;

        if (argc < 2) {
                printf("Invalid input\n");
                return -1;
        }
        op = atoi(argv[1]);

        /* Open PolyOS controller module */
        devfd = open(DEVICE_FILE, O_RDWR);
        if (devfd < 0) {
                perror("Failed to open the device");
                return -1;
        }

        /* Perform Operations to PolyOS controller */
	switch(op) {
		default:
		case 0:
			/* Init */
			ret = polystore_init_client(devfd);
			break;
		case 1:
			/* Open */
			ret = polystore_open(devfd);
			break;

		case 2:
			/* Close */
			ret = polystore_close(devfd);
			break;
	}

        close(devfd);
        return 0;
}

