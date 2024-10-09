// SPDX-License-Identifier: GPL-2.0-only
/*
 * SMP initialisation and IPI support
 * Based on arch/arm64/kernel/smp.c
 *
 * Copyright (C) 2012 ARM Ltd.
 * Copyright (C) 2015 Regents of the University of California
 * Copyright (C) 2017 SiFive
 */

#include <linux/arch_topology.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/kernel_stat.h>
#include <linux/notifier.h>
#include <linux/cpu.h>
#include <linux/percpu.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/irq.h>
#include <linux/of.h>
#include <linux/sched/task_stack.h>
#include <linux/sched/mm.h>
#include <asm/cpu_ops.h>
#include <asm/irq.h>
#include <asm/mmu_context.h>
#include <asm/numa.h>
#include <asm/tlbflush.h>
#include <asm/sections.h>
#include <asm/sbi.h>
#include <asm/smp.h>
#include <asm/processor.h>

#include "head.h"

static DECLARE_COMPLETION(cpu_running);

// BEANDIP

DEFINE_PER_CPU(struct beandip_info, beandip_info);

u32 beandip_get_poll_count(unsigned int cpu_id)
{
	struct beandip_info *bi = per_cpu_ptr(&beandip_info, cpu_id);

	return bi->poll_count;
}
EXPORT_SYMBOL(beandip_get_poll_count);

u32 beandip_get_hwint_count(unsigned int cpu_id)
{
	struct beandip_info *bi = per_cpu_ptr(&beandip_info, cpu_id);

	return bi->hwint_count;
}
EXPORT_SYMBOL(beandip_get_hwint_count);

// END BEANDIP

void __init smp_prepare_boot_cpu(void)
{
}

void __init smp_prepare_cpus(unsigned int max_cpus)
{
	int cpuid;
	int ret;
	unsigned int curr_cpuid;

	init_cpu_topology();

	curr_cpuid = smp_processor_id();
	store_cpu_topology(curr_cpuid);
	numa_store_cpu_info(curr_cpuid);
	numa_add_cpu(curr_cpuid);

	/* This covers non-smp usecase mandated by "nosmp" option */
	if (max_cpus == 0)
		return;

	for_each_possible_cpu(cpuid) {
		if (cpuid == curr_cpuid)
			continue;
		if (cpu_ops[cpuid]->cpu_prepare) {
			ret = cpu_ops[cpuid]->cpu_prepare(cpuid);
			if (ret)
				continue;
		}
		set_cpu_present(cpuid, true);
		numa_store_cpu_info(cpuid);
	}
}

void __init setup_smp(void)
{
	struct device_node *dn;
	unsigned long hart;
	bool found_boot_cpu = false;
	int cpuid = 1;
	int rc;

	cpu_set_ops(0);

	for_each_of_cpu_node(dn) {
		rc = riscv_of_processor_hartid(dn, &hart);
		if (rc < 0)
			continue;

		if (hart == cpuid_to_hartid_map(0)) {
			BUG_ON(found_boot_cpu);
			found_boot_cpu = 1;
			early_map_cpu_to_node(0, of_node_to_nid(dn));
			continue;
		}
		if (cpuid >= NR_CPUS) {
			pr_warn("Invalid cpuid [%d] for hartid [%lu]\n",
				cpuid, hart);
			continue;
		}

		cpuid_to_hartid_map(cpuid) = hart;
		early_map_cpu_to_node(cpuid, of_node_to_nid(dn));
		cpuid++;
	}

	BUG_ON(!found_boot_cpu);

	if (cpuid > nr_cpu_ids)
		pr_warn("Total number of cpus [%d] is greater than nr_cpus option value [%d]\n",
			cpuid, nr_cpu_ids);

	for (cpuid = 1; cpuid < nr_cpu_ids; cpuid++) {
		if (cpuid_to_hartid_map(cpuid) != INVALID_HARTID) {
			cpu_set_ops(cpuid);
			set_cpu_possible(cpuid, true);
		}
	}
}

static int start_secondary_cpu(int cpu, struct task_struct *tidle)
{
	if (cpu_ops[cpu]->cpu_start)
		return cpu_ops[cpu]->cpu_start(cpu, tidle);

	return -EOPNOTSUPP;
}

int __cpu_up(unsigned int cpu, struct task_struct *tidle)
{
	int ret = 0;
	tidle->thread_info.cpu = cpu;

	ret = start_secondary_cpu(cpu, tidle);
	if (!ret) {
		wait_for_completion_timeout(&cpu_running,
					    msecs_to_jiffies(1000));

		if (!cpu_online(cpu)) {
			pr_crit("CPU%u: failed to come online\n", cpu);
			ret = -EIO;
		}
	} else {
		pr_crit("CPU%u: failed to start\n", cpu);
	}

	return ret;
}

void __init smp_cpus_done(unsigned int max_cpus)
{
	// disable timer
	// csr_clear(CSR_IE, BIT(5));

	int cpu;
	struct beandip_info *bi;

	pr_info("SMP: Total of %d processors activated.\n", num_online_cpus());

	for_each_online_cpu(cpu) {
		pr_info("beandip init CPU: %d\n", cpu);
		bi = per_cpu_ptr(&beandip_info, cpu);
		bi->poll_count = 0;
		bi->hwint_count = 0;
    }

	// this is where it is safe to now start writing per-cpu data

	// for_each_irq_desc(i, desc) {
	// 	struct irq_chip *chip;
	// 	int ret;

	// 	chip = irq_desc_get_chip(desc);
	// 	if (!chip)
	// 		continue;

	// 	int  hwirq = desc->irq_data.hwirq;

	// 	pr_info("beandip hwirq: %d\n", hwirq);

	// 	int cpu;

	// 	for_each_online_cpu(cpu) {
	// 		struct plic_handler *handler = per_cpu_ptr(&plic_handlers, cpu);

	// 		plic_toggle(handler, hwirq, 0);
	// 	}
	// }

	pr_info("beandip initialized.\n");

	// unsigned int irq = plic_irq_claim();
	// pr_info("IRQ: %ld\n", irq);
}

/*
 * C entry point for a secondary processor.
 */
asmlinkage __visible void smp_callin(void)
{
	struct mm_struct *mm = &init_mm;
	unsigned int curr_cpuid = smp_processor_id();

	riscv_clear_ipi();

	/* All kernel threads share the same mm context.  */
	mmgrab(mm);
	current->active_mm = mm;

	store_cpu_topology(curr_cpuid);
	notify_cpu_starting(curr_cpuid);
	numa_add_cpu(curr_cpuid);
	set_cpu_online(curr_cpuid, 1);

	/*
	 * Remote TLB flushes are ignored while the CPU is offline, so emit
	 * a local TLB flush right now just in case.
	 */
	local_flush_tlb_all();
	complete(&cpu_running);
	/*
	 * Disable preemption before enabling interrupts, so we don't try to
	 * schedule a CPU that hasn't actually started yet.
	 */
	local_irq_enable();
	cpu_startup_entry(CPUHP_AP_ONLINE_IDLE);
}

static volatile int vol = 0;

void beandip_static_guarded_poll(int poll_site_id, uint64_t target_interval)
{
	struct beandip_info *bi;
	unsigned int hwirq;

	if (!beandip_is_ready()) {
		return;
	}

	bi = this_cpu_ptr(&beandip_info);
	bi->poll_count++;

	if (arch_irqs_disabled()) {
		return;
	}
	hwirq = plic_irq_claim_handle();

	// if (hwirq) {
	// 	printk(KERN_INFO "IRQ: %ld\n", hwirq);
	// }

	*(volatile int *)(&vol) = 9;
}

void beandip_static_accum_thread_local_latency(uint64_t latency,
					       uint64_t targetInterval)
{
	// tot += latency;
	// if (tot >= targetInterval) {
	//   beandip_static_poll(0);
	//   tot = 0;
	// }
}

static inline bool beandip_static_preheader_callback(
	int poll_site_id, int64_t first, int64_t last, int64_t step,
	int64_t LLS, uint32_t *currLatency, uint64_t targetInterval,
	uint64_t finalInterval)
{
	int64_t iterations = (last - first) / step;
	int64_t totalLoopLatency = iterations * LLS;
	// printf("Total loop latency: %ld, %ld, %ld\n", totalLoopLatency, iterations, LLS);
	if (totalLoopLatency < targetInterval) {
		*currLatency += totalLoopLatency;
		if (*currLatency > targetInterval) {
			*currLatency = 0;
			beandip_static_guarded_poll(poll_site_id,
						    finalInterval);
		}
		return false;
	}

	return true;
}

bool beandip_static_preheader_callback_i8(int poll_site_id, int8_t first,
					  int8_t last, int8_t step, int64_t LLS,
					  uint32_t *currLatency,
					  uint64_t targetInterval,
					  uint64_t finalInterval)
{
	return beandip_static_preheader_callback(poll_site_id, (int64_t)first,
						 (int64_t)last, (int64_t)step,
						 LLS, currLatency,
						 targetInterval, finalInterval);
}

bool beandip_static_preheader_callback_i16(int poll_site_id, int16_t first,
					   int16_t last, int16_t step,
					   int64_t LLS, uint32_t *currLatency,
					   uint64_t targetInterval,
					   uint64_t finalInterval)
{
	return beandip_static_preheader_callback(poll_site_id, (int64_t)first,
						 (int64_t)last, (int64_t)step,
						 LLS, currLatency,
						 targetInterval, finalInterval);
}

bool beandip_static_preheader_callback_i32(int poll_site_id, int32_t first,
					   int32_t last, int32_t step,
					   int64_t LLS, uint32_t *currLatency,
					   uint64_t targetInterval,
					   uint64_t finalInterval)
{
	return beandip_static_preheader_callback(poll_site_id, (int64_t)first,
						 (int64_t)last, (int64_t)step,
						 LLS, currLatency,
						 targetInterval, finalInterval);
}

// return true if it is safe to do this loop without any polls then poll at the end (or just increment)
bool beandip_static_preheader_callback_i64(int poll_site_id, int64_t first,
					   int64_t last, int64_t step,
					   int64_t LLS, uint32_t *currLatency,
					   uint64_t targetInterval,
					   uint64_t finalInterval)
{
	return beandip_static_preheader_callback(poll_site_id, (int64_t)first,
						 (int64_t)last, (int64_t)step,
						 LLS, currLatency,
						 targetInterval, finalInterval);
}

// The initial latency value for latency approximation within the current function will be the return value
// of this function. This is useful for ensuring a function polls eventually if it is frequently called but
// does not have enough latency to ever cause a poll to fire when latency starts from zero each time
uint32_t beandip_static_enter_function(void)
{
	*(volatile int *)(&vol) = 5;
	return 0;
}

// init_latency - the initial latency that the current function had (from beandip_static_enter_function)
// final_latency - the latency at the end of this function
// This can be useful for storing the final latency of the function in a static variable, which
// can then be retrieved by beandip_static_enter_function to pick up where things were left off
void beandip_static_exit_function(uint32_t init_latency, uint32_t final_latency)
{
	*(volatile int *)(&vol) = 7;
}
