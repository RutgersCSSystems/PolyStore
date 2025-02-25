/*
 * polyos.c
 */

#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/ioctl.h>
#include <linux/module.h>
#include <linux/uaccess.h>
#include <linux/vmalloc.h>

#include "polyos.h"
#include "polyos_iocmd.h"

struct g_config_var *g_config_var = NULL;
struct g_control_var *g_control_var = NULL;
void *g_poly_inode_addr = NULL;


#define MODULE_NAME "polyos"
#define MODULE_MAX_DEV 1

#define POLYOS_CMDBUF_LEN 4096

static int major = 0;
static struct class *polyos_controller_class = NULL;
static struct cdev cdev;

static uint   param_max_inode_nr;
static uint   param_max_it_node_nr;
//static ulong  param_cache_flush_start;
//static ulong  param_cache_flush_end;
//static ushort param_cache_flush_thread_nr;
static char*  param_fast_dir;
static char*  param_slow_dir;

module_param(param_max_inode_nr,          uint,   S_IRUGO); 
module_param(param_max_it_node_nr,        uint,   S_IRUGO); 
//module_param(param_cache_flush_start,     ulong,  S_IRUGO); 
//module_param(param_cache_flush_end,       ulong,  S_IRUGO); 
//module_param(param_cache_flush_thread_nr, ushort, S_IRUGO); 
module_param(param_fast_dir,              charp,  S_IRUGO); 
module_param(param_slow_dir,              charp,  S_IRUGO); 

MODULE_PARM_DESC(param_max_inode_nr,          "Max inodes supported in PolyStore");
MODULE_PARM_DESC(param_max_it_node_nr,        "Max index nodes supported in PolyStore");
//MODULE_PARM_DESC(param_cache_flush_start,     "Threshold triggers Poly-cache flush start");
//MODULE_PARM_DESC(param_cache_flush_end,       "Threshold triggers Poly-cache flush end");
//MODULE_PARM_DESC(param_cache_flush_thread_nr, "Number of Poly-cache background flush threads");
MODULE_PARM_DESC(param_fast_dir,              "Mount point of the FAST storage in PolyStore");
MODULE_PARM_DESC(param_slow_dir,              "Mount point of the SLOW storage in PolyStore");

struct g_config_var *g_config_var;
struct g_control_var *g_control_var;

static int polyos_control_uevent(struct device *dev, struct kobj_uevent_env *env) {
        add_uevent_var(env, "DEVMODE=%#o", 0666);
        return 0;
}

static int polyos_control_open(struct inode *inode, struct file *file) {
        POLYOS_INFO("Open PolyOS Controller");
        return 0;
}

static int polyos_control_release(struct inode *inode, struct file *file) {
        POLYOS_INFO("Release PolyOS Controller");
        return 0;
}

static long polyos_control_ioctl(struct file *file, 
                         unsigned int cmd, 
                         unsigned long arg) {
        int ret = 0;

        switch (cmd) {
                case POLYOS_CLIENT_INIT_CMD:
                        ret = polyos_client_init(arg);        
                        break;

                case POLYOS_CLIENT_EXIT_CMD:
                        ret = polyos_client_exit(arg);        
                        break;

                case POLYOS_TASK_CTX_REG_CMD:
                        ret = polyos_task_ctx_reg(arg);        
                        break;

                case POLYOS_TASK_CTX_DELETE_CMD:
                        ret = polyos_task_ctx_delete(arg);        
                        break;

                case POLYOS_OPEN_CMD:
                        ret = polyos_open(arg); 
                        break;

                case POLYOS_RW_CMD:
                        ret = polyos_filerw(arg); 
                        break;

                case POLYOS_CLOSE_CMD:
                        ret = polyos_close(arg); 
                        break;

                case POLYOS_RENAME_CMD:
                        ret = polyos_rename(arg); 
                        break;

                case POLYOS_UNLINK_CMD:
                        ret = polyos_unlink(arg); 
                        break;

                default:
                        return -ENOTTY;  // Not a valid ioctl command
        }

        return ret;
}

static struct file_operations polyos_control_fops = {
        .owner = THIS_MODULE,
        .open = polyos_control_open,
        .release = polyos_control_release,
        .unlocked_ioctl = polyos_control_ioctl,
};

static int __init my_init(void) {
        int ret = 0, devno;
        dev_t dev;
	struct device *sysfsdev = NULL;

        /* Check input parameters */
        if (param_fast_dir == NULL) {
                POLYOS_ERROR("Failed to get fast device path from user!");
                ret = -EINVAL;
                goto out;
        }

        if (param_slow_dir == NULL) {
                POLYOS_ERROR("Failed to get slow device path from user!");
                ret = -EINVAL;
                goto out;
        }

        /* Get available char dev major number for PolyOS Controller */
        ret = alloc_chrdev_region(&dev, 0, MODULE_MAX_DEV, MODULE_NAME);
        if (ret < 0) {
                POLYOS_ERROR("Failed to allocate a major number!");
                ret = -ENODEV;
                goto out;
        }
        major = MAJOR(dev);

        /* Create sysfs class */
        polyos_controller_class = class_create(THIS_MODULE, MODULE_NAME);
        if (IS_ERR(polyos_controller_class)) {
                POLYOS_ERROR("Failed to create sysfs class!");
                ret = -ENOENT;
                goto err1;
        }
        polyos_controller_class->dev_uevent = polyos_control_uevent;

        /* Register Quark Controller as a char device */
        devno = MKDEV(major, 0);
        cdev_init(&cdev, &polyos_control_fops);
        cdev.owner = THIS_MODULE;
        ret = cdev_add(&cdev, devno, 1);
        if (ret < 0) {
                POLYOS_ERROR("Failed to register chardev %s!", MODULE_NAME);
                ret = -EIO;
                goto err2;
        }

        /* Create device node in sysfs */
        sysfsdev = device_create(polyos_controller_class, NULL, 
                                 devno, NULL, MODULE_NAME);
        if (IS_ERR(sysfsdev)) {
                POLYOS_ERROR("Failed to create dev node under /dev!");
                ret = -ENODEV;
                goto err2;
        }
        POLYOS_INFO("PolyOS Controller registered with major number %d\n", major);


        /* Create 4KB page for global config variables */
        g_config_var = vmalloc(POLYSTORE_CONFIG_VAR_SIZE);
        if (!g_config_var) {
                POLYOS_ERROR("Failed to allocate page for global config var");
                ret = -ENOMEM;
                goto err3;
        }
        memset(g_config_var, 0, POLYSTORE_CONFIG_VAR_SIZE);

        /* Initialize global config variables */
        g_config_var->max_inode_nr = param_max_inode_nr;
        g_config_var->max_it_node_nr = param_max_it_node_nr;
        //g_config_var->cache_flush_start = param_cache_flush_start;
        //g_config_var->cache_flush_end = param_cache_flush_end;
        //g_config_var->cache_flush_thread_nr = param_cache_flush_thread_nr;
        strncpy(g_config_var->fast_dir, param_fast_dir, MAX_MOUNT_POINT_LEN);
        strncpy(g_config_var->slow_dir, param_slow_dir, MAX_MOUNT_POINT_LEN);
        POLYOS_INFO("max inode nr %d", g_config_var->max_inode_nr);
        POLYOS_INFO("max it node nr %d", g_config_var->max_it_node_nr);

        /* Create 8 pages for global control variables */
        g_control_var = vmalloc(POLYSTORE_CONTROL_VAR_SIZE);
        if (!g_control_var) {
                POLYOS_ERROR("Failed to allocate page for global control var");
                ret = -ENOMEM;
                goto err3;
        }
        memset(g_control_var, 0, POLYSTORE_CONTROL_VAR_SIZE);

        /* Map the on-disk Poly-inode into PolyOS memory */
        //TODO

        /* Success */
        return 0;

err3:
        device_destroy(polyos_controller_class, devno);
err2:
        class_unregister(polyos_controller_class);
        class_destroy(polyos_controller_class);
err1:
        unregister_chrdev_region(dev, MODULE_MAX_DEV);
out:
        return 0;
}

static void __exit my_exit(void) {
        int devno = MKDEV(major, 0);

        /* Unregister PolyOS */
        device_destroy(polyos_controller_class, devno);
        class_unregister(polyos_controller_class); 
        class_destroy(polyos_controller_class); 
        unregister_chrdev_region(devno, MODULE_MAX_DEV); 

        /* Clean up all Poly-inode and Poly-index */
        clean_up_poly_inode_index();

        /* Free the allocated memory */
        if (g_config_var)
                vfree(g_config_var);

        if (g_control_var)
                vfree(g_control_var);

        POLYOS_INFO("PolyOS Controller freed\n");
}

module_init(my_init);
module_exit(my_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Yujie Ren");
MODULE_DESCRIPTION("PolyStore kernel components");
