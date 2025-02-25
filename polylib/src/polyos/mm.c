/*
 * mm.c
 */

#include <linux/mm.h>
#include <linux/version.h>

#include "polyos.h"
#include "polyos_iocmd.h"

void mm_write_lock(struct mm_struct *mm) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 8, 0)
        mmap_write_lock(mm); 
#else
	down_write(&mm->mmap_sem);
#endif
}

void mm_write_unlock(struct mm_struct *mm) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 8, 0)
        mmap_write_unlock(mm); 
#else
	up_write(&mm->mmap_sem);
#endif
}

int check_vaddr_mapped(unsigned long addr) {
        int ret = 0;
        pgd_t *pgd;
        p4d_t *p4d;
        pud_t *pud;
        pmd_t *pmd;
        pte_t *pte;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(5, 8, 0)
        mmap_assert_locked(current->mm);
#endif
        
        /* Walk the page table entry for the address */
        pgd = pgd_offset(current->mm, addr);
        if (pgd_none(*pgd) || pgd_bad(*pgd))
                goto out;

        p4d = p4d_offset(pgd, addr);
        if (p4d_none(*p4d) || p4d_bad(*p4d))
                goto out;

        pud = pud_offset(p4d, addr);
        if (pud_none(*pud) || pud_bad(*pud))
                goto out;

        pmd = pmd_offset(pud, addr);
        if (pmd_none(*pmd) || pmd_bad(*pmd))
                goto out;

        pte = pte_offset_map(pmd, addr);
        if (pte_none(*pte))
                goto out;

        /* Check if the PTE is present (mapped) */
        ret = pte && pte_present(*pte);

out:
        return ret;
}
