/*
 * \brief  Replaces kernel/sched/fair.c
 * \author Stefan Kalkowski
 * \date   2022-07-20
 */

/*
 * Copyright (C) 2022 Genode Labs GmbH
 *
 * This file is distributed under the terms of the GNU General Public License
 * version 2 or later.
 */

#include <linux/mmu_context.h>
#include <linux/sched/cputime.h>
#include <../kernel/sched/sched.h>
#include <linux/sched/nohz.h>

#ifdef CONFIG_SMP
void nohz_balance_enter_idle(int cpu) { }
#endif
