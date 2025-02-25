#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>                                                                                                                                                          
#include <errno.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>

#include "heteroio.h"

/* Global Variables */
uint32_t config_thread_nr = DEFAULT_THREAD_NR;
uint32_t config_io_size = DEFAULT_IO_SIZE;
uint32_t config_fsync_freq = 0;
uint32_t config_sharefile = 0;
uint64_t config_filesize = DEFAULT_PRIVATE_FILE_SIZE;
workload_t config_workload = DEFAULT_WORKLOAD;
io_type_t config_io_type = DEFAULT_IO_TYPE;
random_seed_t config_rand_seed = DEFAULT_RAND_SEED;
hetero_factor_t config_hetero_factor = DEFAULT_HETERO_FACTOR;

uint64_t *random_arr = NULL;

uint32_t fast_thread_nr = 0;
uint32_t slow_thread_nr = 0;

thread_arg_t *fast_thread_args = NULL;
thread_arg_t *slow_thread_args = NULL;
pthread_t *fast_threads = NULL;
pthread_t *slow_threads = NULL;

pthread_mutex_t fast_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t slow_mutex = PTHREAD_MUTEX_INITIALIZER;

double g_fast_throughput = 0.0;
double g_slow_throughput = 0.0;
double g_reader_throughput = 0.0;
double g_writer_throughput = 0.0;

double g_fast_latency = 0.0;
double g_slow_latency = 0.0;
double g_reader_latency = 0.0;
double g_writer_latency = 0.0;

/* Workload function pointer */
void* (*workload_fast[DEFAULT_THREAD_NR])(void*) = {NULL};
void* (*workload_slow[DEFAULT_THREAD_NR])(void*) = {NULL};

/* Random seed distribution function pointer */
void* (*rand_seed)(void) = NULL;

/* Calculate simulation time */
void simulation_time(struct timeval start, struct timeval end, 
                        double *sec, double *msec) {
	*sec = ((end.tv_sec + end.tv_usec * 1.0 / 1000000) -
			(start.tv_sec + start.tv_usec * 1.0 / 1000000));
	*msec = ((end.tv_sec * 1000000.0 + end.tv_usec * 1.0) -
			(start.tv_sec * 1000000.0 + start.tv_usec * 1.0));
}

/* Generate numbe of threads for FAST and NVMe */
void split_dev_threads() {
	switch (config_hetero_factor) {
		case FAST_SLOW_ALL_FAST:
			fast_thread_nr = config_thread_nr;
			slow_thread_nr = 0;
			break;

		case FAST_SLOW_3_1:
			fast_thread_nr = config_thread_nr / 4 * 3;
			slow_thread_nr = config_thread_nr / 4;
			break;

		case FAST_SLOW_1_1:
			fast_thread_nr = config_thread_nr / 2;
			slow_thread_nr = config_thread_nr / 2;
			break;

		case FAST_SLOW_1_3:
			fast_thread_nr = config_thread_nr / 4;
			slow_thread_nr = config_thread_nr / 4 * 3;
			break;

		case FAST_SLOW_ALL_SLOW:
			fast_thread_nr = 0;
			slow_thread_nr = config_thread_nr;
			break;

		default:
			fast_thread_nr = config_thread_nr;
			slow_thread_nr = 0;
	}
	printf("Allocate %d threads for FAST, %d threads for SLOW\n", fast_thread_nr, slow_thread_nr);
}

/* Set workload type */
void setup_workload() {
	int i = 0, c = 0, split = 0;

	switch(config_workload) {
		case READ_SEQ: // sequential read
			for (i = 0; i < fast_thread_nr; ++i) {
				workload_fast[i] = &do_seq_read;
				fast_thread_args[i].id = i;
				fast_thread_args[i].dev = FAST;
				fast_thread_args[i].io_size = config_io_size;
			}
			for (i = 0; i < slow_thread_nr; ++i) {
				workload_slow[i] = &do_seq_read;
				slow_thread_args[i].id = i;
				slow_thread_args[i].dev = SLOW;
				slow_thread_args[i].io_size = config_io_size;
			}
			printf("Running %d Sequential %d Byte Read threads Workload\n",
				config_thread_nr, config_io_size);
			break;

		case WRITE_SEQ: // sequential write
			for (i = 0; i < fast_thread_nr; ++i) {
				workload_fast[i] = &do_seq_write;
				fast_thread_args[i].id = i;
				fast_thread_args[i].dev = FAST;
				fast_thread_args[i].io_size = config_io_size;
			}
			for (i = 0; i < slow_thread_nr; ++i) {
				workload_slow[i] = &do_seq_write;
				slow_thread_args[i].id = i;
				slow_thread_args[i].dev = SLOW;
				slow_thread_args[i].io_size = config_io_size;
			}
			printf("Running %d Sequential %d Byte Write threads Workload\n",
				config_thread_nr, config_io_size);
			break;

		case READ_RAND: // random read
			for (i = 0; i < fast_thread_nr; ++i) {
				workload_fast[i] = &do_rand_read;
				fast_thread_args[i].id = i;
				fast_thread_args[i].dev = FAST;
				fast_thread_args[i].io_size = config_io_size;
			}
			for (i = 0; i < slow_thread_nr; ++i) {
				workload_slow[i] = &do_rand_read;
				slow_thread_args[i].id = i;
				slow_thread_args[i].dev = SLOW;
				slow_thread_args[i].io_size = config_io_size;
			}
			printf("Running %d Random %d Byte Read threads Workload\n",
				config_thread_nr, config_io_size);
			break;

		case WRITE_RAND: // random write
			for (i = 0; i < fast_thread_nr; ++i) {
				workload_fast[i] = &do_rand_write;
				fast_thread_args[i].id = i;
				fast_thread_args[i].dev = FAST;
				fast_thread_args[i].io_size = config_io_size;
			}
			for (i = 0; i < slow_thread_nr; ++i) {
				workload_slow[i] = &do_rand_write;
				slow_thread_args[i].id = i;
				slow_thread_args[i].dev = SLOW;
				slow_thread_args[i].io_size = config_io_size;
			}
			printf("Running %d Random %d Byte Write threads Workload\n",
				config_thread_nr, config_io_size);
			break;

		case MIX_RAND_3_1: // rand read, rand write 3:1
			split = config_thread_nr / 4 * 3;
			for (i = 0; i < fast_thread_nr; ++i) {
				if (c++ < split) {
					workload_fast[i] = &do_rand_read;
				} else {
					workload_fast[i] = &do_rand_write;
				}
				fast_thread_args[i].id = i;
				fast_thread_args[i].dev = FAST;
				fast_thread_args[i].io_size = config_io_size;
			}
			for (i = 0; i < slow_thread_nr; ++i) {
				if (c++ < split) {
					workload_slow[i] = &do_rand_read;
				} else {
					workload_slow[i] = &do_rand_write;
				}
				slow_thread_args[i].id = i;
				slow_thread_args[i].dev = SLOW;
				slow_thread_args[i].io_size = config_io_size;
			}
			printf("Running %d Random %d Byte Read threads, %d Random %d Byte Write threads Workload\n", 
				split, config_io_size, config_thread_nr - split, config_io_size);
			break;

		case MIX_RAND_1_1: // rand read, rand write 1:1
			split = config_thread_nr / 2;
			for (i = 0; i < fast_thread_nr; ++i) {
				if (c++ < split) {
					workload_fast[i] = &do_rand_read;
				} else {
					workload_fast[i] = &do_rand_write;
				}
				fast_thread_args[i].id = i;
				fast_thread_args[i].dev = FAST;
				fast_thread_args[i].io_size = config_io_size;
			}
			for (i = 0; i < slow_thread_nr; ++i) {
				if (c++ < split) {
					workload_slow[i] = &do_rand_read;
				} else {
					workload_slow[i] = &do_rand_write;
				}
				slow_thread_args[i].id = i;
				slow_thread_args[i].dev = SLOW;
				slow_thread_args[i].io_size = config_io_size;
			}
			printf("Running %d Random %d Byte Read threads, %d Random %d Byte Write threads Workload\n", 
				split, config_io_size, config_thread_nr - split, config_io_size);
			break;

		case MIX_RAND_1_3: // rand read, rand write 1:3
			split = config_thread_nr / 4;
			for (i = 0; i < fast_thread_nr; ++i) {
				if (c++ < split) {
					workload_fast[i] = &do_rand_read;
				} else {
					workload_fast[i] = &do_rand_write;
				}
				fast_thread_args[i].id = i;
				fast_thread_args[i].dev = FAST;
				fast_thread_args[i].io_size = config_io_size;
			}
			for (i = 0; i < slow_thread_nr; ++i) {
				if (c++ < split) {
					workload_slow[i] = &do_rand_read;
				} else {
					workload_slow[i] = &do_rand_write;
				}
				slow_thread_args[i].id = i;
				slow_thread_args[i].dev = SLOW;
				slow_thread_args[i].io_size = config_io_size;
			}
			printf("Running %d Random %d Byte Read threads, %d Random %d Byte Write threads Workload\n", 
				split, config_io_size, config_thread_nr - split, config_io_size);
			break;

		default:
			for (i = 0; i < fast_thread_nr; ++i) {
				workload_fast[i] = &do_seq_read;
				fast_thread_args[i].id = i;
				fast_thread_args[i].dev = FAST;
				fast_thread_args[i].io_size = config_io_size;
			}
			for (i = 0; i < slow_thread_nr; ++i) {
				workload_slow[i] = &do_seq_read;
				slow_thread_args[i].id = i;
				slow_thread_args[i].dev = SLOW;
				slow_thread_args[i].io_size = config_io_size;
			}
			printf("Running Sequential Read Workload as default\n");
	}
}

/* Populate testing dir files */
void populate_testing_files() {
	int status = 0;
	int i = 0, j = 0, iter = config_filesize / DEFAULT_IO_SIZE;
	int fd = 0;
	char buf[DEFAULT_IO_SIZE];
	char fname[255];

	/* Make testing dir */
	remove(FAST_DIR);
	if ((status = mkdir(FAST_DIR, 0755)) < 0) {
		printf("Error. Failed to create testing dir for FAST\n");
		return;
	}
	remove(SLOW_DIR);
	if ((status = mkdir(SLOW_DIR, 0755)) < 0) {
		printf("Error. Failed to create testing dir for SLOW\n");
		return;
	}

	/* Populate files */
	for (i = 0; i < fast_thread_nr; ++i) {
		sprintf(fname, "%s/thread_%d", FAST_DIR, i);
		if ((fd = open(fname, O_CREAT | O_RDWR, FILEPERM)) < 0) {
			printf("Error. Failed to create file %s, %d\n", fname, fd);
			return;
		}
		for (j = 0; j < iter; ++j) {
			write(fd, buf, DEFAULT_IO_SIZE);
		}
		printf("Populating testing file %s for FAST with size %lu\n", fname, config_filesize);
		fsync(fd);
		close(fd);
	}

	for (i = 0; i < slow_thread_nr; ++i) {
		sprintf(fname, "%s/thread_%d", SLOW_DIR, i);
		if ((fd = open(fname, O_CREAT | O_RDWR, FILEPERM)) < 0) {
			printf("Error. Failed to create file %s\n", fname);
			return;
		}
		for (j = 0; j < iter; ++j) {
			write(fd, buf, DEFAULT_IO_SIZE);
		}
		printf("Populating testing file %s for SLOW with size %lu\n", fname, config_filesize);
		fsync(fd);
		close(fd);
	}

	/* Clear system-wide page cache */
	sync();
	fd = open("/proc/sys/vm/drop_caches", O_WRONLY);
	if (fd <= 0) {
		printf("Failed to clear cache with fd %d!\n", fd);
		return;
	}

	int ret = write(fd, "3", 1);
	if (ret != 1) {
		printf("Failed to clear cache!\n");
		return;
	}
	close(fd);	
	printf("Clear page cache for populated files\n");
}

/* Removing testing dir and files */
void remove_testing_files() {
	char fname[255];
	int i = 0;

	for (i = 0; i < fast_thread_nr; ++i) {
		sprintf(fname, "%s/thread_%d", FAST_DIR, i);
		remove(fname);
	}

	for (i = 0; i < slow_thread_nr; ++i) {
		sprintf(fname, "%s/thread_%d", SLOW_DIR, i);
		remove(fname);
	}

	remove(FAST_DIR);
	remove(SLOW_DIR);
	printf("Removing testing dir and files\n");
}

/* Workload functions */
void* do_seq_read(void* arg) {
	char fname[255];
	char *buf = NULL;
	int fd = 0, flags = O_RDWR;
	int i = 0, j = 0;
	double sec = 0.0, msec = 0.0;
	double throughput = 0.0, latency = 0.0;
	struct timeval start_t, end_t;
	thread_arg_t argument = *(thread_arg_t*)arg;
	pthread_mutex_t *g_mutex = NULL;
	double *g_throughput = NULL, *g_latency = NULL;
	uint32_t io_size = argument.io_size;
	int ops = 0;

	if (config_sharefile == 0)
		ops = config_filesize / io_size;
	else
		ops = config_filesize / io_size / config_thread_nr;

	if (argument.dev == FAST) {
		if (config_sharefile == 1)
			sprintf(fname, "%s/shared", FAST_DIR);
		else
			sprintf(fname, "%s/thread_%d", FAST_DIR, argument.id);
		g_throughput = &g_fast_throughput;
                g_latency = &g_fast_latency;
		g_mutex = &fast_mutex;	
	} else {
		if (config_sharefile == 1)
			sprintf(fname, "%s/shared", SLOW_DIR);
		else
			sprintf(fname, "%s/thread_%d", SLOW_DIR, argument.id);
		g_throughput = &g_slow_throughput;
                g_latency = &g_slow_latency;
		g_mutex = &slow_mutex;	
	}
	DEBUG_T("DEV %d with id %d, running seq_read with io_size %d\n", argument.dev, argument.id, argument.io_size);

	if (config_io_type == DIRECT_IO) {
		flags |= O_DIRECT;
	}

	if ((fd = open(fname, flags, FILEPERM)) < 0) {
		printf("Error. Failed to open file %s\n", fname);
		return NULL;
	}

	if (config_sharefile == 1) {
		off_t pos = (off_t)ops*argument.id*io_size;
		lseek(fd, pos, SEEK_SET);
	}

	//buf = malloc(io_size*sizeof(char));
	posix_memalign((void**)&buf, 4096, io_size*sizeof(char));
	memset(buf, 'c', io_size);

	gettimeofday(&start_t, NULL);

	for (i = 0; i < ops; i++) {
		if (read(fd, buf, io_size) != io_size){
			printf("Error. Failed to read file %s\n", fname);
			return NULL;
		}
	}

	gettimeofday(&end_t, NULL);
	simulation_time(start_t, end_t, &sec, &msec);
        latency = msec / (double)ops;
	if (config_sharefile == 0) {
		throughput = (double)config_filesize / sec;
	} else {
		throughput = (double)config_filesize / config_thread_nr / sec;
        }

	pthread_mutex_lock(g_mutex);
	*g_throughput += throughput;
        *g_latency += latency;
	pthread_mutex_unlock(g_mutex);

        close(fd);                                                                                                                                                      
        free(buf);

	return NULL;
}


void* do_seq_write(void* arg) {
	char fname[255];
	char *buf = NULL;
	int fd = 0, flags = O_RDWR;
	int i = 0, j = 0;
	double sec = 0.0, msec = 0.0;
	double throughput = 0.0, latency = 0.0;
	struct timeval start_t, end_t;
	thread_arg_t argument = *(thread_arg_t*)arg;
	pthread_mutex_t *g_mutex = NULL;
	double *g_throughput = NULL, *g_latency = NULL;
	uint32_t io_size = argument.io_size;
	int ops = 0;

	if (config_sharefile == 0)
		ops = config_filesize / io_size;
	else
		ops = config_filesize / io_size / config_thread_nr;

	if (argument.dev == FAST) {
		if (config_sharefile == 1)
			sprintf(fname, "%s/shared", FAST_DIR);
		else
			sprintf(fname, "%s/thread_%d", FAST_DIR, argument.id);
		g_throughput = &g_fast_throughput;
                g_latency = &g_fast_latency;
		g_mutex = &fast_mutex;	
	} else {
		if (config_sharefile == 1)
			sprintf(fname, "%s/shared", SLOW_DIR);
		else
			sprintf(fname, "%s/thread_%d", SLOW_DIR, argument.id);
		g_throughput = &g_slow_throughput;
                g_latency = &g_slow_latency;
		g_mutex = &slow_mutex;	
	}
	DEBUG_T("DEV %d with id %d, running seq_write with io_size %d\n", argument.dev, argument.id, argument.io_size);

	if (config_io_type == DIRECT_IO) {
		flags |= O_DIRECT;
	}

	if ((fd = open(fname, flags, FILEPERM)) < 0) {
		printf("Error. Failed to open file %s\n", fname);
		return NULL;
	}

	if (config_sharefile == 1) {
		off_t pos = (off_t)ops*argument.id*io_size;
		lseek(fd, pos, SEEK_SET);
	}

	//buf = malloc(io_size*sizeof(char));
	posix_memalign((void**)&buf, 4096, io_size*sizeof(char));
	memset(buf, 'c', io_size);

	gettimeofday(&start_t, NULL);

	for (i = 0; i < ops; i++) {
		if (write(fd, buf, io_size) != io_size){
			printf("Error. Failed to write file %s\n", fname);
			return NULL;
		}
		if ((config_fsync_freq > 0) && ((i & (config_fsync_freq - 1)) == 0)) {
			fsync(fd);
		}
	}

	gettimeofday(&end_t, NULL);
	simulation_time(start_t, end_t, &sec, &msec);
        latency = msec / (double)ops;
	if (config_sharefile == 0) {
		throughput = (double)config_filesize / sec;
	} else {
		throughput = (double)config_filesize / config_thread_nr / sec;
        }

	pthread_mutex_lock(g_mutex);
	*g_throughput += throughput;
        *g_latency += latency;
	pthread_mutex_unlock(g_mutex);

        close(fd);                                                                                                                                                      
        free(buf);

	return NULL;
}


void* do_rand_read(void* arg) {
	char fname[255];
	char *buf = NULL;
	int fd = 0, flags = O_RDWR;
	int i = 0, j = 0;
	loff_t offset = 0;
	double sec = 0.0, msec = 0.0;
	double throughput = 0.0, latency = 0.0;
	struct timeval start_t, end_t;
	thread_arg_t argument = *(thread_arg_t*)arg;
	pthread_mutex_t *g_mutex = NULL;
	double *g_throughput = NULL, *g_latency = NULL;
	uint32_t io_size = argument.io_size;
	int ops = 0;

	if (config_sharefile == 0)
		ops = config_filesize / io_size;
	else
		ops = config_filesize / io_size / config_thread_nr;

	if (argument.dev == FAST) {
		if (config_sharefile == 1)
			sprintf(fname, "%s/shared", FAST_DIR);
		else
			sprintf(fname, "%s/thread_%d", FAST_DIR, argument.id);
		g_throughput = &g_fast_throughput;
                g_latency = &g_fast_latency;
		g_mutex = &fast_mutex;	
	} else {
		if (config_sharefile == 1)
			sprintf(fname, "%s/shared", SLOW_DIR);
		else
			sprintf(fname, "%s/thread_%d", SLOW_DIR, argument.id);
		g_throughput = &g_slow_throughput;
                g_latency = &g_slow_latency;
		g_mutex = &slow_mutex;	
	}
	DEBUG_T("DEV %d with id %d, running rand_read with io_size %d\n", argument.dev, argument.id, argument.io_size);
	//printf("id %d, running %d\n", argument.id, ops);

	if (config_io_type == DIRECT_IO) {
		flags |= O_DIRECT;
	}

	if ((fd = open(fname, flags, FILEPERM)) < 0) {
		printf("Error. Failed to open file %s\n", fname);
		return NULL;
	}

	//buf = malloc(io_size*sizeof(char));
	posix_memalign((void**)&buf, 4096, io_size*sizeof(char));
	memset(buf, 'c', io_size);

	gettimeofday(&start_t, NULL);

	for (i = 0; i < ops; i++) {
		if (config_sharefile == 0) {
			//offset = (rand() % ops) * io_size;
			//offset = (rand() % (ops >> 1)) * io_size;
			offset = random_arr[i] * io_size;
		} else {
			offset = (ops * argument.id + rand() % ops) * io_size;
		} 
		if (pread(fd, buf, io_size, offset) != io_size){
			printf("Error. Failed to pread file %s\n", fname);
			return NULL;
		}
	}

	gettimeofday(&end_t, NULL);
	simulation_time(start_t, end_t, &sec, &msec);
        latency = msec / (double)ops;
	if (config_sharefile == 0) {
		throughput = (double)config_filesize / sec;
	} else {
		throughput = (double)config_filesize / config_thread_nr / sec;
        }

	pthread_mutex_lock(g_mutex);
	*g_throughput += throughput;
	g_reader_throughput += throughput;
        *g_latency += latency;
	pthread_mutex_unlock(g_mutex);

        close(fd); 
        free(buf);

	return NULL;
}


void* do_rand_write(void* arg) {
	char fname[255];
	char *buf = NULL;
	int fd = 0, flags = O_RDWR;
	int i = 0, j = 0;
	loff_t offset = 0;
	double sec = 0.0, msec = 0.0;
	double throughput = 0.0, latency = 0.0;
	struct timeval start_t, end_t;
	thread_arg_t argument = *(thread_arg_t*)arg;
	pthread_mutex_t *g_mutex = NULL;
	double *g_throughput = NULL, *g_latency = NULL;
	uint32_t io_size = argument.io_size;
	int ops = 0;

	if (config_sharefile == 0)
		ops = config_filesize / io_size;
	else
		ops = config_filesize / io_size / config_thread_nr;

	if (argument.dev == FAST) {
		if (config_sharefile == 1)
			sprintf(fname, "%s/shared", FAST_DIR);
		else
			sprintf(fname, "%s/thread_%d", FAST_DIR, argument.id);
		g_throughput = &g_fast_throughput;
                g_latency = &g_fast_latency;
		g_mutex = &fast_mutex;	
	} else {
		if (config_sharefile == 1)
			sprintf(fname, "%s/shared", SLOW_DIR);
		else
			sprintf(fname, "%s/thread_%d", SLOW_DIR, argument.id);
		g_throughput = &g_slow_throughput;
                g_latency = &g_slow_latency;
		g_mutex = &slow_mutex;	
	}
	DEBUG_T("DEV %d with id %d, running rand_write with io_size %d\n", argument.dev, argument.id, argument.io_size);

	if (config_io_type == DIRECT_IO) {
		flags |= O_DIRECT;
	}

	if ((fd = open(fname, flags, FILEPERM)) < 0) {
		printf("Error. Failed to open file %s\n", fname);
		return NULL;
	}

	//buf = malloc(io_size*sizeof(char));
	posix_memalign((void**)&buf, BLOCK_SIZE, io_size*sizeof(char));
	memset(buf, 'c', io_size);

	gettimeofday(&start_t, NULL);

	for (i = 0; i < ops; i++) {
		if (config_sharefile == 0) {
			//offset = (rand() % ops) * io_size;
			offset = random_arr[i] * io_size;
		} else {
			offset = (ops * argument.id + rand() % ops) * io_size;
		}
		if (pwrite(fd, buf, io_size, offset) != io_size){
			printf("Error. Failed to pwrite file %s\n", fname);
			return NULL;
		}
		if ((config_fsync_freq > 0) && ((i & (config_fsync_freq - 1)) == 0)) {
			fsync(fd);
		}
	}

	gettimeofday(&end_t, NULL);
	simulation_time(start_t, end_t, &sec, &msec);
        latency = msec / (double)ops;
	if (config_sharefile == 0) {
		throughput = (double)config_filesize / sec;
	} else {
		throughput = (double)config_filesize / config_thread_nr / sec;
        }

	pthread_mutex_lock(g_mutex);
	*g_throughput += throughput;
	g_writer_throughput += throughput;
        *g_latency += latency;
	pthread_mutex_unlock(g_mutex);

        close(fd);                                                                                                                                                      
        free(buf);

	return NULL;
}

void setup_random_arr() {
	int N = config_filesize / config_io_size;
	int i = 0, idx = 0;
	uint64_t tmp = 0;

	// Initialize this arrary
	/*random_arr = mmap(0, N*sizeof(uint64_t), PROT_READ | PROT_WRITE, 
				MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);*/
	random_arr = malloc(N*sizeof(uint64_t));
	for (i = 0; i < N; ++i)
		random_arr[i] = i;

	// Generate the number
	for (int i = 0; i < N; ++i) {
		idx = i + rand() % (N - i);
		tmp = random_arr[i];
		random_arr[i] = random_arr[idx];
		random_arr[idx] = tmp;
	}
}

void release_random_arr() {
	int N = config_filesize / config_io_size;
	
	// Release this array
	//munmap(random_arr, N*sizeof(uint64_t));
	free(random_arr);
}

int main(int argc, char **argv) {
	struct timeval start, end;	
	double sec = 0.0, msec = 0.0;
	int i = 0, j = 0;
	
	/* Parse input arguments */
	if (argc < 2) {
		printf("Incorrect number of arguments \n");
		exit(-1);
	}
	getargs(argc, argv);

	/* Get mount point for FAST and SLOW from env */
	FAST_DIR = getenv("FAST_DIR");
	SLOW_DIR = getenv("SLOW_DIR");

	/* Perform benchmark for each thread */
	fast_thread_args = malloc(config_thread_nr*sizeof(thread_arg_t));
	slow_thread_args = malloc(config_thread_nr*sizeof(thread_arg_t));
	fast_threads = malloc(config_thread_nr*sizeof(pthread_t));
	slow_threads = malloc(config_thread_nr*sizeof(pthread_t));	

	/* Generate numbe of threads for FAST and NVMe */
	split_dev_threads();

	/* Populate testing dir and files */
	//populate_testing_files();

	/* Set workload type */
	setup_workload();

	/* Set random arr for random workload */
	setup_random_arr();

	/* Start global timing */
	gettimeofday(&start, NULL);

	/* Run the actual workloads */
	for (i = 0; i < fast_thread_nr; ++i) {
		pthread_create(&fast_threads[i], NULL, workload_fast[i], &fast_thread_args[i]);
	}

	for (i = 0; i < slow_thread_nr; ++i) {
		pthread_create(&slow_threads[i], NULL, workload_slow[i], &slow_thread_args[i]);
	}

	for (i = 0; i < fast_thread_nr; ++i) {
		pthread_join(fast_threads[i], NULL);
	}

	for (i = 0; i < slow_thread_nr; ++i) {
		pthread_join(slow_threads[i], NULL);
	}

        gettimeofday(&end, NULL);
        simulation_time(start, end, &sec, &msec);
        printf("Benchmark takes %.2lf s\n", sec);
        printf("aggregated FAST thruput %.2lf MB/s\n", g_fast_throughput / 1024 / 1024);
        printf("aggregated SLOW thruput %.2lf MB/s\n", g_slow_throughput / 1024 / 1024);
	if (config_workload > 4) {
		printf("aggregated reader thruput %.2lf MB/s\n", g_reader_throughput / 1024 / 1024);
		printf("aggregated writer thruput %.2lf MB/s\n", g_writer_throughput / 1024 / 1024);
	}	
        printf("average FAST latency %.2lf us\n", g_fast_latency / config_thread_nr);
        printf("average SLOW latency %.2lf us\n", g_slow_latency / config_thread_nr);

        printf("Benchmark completed \n");

	/* End global timing */
	gettimeofday(&end, NULL);

	free(fast_threads);
	free(slow_threads);
	free(fast_thread_args);
	free(slow_thread_args);

	/* Remove testing dir and files */
	//remove_testing_files();

	/* Release random arr for random workload */
	release_random_arr();

	return 0;
}
