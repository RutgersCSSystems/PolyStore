#define _GNU_SOURCE

#include <unistd.h>
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/types.h>

//#define FILE_SIZE 1048576UL
#define FILE_SIZE 16777216UL

#define SMALL_IO_SIZE 256
#define LARGE_IO_SIZE 8192

pthread_t *large_io_threads = NULL;

int *large_io_threads_idx = NULL;

int thread_nr = 32;

void* large_io_thread(void* arg) {
        char fname[PATH_MAX];
        char buf[LARGE_IO_SIZE];
        int fd = 0, i = 0;
        int id = *(int*)arg;
        int iter = FILE_SIZE / LARGE_IO_SIZE;

        memset(buf, 'a', LARGE_IO_SIZE);
        sprintf(fname, "/mnt/polystore/large%d.txt", id);

        fd = open(fname, O_CREAT | O_RDWR, 0666);

        for (i = 0; i < iter; ++i)
                write(fd, buf, LARGE_IO_SIZE);

        close(fd);

        return NULL;
}

/* fopen() version
void* large_io_thread(void* arg) {
        char fname[PATH_MAX];
        char buf[LARGE_IO_SIZE];
        int i = 0;
        int id = *(int*)arg;
        int iter = FILE_SIZE / LARGE_IO_SIZE;
        FILE *fp = NULL;

        memset(buf, 'a', LARGE_IO_SIZE);
        sprintf(fname, "/mnt/polystore/large%d.txt", id);

        fp = fopen(fname, "a");

        for (i = 0; i < iter; ++i)
                fwrite(buf, 1, LARGE_IO_SIZE, fp);

        fclose(fp);

        return NULL;
}
*/


int main(int argc, char** argv) {
        int i = 0;

        /* Syscall intercept coverage test */
        int fd = 0, ret = 0;
        off_t off = 0;
        char buf[4096];
        struct stat statbuf;

        /* creat */
        fd = creat("/mnt/polystore/testfile", 0666);
        if (fd < 0) {
                fprintf(stderr, "creat failed\n");
                exit(-1);
        }

        /* write */
        memset(buf, 'a', 1024);
        memset(buf+1024, 'b', 1024);
        memset(buf+2048, 'c', 1024);
        memset(buf+3072, 'd', 1024);
        ret = write(fd, buf, 4096);
        if (ret != 4096) {
                fprintf(stderr, "write failed ret %d\n", ret);
                exit(-1);
        }

        /* close */
        close(fd);

        /* open */
        fd = open("/mnt/polystore/testfile", O_RDWR, 0666);
        if (fd < 0) {
                fprintf(stderr, "open failed\n");
                exit(-1);
        }

        /* pread */
        ret = pread(fd, buf, 1024, 2048);
        if (ret != 1024 || buf[0] != 'c') {
                fprintf(stderr, "pread failed ret %d, buf %c\n", ret, buf[0]);
                exit(-1);
        }

        /* lseek */
        off = lseek(fd, 2048, SEEK_SET);
        if (off != 2048) {
                fprintf(stderr, "lseek failed\n");
                exit(-1);
        }

        /* pwrite */
        memset(buf, 'v', 1024);
        ret = pwrite(fd, buf, 1024, 2048);
        if (ret != 1024) {
                fprintf(stderr, "pwrite failed\n");
                exit(-1);
        }

        /* read */
        ret = read(fd, buf, 1024);
        if (ret != 1024 && buf[0] != 'v') {
                fprintf(stderr, "read failed\n");
                exit(-1);
        }

        /* fallocate */
        ret = fallocate(fd, FALLOC_FL_INSERT_RANGE, 4096, 4096);
        if (ret != 0) {
                fprintf(stderr, "fallocate failed\n");
                exit(-1);
        }

        /* ftruncate */
        ret = ftruncate(fd, 8192);
        if (ret != 0) {
                fprintf(stderr, "ftruncate failed\n");
                exit(-1);
        }

        /* fstat */
        fstat(fd, &statbuf);
        if (statbuf.st_size != 8192) {
                fprintf(stderr, "either fstat or ftruncate failed\n");
                exit(-1);
        }

        /* close */
        close(fd);

        /* rename */
        ret = rename("/mnt/polystore/testfile", "/mnt/polystore/testfilern");

        /* access */
        ret = access("/mnt/polystore/testfilern", R_OK | W_OK);
        if (ret != 0) {
                fprintf(stderr, "access failed\n");
                exit(-1);
        }

        /* truncate */
        ret = truncate("/mnt/polystore/testfilern", 16384);
        if (ret != 0) {
                fprintf(stderr, "truncate failed\n");
                exit(-1);
        }

        /* stat */
        stat("/mnt/polystore/testfilern", &statbuf);
        if (statbuf.st_size != 16384) {
                fprintf(stderr, "either stat or truncate failed\n");
                exit(-1);
        }

        /* unlink */
        ret = unlink("/mnt/polystore/testfilern");

        /* mkdir */
        ret = mkdir("/mnt/polystore/testdir", 0755);
        if (ret != 0) {
                fprintf(stderr, "mkdir failed\n");
                exit(-1);
        }

        /* rmdir */
        ret = rmdir("/mnt/polystore/testdir");
        if (ret != 0) {
                fprintf(stderr, "rmdir failed\n");
                exit(-1);
        }
        
        //return 0;

        /* Multi-thread test */
        large_io_threads = malloc(thread_nr*sizeof(pthread_t));
        large_io_threads_idx = malloc(thread_nr*sizeof(int));

        for (i = 0; i < thread_nr; ++i) {
                large_io_threads_idx[i] = i;
                pthread_create(&large_io_threads[i], NULL, large_io_thread, &large_io_threads_idx[i]);
        }

        for (i = 0; i < thread_nr; ++i) {
                pthread_join(large_io_threads[i], NULL);                
        }

        free(large_io_threads);
        free(large_io_threads_idx);

        return 0;
}
