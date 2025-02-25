#ifndef DS_H
#define DS_H

#include <pthread.h>

#include "shmht.h"
#include "interval_tree.h"

#define INODE_SHM_ADDR 0x0000005000000000
#define INDEX_SHM_ADDR 0x0000006000000000

#define SHM_SIZE 0x1000000

char semserver[32] = "server_sem";
char semclient[32] = "client_sem";
char ctrlshmpath[32] = "/tmp/shmctrl";
char shmhtpath[32] = "/tmp/shmht";
char inodeshmpath[32] = "/tmp/shminode";
char indexshmpath[32] = "/tmp/shmindex";

char filename[32] = "cmk";

typedef struct h_inode {
        char pathname[4096];
        int ino;
        pthread_rwlock_t tree_lock;
        pthread_rwlockattr_t tree_lock_attr;
        struct rb_root it_tree;
} h_inode_t;

struct it_node_entry {
        void *buf_addr;
        int no;
        pthread_mutex_t mutex;
        pthread_mutexattr_t mutex_attr;
        struct interval_tree_node it;
};

unsigned int hash(void *ptr) {
	char *str = (char*)ptr;
	unsigned int hash = 5381;
	int c;

	while (c = *str++)
		hash = ((hash << 5) + hash) + c; /* hash * 33 + c */

	return hash;
}

int equal(void *a, void *b) {
	char *astr = (char*)a;
	char *bstr = (char*)b;
	return strcmp(astr, bstr);
}

#endif
