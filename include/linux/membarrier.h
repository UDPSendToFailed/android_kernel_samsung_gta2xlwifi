/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_MEMBARRIER_H
#define _LINUX_MEMBARRIER_H

#include <uapi/linux/membarrier.h>

#ifdef CONFIG_MEMBARRIER
enum {
	MEMBARRIER_STATE_PRIVATE_EXPEDITED_READY	= (1U << 0),
};

static inline void membarrier_execve(struct task_struct *t)
{
	atomic_set(&t->mm->membarrier_state, 0);
}
#else
static inline void membarrier_execve(struct task_struct *t)
{
}
#endif

#endif /* _LINUX_MEMBARRIER_H */
