/*
 *  mm/userfaultfd.c
 *
 *  Copyright (C) 2015  Red Hat, Inc.
 *
 *  This work is licensed under the terms of the GNU GPL, version 2. See
 *  the COPYING file in the top-level directory.
 */

#include <linux/mm.h>
#include <linux/pagemap.h>
#include <linux/rmap.h>
#include <linux/swap.h>
#include <linux/swapops.h>
#include <linux/userfaultfd_k.h>
#include <linux/mmu_notifier.h>
#include <linux/shmem_fs.h>
#include <asm/tlbflush.h>
#include <linux/page-flags.h>
#include "internal.h"

/*
 * Check if a VMA is backed by shmem/tmpfs (includes memfd).
 * Returns true for MAP_PRIVATE or MAP_SHARED shmem VMAs.
 */
static inline bool vma_is_shmem(struct vm_area_struct *vma)
{
	return vma->vm_file && shmem_mapping(vma->vm_file->f_mapping);
}

static int mcopy_atomic_pte(struct mm_struct *dst_mm,
			    pmd_t *dst_pmd,
			    struct vm_area_struct *dst_vma,
			    unsigned long dst_addr,
			    unsigned long src_addr,
			    struct page **pagep)
{
	struct mem_cgroup *memcg;
	pte_t _dst_pte, *dst_pte;
	spinlock_t *ptl;
	void *page_kaddr;
	int ret;
	struct page *page;

	if (!*pagep) {
		ret = -ENOMEM;
		page = alloc_page_vma(GFP_HIGHUSER_MOVABLE, dst_vma, dst_addr);
		if (!page)
			goto out;

		page_kaddr = kmap_atomic(page);
		ret = copy_from_user(page_kaddr,
				     (const void __user *) src_addr,
				     PAGE_SIZE);
		kunmap_atomic(page_kaddr);

		/* fallback to copy_from_user outside mmap_sem */
		if (unlikely(ret)) {
			ret = -EFAULT;
			*pagep = page;
			/* don't free the page */
			goto out;
		}
	} else {
		page = *pagep;
		*pagep = NULL;
	}

	/*
	 * The memory barrier inside __SetPageUptodate makes sure that
	 * preceeding stores to the page contents become visible before
	 * the set_pte_at() write.
	 */
	__SetPageUptodate(page);

	ret = -ENOMEM;
	if (mem_cgroup_try_charge(page, dst_mm, GFP_KERNEL, &memcg))
		goto out_release;

	_dst_pte = mk_pte(page, dst_vma->vm_page_prot);
	if (dst_vma->vm_flags & VM_WRITE)
		_dst_pte = pte_mkwrite(pte_mkdirty(_dst_pte));

	ret = -EEXIST;
	dst_pte = pte_offset_map_lock(dst_mm, dst_pmd, dst_addr, &ptl);
	if (!pte_none(*dst_pte))
		goto out_release_uncharge_unlock;

	inc_mm_counter(dst_mm, MM_ANONPAGES);
	page_add_new_anon_rmap(page, dst_vma, dst_addr);
	mem_cgroup_commit_charge(page, memcg, false);
	lru_cache_add_active_or_unevictable(page, dst_vma);

	set_pte_at(dst_mm, dst_addr, dst_pte, _dst_pte);

	/* No need to invalidate - it was non-present before */
	update_mmu_cache(dst_vma, dst_addr, dst_pte);

	pte_unmap_unlock(dst_pte, ptl);
	ret = 0;
out:
	return ret;
out_release_uncharge_unlock:
	pte_unmap_unlock(dst_pte, ptl);
	mem_cgroup_cancel_charge(page, memcg);
out_release:
	page_cache_release(page);
	goto out;
}

static int mfill_zeropage_pte(struct mm_struct *dst_mm,
			      pmd_t *dst_pmd,
			      struct vm_area_struct *dst_vma,
			      unsigned long dst_addr)
{
	pte_t _dst_pte, *dst_pte;
	spinlock_t *ptl;
	int ret;

	_dst_pte = pte_mkspecial(pfn_pte(my_zero_pfn(dst_addr),
					 dst_vma->vm_page_prot));
	ret = -EEXIST;
	dst_pte = pte_offset_map_lock(dst_mm, dst_pmd, dst_addr, &ptl);
	if (!pte_none(*dst_pte))
		goto out_unlock;
	set_pte_at(dst_mm, dst_addr, dst_pte, _dst_pte);
	/* No need to invalidate - it was non-present before */
	update_mmu_cache(dst_vma, dst_addr, dst_pte);
	ret = 0;
out_unlock:
	pte_unmap_unlock(dst_pte, ptl);
	return ret;
}

static pmd_t *mm_alloc_pmd(struct mm_struct *mm, unsigned long address)
{
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd = NULL;

	pgd = pgd_offset(mm, address);
	pud = pud_alloc(mm, pgd, address);
	if (pud)
		/*
		 * Note that we didn't run this because the pmd was
		 * missing, the *pmd may be already established and in
		 * turn it may also be a trans_huge_pmd.
		 */
		pmd = pmd_alloc(mm, pud, address);
	return pmd;
}

static __always_inline ssize_t __mcopy_atomic(struct mm_struct *dst_mm,
					      unsigned long dst_start,
					      unsigned long src_start,
					      unsigned long len,
					      bool zeropage)
{
	struct vm_area_struct *dst_vma;
	ssize_t err;
	pmd_t *dst_pmd;
	unsigned long src_addr, dst_addr;
	long copied;
	struct page *page;

	/*
	 * Sanitize the command parameters:
	 */
	BUG_ON(dst_start & ~PAGE_MASK);
	BUG_ON(len & ~PAGE_MASK);

	/* Does the address range wrap, or is the span zero-sized? */
	BUG_ON(src_start + len <= src_start);
	BUG_ON(dst_start + len <= dst_start);

	src_addr = src_start;
	dst_addr = dst_start;
	copied = 0;
	page = NULL;
retry:
	down_read(&dst_mm->mmap_sem);

	/*
	* Make sure the vma is not shared, that the dst range is
	* both valid and fully within a single existing vma.
	*/
   dst_vma = find_vma(dst_mm, dst_start);
	err = -ENOENT;
	if (!dst_vma)
	   goto out_unlock;

	err = -EINVAL;
	if ((dst_vma->vm_flags & VM_SHARED))
		goto out_unlock;

   if (dst_start < dst_vma->vm_start ||
	   dst_start + len > dst_vma->vm_end)
	   goto out_unlock;

   /*
	* Be strict and only allow __mcopy_atomic on userfaultfd
	* registered ranges to prevent userland errors going
	* unnoticed. As far as the VM consistency is concerned, it
	* would be perfectly safe to remove this check, but there's
	* no useful usage for __mcopy_atomic ouside of userfaultfd
	* registered ranges. This is after all why these are ioctls
	* belonging to the userfaultfd and not syscalls.
	*/
	err = -ENOENT;
   if (!dst_vma->vm_userfaultfd_ctx.ctx)
	   goto out_unlock;

   /*
	* Allow copying on anonymous VMAs and shmem (memfd/tmpfs) VMAs.
	* Shmem VMAs are used by ART's MarkCompact GC via memfd.
	* UFFDIO_COPY installs anonymous pages into the shmem VMA,
	* which works correctly via COW semantics.
	*/
	err = -EINVAL;
   if (!vma_is_anonymous(dst_vma) && !vma_is_shmem(dst_vma))
	   goto out_unlock;

	/*
	 * Ensure the dst_vma has a anon_vma or this page
	 * would get a NULL anon_vma when moved in the
	 * dst_vma.
	 */
	err = -ENOMEM;
	if (unlikely(anon_vma_prepare(dst_vma)))
		goto out_unlock;

	while (src_addr < src_start + len) {
		pmd_t dst_pmdval;

		BUG_ON(dst_addr >= dst_start + len);

		dst_pmd = mm_alloc_pmd(dst_mm, dst_addr);
		if (unlikely(!dst_pmd)) {
			err = -ENOMEM;
			break;
		}

		dst_pmdval = pmd_read_atomic(dst_pmd);
		/*
		 * If the dst_pmd is mapped as THP don't
		 * override it and just be strict.
		 */
		if (unlikely(pmd_trans_huge(dst_pmdval))) {
			err = -EEXIST;
			break;
		}
		if (unlikely(pmd_none(dst_pmdval)) &&
		    unlikely(__pte_alloc(dst_mm, dst_vma, dst_pmd,
					 dst_addr))) {
			err = -ENOMEM;
			break;
		}
		/* If an huge pmd materialized from under us fail */
		if (unlikely(pmd_trans_huge(*dst_pmd))) {
			err = -EFAULT;
			break;
		}

		BUG_ON(pmd_none(*dst_pmd));
		BUG_ON(pmd_trans_huge(*dst_pmd));

		if (!zeropage)
			err = mcopy_atomic_pte(dst_mm, dst_pmd, dst_vma,
					       dst_addr, src_addr, &page);
		else
			err = mfill_zeropage_pte(dst_mm, dst_pmd, dst_vma,
						 dst_addr);

		cond_resched();

		if (unlikely(err == -EFAULT)) {
			void *page_kaddr;
			struct vm_area_struct *src_vma;

			up_read(&dst_mm->mmap_sem);
			BUG_ON(!page);

			/*
			 * The source page is not present. We cannot use
			 * handle_mm_fault() here because the source VMA may
			 * be registered with userfaultfd (UFFD_FEATURE_SIGBUS),
			 * which would cause handle_mm_fault to return
			 * VM_FAULT_SIGBUS instead of faulting in the page.
			 *
			 * Instead, walk the page tables directly:
			 *  - pte absent (zero page): clear_page the destination
			 *  - pte present: copy from the physical page directly
			 *  - swap entry: bring it in via swapin_readahead
			 */
			down_read(&current->mm->mmap_sem);
			src_vma = find_vma(current->mm, src_addr);
			if (src_vma && src_vma->vm_start <= src_addr) {
				pgd_t *pgd;
				pud_t *pud;
				pmd_t *pmd;
				pte_t *ptep, pte_val;
				spinlock_t *ptl;
				struct page *src_page = NULL;

				err = -EFAULT;

				pgd = pgd_offset(current->mm, src_addr);
				if (pgd_none(*pgd) || pgd_bad(*pgd))
					goto src_zero_fill;

				pud = pud_offset(pgd, src_addr);
				if (pud_none(*pud) || pud_bad(*pud))
					goto src_zero_fill;

				pmd = pmd_offset(pud, src_addr);
				if (pmd_none(*pmd) || pmd_bad(*pmd))
					goto src_zero_fill;

				if (unlikely(pmd_trans_huge(*pmd)))
					goto src_fallback_done;

				ptep = pte_offset_map_lock(current->mm, pmd,
							   src_addr, &ptl);
				pte_val = *ptep;

				if (pte_none(pte_val)) {
					/* Page never accessed — zero fill */
					pte_unmap_unlock(ptep, ptl);
					goto src_zero_fill;
				}

				if (pte_present(pte_val)) {
					src_page = vm_normal_page(src_vma,
								  src_addr,
								  pte_val);
					if (!src_page) {
						/*
						 * Special mapping (e.g. the
						 * shared zero page). Use
						 * pte_page as fallback.
						 */
						src_page = pte_page(pte_val);
					}
					get_page(src_page);
					pte_unmap_unlock(ptep, ptl);

					page_kaddr = kmap(page);
					{
						void *src_kaddr =
							kmap(src_page);
						copy_page(page_kaddr,
							  src_kaddr);
						kunmap(src_page);
					}
					kunmap(page);
					put_page(src_page);
					err = 0;
					goto src_fallback_done;
				}

				/*
				 * Not present, not none → swap entry.
				 * Read it in without going through userfaultfd.
				 */
				if (!pte_file(pte_val)) {
					swp_entry_t entry =
						pte_to_swp_entry(pte_val);
					pte_unmap_unlock(ptep, ptl);

					if (is_migration_entry(entry)) {
						/*
						 * Wait for migration to
						 * complete and retry.
						 */
						migration_entry_wait(
						    current->mm, pmd,
						    src_addr);
						up_read(
						    &current->mm->mmap_sem);
						goto retry;
					}

					if (!non_swap_entry(entry)) {
						src_page = swapin_readahead(
							entry,
							GFP_HIGHUSER_MOVABLE,
							src_vma, src_addr);
						if (src_page) {
							lock_page(src_page);
							page_kaddr = kmap(page);
							{
								void *sk =
								    kmap(src_page);
								copy_page(
								    page_kaddr,
								    sk);
								kunmap(
								    src_page);
							}
							kunmap(page);
							unlock_page(src_page);
							page_cache_release(
							    src_page);
							err = 0;
						}
					}
					goto src_fallback_done;
				}

				pte_unmap_unlock(ptep, ptl);
				goto src_fallback_done;

src_zero_fill:
				page_kaddr = kmap(page);
				clear_page(page_kaddr);
				kunmap(page);
				err = 0;

src_fallback_done: ;
			} else {
				err = -EFAULT;
			}
			up_read(&current->mm->mmap_sem);

			if (unlikely(err)) {
				err = -EFAULT;
				goto out;
			}
			goto retry;
		} else
			BUG_ON(page);

		if (!err) {
			dst_addr += PAGE_SIZE;
			src_addr += PAGE_SIZE;
			copied += PAGE_SIZE;

			if (fatal_signal_pending(current))
				err = -EINTR;
		}
		if (err)
			break;
	}

out_unlock:
	up_read(&dst_mm->mmap_sem);
out:
	if (page)
		page_cache_release(page);
	BUG_ON(copied < 0);
	BUG_ON(err > 0);
	BUG_ON(!copied && !err);
	return copied ? copied : err;
}

ssize_t mcopy_atomic(struct mm_struct *dst_mm, unsigned long dst_start,
		     unsigned long src_start, unsigned long len)
{
	return __mcopy_atomic(dst_mm, dst_start, src_start, len, false);
}

/*
 * Helpers for dual-PTL locking used by UFFDIO_MOVE.
 * Lock in pointer-address order to prevent ABBA deadlocks.
 */
static void uffd_double_pt_lock(spinlock_t *ptl1, spinlock_t *ptl2)
{
	if (ptl1 > ptl2)
		swap(ptl1, ptl2);
	spin_lock(ptl1);
	if (ptl1 != ptl2)
		spin_lock_nested(ptl2, SINGLE_DEPTH_NESTING);
}

static void uffd_double_pt_unlock(spinlock_t *ptl1, spinlock_t *ptl2)
{
	spin_unlock(ptl1);
	if (ptl1 != ptl2)
		spin_unlock(ptl2);
}

static int move_present_pte(struct mm_struct *mm,
			    struct vm_area_struct *dst_vma,
			    struct vm_area_struct *src_vma,
			    unsigned long dst_addr, unsigned long src_addr,
			    pte_t *dst_pte, pte_t *src_pte,
			    pte_t orig_dst_pte, pte_t orig_src_pte,
			    spinlock_t *dst_ptl, spinlock_t *src_ptl,
			    struct page *src_page)
{
	int err = 0;
	pte_t new_dst_pte;

	uffd_double_pt_lock(dst_ptl, src_ptl);

	if (!pte_same(*src_pte, orig_src_pte) ||
	    !pte_same(*dst_pte, orig_dst_pte)) {
		err = -EAGAIN;
		goto out;
	}

	/*
	 * Relaxed lock check: Android 3.18 background scanners (kswapd/KSM)
	 * often hold a transient +1 reference on active LRU pages.
	 * As long as mapcount == 1, the virtual mapping is exclusively ours.
	 */
	if (page_mapcount(src_page) != 1 || page_count(src_page) > 3) {
		err = -EBUSY;
		goto out;
	}

	/* THE ZRAM SHIELD: Never move a page queued for swap compression */
	if (PageSwapCache(src_page)) {
		err = -EBUSY;
		goto out;
	}

	orig_src_pte = ptep_clear_flush(src_vma, src_addr, src_pte);

	if (page_count(src_page) > 3) {
		set_pte_at(mm, src_addr, src_pte, orig_src_pte);
		err = -EBUSY;
		goto out;
	}

	/*
	 * Update rmap. We hold both PTLs, page lock, and anon_vma lock,
	 * so this is safe against concurrent rmap walkers.
	 */
	src_page->index = linear_page_index(dst_vma, dst_addr);
	page_move_anon_rmap(src_page, dst_vma, dst_addr);

	/*
	 * Rebuild the destination PTE from the VMA protections.
	 * Following mremap() semantics: mark the entry dirty after move.
	 */
	new_dst_pte = mk_pte(src_page, dst_vma->vm_page_prot);
	new_dst_pte = pte_mkdirty(new_dst_pte);
	if (dst_vma->vm_flags & VM_WRITE)
		new_dst_pte = pte_mkwrite(new_dst_pte);

	set_pte_at(mm, dst_addr, dst_pte, new_dst_pte);
	update_mmu_cache(dst_vma, dst_addr, dst_pte);
out:
	uffd_double_pt_unlock(dst_ptl, src_ptl);
	return err;
}

/*
 * move_present_pte_shmem - move a shmem page from src to dst
 *
 * Similar to move_present_pte() but handles shmem/tmpfs (memfd) pages.
 * These pages live in the shmem page cache (radix tree) and must be
 * relocated atomically from the source file offset to the destination.
 *
 * Locking order: radix_tree_preload -> PTL -> tree_lock
 * The page lock must be held by the caller.
 */
static int move_present_pte_shmem(struct mm_struct *mm,
				  struct vm_area_struct *dst_vma,
				  struct vm_area_struct *src_vma,
				  unsigned long dst_addr, unsigned long src_addr,
				  pte_t *dst_pte, pte_t *src_pte,
				  pte_t orig_dst_pte, pte_t orig_src_pte,
				  spinlock_t *dst_ptl, spinlock_t *src_ptl,
				  struct page *src_page)
{
	struct address_space *mapping = src_page->mapping;
	pgoff_t src_pgoff = src_page->index;
	pgoff_t dst_pgoff = linear_page_index(dst_vma, dst_addr);
	pte_t new_dst_pte;
	void *old;
	int err = 0;
	int preloaded = 0;

	/*
	 * Pre-allocate radix tree nodes before taking spinlocks,
	 * since radix_tree_insert() may need memory.
	 */
	if (src_pgoff != dst_pgoff) {
		err = radix_tree_preload(GFP_KERNEL);
		if (err)
			return err;
		preloaded = 1;
	}

	uffd_double_pt_lock(dst_ptl, src_ptl);

	if (!pte_same(*src_pte, orig_src_pte) ||
	    !pte_same(*dst_pte, orig_dst_pte)) {
		err = -EAGAIN;
		goto out;
	}

	/* Shmem pages have an extra +1 baseline refcount from the radix tree */
	if (page_mapcount(src_page) != 1 || page_count(src_page) > 4) {
		err = -EBUSY;
		goto out;
	}

	/* Clear source PTE and flush TLB before any page cache changes */
	orig_src_pte = ptep_clear_flush(src_vma, src_addr, src_pte);

	if (page_count(src_page) > 4) {
		set_pte_at(mm, src_addr, src_pte, orig_src_pte);
		err = -EBUSY;
		goto out;
	}

	/*
	 * Relocate the page in the shmem page cache (radix tree)
	 * from the source file offset to the destination file offset.
	 */
	if (src_pgoff != dst_pgoff) {
		spin_lock_irq(&mapping->tree_lock);

		/* Remove page from old radix tree position */
		old = radix_tree_delete_item(&mapping->page_tree,
					    src_pgoff, src_page);
		if (old != src_page) {
			spin_unlock_irq(&mapping->tree_lock);
			/* Restore PTE - someone else modified the page cache */
			set_pte_at(mm, src_addr, src_pte, orig_src_pte);
			err = -EAGAIN;
			goto out;
		}

		/* Insert page at new radix tree position */
		err = radix_tree_insert(&mapping->page_tree, dst_pgoff,
					src_page);
		if (err) {
			/* Rollback: restore page at old position */
			radix_tree_insert(&mapping->page_tree, src_pgoff,
					  src_page);
			spin_unlock_irq(&mapping->tree_lock);
			set_pte_at(mm, src_addr, src_pte, orig_src_pte);
			goto out;
		}

		src_page->index = dst_pgoff;
		spin_unlock_irq(&mapping->tree_lock);
	}

	/*
	 * Build the destination PTE. Mark dirty after move
	 * (following mremap() semantics).
	 */
	new_dst_pte = mk_pte(src_page, dst_vma->vm_page_prot);
	new_dst_pte = pte_mkdirty(new_dst_pte);
	if (dst_vma->vm_flags & VM_WRITE)
		new_dst_pte = pte_mkwrite(new_dst_pte);

	set_pte_at(mm, dst_addr, dst_pte, new_dst_pte);
	update_mmu_cache(dst_vma, dst_addr, dst_pte);

out:
	uffd_double_pt_unlock(dst_ptl, src_ptl);
	if (preloaded)
		radix_tree_preload_end();
	return err;
}

static int move_swap_pte(struct mm_struct *mm,
			 unsigned long dst_addr, unsigned long src_addr,
			 pte_t *dst_pte, pte_t *src_pte,
			 pte_t orig_dst_pte, pte_t orig_src_pte,
			 spinlock_t *dst_ptl, spinlock_t *src_ptl)
{
	uffd_double_pt_lock(dst_ptl, src_ptl);

	if (!pte_same(*src_pte, orig_src_pte) ||
	    !pte_same(*dst_pte, orig_dst_pte)) {
		uffd_double_pt_unlock(dst_ptl, src_ptl);
		return -EAGAIN;
	}

	orig_src_pte = ptep_get_and_clear(mm, src_addr, src_pte);
	set_pte_at(mm, dst_addr, dst_pte, orig_src_pte);

	uffd_double_pt_unlock(dst_ptl, src_ptl);
	return 0;
}

/*
 * move_pages_pte - move a single page from src to dst
 *
 * This implements UFFDIO_MOVE at the PTE level. It handles:
 * - Present anonymous pages: moved atomically with rmap update
 * - Swap entries: moved atomically (swap entry relocated)
 * - Migration entries: waited upon, then retried
 * - Source holes (with ALLOW_SRC_HOLES): skipped without error
 *
 * Called with mmap_sem held for reading.
 */
static int move_pages_pte(struct mm_struct *mm, pmd_t *dst_pmd, pmd_t *src_pmd,
			  struct vm_area_struct *dst_vma,
			  struct vm_area_struct *src_vma,
			  unsigned long dst_addr, unsigned long src_addr,
			  __u64 mode)
{
	pte_t orig_src_pte, orig_dst_pte;
	pte_t src_page_pte; /* PTE value at time src_page was pinned */
	spinlock_t *src_ptl, *dst_ptl;
	pte_t *src_pte = NULL;
	pte_t *dst_pte = NULL;
	struct page *src_page = NULL;
	struct anon_vma *src_anon_vma = NULL;
	int src_page_is_anon = 0;
	int err = 0;
	swp_entry_t entry;

	flush_cache_range(src_vma, src_addr, src_addr + PAGE_SIZE);
	mmu_notifier_invalidate_range_start(mm, src_addr, src_addr + PAGE_SIZE);

retry:
	src_pte = pte_offset_map(src_pmd, src_addr);
	dst_pte = pte_offset_map(dst_pmd, dst_addr);

	src_ptl = pte_lockptr(mm, src_pmd);
	dst_ptl = pte_lockptr(mm, dst_pmd);

	/*
	 * Sanity checks: PMDs must be valid and not huge at this point.
	 */
	if (WARN_ON_ONCE(pmd_none(*dst_pmd)) ||
	    WARN_ON_ONCE(pmd_none(*src_pmd)) ||
	    WARN_ON_ONCE(pmd_trans_huge(*dst_pmd)) ||
	    WARN_ON_ONCE(pmd_trans_huge(*src_pmd))) {
		err = -EINVAL;
		goto out;
	}

	/* Read PTEs under their respective locks */
	spin_lock(dst_ptl);
	orig_dst_pte = *dst_pte;
	spin_unlock(dst_ptl);
	if (!pte_none(orig_dst_pte)) {
		err = -EEXIST;
		goto out;
	}

	spin_lock(src_ptl);
	orig_src_pte = *src_pte;
	spin_unlock(src_ptl);
	if (pte_none(orig_src_pte)) {
		if (!(mode & UFFDIO_MOVE_MODE_ALLOW_SRC_HOLES))
			err = -ENOENT;
		else
			err = 0; /* nothing to do for a hole */
		goto out;
	}

	/* If PTE changed after we locked the page, start over */
	if (src_page && unlikely(!pte_same(src_page_pte, orig_src_pte))) {
		err = -EAGAIN;
		goto out;
	}

	if (pte_present(orig_src_pte)) {
		/*
		 * Pin and lock the source page. Accept both anonymous
		 * pages and shmem (memfd/tmpfs) pages. Since we cannot
		 * block under PTL, on contention we unmap PTEs, obtain
		 * the lock and retry.
		 */
		if (!src_page) {
			struct page *page;

			spin_lock(src_ptl);
			if (!pte_same(orig_src_pte, *src_pte)) {
				spin_unlock(src_ptl);
				err = -EAGAIN;
				goto out;
			}

			page = vm_normal_page(src_vma, src_addr, orig_src_pte);
			if (!page) {
				spin_unlock(src_ptl);
				
				/* Handle zero-page move */
				if (is_zero_pfn(pte_pfn(orig_src_pte))) {
					pte_t _dst_pte;
					
					/* Lock both page tables to safely swap the zero page */
					uffd_double_pt_lock(dst_ptl, src_ptl);
					if (!pte_same(*src_pte, orig_src_pte) || !pte_none(*dst_pte)) {
						uffd_double_pt_unlock(dst_ptl, src_ptl);
						err = -EAGAIN;
						goto out;
					}
					
					/* Unmap from source, map to destination */
					orig_src_pte = ptep_clear_flush(src_vma, src_addr, src_pte);
					_dst_pte = pte_mkspecial(pfn_pte(my_zero_pfn(dst_addr), dst_vma->vm_page_prot));
					set_pte_at(mm, dst_addr, dst_pte, _dst_pte);
					update_mmu_cache(dst_vma, dst_addr, dst_pte);
					
					uffd_double_pt_unlock(dst_ptl, src_ptl);
					err = 0;
				} else {
					/* Not a zero page, truly a hole or special mapped IO */
					if (!(mode & UFFDIO_MOVE_MODE_ALLOW_SRC_HOLES))
						err = -ENOENT;
					else
						err = 0;
				}
				goto out;
			}

		if (!PageAnon(page) &&
		    !(page->mapping &&
		      shmem_mapping(page->mapping))) {
			spin_unlock(src_ptl);
			err = -EBUSY;
			goto out;
		}

			get_page(page);
			src_page = page;
			src_page_is_anon = PageAnon(page);
			src_page_pte = orig_src_pte;
			spin_unlock(src_ptl);

			if (!trylock_page(src_page)) {
				pte_unmap(src_pte);
				pte_unmap(dst_pte);
				src_pte = dst_pte = NULL;
				/* Now we can block and wait */
				lock_page(src_page);
				goto retry;
			}

			if (src_page_is_anon &&
			    WARN_ON_ONCE(!PageAnon(src_page))) {
				err = -EBUSY;
				goto out;
			}
		}

		if (src_page_is_anon) {
			/* Anonymous page: lock anon_vma for rmap safety */
			if (!src_anon_vma) {
				/*
				 * folio_referenced walks the anon_vma chain
				 * without the page lock. Serialize against
				 * it with the anon_vma lock; the page lock
				 * alone is not enough.
				 */
				src_anon_vma = page_get_anon_vma(src_page);
				if (!src_anon_vma) {
					/* Page was unmapped from under us */
					err = -EAGAIN;
					goto out;
				}
				if (!down_write_trylock(
					    &src_anon_vma->root->rwsem)) {
					pte_unmap(src_pte);
					pte_unmap(dst_pte);
					src_pte = dst_pte = NULL;
					/* Now we can block and wait */
					anon_vma_lock_write(src_anon_vma);
					goto retry;
				}
			}

			err = move_present_pte(mm, dst_vma, src_vma,
					       dst_addr, src_addr,
					       dst_pte, src_pte,
					       orig_dst_pte, orig_src_pte,
					       dst_ptl, src_ptl, src_page);
		} else {
			/*
			 * Shmem page: move via page cache update.
			 * No anon_vma locking needed - shmem rmap uses
			 * the address_space VMA interval tree.
			 */
			err = move_present_pte_shmem(mm, dst_vma, src_vma,
						    dst_addr, src_addr,
						    dst_pte, src_pte,
						    orig_dst_pte, orig_src_pte,
						    dst_ptl, src_ptl,
						    src_page);
		}
	} else {
		/*
		 * Non-present PTE: could be a swap entry, migration
		 * entry, or hwpoison entry.
		 */
		entry = pte_to_swp_entry(orig_src_pte);
		if (non_swap_entry(entry)) {
			if (is_migration_entry(entry)) {
				pte_unmap(src_pte);
				pte_unmap(dst_pte);
				src_pte = dst_pte = NULL;
				migration_entry_wait(mm, src_pmd, src_addr);
				err = -EAGAIN;
			} else {
				err = -EFAULT;
			}
			goto out;
		}

		/*
		 * Regular swap entry: move it atomically to destination.
		 */
		err = move_swap_pte(mm, dst_addr, src_addr,
				    dst_pte, src_pte,
				    orig_dst_pte, orig_src_pte,
				    dst_ptl, src_ptl);
	}

out:
	if (src_anon_vma) {
		anon_vma_unlock_write(src_anon_vma);
		put_anon_vma(src_anon_vma);
	}
	if (src_page) {
		unlock_page(src_page);
		put_page(src_page);
	}
	if (dst_pte)
		pte_unmap(dst_pte);
	if (src_pte)
		pte_unmap(src_pte);
	mmu_notifier_invalidate_range_end(mm, src_addr, src_addr + PAGE_SIZE);
	return err;
}

/*
 * Validate that both VMAs are suitable for UFFDIO_MOVE:
 * - Same access/protection flags
 * - Both anonymous, both writable
 * - Dst has userfaultfd context
 * - Dst anon_vma is prepared
 */
static int validate_move_areas(struct userfaultfd_ctx *ctx,
			       struct vm_area_struct *src_vma,
			       struct vm_area_struct *dst_vma)
{
	if ((src_vma->vm_flags & (VM_READ | VM_WRITE | VM_EXEC)) !=
	    (dst_vma->vm_flags & (VM_READ | VM_WRITE | VM_EXEC)))
		return -EINVAL;

	if (pgprot_val(src_vma->vm_page_prot) !=
	    pgprot_val(dst_vma->vm_page_prot))
		return -EINVAL;

	if (!(src_vma->vm_flags & VM_WRITE))
		return -EINVAL;

	if (!dst_vma->vm_userfaultfd_ctx.ctx ||
	    dst_vma->vm_userfaultfd_ctx.ctx != ctx)
		return -EINVAL;

	/*
	 * Accept both pure anonymous VMAs and shmem (memfd/tmpfs) VMAs.
	 * Shmem VMAs are used by modern Android (ART) for the Dalvik heap
	 * via memfd. These VMAs have shmem_vm_ops, so vma_is_anonymous()
	 * returns false, but they can contain both anonymous pages (from
	 * UFFDIO_COPY or COW) and shmem page cache pages.
	 */
	if (vma_is_anonymous(src_vma) && vma_is_anonymous(dst_vma)) {
		/* Both anonymous - original path */
		if (unlikely(anon_vma_prepare(dst_vma)))
			return -ENOMEM;
		return 0;
	}

	if (vma_is_shmem(src_vma) && vma_is_shmem(dst_vma)) {
		/*
		 * Both shmem. For safety, require they map the same
		 * underlying file (same address_space / inode).
		 */
		if (src_vma->vm_file->f_mapping !=
		    dst_vma->vm_file->f_mapping)
			return -EINVAL;

		/*
		 * Prepare anon_vma for dst even on shmem VMAs, because
		 * the pages may be anonymous (placed by UFFDIO_COPY or
		 * COW'd), and move_present_pte needs dst_vma->anon_vma.
		 */
		if (unlikely(anon_vma_prepare(dst_vma)))
			return -ENOMEM;
		return 0;
	}

	/* Mixed anon/shmem or unsupported VMA types */
	return -EINVAL;
}

/**
 * mcopy_atomic_move - atomically move anonymous pages via UFFDIO_MOVE
 * @ctx:        userfaultfd context (for validation)
 * @dst_mm:     target mm_struct (same as source, since this is within one
 *              address space)
 * @dst_start:  start of destination virtual memory range
 * @src_start:  start of source virtual memory range
 * @len:        length of the range
 * @mode:       UFFDIO_MOVE_MODE flags
 *
 * Backport of Linux 6.8 move_pages() for UFFDIO_MOVE. Moves physical
 * pages atomically between two anonymous VMAs in zero-copy fashion.
 *
 * The source pages must have mapcount == 1 (exclusively mapped).
 * Proper locking order: mmap_sem(read) -> page lock -> anon_vma lock -> PTL.
 *
 * Returns the number of bytes moved, or negative error.
 */
ssize_t mcopy_atomic_move(struct userfaultfd_ctx *ctx,
			  struct mm_struct *dst_mm,
			  unsigned long dst_start,
			  unsigned long src_start,
			  unsigned long len,
			  __u64 mode)
{
	struct vm_area_struct *dst_vma, *src_vma;
	ssize_t err;
	pmd_t *dst_pmd, *src_pmd;
	unsigned long src_addr, dst_addr;
	long moved = 0;

	BUG_ON(dst_start & ~PAGE_MASK);
	BUG_ON(src_start & ~PAGE_MASK);
	BUG_ON(len & ~PAGE_MASK);

	if (src_start + len <= src_start || dst_start + len <= dst_start)
		return -EINVAL;

	/*
	 * Flush all remote CPU pagevec caches before attempting to move memory.
	 * This forces all newly allocated pages onto the formal LRU list,
	 * ensuring accurate page mapcounts by resolving transient +1 refcounts.
	 */
	lru_add_drain_all();

	down_read(&dst_mm->mmap_sem);

	err = -ENOENT;
	src_vma = find_vma(dst_mm, src_start);
	if (!src_vma)
		goto out_unlock;
	if (src_vma->vm_flags & VM_SHARED)
		goto out_unlock;
	if (src_start < src_vma->vm_start || src_start + len > src_vma->vm_end)
		goto out_unlock;

	dst_vma = find_vma(dst_mm, dst_start);
	if (!dst_vma)
		goto out_unlock;
	if (dst_vma->vm_flags & VM_SHARED)
		goto out_unlock;
	if (dst_start < dst_vma->vm_start || dst_start + len > dst_vma->vm_end)
		goto out_unlock;

	err = validate_move_areas(ctx, src_vma, dst_vma);
	if (err)
		goto out_unlock;

	for (src_addr = src_start, dst_addr = dst_start;
	     src_addr < src_start + len; ) {

		pmd_t dst_pmdval;

		src_pmd = mm_alloc_pmd(dst_mm, src_addr);
		if (unlikely(!src_pmd)) {
			err = -ENOMEM;
			break;
		}

		if (pmd_none(*src_pmd)) {
			if (!(mode & UFFDIO_MOVE_MODE_ALLOW_SRC_HOLES)) {
				err = -ENOENT;
				break;
			}
			if (unlikely(__pte_alloc(dst_mm, src_vma, src_pmd,
						 src_addr))) {
				err = -ENOMEM;
				break;
			}
		}

		if (unlikely(pmd_trans_huge(*src_pmd))) {
			err = -EBUSY;
			break;
		}

		dst_pmd = mm_alloc_pmd(dst_mm, dst_addr);
		if (!dst_pmd) {
			err = -ENOMEM;
			break;
		}

		dst_pmdval = pmd_read_atomic(dst_pmd);
		if (unlikely(pmd_trans_huge(dst_pmdval))) {
			err = -EEXIST;
			break;
		}

		if (unlikely(pmd_none(dst_pmdval)) &&
		    unlikely(__pte_alloc(dst_mm, dst_vma, dst_pmd,
					 dst_addr))) {
			err = -ENOMEM;
			break;
		}
		if (unlikely(pmd_trans_huge(*dst_pmd))) {
			err = -EFAULT;
			break;
		}

		err = move_pages_pte(dst_mm, dst_pmd, src_pmd,
				     dst_vma, src_vma,
				     dst_addr, src_addr, mode);

		cond_resched();

		if (fatal_signal_pending(current)) {
			if (!err || err == -EAGAIN)
				err = -EINTR;
			break;
		}

		if (err) {
			if (err == -EAGAIN) {
				continue;
			}
			break;
		}

		dst_addr += PAGE_SIZE;
		src_addr += PAGE_SIZE;
		moved += PAGE_SIZE;
	}

out_unlock:
	up_read(&dst_mm->mmap_sem);
	return moved ? moved : err;
}

ssize_t mfill_zeropage(struct mm_struct *dst_mm, unsigned long start,
		       unsigned long len)
{
	return __mcopy_atomic(dst_mm, start, 0, len, true);
}
