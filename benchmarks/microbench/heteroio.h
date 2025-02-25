#ifndef HETERO_IO_H
#define HETERO_IO_H

#include <stdint.h>
#include <stdarg.h>

#define DEFAULT_FILE_SIZE 1073741824UL
//#define DEFAULT_FILE_SIZE 134217728UL

#define DEFAULT_PRIVATE_FILE_SIZE 1073741824UL
//#define DEFAULT_PRIVATE_FILE_SIZE 134217728UL
#define DEFAULT_SHARED_FILE_SIZE 1073741824UL

//#define DEFAULT_PRIVATE_FILE_SIZE 4294967296UL
//#define DEFAULT_SHARED_FILE_SIZE 17179869184UL
#define DEFAULT_THREAD_NR 32

#define DEFAULT_IO_SIZE 4096
#define BLOCK_SIZE 4096

#define DEFAULT_WORKLOAD READ_SEQ
#define DEFAULT_IO_TYPE SYNC_IO
#define DEFAULT_RAND_SEED DIST_NORMAL
#define DEFAULT_HETERO_FACTOR FAST_SLOW_ALL_FAST

#define FILEPERM 0666

extern char* FAST_DIR;
extern char* SLOW_DIR;
extern char* HETERO_DIR;

void DEBUG_T(const char* format, ... );

typedef enum {
	READ_SEQ = 1,
	WRITE_SEQ,
	READ_RAND,
	WRITE_RAND,
	MIX_RAND_3_1,
	MIX_RAND_1_1,
	MIX_RAND_1_3
} workload_t;

typedef enum {
	SYNC_IO = 1,
	DIRECT_IO,
	ASYNC_IO
} io_type_t;

typedef enum {
	DIST_NORMAL = 1,
	DIST_ZIPFAN
} random_seed_t;

typedef enum {
	FAST_SLOW_ALL_FAST = 1,
	FAST_SLOW_3_1,
	FAST_SLOW_1_1,
	FAST_SLOW_1_3,
	FAST_SLOW_ALL_SLOW
} hetero_factor_t;

typedef enum {
	FAST = 1,
	SLOW
} storage_device_t;

typedef struct thread_arg {
	int id;
	storage_device_t dev;	
	uint32_t io_size;
} thread_arg_t;

/* Configuration variables declaration */
extern uint32_t config_thread_nr;
extern uint32_t config_io_size;
extern uint32_t config_fsync_freq;
extern uint32_t config_sharefile;
extern uint64_t config_filesize;
extern workload_t config_workload;
extern io_type_t config_io_type;
extern random_seed_t config_rand_seed;
extern hetero_factor_t config_hetero_factor;

/* Workload functions */
void* do_seq_read(void* arg);
void* do_seq_write(void* arg);
void* do_rand_read(void* arg);
void* do_rand_write(void* arg);

/* Utility functions */
unsigned long capacity_stoul(char* origin);
int getargs(int argc, char **argv);

#endif	// HETERO_IO_H
