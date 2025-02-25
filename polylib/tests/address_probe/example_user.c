#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>

#define DEVICE_FILE "/dev/example_module"
#define IOCTL_ALLOC_MEMORY _IOW('k', 1, struct alloc_params)

struct alloc_params {
    unsigned long size;
    unsigned long addr;
};

int main()
{
    int fd;
    struct alloc_params params;
    params.size = 4096; // Change this to your desired size
    params.addr = 0x1000000; // Change this to your desired address

    fd = open(DEVICE_FILE, O_RDWR);
    if (fd == -1)
    {
        perror("Failed to open the device");
        return -1;
    }

    if (ioctl(fd, IOCTL_ALLOC_MEMORY, &params) == -1)
    {
        perror("IOCTL failed");
    }

    close(fd);

    return 0;
}

