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
#include <sys/syscall.h>

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
const char *shell_cache_policy = NULL;

uint64_t *random_arr = NULL;

thread_arg_t *thread_args = NULL;
pthread_t *threads = NULL;

pthread_mutex_t g_mutex = PTHREAD_MUTEX_INITIALIZER;

double g_throughput = 0.0;
double g_reader_throughput = 0.0;
double g_writer_throughput = 0.0;
double g_latency = 0.0;

/* Workload function pointer */
void* (*workload[DEFAULT_THREAD_NR])(void*) = {NULL};

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

/* Set workload type */
void setup_write_workload() {
	int i = 0, c = 0, split = 0;

	split = config_thread_nr / 2;
	for (i = 0; i < config_thread_nr; ++i) {
		if (c++ < split) {
			workload[i] = &do_seq_write;
			thread_args[i].io_size = 1024*1024;
		} else {
			workload[i] = &do_rand_write;
			thread_args[i].io_size = 256;
		}
		thread_args[i].id = i;
	}
	printf("Running %d Sequential %d Byte Write threads, %d Random %d Byte Write threads Workload\n", 
		split, 1024*1024, config_thread_nr - split, 256);

}

void setup_read_workload() {
	int i = 0;

	for (i = 0; i < config_thread_nr; ++i) {
		workload[i] = &do_rand_read;
		thread_args[i].io_size = config_io_size;
		thread_args[i].id = i;
	}
	printf("Running %d Random %d Byte Read threads Workload\n", config_thread_nr, config_io_size);

}

/* Set workload type */
void setup_workload() {
	int i = 0, c = 0, split = 0;

	switch(config_workload) {
		case READ_SEQ: // sequential read
  			for (i = 0; i < config_thread_nr; ++i) {
				workload[i] = &do_seq_read;
				thread_args[i].id = i;
				thread_args[i].io_size = config_io_size;
			}
			printf("Running %d Sequential %d Byte Read threads Workload\n",
				config_thread_nr, config_io_size);
			break;

		case WRITE_SEQ: // sequential write
  			for (i = 0; i < config_thread_nr; ++i) {
				workload[i] = &do_seq_write;
				thread_args[i].id = i;
				thread_args[i].io_size = config_io_size;
			}
			printf("Running %d Sequential %d Byte Write threads Workload\n",
				config_thread_nr, config_io_size);
			break;

		case READ_RAND: // random read
  			for (i = 0; i < config_thread_nr; ++i) {
				workload[i] = &do_rand_read;
				thread_args[i].id = i;
				thread_args[i].io_size = config_io_size;
			}
			printf("Running %d Random %d Byte Read threads Workload\n",
				config_thread_nr, config_io_size);

			break;

		case WRITE_RAND: // random write
  			for (i = 0; i < config_thread_nr; ++i) {
				workload[i] = &do_rand_write;
				thread_args[i].id = i;
				thread_args[i].io_size = config_io_size;
			}
			printf("Running %d Random %d Byte Write threads Workload\n",
				config_thread_nr, config_io_size);
			break;

		case MIX_RAND_3_1: // rand read, rand write 3:1
			split = config_thread_nr / 4 * 3;
			for (i = 0; i < config_thread_nr; ++i) {
				if (c++ < split) {
					workload[i] = &do_rand_read;
				} else {
					workload[i] = &do_rand_write;
				}
				thread_args[i].id = i;
				thread_args[i].io_size = config_io_size;
			}
			printf("Running %d Random %d Byte Read threads, %d Random %d Byte Write threads Workload\n",
				split, config_io_size, config_thread_nr - split, config_io_size);
			break;

		case MIX_RAND_1_1: // rand read, rand write 1:1
			split = config_thread_nr / 2;
			for (i = 0; i < config_thread_nr; ++i) {
				if (c++ < split) {
					workload[i] = &do_rand_read;
				} else {
					workload[i] = &do_rand_write;
				}
				thread_args[i].id = i;
				thread_args[i].io_size = config_io_size;
			}
			printf("Running %d Random %d Byte Read threads, %d Random %d Byte Write threads Workload\n",
				split, config_io_size, config_thread_nr - split, config_io_size);
			break;

		case MIX_RAND_1_3: // rand read, rand write 1:3
			split = config_thread_nr / 4;
			for (i = 0; i < config_thread_nr; ++i) {
				if (c++ < split) {
					workload[i] = &do_rand_read;
				} else {
					workload[i] = &do_rand_write;
				}
				thread_args[i].id = i;
				thread_args[i].io_size = config_io_size;
			}
			printf("Running %d Random %d Byte Read threads, %d Random %d Byte Write threads Workload\n",
				split, config_io_size, config_thread_nr - split, config_io_size);
			break;

		default:
  			for (i = 0; i < config_thread_nr; ++i) {
				workload[i] = &do_seq_read;
				thread_args[i].id = i;
				thread_args[i].io_size = config_io_size;
			}
			printf("Running Sequential Read Workload as default\n");
	}
}

/* Clear system-wide page cache programatically */
void clear_page_cache() {
	/* Clear system-wide page cache */
	sync();
	int fd = open("/proc/sys/vm/drop_caches", O_WRONLY);
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
	printf("Clear system-wide page cache\n");
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
	uint32_t io_size = argument.io_size;
	int ops = 0;

	if (config_sharefile == 0) {
		ops = config_filesize / io_size;
		sprintf(fname, "%s/thread_%d", HETERO_DIR, argument.id);
	} else {
		ops = config_filesize / io_size / config_thread_nr;
		sprintf(fname, "%s/shared", HETERO_DIR);
	}

	DEBUG_T("id %d, running seq_read with io_size %d\n", argument.id, argument.io_size);

	if (config_io_type == DIRECT_IO) {
		flags |= O_DIRECT;
	}

	if ((fd = open(fname, flags, FILEPERM)) < 0) {
		printf("Error. Failed to open file %s\n", fname);
		return NULL;
	}

	if (config_sharefile == 1)
		lseek(fd, ops*argument.id*io_size, SEEK_SET);

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

	pthread_mutex_lock(&g_mutex);
	g_throughput += throughput;
        g_latency += latency;
	pthread_mutex_unlock(&g_mutex);

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
	uint32_t io_size = argument.io_size;
	int ops = 0;

	if (config_sharefile == 0) {
		ops = config_filesize / io_size;
		sprintf(fname, "%s/thread_%d", HETERO_DIR, argument.id);
	} else {
		ops = config_filesize / io_size / config_thread_nr;
		sprintf(fname, "%s/shared", HETERO_DIR);
	}

	DEBUG_T("id %d, running seq_write with io_size %d\n", argument.id, argument.io_size);

	if (config_io_type == DIRECT_IO) {
		flags |= O_DIRECT;
	}

	if ((fd = open(fname, flags, FILEPERM)) < 0) {
		printf("Error. Failed to open file %s\n", fname);
		return NULL;
	}

 	if (config_sharefile == 1)
		lseek(fd, ops*argument.id*io_size, SEEK_SET);

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

	pthread_mutex_lock(&g_mutex);
	g_throughput += throughput;
        g_latency += latency;
	pthread_mutex_unlock(&g_mutex);

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
	uint32_t io_size = argument.io_size;
	int ops = 0;

	if (config_sharefile == 0) {
		ops = config_filesize / io_size;
		sprintf(fname, "%s/thread_%d", HETERO_DIR, argument.id);
	} else {
		ops = config_filesize / io_size / config_thread_nr;
		sprintf(fname, "%s/shared", HETERO_DIR);
	}

	DEBUG_T("id %d, running rand_read with io_size %d\n", argument.id, argument.io_size);

	if (config_io_type == DIRECT_IO) {
		flags |= O_DIRECT;
	}

	//printf("thread %d starts\n", argument.id);

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
		if (pread(fd, buf, io_size, offset) != io_size) {
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

	pthread_mutex_lock(&g_mutex);
	g_throughput += throughput;
	g_reader_throughput += throughput;
        g_latency += latency;
	pthread_mutex_unlock(&g_mutex);

        close(fd); 
        free(buf);

	//printf("thread %d finishes\n", argument.id);
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
	uint32_t io_size = argument.io_size;
	int ops = 0;

	if (config_sharefile == 0) {
		ops = config_filesize / io_size;
		sprintf(fname, "%s/thread_%d", HETERO_DIR, argument.id);
	} else {
		ops = config_filesize / io_size / config_thread_nr;
		sprintf(fname, "%s/shared", HETERO_DIR);
	}

	DEBUG_T("id %d, running rand_write with io_size %d\n", argument.id, argument.io_size);

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

	pthread_mutex_lock(&g_mutex);
	g_throughput += throughput;
	g_writer_throughput += throughput;
        g_latency += latency;
	pthread_mutex_unlock(&g_mutex);

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

#if 0
	/* Disable kernel inode rwsem */
	if (config_workload != WRITE_SEQ) {
		if (syscall(436, 0xFF) == 0xFF)
			printf("Successfully disable inode rwsem\n");
		else
			printf("Failed disable inode rwsem\n");
	}
#endif

	/* Get mount point for HETERO from env */
	HETERO_DIR = getenv("HETERO_DIR");

	/* Perform benchmark for each thread */
	thread_args = malloc(config_thread_nr*sizeof(thread_arg_t));
	threads = malloc(config_thread_nr*sizeof(pthread_t));

	/*** Clear system-wide page cache ***/
	//clear_page_cache();
	g_throughput = 0.0;

	/* Set workload type */
	setup_workload();

	/* Set random arr for random workload */
	setup_random_arr();

	/* Start global timing */
	gettimeofday(&start, NULL);

	/* Run the actual workloads */
	for (i = 0; i < config_thread_nr; ++i) {
		pthread_create(&threads[i], NULL, workload[i], &thread_args[i]);
	}

	for (i = 0; i < config_thread_nr; ++i) {
		pthread_join(threads[i], NULL);
	}

	/* End global timing */
	gettimeofday(&end, NULL);
	simulation_time(start, end, &sec, &msec);
	printf("Benchmark takes %.2lf s\n", sec);
	printf("aggregated thruput %.2lf MB/s\n", g_throughput / 1024 / 1024);
        if (config_workload > 4) {
                printf("aggregated reader thruput %.2lf MB/s\n", g_reader_throughput / 1024 / 1024);
                printf("aggregated writer thruput %.2lf MB/s\n", g_writer_throughput / 1024 / 1024);
        }
        printf("average latency %.2lf us\n", g_latency / config_thread_nr);

	free(threads);
	free(thread_args);

	/* Release random arr for random workload */
	release_random_arr();

	return 0;
}
