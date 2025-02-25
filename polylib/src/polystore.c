/*
 * polystore.c
 */

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/time.h>

#include "polystore.h"
#include "debug.h"

struct g_config_var *g_config_var = NULL;
struct g_control_var *g_control_var = NULL;
const char* FAST_DIR = NULL;
const char* SLOW_DIR = NULL;

int polyos_dev = 0;

struct sigaction polystoreSigHandle;
struct itimerval epochTime;

unsigned long profile_fast_placed;
unsigned long profile_slow_placed;
char profile_g_stat_dump_fname[PATH_MAX];
FILE *profile_g_stat_dump_file;

/* PolyLib I/O routines begin here */
void polystore_init(void) {
        int ret = 0;
        const char *shell_sched_split_point = NULL;
        const char *shell_cache_policy = NULL;
        const char *shell_cache_flush_begin = NULL;
        const char *shell_cache_flush_end = NULL;
        struct polyos_client_init_cmd cmd;
        void *mmap_inode = NULL, *mmap_index = NULL, *mmap_taskctx = NULL;

        /* Get parameters from env */
        shell_sched_split_point = getenv("POLYSTORE_SCHED_SPLIT_POINT");
        if (shell_sched_split_point) {
                sched_split_point = atoi((char*)shell_sched_split_point);
        } else {
                sched_split_point = 1;
        }

#ifdef POLYSTORE_POLYCACHE 
        shell_cache_policy = getenv("POLYSTORE_POLYCACHE_POLICY");
                if (shell_cache_policy) {
                poly_cache_policy = atoi((char*)shell_cache_policy);
        }

        shell_cache_flush_begin = getenv("POLYSTORE_POLYCACHE_FLUSH_BEGIN");
                if (shell_cache_flush_begin) {
                poly_cache_flushing_begin = atol((char*)shell_cache_flush_begin);
        }

        shell_cache_flush_end = getenv("POLYSTORE_POLYCACHE_FLUSH_END");
                if (shell_cache_flush_end) {
                poly_cache_flushing_end = atol((char*)shell_cache_flush_end);
        }
#endif

        /* Create VMA for global config and control variables */
	g_config_var = mmap((struct g_config_var*)POLYSTORE_CONFIG_VAR_VADDR, 
	                        POLYSTORE_CONFIG_VAR_SIZE, PROT_READ, 
                                MAP_ANONYMOUS | MAP_SHARED, -1, 0);
	if (g_config_var == MAP_FAILED) {
		ERROR_T("Failed to create VMA for global config variable");
                exit(-1);
	}

	g_control_var = mmap((struct g_control_var*)POLYSTORE_CONTROL_VAR_VADDR, 
                                POLYSTORE_CONTROL_VAR_SIZE, PROT_READ | PROT_WRITE, 
                                MAP_ANONYMOUS | MAP_SHARED, -1, 0);
	if (g_control_var == MAP_FAILED) {
		ERROR_T("Failed to create VMA for global control variable");
                exit(-1);
	}

        /* Create VMA for task contexts */
	mmap_taskctx = mmap((void*)POLYSTORE_TASK_CTX_VADDR_BASE, 
                                POLYSTORE_TASK_CTX_VADDR_RANGE, 
                                PROT_READ | PROT_WRITE, 
                                MAP_ANONYMOUS | MAP_SHARED, -1, 0);
	if (mmap_taskctx == MAP_FAILED) {
		ERROR_T("Failed to map task ctx, errno %d", errno);
                exit(-1);
	}

        /* Establish connection to PolyOS kernel component */
        polyos_dev = open(POLYSTORE_DEVICE_PATH, O_RDWR); 
        if (polyos_dev == -1) {
                ERROR_T("Failed to open PolyOS module");
                goto out;
        }

        /* Map the global config and control variables */
        cmd.argsz = sizeof(cmd);
	cmd.config_var_addr = POLYSTORE_CONFIG_VAR_VADDR;
	cmd.control_var_addr = POLYSTORE_CONTROL_VAR_VADDR;
        ret = ioctl(polyos_dev, POLYOS_CLIENT_INIT_CMD, &cmd);
        if (ret < 0) {
                ERROR_T("Failed to initialize with PolyOS, err %d", ret);
                goto err2;
        }

        INFO_T("thread %d, config_var mapped at %lx", 
                                getpid(), cmd.config_var_addr);
        INFO_T("thread %d, control_var mapped at %lx", 
                                getpid(), cmd.control_var_addr);

        /* Setup mount point for storage devices */
        FAST_DIR = g_config_var->fast_dir;
        SLOW_DIR = g_config_var->slow_dir;

        /* Register task context of main process with PolyOS */
        ret = polystore_task_ctx_register(POLYSTORE_TASK_MAIN);
        if (ret < 0) {
                ERROR_T("Failed to register task context with PolyOS, err %d", ret);
                goto err2;
        }

        /* Create VMA for poly-inode and poly-index */
	mmap_inode = mmap((void*)POLY_INODE_VADDR_BASE, POLY_INODE_VADDR_RANGE, 
                                PROT_READ | PROT_WRITE, 
                                MAP_ANONYMOUS | MAP_SHARED, -1, 0);
	if (mmap_inode == MAP_FAILED) {
		ERROR_T("Failed to map inode region");
                goto err1;
	}

	mmap_index = mmap((void*)POLY_INDEX_VADDR_BASE, POLY_INDEX_VADDR_RANGE, 
                                PROT_READ | PROT_WRITE, 
                                MAP_ANONYMOUS | MAP_SHARED, -1, 0);
	if (mmap_index == MAP_FAILED) {
		ERROR_T("Failed to map index region");
                goto err1;
	}

#ifdef POLYSTORE_POLYCACHE 
        poly_cache_initialize();
#endif

        // TODO
#ifdef POLYSTORE_IO_STAT
#ifndef POLYSTORE_DYNAMIC_PLACEMENT
        strcpy(profile_g_stat_dump_fname, "static.data");
#else
        strcpy(profile_g_stat_dump_fname, "dynamic.data");
#endif
        profile_g_stat_dump_file = fopen(profile_g_stat_dump_fname, "a");

        if (profile_g_stat_dump_file == NULL) {
                ERROR_T("Failed to create global stat file %s\n", 
                                profile_g_stat_dump_fname);
        }
#endif

        memset(&polystoreSigHandle, 0, sizeof(polystoreSigHandle));
        polystoreSigHandle.sa_flags = SA_SIGINFO;
#ifndef POLYSTORE_DYNAMIC_PLACEMENT
        polystoreSigHandle.sa_handler = (void*)&polystore_placement_static_statonly;
#else
        polystoreSigHandle.sa_handler = (void*)&polystore_placement_dynamic;
#endif
        if (sigaction(SIGPROF, &polystoreSigHandle, NULL) == -1) {
                ERROR_T("Failed to register sighandler, err %d\n", errno);
	} 

        epochTime.it_value.tv_sec = 0;
        epochTime.it_value.tv_usec = 200000;
        epochTime.it_interval.tv_sec = 0;
        epochTime.it_interval.tv_usec = epochTime.it_value.tv_usec;
        if (setitimer(ITIMER_PROF, &epochTime, NULL) == -1) {
                ERROR_T("Failed to set timer, err %d\n", errno);
	} 

        return;

err1:
        polystore_task_ctx_delete(); 

err2:
        close(polyos_dev);

out:
        return;
}

void polystore_exit(void) {
        int ret = 0;
        struct polyos_client_exit_cmd cmd;

#ifdef POLYSTORE_POLYCACHE 
        poly_cache_exit();
#endif

        /* Delete task context of main process with PolyOS */
        ret = polystore_task_ctx_delete();
        if (ret < 0) {
                ERROR_T("Failed to delete task context with PolyOS, err %d", ret);
                exit(-1);
        }

        /* Disconnect with PolyOS component */
        cmd.argsz = sizeof(cmd);
	cmd.config_var_addr = POLYSTORE_CONFIG_VAR_VADDR;
	cmd.control_var_addr = POLYSTORE_CONTROL_VAR_VADDR;
        ret = ioctl(polyos_dev, POLYOS_CLIENT_EXIT_CMD, &cmd);
        if (ret < 0) {
                ERROR_T("Failed to disconnect with PolyOS, err %d", ret);
        }
        close(polyos_dev);

        /* Unmap poly-inode and poly-index */
        ret = munmap((void*)POLY_INODE_VADDR_BASE, POLY_INODE_VADDR_RANGE);
        if (ret) {
                ERROR_T("Failed to unmap poly-inode region, err %d", ret);
        }

        ret = munmap((void*)POLY_INDEX_VADDR_BASE, POLY_INDEX_VADDR_RANGE);
        if (ret) {
                ERROR_T("Failed to unmap poly-inode region, err %d", ret);
        }

        /* Unmap taskctx */
        ret = munmap((void*)POLYSTORE_TASK_CTX_VADDR_BASE, POLYSTORE_TASK_CTX_VADDR_RANGE);
        if (ret) {
                ERROR_T("Failed to unmap taskctx region, err %d", ret);
        }

        /* unmap global config and control variables */
        ret = munmap(g_config_var, POLYSTORE_CONFIG_VAR_SIZE);
        if (ret) {
                ERROR_T("Failed to unmap global config variable, err %d", ret);
        }

        ret = munmap(g_control_var, POLYSTORE_CONTROL_VAR_SIZE);
        if (ret) {
                ERROR_T("Failed to unmap global control variable, err %d", ret);
        }

}


