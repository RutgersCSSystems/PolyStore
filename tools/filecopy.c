#define _GNU_SOURCE

#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <pthread.h>

#define BUFFER_SIZE 4096

void* buffer = NULL;
char src_path[256];
char dest_path[256];

void* copy(void *arg) {
	int sourceFile, destFile;
	ssize_t bytesRead, bytesWritten;

        // Open the source file in read-only mode
        sourceFile = open(src_path, O_RDONLY);
        if (sourceFile < 0) {
                perror("Could not open source file");
                return NULL;
        }

        // Open (or create) the destination file in write-only mode, with appropriate permissions
        destFile = open(dest_path, O_WRONLY | O_CREAT | O_TRUNC | O_DIRECT, 0644);
        if (destFile < 0) {
                perror("Could not open or create destination file");
                close(sourceFile);
                return NULL;
        }

        // Copy the content from source file to destination file using a buffer
        while ((bytesRead = read(sourceFile, buffer, BUFFER_SIZE)) > 0) {
                bytesWritten = write(destFile, buffer, bytesRead);
                if (bytesWritten != bytesRead) {
                        perror("Error writing to destination file");
                        close(sourceFile);
                        close(destFile);
                        return NULL;
                }
        }

        if (bytesRead < 0) {
                perror("Error reading from source file");
        } else {
                printf("File copied successfully.\n");
        }

        // Close the opened files
        close(sourceFile);
        close(destFile);

	return NULL;
}

int main(int argc, char *argv[]) {
	pthread_t thread;

        // Check if the correct number of arguments are provided
        if (argc != 3) {
                printf("Usage: %s <source-file> <destination-file>\n", argv[0]);
                return 1;
        }

	strcpy(src_path, argv[1]);
	strcpy(dest_path, argv[2]);

	// Allocate buffer
	posix_memalign(&buffer, 4096, 4096);
	if (!buffer) {
	        perror("Failed to allocate file");
                return 1;
	}

	// Create a new thread that will execute 'threadFunction'
	if (pthread_create(&thread, NULL, copy, NULL)) {
		printf("Error creating thread\n");
        	return 1;
	}

	// Wait for the thread to finish
	if (pthread_join(thread, NULL)) {
		printf("Error joining thread\n");
		return 1;
	}

        return 0;
}
