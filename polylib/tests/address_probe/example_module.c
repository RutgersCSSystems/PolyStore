#include <linux/init.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/vmalloc.h>

#define MODULE_NAME "example_module"
#define IOCTL_ALLOC_MEMORY _IOW('k', 1, struct alloc_params)

struct alloc_params {
    unsigned long size;
    unsigned long addr;
};

static unsigned long allocated_addr = 0;

static long ioctl_handler(struct file *file, unsigned int cmd, unsigned long arg)
{
    struct alloc_params params;
    void *ptr;

    switch (cmd)
    {
    case IOCTL_ALLOC_MEMORY:
        if (copy_from_user(&params, (void *)arg, sizeof(struct alloc_params)))
        {
            return -EFAULT;
        }

        if (allocated_addr)
        {
            vfree((void *)allocated_addr);
            pr_info("%s: Freed previous allocation\n", MODULE_NAME);
        }

        ptr = vmalloc(params.size);
        if (!ptr)
        {
            pr_err("%s: Allocation failed\n", MODULE_NAME);
            return -ENOMEM;
        }

        allocated_addr = (unsigned long)ptr;
        pr_info("%s: Allocated %lu bytes at address %lx\n", MODULE_NAME, params.size, allocated_addr);

        if (remap_vmalloc_range((unsigned long)params.addr, allocated_addr, 0))
        {
            pr_err("%s: Mapping failed\n", MODULE_NAME);
            vfree((void *)allocated_addr);
            return -EFAULT;
        }

        pr_info("%s: Mapped at address %lx\n", MODULE_NAME, params.addr);
        break;

    default:
        return -ENOTTY; // Command not recognized
    }

    return 0;
}

static struct file_operations fops = {
    .unlocked_ioctl = ioctl_handler,
};

static int __init example_module_init(void)
{
    pr_info("%s: Module loaded\n", MODULE_NAME);
    register_chrdev(0, MODULE_NAME, &fops);
    return 0;
}

static void __exit example_module_exit(void)
{
    if (allocated_addr)
    {
        vfree((void *)allocated_addr);
        pr_info("%s: Freed allocated memory\n", MODULE_NAME);
    }

    unregister_chrdev(0, MODULE_NAME);
    pr_info("%s: Module unloaded\n", MODULE_NAME);
}

module_init(example_module_init);
module_exit(example_module_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Your Name");
MODULE_DESCRIPTION("Example kernel module for memory allocation");

