CC=gcc
SHMHASH_DIR := $(abspath ./libshmht/)
INTERVAL_TREE_DIR := $(abspath ./interval-tree/)
LDFLAGS=-L$(INTERVAL_TREE_DIR) -lintervaltreelib -L$(SHMHASH_DIR) -lshmht -lpthread
INCLUDE=-I$(SHMHASH_DIR) -I$(INTERVAL_TREE_DIR)

all: server client

server:
	$(CC) $(INCLUDE) -o server server.c $(LDFLAGS)

client:
	$(CC) $(INCLUDE) -o client client.c $(LDFLAGS)

clean:
	rm server client
