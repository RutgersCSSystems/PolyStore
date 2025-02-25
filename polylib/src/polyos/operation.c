/*
 * operation.c
 */

#include <linux/fcntl.h>
#include <linux/fdtable.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/fsnotify.h>
#include <linux/io.h>
#include <linux/mm.h>
#include <linux/stat.h>
#include <linux/uaccess.h>
#include <linux/version.h>

#include "polyos.h"
#include "polyos_iocmd.h"

static struct file* polyos_sys_open(char *pathname, 
                             int flags, 
                             umode_t mode, 
                             int *outfd) {
        int fd = 0;
        struct file *f = NULL;

        fd = get_unused_fd_flags(flags);
        if (fd >= 0) {
                f = filp_open(pathname, flags, mode);
                if (IS_ERR(f)) {
                        put_unused_fd(fd);
                        fd = PTR_ERR(f);
                } else {
                        fsnotify_open(f);
                        fd_install(fd, f);
                }
        }

        *outfd = fd;
        return f;
}

long polyos_client_init(unsigned long arg) {
        long ret = 0;
        unsigned long cmdsz = 0;
        unsigned long pace = 0;
        struct polyos_client_init_cmd cmd;
	struct vm_area_struct *vma = NULL;

        cmdsz = sizeof(struct polyos_client_init_cmd);

        /* Copy cmd from user space client */
        if (copy_from_user(&cmd, (void __user *)arg, cmdsz)) {
                POLYOS_ERROR("Failed to copy arg from user!");
                ret = -EFAULT;
                goto err;
        }

        /* Map global config page to client as rd-only */
        vma = find_vma(current->mm, (unsigned long)cmd.config_var_addr);
        if (!vma || vma->vm_start != cmd.config_var_addr || 
                    vma->vm_end != cmd.config_var_addr + POLYSTORE_CONFIG_VAR_SIZE) {
                POLYOS_ERROR("Find VMA failed!");
                ret = -ENOMEM;
                goto err;
        } 

        mm_write_lock(current->mm); 
        //ret = remap_pfn_range(vma, cmd.config_var_addr, vmalloc_to_pfn(g_config_var),
        //                      PAGE_SIZE, vma->vm_page_prot);
        while (pace < POLYSTORE_CONFIG_VAR_SIZE) {
                ret = remap_pfn_range(vma, cmd.config_var_addr + pace, 
                                      vmalloc_to_pfn((void*)g_config_var + pace),
                                      PAGE_SIZE, vma->vm_page_prot);
                if (ret) {
                        POLYOS_ERROR("Faild to map to user addr 0x%lx!", 
        			     cmd.config_var_addr + pace);
                        ret = -EFAULT;
                        mm_write_unlock(current->mm); 
                        goto err;
                }
                pace += PAGE_SIZE;
        }
        mm_write_unlock(current->mm); 

	POLYOS_INFO("config_var uaddr 0x%lx", cmd.config_var_addr);
	POLYOS_INFO("config_var kaddr 0x%p", g_config_var);
	POLYOS_INFO("config_var max inode %d", g_config_var->max_inode_nr);
	POLYOS_INFO("config_var max it node %d", g_config_var->max_it_node_nr);

        /* Map global control page to client as rdwr */
        vma = find_vma(current->mm, (unsigned long)cmd.control_var_addr);
        if (!vma || vma->vm_start != cmd.control_var_addr || 
                    vma->vm_end != cmd.control_var_addr + POLYSTORE_CONTROL_VAR_SIZE) {
                POLYOS_ERROR("Find VMA failed!");
                ret = -ENOMEM;
                goto err;
        } 
        pace = 0;

        mm_write_lock(current->mm); 
        //ret = remap_pfn_range(vma, cmd.control_var_addr, vmalloc_to_pfn(g_control_var),
        //                      PAGE_SIZE, vma->vm_page_prot);
        while (pace < POLYSTORE_CONTROL_VAR_SIZE) {
                ret = remap_pfn_range(vma, cmd.control_var_addr + pace, 
                                      vmalloc_to_pfn((void*)g_control_var + pace),
                                      PAGE_SIZE, vma->vm_page_prot);
                if (ret) {
                        POLYOS_ERROR("Faild to map to user addr 0x%lx!", 
        			     cmd.control_var_addr + pace);
                        ret = -EFAULT;
                        mm_write_unlock(current->mm); 
                        goto err;
                }
                pace += PAGE_SIZE;
        }

        mm_write_unlock(current->mm); 

	POLYOS_INFO("control_var uaddr 0x%lx", cmd.control_var_addr);
	POLYOS_INFO("control_var kaddr 0x%p", g_control_var);

err:
        return ret;
}


long polyos_client_exit(unsigned long arg) {
        long ret = 0;
        unsigned long cmdsz = 0;
        unsigned long pace = 0;
        struct polyos_client_exit_cmd cmd;
	struct vm_area_struct *vma = NULL;

        cmdsz = sizeof(struct polyos_client_exit_cmd);

        /* Copy cmd from user space client */
        if (copy_from_user(&cmd, (void __user *)arg, cmdsz)) {
                POLYOS_ERROR("Failed to copy arg from user!");
                ret = -EFAULT;
                goto err;
        }

        /* Unmap global config page to client as rd-only */
        vma = find_vma(current->mm, (unsigned long)cmd.config_var_addr);
        if (!vma || vma->vm_start != cmd.config_var_addr || 
                    vma->vm_end != cmd.config_var_addr + POLYSTORE_CONFIG_VAR_SIZE) {
                POLYOS_ERROR("Find VMA failed!");
        } 

        mm_write_lock(current->mm); 
        while (pace < POLYSTORE_CONFIG_VAR_SIZE) {
                zap_vma_ptes(vma, cmd.config_var_addr + pace, PAGE_SIZE);
                pace += PAGE_SIZE;
        }
        mm_write_unlock(current->mm); 

        pace = 0;        
        /* Unmap global control page to client as rdwr */
        vma = find_vma(current->mm, (unsigned long)cmd.control_var_addr);
        if (!vma || vma->vm_start != cmd.control_var_addr || 
                    vma->vm_end != cmd.control_var_addr + POLYSTORE_CONTROL_VAR_SIZE) {
                POLYOS_ERROR("Find VMA failed!");
        } 

        mm_write_lock(current->mm); 
        while (pace < POLYSTORE_CONFIG_VAR_SIZE) {
                zap_vma_ptes(vma, cmd.control_var_addr + pace, PAGE_SIZE);
                pace += PAGE_SIZE;
        }
        mm_write_unlock(current->mm); 

        // TODO syncing files in buffer?
err:
        return ret;
}


long polyos_task_ctx_reg(unsigned long arg) {
        long ret = 0;
        int task_id = 0;
        unsigned long cmdsz = 0, task_ctx_addr = 0;
        struct polyos_task_ctx_reg_cmd cmd;
        struct g_task_ctx *task_ctx = NULL;

        cmdsz = sizeof(struct polyos_task_ctx_reg_cmd);

        /* Copy cmd from user space client */
        if (copy_from_user(&cmd, (void __user *)arg, cmdsz)) {
                POLYOS_ERROR("Failed to copy arg from user!");
                ret = -EFAULT;
                goto err;
        }

        /* Allocate task struct in PolyOS controller */
        task_ctx = task_ctx_alloc();
        if (!task_ctx) {
                POLYOS_ERROR("Failed to allocate task_ctx struct!");
                ret = -ENOMEM;
                goto err;
        }
        task_id = task_ctx->task_id;
        task_ctx_addr = GET_TASK_CONTEXT_VADDR(task_id);

        /* Insert to task hashmap */
        ret = task_ctx_hashtable_insert((uint32_t)task_id, task_ctx);
        if (ret < 0) {
                POLYOS_ERROR("Failed to insert task_ctx to hashmap!");
                goto err;
        }

	POLYOS_INFO("task id %d map to addr 0x%lx", task_id, task_ctx_addr);

        /* Copy cmd back to user space client */
        cmd.task_id = task_id;
        cmd.task_ctx_addr = task_ctx_addr;

        if (copy_to_user((void __user *)arg, &cmd, cmdsz)) {
                POLYOS_ERROR("Failed to copy arg to user!");
                ret = -EFAULT;
        }
err:
        return ret;
}


long polyos_task_ctx_delete(unsigned long arg) {
        long ret = 0;
        int task_id = 0;
        unsigned long cmdsz = 0;
        struct polyos_task_ctx_delete_cmd cmd;
        struct g_task_ctx *task_ctx = NULL;

        cmdsz = sizeof(struct polyos_task_ctx_delete_cmd);

        /* Copy cmd from user space client */
        if (copy_from_user(&cmd, (void __user *)arg, cmdsz)) {
                POLYOS_ERROR("Failed to copy arg from user!");
                ret = -EFAULT;
                goto err;
        }
        task_id = cmd.task_id;

        /* Lookup and remove task context from the hashmap in PolyOS controller */
        task_ctx = task_ctx_hashtable_search((uint32_t)task_id);
        if (!task_ctx) {
                POLYOS_ERROR("Failed to find task_ctx struct, id = %d!", task_id);
                ret = -ENOMEM;
                goto err;
        }
        task_ctx_hashtable_delete((uint32_t)task_id);

        /* Free task context struct in PolyOS controller */
        task_ctx_free(task_ctx);

	POLYOS_INFO("task id %d unmap", task_id);
err:
        return ret;
}


long polyos_index_alloc(unsigned long arg) {
        long ret = 0;
        unsigned long cmdsz = 0;
        struct polyos_index_alloc_cmd cmd;

        cmdsz = sizeof(struct polyos_index_alloc_cmd);

        /* Copy cmd from user space client */
        if (copy_from_user(&cmd, (void __user *)arg, cmdsz)) {
                POLYOS_ERROR("Failed to copy arg from user!");
                ret = -EFAULT;
                goto err;
        }

        // TODO Reserved for now
err:
        return ret;
}


long polyos_open(unsigned long arg) {
        long ret = 0;
        unsigned long cmdsz = 0;
        struct polyos_open_cmd cmd;
        struct file *fastfp = NULL, *slowfp = NULL;
        int fastfd = 0, slowfd = 0, polyfd = 0;
        struct poly_inode *inode = NULL;
        struct poly_index *index = NULL;
        unsigned long inode_addr = 0UL, index_addr = 0UL, pace = 0UL;
	struct vm_area_struct *vma = NULL;

        /* Copy cmd from user space client */
        cmdsz = sizeof(struct polyos_open_cmd);
        if (copy_from_user(&cmd, (void __user *)arg, cmdsz)) {
                POLYOS_ERROR("Failed to copy arg from user!");
                ret = -EFAULT;
                goto err;
        }

        /* Open files in fast and slow */
        fastfp = polyos_sys_open(cmd.fastpath, cmd.flags | O_LARGEFILE, cmd.mode, &fastfd);
        if (IS_ERR(fastfp)) {
                POLYOS_ERROR("Failed to open file %s", cmd.fastpath);
		cmd.fd = fastfd;
                ret = -EINVAL;
                goto err;
        }

        slowfp = polyos_sys_open(cmd.slowpath, cmd.flags | O_LARGEFILE, cmd.mode, &slowfd);
        if (IS_ERR(slowfp)) {
                POLYOS_ERROR("Failed to open file %s", cmd.slowpath);
		cmd.fd = slowfd;
                ret = -EINVAL;
                goto err;
        }

        /* Construct fd and map to the internal file pointers */
        polyfd = POLYSTORE_GEN_FD(fastfd, slowfd);
        cmd.fd = polyfd;

        /* Look-up the Poly-inode and create if necessary */
        // FIXME
        inode = i_hashtable_search(cmd.path_hash);
        if (!inode) {
                inode = i_hashtable_search_and_insert(cmd.path_hash, cmd.mode);
                if (!inode || IS_ERR(inode)) {
                        POLYOS_ERROR("Failed to get Poly-inode");
                        ret = -ENOENT;
                        goto err;
                }
        }

        index = (struct poly_index*)inode->index;
        if (IS_ERR(index)) {
                POLYOS_ERROR("Failed to get Poly-index");
                ret = -ENOMEM;
                goto err;
        }

        /* Map the Poly-inode to the user space client */
        inode_addr = GET_POLY_INODE_VADDR(inode->ino);
 
        vma = find_vma(current->mm, (unsigned long)inode_addr);
        if (!vma || vma->vm_start != POLY_INODE_VADDR_BASE || 
                    vma->vm_end != POLY_INODE_VADDR_BASE + 
                                   POLY_INODE_VADDR_RANGE) {
                POLYOS_ERROR("Find VMA failed!");
                ret = -EFAULT;
		goto err;
        } 

        mm_write_lock(current->mm); 
        if (check_vaddr_mapped(inode_addr) == 0) {
                /* Only do remap_pfn_range when the pte is not mapped */
                ret = remap_pfn_range(vma, inode_addr, vmalloc_to_pfn(inode),
                                      PAGE_SIZE, vma->vm_page_prot);
                if (ret) {
                        POLYOS_ERROR("Map poly_inode page to user failed!");
                        ret = -EFAULT;
                        mm_write_unlock(current->mm); 
                        goto err;
                }
        }
        mm_write_unlock(current->mm); 
        cmd.inode_addr = inode_addr;

        /* Map the Poly-index to the user space client */
        index_addr = GET_POLY_INDEX_VADDR(inode->ino);
 
        vma = find_vma(current->mm, (unsigned long)index_addr);
        if (!vma || vma->vm_start != POLY_INDEX_VADDR_BASE || 
                    vma->vm_end != POLY_INDEX_VADDR_BASE + 
                                   POLY_INDEX_VADDR_RANGE) {
                POLYOS_ERROR("Find VMA failed!");
                ret = -EFAULT;
		goto err;
        } 

        mm_write_lock(current->mm); 
        if (check_vaddr_mapped(index_addr) == 0) {
                /* Only do remap_pfn_range when the pte is not mapped */
                while (pace < POLY_INDEX_MAX_MEM_SIZE) {
                        ret = remap_pfn_range(vma, index_addr + pace, 
                                              vmalloc_to_pfn((void*)index + pace),
                                              PAGE_SIZE, 
                                              vma->vm_page_prot);
                        if (ret) {
                                POLYOS_ERROR("Map poly-index to user failed!");
                                ret = -EFAULT;
                                mm_write_unlock(current->mm); 
                                goto err;
                        }
                        pace += PAGE_SIZE;
                }
        }
        mm_write_unlock(current->mm); 
        cmd.index_addr = index_addr;

err:
        /* Copy cmd back to user space client */
        if (copy_to_user((void __user *)arg, &cmd, cmdsz)) {
                POLYOS_ERROR("Failed to copy arg to user!");
                ret = -EFAULT;
        }
        return ret;
}

long polyos_close(unsigned long arg) {
        long ret = 0;
        int fd = 0, fastfd = 0, slowfd = 0;
        int path_hash;
        unsigned long cmdsz = 0;
        struct polyos_close_cmd cmd;
        struct poly_inode *inode = NULL;

        /* Copy cmd from user space client */
        cmdsz = sizeof(struct polyos_close_cmd);
        if (copy_from_user(&cmd, (void __user *)arg, cmdsz)) {
                POLYOS_ERROR("Failed to copy arg from user!");
                ret = -EFAULT;
                goto out;
        }
        path_hash = cmd.path_hash;
        fd = cmd.fd;
        fastfd = POLYSTORE_FAST_FD(fd);
        slowfd = POLYSTORE_SLOW_FD(fd);

        inode = i_hashtable_search(path_hash);
        if (!inode) {
                POLYOS_ERROR("Failed to find inode for close!");
                ret = -ENOENT;
                goto out;
        }

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 220)
        ret = close_fd(fastfd);
#else
        ret =  __close_fd(current->files, fastfd);
#endif
        if (ret) {
                POLYOS_ERROR("Failed to close fd: %d!", fastfd);
        }

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 10, 220)
        ret = close_fd(slowfd);
#else
        ret =  __close_fd(current->files, slowfd);
#endif
        if (ret) {
                POLYOS_ERROR("Failed to close fd: %d!", slowfd);
        }

        /* When refcount drop 0, reclaim inode if is unlinked */
        while(__sync_lock_test_and_set(&inode->spinlock, 1));
        if (--inode->refcount == 0 && inode->reclaim == 1) {
                i_hashtable_delete(path_hash);
                free_poly_index(inode);
                free_poly_inode(inode);
                goto out;
        }
        __sync_lock_release(&inode->spinlock);

out:
        return ret;
}


long polyos_filerw(unsigned long arg) {
        // TODO
	return 0;
}

long polyos_rename(unsigned long arg) {
        long ret = 0;
        unsigned long cmdsz = 0;
        struct polyos_rename_cmd cmd;
        int old_path_hash, new_path_hash;

        /* Copy cmd from user space client */
        cmdsz = sizeof(struct polyos_rename_cmd);
        if (copy_from_user(&cmd, (void __user *)arg, cmdsz)) {
                POLYOS_ERROR("Failed to copy arg from user!");
                ret = -EFAULT;
                goto out;
        }
        old_path_hash = cmd.old_path_hash;
        new_path_hash = cmd.new_path_hash;

        ret = i_hashtable_search_and_rename(old_path_hash, 
                                            new_path_hash);

 out:
        return ret;
}

long polyos_unlink(unsigned long arg) {
        long ret = 0;
        unsigned long cmdsz = 0;
        struct poly_inode *inode = NULL;
        struct polyos_unlink_cmd cmd;
        int path_hash;

        /* Copy cmd from user space client */
        cmdsz = sizeof(struct polyos_unlink_cmd);
        if (copy_from_user(&cmd, (void __user *)arg, cmdsz)) {
                POLYOS_ERROR("Failed to copy arg from user!");
                ret = -EFAULT;
                goto out;
        }
        path_hash = cmd.path_hash;

        inode = i_hashtable_search(path_hash);
        if (!inode) {
                POLYOS_ERROR("Failed to find inode for unlink!");
                ret = -ENOENT;
                goto out;
        }

        /* Free the inode only when ref count is 0 */
        while(__sync_lock_test_and_set(&inode->spinlock, 1));
        if (inode->refcount == 0) {
                i_hashtable_delete(path_hash);
                free_poly_index(inode);
                free_poly_inode(inode);
                goto out;
        } else {
                inode->reclaim = 1;
        }
        __sync_lock_release(&inode->spinlock);

out:
        return ret;
}



