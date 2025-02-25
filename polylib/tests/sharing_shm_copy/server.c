#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <semaphore.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>

#include "ds.h"

void *config_param = NULL;
void *inode_addr = NULL;
void *index_addr = NULL;
void *ctrl_addr = NULL;
int *next_avail_index = NULL;

int main() {
        key_t ctrl_key, inode_key, index_key;
        int ctrl_shmid, inode_shmid, index_shmid; 
        size_t ret_size;
        sem_t *sem_server_id = NULL, *sem_client_id = NULL;
        struct shmht *ht = NULL;

        h_inode_t *inode = NULL;
        struct it_node_entry *it_entry = NULL;
        struct interval_tree_node *it_node = NULL;

        printf("Hello Server\n");

        /* Initialize Semaphore */
        sem_server_id = sem_open(semserver, O_CREAT, 0600, 0);
        if (sem_server_id == SEM_FAILED) {
                printf("Server:sem_open error\n");
                exit(1);
        }
        sem_client_id = sem_open(semclient, O_CREAT, 0600, 0);
        if (sem_client_id == SEM_FAILED) {
                printf("Server:sem_open error\n");
                exit(1);
        }

        /* Get the control_params */
        if (-1 == (ctrl_key = ftok(ctrlshmpath, 'C'))) {
                printf("Server:ftok error ctrl_key\n");
                exit(1);
        }

        if (-1 == (ctrl_shmid = shmget(ctrl_key, 4096, 0644 | IPC_CREAT))) {
                printf("Server:shmget error ctrl_key\n");
                exit(1);
        }       

        if ((ctrl_addr = shmat(ctrl_shmid, NULL, 0)) == (void*)-1) {
                printf("Server:shmat error ctrl_key\n");
                exit(1);
        }
        printf("Server mmap ctrl param at %p\n", ctrl_addr);
        next_avail_index = ctrl_addr; 

        /* Get the inode shm at the fixed address */
        if (-1 == (inode_key = ftok(inodeshmpath, 'I'))) {
                printf("Server:ftok error inode_key\n");
                exit(1);
        }

        if (-1 == (inode_shmid = shmget(inode_key, SHM_SIZE, 0644 | IPC_CREAT))) {
                printf("Server:shmget error inode_key\n");
                exit(1);
        }       

        if ((inode_addr = shmat(inode_shmid, (void*)INODE_SHM_ADDR, 0)) == (void*)-1) {
                printf("Server:shmat error inode_key\n");
                exit(1);
        }
        printf("Server mmap inode at %p\n", inode_addr);

        /* Get the index shm at the fixed address */
         if (-1 == (index_key = ftok(indexshmpath, 'X'))) {
                printf("Server:ftok error index_key\n");
                exit(1);
        }

        if (-1 == (index_shmid = shmget(index_key, SHM_SIZE, 0644 | IPC_CREAT))) {
                printf("Server:shmget error index_key\n");
                exit(1);
        }       

        if ((index_addr = shmat(index_shmid, (void*)INDEX_SHM_ADDR, 0)) == (void*)-1) {
                printf("Server:shmat error index_key\n");
                exit(1);
        }
        printf("Server mmap index at %p\n", index_addr);

        /* Initialize inode shmht */
        ht = create_shmht(shmhtpath, 16, 32, hash, equal);
        if (!ht) {
		printf("Server:Failed to allocate shmht\n");
                exit(1);
        }

        /* Allocate an inode */
        inode = inode_addr;
        inode->ino = 0xaa;
        pthread_rwlockattr_init(&inode->tree_lock_attr);
        pthread_rwlockattr_setpshared(&inode->tree_lock_attr, PTHREAD_PROCESS_SHARED);
        pthread_rwlock_init(&inode->tree_lock, &inode->tree_lock_attr);

        /* Populate interval tree with range [0-1023], [2048-3071] */
        for (int i = 0; i <= 2048; i += 2048) {
                int no = __sync_fetch_and_add(next_avail_index, 1);
                printf("server: get new it node %d\n", no);
                it_entry = index_addr + no*sizeof(struct it_node_entry);
                it_entry->no = no;
                it_entry->it.start = i;
                it_entry->it.last = i + 1023;
                pthread_mutexattr_init(&it_entry->mutex_attr);
                pthread_mutexattr_setpshared(&it_entry->mutex_attr, PTHREAD_PROCESS_SHARED);
                pthread_mutex_init(&it_entry->mutex, &it_entry->mutex_attr);

                pthread_rwlock_wrlock(&inode->tree_lock);
                interval_tree_insert(&it_entry->it, &inode->it_tree);
                pthread_rwlock_unlock(&inode->tree_lock);
        }


        /* Insert the inode into shmht */
	int ret = shmht_insert(ht, filename, 32, (char*)&inode, sizeof(char*));
        if (ret <= 0) {
		printf("Server:Failed to insert inode into shmht ,ret %d\n", ret);
                exit(1);
        }

        /* Signal client and Wait for client */
        sem_post(sem_client_id);
        sem_wait(sem_server_id);

        /* check if range [1024-2047], [3072-4095] are ready. */
        pthread_rwlock_rdlock(&inode->tree_lock);
        it_node = interval_tree_iter_first(&inode->it_tree, 0, 4096);
        while (it_node) {
                it_entry = container_of(it_node, struct it_node_entry, it);        
                printf("range [%ld-%ld] found in node %d\n", it_node->start, it_node->last, it_entry->no);
                it_node = interval_tree_iter_next(it_node, it_node->last+1, 4096);
        }
        pthread_rwlock_unlock(&inode->tree_lock);

        /* check if the shmht is updated by client */
	void *rt = (h_inode_t*)shmht_search(ht, filename, 32, &ret_size);
        if (rt != NULL) {
		printf("Server:Failed, get an inode deleted in shmht\n");
                exit(1);
        }

        printf("Bye Server\n");

	return 0;
}
