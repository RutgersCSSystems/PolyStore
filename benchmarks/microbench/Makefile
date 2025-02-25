all: hetero_io_bench hetero_io_bench_transparent

CC=gcc
LIB_S=-lpthread -lrt

hetero_io_bench: heteroio.c heteroio_config.c
	$(CC) heteroio.c heteroio_config.c -o hetero_io_bench $(LIB_S)

hetero_io_bench_transparent: heteroio_transparent.c heteroio_config.c
	$(CC) heteroio_transparent.c heteroio_config.c -o hetero_io_bench_transparent $(LIB_S)

clean:
	rm -rf *.o
	rm -rf hetero_io_bench hetero_io_bench_transparent

