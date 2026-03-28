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
#include <linux/page-flags.h>
#include <asm/tlbflush.h>
#include "internal.h"

#ifdef CONFIG_USERFAULTFD_DEBUG
static atomic_long_t uffd_copy_count = ATOMIC_LONG_INIT(0);
static atomic_long_t uffd_zero_count = ATOMIC_LONG_INIT(0);
static atomic_long_t uffd_move_count = ATOMIC_LONG_INIT(0);
static atomic_long_t uffd_move_pte_count = ATOMIC_LONG_INIT(0);
atomic_long_t uffd_sigbus_count = ATOMIC_LONG_INIT(0);
static atomic_long_t uffd_trylock_fail_count = ATOMIC_LONG_INIT(0);
#endif

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

		flush_dcache_page(page);
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
	if (mem_cgroup_try_charge(page, dst_mm, GFP_KERNEL, &memcg, false))
		goto out_release;

	_dst_pte = mk_pte(page, dst_vma->vm_page_prot);
	if (dst_vma->vm_flags & VM_WRITE)
		_dst_pte = pte_mkwrite(pte_mkdirty(_dst_pte));

	ret = -EEXIST;
	dst_pte = pte_offset_map_lock(dst_mm, dst_pmd, dst_addr, &ptl);
	if (!pte_none(*dst_pte))
		goto out_release_uncharge_unlock;

	inc_mm_counter(dst_mm, MM_ANONPAGES);
	page_add_new_anon_rmap(page, dst_vma, dst_addr, false);
	mem_cgroup_commit_charge(page, memcg, false, false);
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
	mem_cgroup_cancel_charge(page, memcg, false);
out_release:
	put_page(page);
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
					      bool zeropage,
					      bool mmap_trylock)
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
	if (mmap_trylock) {
		if (!down_read_trylock(&dst_mm->mmap_sem)) {
#ifdef CONFIG_USERFAULTFD_DEBUG
			long cnt = atomic_long_inc_return(&uffd_trylock_fail_count);
			if (cnt <= 50 || (cnt % 1000) == 0)
				pr_warn("UFFD-DBG: %s trylock FAILED (pid=%d comm=%s cnt=%ld dst=0x%lx len=0x%lx zp=%d)\n",
					__func__, current->pid, current->comm, cnt,
					dst_start, len, zeropage);
#endif
			err = -EAGAIN;
			goto out;
		}
	} else {
		down_read(&dst_mm->mmap_sem);
	}

	/*
	 * Make sure the vma is not shared, that the dst range is
	 * both valid and fully within a single existing vma.
	 */
	err = -EINVAL;
	dst_vma = find_vma(dst_mm, dst_start);
	if (!dst_vma || (dst_vma->vm_flags & VM_SHARED))
		goto out_unlock;
	if (dst_start < dst_vma->vm_start ||
	    dst_start + len > dst_vma->vm_end)
		goto out_unlock;

	/*
	 * Check the vma is registered in uffd, this is required to
	 * enforce the VM_MAYWRITE check done at uffd registration
	 * time.
	 */
	if (!dst_vma->vm_userfaultfd_ctx.ctx)
		goto out_unlock;

	/*
	 * FIXME: only allow copying on anonymous vmas, tmpfs should
	 * be added.
	 */
	if (dst_vma->vm_ops)
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
		    unlikely(__pte_alloc(dst_mm, dst_pmd, dst_addr))) {
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

			up_read(&dst_mm->mmap_sem);
			BUG_ON(!page);

			page_kaddr = kmap(page);
			err = copy_from_user(page_kaddr,
					     (const void __user *) src_addr,
					     PAGE_SIZE);
			kunmap(page);
			if (unlikely(err)) {
				err = -EFAULT;
				goto out;
			}
			flush_dcache_page(page);
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
		put_page(page);
	BUG_ON(copied < 0);
	BUG_ON(err > 0);
	BUG_ON(!copied && !err);
	return copied ? copied : err;
}

ssize_t mcopy_atomic(struct mm_struct *dst_mm, unsigned long dst_start,
		     unsigned long src_start, unsigned long len,
		     bool mmap_trylock)
{
#ifdef CONFIG_USERFAULTFD_DEBUG
	ssize_t ret;
	long cnt = atomic_long_inc_return(&uffd_copy_count);

	if (cnt <= 20 || (cnt % 5000) == 0)
		pr_warn("UFFD-DBG: mcopy_atomic ENTER (pid=%d comm=%s cnt=%ld dst=0x%lx src=0x%lx len=0x%lx trylock=%d)\n",
			current->pid, current->comm, cnt, dst_start, src_start, len, mmap_trylock);

	ret = __mcopy_atomic(dst_mm, dst_start, src_start, len, false,
			      mmap_trylock);

	if (ret < 0 && cnt <= 20)
		pr_warn("UFFD-DBG: mcopy_atomic FAIL (pid=%d err=%zd dst=0x%lx)\n",
			current->pid, ret, dst_start);
	return ret;
#else
	return __mcopy_atomic(dst_mm, dst_start, src_start, len, false,
			      mmap_trylock);
#endif
}

ssize_t mfill_zeropage(struct mm_struct *dst_mm, unsigned long start,
		       unsigned long len, bool mmap_trylock)
{
#ifdef CONFIG_USERFAULTFD_DEBUG
	ssize_t ret;
	long cnt = atomic_long_inc_return(&uffd_zero_count);

	if (cnt <= 20 || (cnt % 5000) == 0)
		pr_warn("UFFD-DBG: mfill_zeropage ENTER (pid=%d comm=%s cnt=%ld dst=0x%lx len=0x%lx trylock=%d)\n",
			current->pid, current->comm, cnt, start, len, mmap_trylock);

	ret = __mcopy_atomic(dst_mm, start, 0, len, true, mmap_trylock);

	if (ret < 0 && cnt <= 20)
		pr_warn("UFFD-DBG: mfill_zeropage FAIL (pid=%d err=%zd dst=0x%lx)\n",
			current->pid, ret, start);
	return ret;
#else
	return __mcopy_atomic(dst_mm, start, 0, len, true, mmap_trylock);
#endif
}

/*
 * Helpers for dual-PTL locking used by UFFDIO_MOVE.
 * Lock in pointer-address order to prevent ABBA deadlocks.
 */
void double_pt_lock(spinlock_t *ptl1, spinlock_t *ptl2)
{
	if (ptl1 > ptl2)
		swap(ptl1, ptl2);
	spin_lock(ptl1);
	if (ptl1 != ptl2)
		spin_lock_nested(ptl2, SINGLE_DEPTH_NESTING);
}

void double_pt_unlock(spinlock_t *ptl1, spinlock_t *ptl2)
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

	double_pt_lock(dst_ptl, src_ptl);

	if (!pte_same(*src_pte, orig_src_pte) ||
	    !pte_same(*dst_pte, orig_dst_pte)) {
		err = -EAGAIN;
		goto out;
	}

	if (page_mapcount(src_page) != 1) {
		err = -EBUSY;
		goto out;
	}

	orig_src_pte = ptep_clear_flush(src_vma, src_addr, src_pte);

	/* Page got pinned from under us. Put it back and fail the move. */
	if (page_count(src_page) > 1 + page_mapcount(src_page)) {
#ifdef CONFIG_USERFAULTFD_DEBUG
		pr_warn("UFFD-DBG: move_present_pte EBUSY pinned count=%d mapcount=%d swapcache=%d lru=%d active=%d locked=%d pfn=%lx (pid=%d src=0x%lx)\n",
			page_count(src_page), page_mapcount(src_page),
			PageSwapCache(src_page), PageLRU(src_page),
			PageActive(src_page), PageLocked(src_page),
			page_to_pfn(src_page),
			current->pid, src_addr);
#endif
		set_pte_at(mm, src_addr, src_pte, orig_src_pte);
		err = -EBUSY;
		goto out;
	}

	src_page->index = linear_page_index(dst_vma, dst_addr);
	page_move_anon_rmap(src_page, dst_vma);

	orig_dst_pte = mk_pte(src_page, dst_vma->vm_page_prot);
	/* Follow mremap() behavior and treat the entry dirty after the move */
	orig_dst_pte = pte_mkwrite(pte_mkdirty(orig_dst_pte));

	set_pte_at(mm, dst_addr, dst_pte, orig_dst_pte);
out:
	double_pt_unlock(dst_ptl, src_ptl);
	return err;
}

static int move_swap_pte(struct mm_struct *mm,
			 unsigned long dst_addr, unsigned long src_addr,
			 pte_t *dst_pte, pte_t *src_pte,
			 pte_t orig_dst_pte, pte_t orig_src_pte,
			 spinlock_t *dst_ptl, spinlock_t *src_ptl)
{
	double_pt_lock(dst_ptl, src_ptl);

	if (!pte_same(*src_pte, orig_src_pte) ||
	    !pte_same(*dst_pte, orig_dst_pte)) {
		double_pt_unlock(dst_ptl, src_ptl);
		return -EAGAIN;
	}

	orig_src_pte = ptep_get_and_clear(mm, src_addr, src_pte);
	set_pte_at(mm, dst_addr, dst_pte, orig_src_pte);
	double_pt_unlock(dst_ptl, src_ptl);

	return 0;
}

/*
 * The mmap_sem for reading is held by the caller. Just move the page
 * from src_pmd to dst_pmd if possible, and return 0 if succeeded
 * in moving the page.
 */
static int move_pages_pte(struct mm_struct *mm, pmd_t *dst_pmd, pmd_t *src_pmd,
			  struct vm_area_struct *dst_vma,
			  struct vm_area_struct *src_vma,
			  unsigned long dst_addr, unsigned long src_addr,
			  __u64 mode)
{
	swp_entry_t entry;
	pte_t orig_src_pte, orig_dst_pte;
	pte_t src_folio_pte;
	spinlock_t *src_ptl, *dst_ptl;
	pte_t *src_pte = NULL;
	pte_t *dst_pte = NULL;

	struct page *src_page = NULL;
	struct anon_vma *src_anon_vma = NULL;
	int err = 0;

	flush_cache_range(src_vma, src_addr, src_addr + PAGE_SIZE);
	mmu_notifier_invalidate_range_start(mm, src_addr,
					    src_addr + PAGE_SIZE);
retry:
	dst_pte = pte_offset_map(dst_pmd, dst_addr);
	src_pte = pte_offset_map(src_pmd, src_addr);

	dst_ptl = pte_lockptr(mm, dst_pmd);
	src_ptl = pte_lockptr(mm, src_pmd);

	/* Sanity checks before the operation */
	if (WARN_ON_ONCE(pmd_none(*dst_pmd)) ||
	    WARN_ON_ONCE(pmd_none(*src_pmd)) ||
	    WARN_ON_ONCE(pmd_trans_huge(*dst_pmd)) ||
	    WARN_ON_ONCE(pmd_trans_huge(*src_pmd))) {
		err = -EINVAL;
		goto out;
	}

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
		else /* nothing to do to move a hole */
			err = 0;
		goto out;
	}

	/* If PTE changed after we locked the page, start over */
	if (src_page && unlikely(!pte_same(src_folio_pte, orig_src_pte))) {
		err = -EAGAIN;
		goto out;
	}

	if (pte_present(orig_src_pte)) {
		/*
		 * Pin and lock both source page and anon_vma. Since we are in
		 * RCU read section, we can't block, so on contention have to
		 * unmap the ptes, obtain the lock and retry.
		 */
		if (!src_page) {
			struct page *page;

			/*
			 * Pin the page while holding the lock to be sure the
			 * page isn't freed under us
			 */
			spin_lock(src_ptl);
			if (!pte_same(orig_src_pte, *src_pte)) {
				spin_unlock(src_ptl);
				err = -EAGAIN;
				goto out;
			}

			page = vm_normal_page(src_vma, src_addr, orig_src_pte);
			if (!page || !PageAnon(page)) {
				spin_unlock(src_ptl);
				err = -EBUSY;
				goto out;
			}

			get_page(page);
			src_page = page;
			src_folio_pte = orig_src_pte;
			spin_unlock(src_ptl);

			if (!trylock_page(src_page)) {
				pte_unmap(src_pte);
				pte_unmap(dst_pte);
				src_pte = dst_pte = NULL;
				/* now we can block and wait */
				lock_page(src_page);
				goto retry;
			}

			if (WARN_ON_ONCE(!PageAnon(src_page))) {
				err = -EBUSY;
				goto out;
			}

			/*
			 * Drop swap cache ref and drain current-CPU LRU
			 * pagevec to eliminate extra page_count references
			 * that would cause move_present_pte() to return
			 * -EBUSY.  The bulk lru_add_drain_all() in
			 * move_pages() handles most cases; this per-page
			 * drain catches pages added to the pagevec between
			 * the bulk drain and here.
			 */
			if (PageSwapCache(src_page))
				try_to_free_swap(src_page);

			lru_add_drain();
		}

		/* at this point we have src_page locked */
		if (!src_anon_vma) {
			/*
			 * folio_referenced walks the anon_vma chain
			 * without the page lock. Serialize against it with
			 * the anon_vma lock, the page lock is not enough.
			 */
			src_anon_vma = page_get_anon_vma(src_page);
			if (!src_anon_vma) {
				/* page was unmapped from under us */
				err = -EAGAIN;
				goto out;
			}
			if (!down_write_trylock(
				    &src_anon_vma->root->rwsem)) {
				pte_unmap(src_pte);
				pte_unmap(dst_pte);
				src_pte = dst_pte = NULL;
				/* now we can block and wait */
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
		entry = pte_to_swp_entry(orig_src_pte);
		if (non_swap_entry(entry)) {
			if (is_migration_entry(entry)) {
				pte_unmap(src_pte);
				pte_unmap(dst_pte);
				src_pte = dst_pte = NULL;
				migration_entry_wait(mm, src_pmd, src_addr);
				err = -EAGAIN;
			} else
				err = -EFAULT;
			goto out;
		}

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
	mmu_notifier_invalidate_range_end(mm, src_addr,
					  src_addr + PAGE_SIZE);
	return err;
}

static inline bool vma_move_compatible(struct vm_area_struct *vma)
{
	return !(vma->vm_flags & (VM_PFNMAP | VM_IO | VM_HUGETLB |
				  VM_MIXEDMAP));
}

static int validate_move_areas(struct userfaultfd_ctx *ctx,
			       struct vm_area_struct *src_vma,
			       struct vm_area_struct *dst_vma)
{
	/* Only allow moving if both have the same access and protection */
	if ((src_vma->vm_flags & (VM_READ | VM_WRITE | VM_EXEC)) !=
	    (dst_vma->vm_flags & (VM_READ | VM_WRITE | VM_EXEC)) ||
	    pgprot_val(src_vma->vm_page_prot) !=
	    pgprot_val(dst_vma->vm_page_prot))
		return -EINVAL;

	/* Only allow moving if both are mlocked or both aren't */
	if ((src_vma->vm_flags & VM_LOCKED) !=
	    (dst_vma->vm_flags & VM_LOCKED))
		return -EINVAL;

	/*
	 * For now, keep it simple and only move between writable VMAs.
	 * Access flags are equal, therefore checking only the source is enough.
	 */
	if (!(src_vma->vm_flags & VM_WRITE))
		return -EINVAL;

	/* Check if vma flags indicate content which can be moved */
	if (!vma_move_compatible(src_vma) || !vma_move_compatible(dst_vma))
		return -EINVAL;

	/* Ensure dst_vma is registered in uffd we are operating on */
	if (!dst_vma->vm_userfaultfd_ctx.ctx ||
	    dst_vma->vm_userfaultfd_ctx.ctx != ctx)
		return -EINVAL;

	/* Only allow moving across anonymous vmas */
	if (!vma_is_anonymous(src_vma) || !vma_is_anonymous(dst_vma))
		return -EINVAL;

	/*
	 * Ensure the dst_vma has a anon_vma or this page
	 * would get a NULL anon_vma when moved in the
	 * dst_vma.
	 */
	if (unlikely(anon_vma_prepare(dst_vma)))
		return -ENOMEM;

	return 0;
}

/**
 * move_pages - move arbitrary anonymous pages of an existing vma
 * @ctx: pointer to the userfaultfd context
 * @mm: the address space to move pages in
 * @dst_start: start of the destination virtual memory range
 * @src_start: start of the source virtual memory range
 * @len: length of the virtual memory range
 * @mode: flags from uffdio_move.mode
 *
 * Must be called with mmap_sem held for read.
 *
 * move_pages() remaps arbitrary anonymous pages atomically in zero
 * copy. It only works on non shared anonymous pages because those can
 * be relocated without generating non linear anon_vmas in the rmap
 * code.
 *
 * It provides a zero copy mechanism to handle userspace page faults.
 * The source vma pages should have mapcount == 1, which can be
 * enforced by using madvise(MADV_DONTFORK) on src vma.
 *
 * The thread receiving the page during the userland page fault
 * will receive the faulting page in the source vma through the network,
 * storage or any other I/O device (MADV_DONTFORK in the source vma
 * avoids move_pages() to fail with -EBUSY if the process forks before
 * move_pages() is called), then it will call move_pages() to map the
 * page in the faulting address in the destination vma.
 *
 * This userfaultfd command works purely via pagetables, so it's the
 * most efficient way to move physical non shared anonymous pages
 * across different virtual addresses. Unlike mremap()/mmap()/munmap()
 * it does not create any new vmas. The mapping in the destination
 * address is atomic.
 *
 * It only works if the vma protection bits are identical from the
 * source and destination vma.
 *
 * It can remap non shared anonymous pages within the same vma too.
 *
 * If the source virtual memory range has any unmapped holes, or if
 * the destination virtual memory range is not a whole unmapped hole,
 * move_pages() will fail respectively with -ENOENT or -EEXIST. This
 * provides a very strict behavior to avoid any chance of memory
 * corruption going unnoticed if there are userland race conditions.
 * Only one thread should resolve the userland page fault at any given
 * time for any given faulting address. This means that if two threads
 * try to both call move_pages() on the same destination address at the
 * same time, the second thread will get an explicit error from this
 * command.
 *
 * The command retval will return "len" is successful. The command
 * however can be interrupted by fatal signals or errors. If
 * interrupted it will return the number of bytes successfully
 * remapped before the interruption if any, or the negative error if
 * none. It will never return zero. Either it will return an error or
 * an amount of bytes successfully moved. If the retval reports a
 * "short" remap, the move_pages() command should be repeated by
 * userland with src+retval, dst+retval, len-retval if it wants to know
 * about the error that interrupted it.
 *
 * The UFFDIO_MOVE_MODE_ALLOW_SRC_HOLES flag can be specified to
 * prevent -ENOENT errors to materialize if there are holes in the
 * source virtual range that is being remapped. The holes will be
 * accounted as successfully remapped in the retval of the command.
 * This is mostly useful to remap hugepage naturally aligned virtual
 * regions without knowing if there are transparent hugepage in the
 * regions or not, but preventing the risk of having to split the
 * hugepmd during the remap.
 *
 * If there's any rmap walk that is taking the anon_vma locks without
 * first obtaining the page lock (the only current instance is
 * folio_referenced), they will have to verify if the page->mapping
 * has changed after taking the anon_vma lock. If it changed they
 * should release the lock and retry obtaining a new anon_vma, because
 * it means the anon_vma was changed by move_pages() before the lock
 * could be obtained. This is the only additional complexity added to
 * the rmap code to provide this anonymous page remapping functionality.
 */
ssize_t move_pages(struct userfaultfd_ctx *ctx, struct mm_struct *mm,
		   unsigned long dst_start, unsigned long src_start,
		   unsigned long len, __u64 mode)
{
	struct vm_area_struct *src_vma, *dst_vma;
	unsigned long src_addr, dst_addr;
	pmd_t *src_pmd, *dst_pmd;
	long err = -EINVAL;
	ssize_t moved = 0;

	/* Sanitize the command parameters. */
	BUG_ON(src_start & ~PAGE_MASK);
	BUG_ON(dst_start & ~PAGE_MASK);
	BUG_ON(len & ~PAGE_MASK);

	/* Does the address range wrap, or is the span zero-sized? */
	BUG_ON(src_start + len <= src_start);
	BUG_ON(dst_start + len <= dst_start);

	/*
	 * Make sure the vma is not shared, that the src and dst remap
	 * ranges are both valid and fully within a single existing vma.
	 */
	src_vma = find_vma(mm, src_start);
	if (!src_vma || (src_vma->vm_flags & VM_SHARED))
		goto out;
	if (src_start < src_vma->vm_start ||
	    src_start + len > src_vma->vm_end)
		goto out;

	dst_vma = find_vma(mm, dst_start);
	if (!dst_vma || (dst_vma->vm_flags & VM_SHARED))
		goto out;
	if (dst_start < dst_vma->vm_start ||
	    dst_start + len > dst_vma->vm_end)
		goto out;

	err = validate_move_areas(ctx, src_vma, dst_vma);
	if (err)
		goto out;

	/*
	 * Drain LRU pagevecs on all CPUs to minimize extra page references.
	 * Pages sitting in per-CPU lru_add_pvec have an extra get_page() ref
	 * from __lru_cache_add() that would cause move_present_pte() to
	 * return -EBUSY.  On 4.9, there is no GUP pin bias (no
	 * folio_maybe_dma_pinned), so page_count > 1 + page_mapcount is the
	 * only pin check, and LRU pagevec refs cause false positives.
	 */
	lru_add_drain_all();

	for (src_addr = src_start, dst_addr = dst_start;
	     src_addr < src_start + len;) {
		pmd_t dst_pmdval;

		src_pmd = mm_alloc_pmd(mm, src_addr);
		if (unlikely(!src_pmd)) {
			err = -ENOMEM;
			break;
		}

		dst_pmd = mm_alloc_pmd(mm, dst_addr);
		if (unlikely(!dst_pmd)) {
			err = -ENOMEM;
			break;
		}

		dst_pmdval = pmd_read_atomic(dst_pmd);
		/*
		 * If the dst_pmd is mapped as THP don't override it and just
		 * be strict. If dst_pmd changes into THP after this check,
		 * move_pages_pte() will detect the change and fail.
		 */
		if (unlikely(pmd_trans_huge(dst_pmdval))) {
			err = -EEXIST;
			break;
		}

		if (pmd_none(*src_pmd)) {
			if (!(mode & UFFDIO_MOVE_MODE_ALLOW_SRC_HOLES)) {
				err = -ENOENT;
				break;
			}
			if (unlikely(__pte_alloc(mm, src_pmd, src_addr))) {
				err = -ENOMEM;
				break;
			}
		}

		if (unlikely(pmd_trans_huge(*src_pmd))) {
			err = -EBUSY;
			break;
		}

		if (unlikely(pmd_none(dst_pmdval)) &&
		    unlikely(__pte_alloc(mm, dst_pmd, dst_addr))) {
			err = -ENOMEM;
			break;
		}
		if (unlikely(pmd_trans_huge(*dst_pmd))) {
			err = -EFAULT;
			break;
		}

		err = move_pages_pte(mm, dst_pmd, src_pmd,
				     dst_vma, src_vma,
				     dst_addr, src_addr, mode);

		cond_resched();

		if (fatal_signal_pending(current)) {
			/* Do not override an error */
			if (!err || err == -EAGAIN)
				err = -EINTR;
			break;
		}

		if (err) {
			if (err == -EAGAIN)
				continue;
			break;
		}

		/* Proceed to the next page */
		dst_addr += PAGE_SIZE;
		src_addr += PAGE_SIZE;
		moved += PAGE_SIZE;
	}

out:
	VM_WARN_ON(moved < 0);
	VM_WARN_ON(err > 0);
	VM_WARN_ON(!moved && !err);
	return moved ? moved : err;
}
