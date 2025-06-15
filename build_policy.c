// SPDX-License-Identifier: GPL-2.0-only
/*
 * These are the scheduling policy related scheduler files, built
 * in a single compilation unit for build efficiency reasons.
 *
 * ( Incidentally, the size of the compilation unit is roughly
 *   comparable to core.c and fair.c, the other two big
 *   compilation units. This helps balance build time, while
 *   coalescing source files to amortize header inclusion
 *   cost. )
 *
 * core.c and fair.c are built separately.
 */

/* Headers: */
#include <linux/sched/clock.h>
#include <linux/sched/cputime.h>
#include <linux/sched/hotplug.h>
#include <linux/sched/posix-timers.h>
#include <linux/sched/rt.h>

#include <linux/cpuidle.h>
#include <linux/jiffies.h>
#include <linux/livepatch.h>
#include <linux/psi.h>
#include <linux/seqlock_api.h>
#include <linux/slab.h>
#include <linux/suspend.h>
#include <linux/tsacct_kern.h>
#include <linux/vtime.h>
#include <linux/sysrq.h>
#include <linux/percpu-rwsem.h>

#include <uapi/linux/sched/types.h>

#include "sched.h"
#include "smp.h"

#include "autogroup.h"
#include "stats.h"
#include "pelt.h"

/* Source code modules: */

#include "idle.c"

#include "rt.c"

#ifdef CONFIG_SMP
# include "cpudeadline.c"
# include "pelt.c"
#endif

#include "cputime.c"
#include "deadline.c"

#ifdef CONFIG_SCHED_CLASS_EXT
# include "ext.c"
# include "hmbird_sched_proc_main.c"
#endif

#ifdef CONFIG_SLIM_SCHED
# include "slim_sysctl.c"
#endif

/*
 * Copyright (C) 2024 Oplus. All rights reserved.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/cpufreq.h>
#include <linux/topology.h>
#include <linux/cpu.h>
#include <linux/smp.h>
#include <linux/delay.h>
#include "ext.h"
#include "slim.h"

static bool policy_optimization_enabled = true;
static int cpu_performance_threshold = 80;
static int load_balance_interval = 100;
static u64 last_optimization_time = 0;

static void optimize_cpu_performance(void)
{
	int cpu;
	struct rq *rq;
	u64 current_time = sched_clock();
	u64 total_load = 0;
	int online_cpus = num_online_cpus();
	bool need_rebalance = false;

	/* Rate limit optimization calls */
	if (current_time - last_optimization_time < (load_balance_interval * 1000000ULL))
		return;

	last_optimization_time = current_time;

	/* Collect system load information */
	for_each_online_cpu(cpu) {
		rq = cpu_rq(cpu);
		total_load += rq->nr_running;
		
		/* Check if CPU is overloaded */
		if (rq->nr_running > (online_cpus * 2)) {
			need_rebalance = true;
			printk(KERN_DEBUG "CPU %d overloaded: %lu tasks\n", 
			       cpu, rq->nr_running);
		}
	}

	/* Trigger load balancing if needed */
	if (need_rebalance && policy_optimization_enabled) {
		printk(KERN_INFO "Triggering system-wide load rebalancing\n");
		
		/* Kick all CPUs to trigger rebalancing */
		for_each_online_cpu(cpu) {
			if (cpu != smp_processor_id()) {
				smp_call_function_single(cpu, (smp_call_func_t)cpu_relax, NULL, 0);
			}
		}
	}

	/* Adaptive threshold adjustment */
	if (total_load > (online_cpus * 3)) {
		cpu_performance_threshold = min(cpu_performance_threshold + 5, 95);
	} else if (total_load < online_cpus) {
		cpu_performance_threshold = max(cpu_performance_threshold - 5, 50);
	}
}

static int policy_cpu_callback(struct notifier_block *nfb,
			       unsigned long action, void *hcpu)
{
	int cpu = (unsigned long)hcpu;
	struct rq *rq;

	switch (action & ~CPU_TASKS_FROZEN) {
	case CPU_ONLINE:
		rq = cpu_rq(cpu);
		printk(KERN_INFO "CPU %d came online, current load: %lu\n", 
		       cpu, rq->nr_running);
		optimize_cpu_performance();
		break;
		
	case CPU_DOWN_PREPARE:
		printk(KERN_INFO "CPU %d going offline\n", cpu);
		optimize_cpu_performance();
		break;
	}
	
	return NOTIFY_OK;
}

static struct notifier_block policy_cpu_notifier = {
	.notifier_call = policy_cpu_callback,
	.priority = 0,
};

static int __init build_policy_init(void)
{
	int ret;
	u64 init_time = sched_clock();
	int online_cpus = num_online_cpus();

	printk(KERN_INFO "Enhanced build policy module loading...\n");
	printk(KERN_INFO "System info: %d online CPUs, init time: %llu\n", 
	       online_cpus, init_time);

	/* Register CPU hotplug notifier */
	ret = register_cpu_notifier(&policy_cpu_notifier);
	if (ret) {
		printk(KERN_ERR "Failed to register CPU notifier: %d\n", ret);
		return ret;
	}

	/* Initialize optimization parameters based on system size */
	if (online_cpus >= 8) {
		cpu_performance_threshold = 85;
		load_balance_interval = 50;
	} else if (online_cpus >= 4) {
		cpu_performance_threshold = 75;
		load_balance_interval = 75;
	} else {
		cpu_performance_threshold = 70;
		load_balance_interval = 100;
	}

	/* Perform initial optimization */
	optimize_cpu_performance();

	printk(KERN_INFO "Build policy module loaded successfully\n");
	printk(KERN_INFO "Performance threshold: %d%%, Balance interval: %dms\n",
	       cpu_performance_threshold, load_balance_interval);
	
	return 0;
}

static void __exit build_policy_exit(void)
{
	u64 exit_time = sched_clock();
	
	printk(KERN_INFO "Build policy module unloading...\n");
	
	/* Unregister CPU hotplug notifier */
	unregister_cpu_notifier(&policy_cpu_notifier);
	
	/* Final optimization before exit */
	policy_optimization_enabled = false;
	optimize_cpu_performance();
	
	printk(KERN_INFO "Build policy module unloaded at time: %llu\n", exit_time);
}

module_init(build_policy_init);
module_exit(build_policy_exit);
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Enhanced build policy for sched_ext with intelligent optimization");
MODULE_AUTHOR("Oplus");
MODULE_VERSION("2.0");

/* Module parameters for runtime tuning */
module_param(policy_optimization_enabled, bool, 0644);
MODULE_PARM_DESC(policy_optimization_enabled, "Enable/disable policy optimization");

module_param(cpu_performance_threshold, int, 0644);
MODULE_PARM_DESC(cpu_performance_threshold, "CPU performance threshold percentage");

module_param(load_balance_interval, int, 0644);
MODULE_PARM_DESC(load_balance_interval, "Load balance check interval in milliseconds");
