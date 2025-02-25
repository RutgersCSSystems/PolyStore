/*
 * placement.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "debug.h"
#include "polystore.h"

#define POLYSTORE_EPOCH_LOW_THRPUT 16
#define POLYSTORE_FILECOPY_THRES (16UL * 1024 * 1024 * 1024)

unsigned int sched_split_point = 0;
unsigned int sched_epoch_tic = 0;
unsigned int sched_fastdev_usage = 0;
unsigned long sched_aggre_throughput = 0;

uint64_t profile_fast_placed = 0;
uint64_t profile_slow_placed = 0;
char profile_g_stat_dump_fname[PATH_MAX];
FILE *profile_g_stat_dump_file = NULL;

/* Static, with stat only, no dynamic placement */
void polystore_placement_static_statonly(void) {
        struct l_task_ctx *task_ctx_list_node = NULL, *tmp = NULL;
        struct g_task_ctx *task_ctx = NULL;
        unsigned long throughput = 0;

        pthread_rwlock_rdlock(&task_ctx_list_lock);
        list_for_each_entry_safe(task_ctx_list_node, tmp, 
                                &task_ctx_list, list) {
                task_ctx = task_ctx_list_node->task_ctx;
                if (!task_ctx) {
                        ERROR_T("task ctx empty!");
                        continue;
                }
                throughput += task_ctx->throughput;
                task_ctx->last_throughput = task_ctx->throughput;
                task_ctx->throughput = 0;
                task_ctx->throughput_read = 0;
                task_ctx->throughput_write = 0;
        }
        pthread_rwlock_unlock(&task_ctx_list_lock);

#ifdef POLYSTORE_IO_STAT
	if (profile_g_stat_dump_file) {
                /* Add the epoch id, I/O amount to stat file */
		fprintf(profile_g_stat_dump_file, "epoch %d, I/O %ld\n", 
				sched_epoch_tic, throughput);
	}
#endif
        sched_aggre_throughput += throughput;

#ifdef POLYSTORE_FILECOPY
        if ((task_ctx->placement == BLK_PLACEMENT_FAST) && 
            (sched_aggre_throughput > POLYSTORE_FILECOPY_THRES)) {
                task_ctx->placement = BLK_PLACEMENT_SLOW;
        }
#endif

        __sync_fetch_and_add(&sched_epoch_tic, 1);
}


void polystore_placement_dynamic(void) {
        struct l_task_ctx *task_ctx_list_node = NULL, *tmp = NULL;
        struct g_task_ctx *task_ctx = NULL;
        unsigned long throughput = 0;

        pthread_rwlock_rdlock(&task_ctx_list_lock);
        list_for_each_entry_safe(task_ctx_list_node, tmp, 
                                &task_ctx_list, list) {
                task_ctx = task_ctx_list_node->task_ctx;
                if (!task_ctx) {
                        ERROR_T("task ctx empty!");
                        continue;
                }
                throughput += task_ctx->throughput;
                task_ctx->last_throughput = task_ctx->throughput;
                task_ctx->throughput = 0;

                if (task_ctx->placement == BLK_PLACEMENT_SLOW &&
                    __sync_fetch_and_add(&sched_fastdev_usage, 0) < sched_split_point) {
                        /* 
                         * Thread considered using slow storage
                         * seems not utlizing it well, hence
                         * promote to fast storage
                         */
                        __sync_fetch_and_add(&sched_fastdev_usage, 1);
                        task_ctx->placement = BLK_PLACEMENT_FAST;
                }

                if (task_ctx->last_throughput == 0) {
                        ++task_ctx->low_throughput_tic;
                } else {
                        task_ctx->low_throughput_tic = 0;
                }

                if (task_ctx->low_throughput_tic > POLYSTORE_EPOCH_LOW_THRPUT &&
                    task_ctx->placement == BLK_PLACEMENT_FAST) {
                        /* 
                         * Thread considered using fast storage
                         * seems not utlizing it well, hence
                         * demote to slow storage
                         */
                        task_ctx->placement = BLK_PLACEMENT_SLOW;
                        __sync_fetch_and_sub(&sched_fastdev_usage, 1);
                }

        }
        pthread_rwlock_unlock(&task_ctx_list_lock);

#ifdef POLYSTORE_IO_STAT
	if (profile_g_stat_dump_file) {
                /* Add the epoch id, I/O amount to stat file */
		fprintf(profile_g_stat_dump_file, "epoch %d, I/O %ld\n", 
				sched_epoch_tic, throughput);
	}
#endif
        sched_aggre_throughput += throughput;

        __sync_fetch_and_add(&sched_epoch_tic, 1);
}

#if 0
void polystore_placement_dynamic(void) {
        struct l_task_ctx *task_ctx_list_node = NULL, *tmp = NULL;
        struct g_task_ctx *task_ctx = NULL;
        unsigned long throughput = 0;

        return;
        pthread_rwlock_rdlock(&task_ctx_list_lock);
        list_for_each_entry_safe(task_ctx_list_node, tmp, 
                                &task_ctx_list, list) {
                task_ctx = task_ctx_list_node->task_ctx;
                if (!task_ctx) {
                        ERROR_T("task ctx empty!");
                        continue;
                }
                throughput += task_ctx->throughput;
                task_ctx->last_throughput = task_ctx->throughput;
                task_ctx->throughput = 0;
        }
        pthread_rwlock_unlock(&task_ctx_list_lock);

        pthread_rwlock_rdlock(&task_ctx_list_lock);
        list_for_each_entry_safe(task_ctx_list_node, tmp, 
                                &task_ctx_list, list) {
                task_ctx = task_ctx_list_node->task_ctx;
                if (!task_ctx) {
                        ERROR_T("task ctx empty!");
                        continue;
                }

                if (task_ctx->placement == BLK_PLACEMENT_SLOW &&
                    __sync_fetch_and_add(&sched_demote_nr, 0) > 0) {
                        /* 
                         * Thread considered using slow storage
                         * seems not utlizing it well, hence
                         * promote to fast storage
                         */
                        __sync_fetch_and_sub(&sched_demote_nr, 1);
                        task_ctx->placement = BLK_PLACEMENT_FAST;
                }

                if (task_ctx->last_throughput == 0) {
                        ++task_ctx->low_throughput_tic;
                } else {
                        task_ctx->low_throughput_tic = 0;
                }

                if (task_ctx->low_throughput_tic > POLYSTORE_EPOCH_LOW_THRPUT &&
                    task_ctx->placement == BLK_PLACEMENT_FAST) {
                        /* 
                         * Thread considered using fast storage
                         * seems not utlizing it well, hence
                         * demote to slow storage
                         */
                        task_ctx->placement = BLK_PLACEMENT_SLOW;
                        __sync_fetch_and_add(&sched_demote_nr, 1);
                }

        }
        pthread_rwlock_unlock(&task_ctx_list_lock);

#ifdef POLYSTORE_IO_STAT
	if (profile_g_stat_dump_file) {
                /* Add the epoch id, I/O amount to stat file */
		fprintf(profile_g_stat_dump_file, "epoch %d, I/O %ld\n", 
				sched_epoch_tic, throughput);
	}
#endif
        sched_aggre_throughput += throughput;

        __sync_fetch_and_add(&sched_epoch_tic, 1);
}
#endif
