/*
 * indexing.c
 */

#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>

#include "polystore.h"
#include "debug.h"

struct it_node_entry* polystore_index_entry_allocate(struct poly_inode *inode) {
        unsigned long index = (unsigned long)inode->index;
        struct it_node_entry *new_it_entry = NULL;
        int it_no = __sync_fetch_and_add(&inode->next_avail_it_node_no, 1);

        if (!inode->index) {
                ERROR_T("NULL pointer for Poly-index!");
                goto out;
        }

        if (it_no > MAX_POLY_INDEX_NODE_NR) {
                ERROR_T("Exceeding max available Poly-index node!");
                goto out;
        }
        new_it_entry = (struct it_node_entry*)
                        (index + (it_no << POLY_INDEX_STRUCT_SIZE_SHIFT));
        /*new_it_entry = (struct it_node_entry*)malloc(sizeof(struct it_node_entry));
        if (!new_it_entry) {
                ERROR_T("Fail to allocate Poly-index node!");
                goto out;
        }*/
        new_it_entry->idx_no = it_no;

out:
        return new_it_entry;
}

void polystore_index_insert(struct poly_inode *inode, 
                            struct it_node_entry *entry) {
        return interval_tree_insert(&entry->it, &inode->it_tree);
}

struct it_node_entry* polystore_index_lookup(struct poly_inode *inode,
                                             unsigned long start, 
                                             unsigned long last) {
        struct interval_tree_node *node = NULL;
        node = interval_tree_iter_first(&inode->it_tree, start, last);
        return node ? 
               container_of(node, struct it_node_entry, it) : NULL;
}

struct it_node_entry* polystore_index_lookup_next(struct it_node_entry *entry,
                                             unsigned long start, 
                                             unsigned long last) {
        struct interval_tree_node *node = &entry->it, *next = NULL;
        next = interval_tree_iter_next(node, start, last);
        return next ? 
               container_of(next, struct it_node_entry, it) : NULL;
}

void polystore_index_remove(struct poly_inode *inode, 
                            struct it_node_entry *entry) {
        return interval_tree_remove(&entry->it, &inode->it_tree);
}

int polystore_index_load(struct poly_inode *inode, 
                         unsigned long start, unsigned long last) {
        //TODO
        return 0;
}

int polystore_index_sync(struct poly_inode *inode, 
                         unsigned long start, unsigned long last) {
        //TODO
        return 0;
}


