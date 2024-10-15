/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2012 Regents of the University of California
 */

#ifndef _ASM_RISCV_SMP_H
#define _ASM_RISCV_SMP_H

#include <linux/cpumask.h>
#include <linux/irqreturn.h>
#include <linux/thread_info.h>

#define INVALID_HARTID ULONG_MAX

struct seq_file;
extern unsigned long boot_cpu_hartid;

struct riscv_ipi_ops {
	void (*ipi_inject)(const struct cpumask *target);
	void (*ipi_clear)(void);
};

#ifdef CONFIG_SMP
/*
 * Mapping between linux logical cpu index and hartid.
 */
extern unsigned long __cpuid_to_hartid_map[NR_CPUS];
#define cpuid_to_hartid_map(cpu)    __cpuid_to_hartid_map[cpu]

/* print IPI stats */
void show_ipi_stats(struct seq_file *p, int prec);

/* SMP initialization hook for setup_arch */
void __init setup_smp(void);

/* Called from C code, this handles an IPI. */
void handle_IPI(struct pt_regs *regs);

/* Hook for the generic smp_call_function_many() routine. */
void arch_send_call_function_ipi_mask(struct cpumask *mask);

/* Hook for the generic smp_call_function_single() routine. */
void arch_send_call_function_single_ipi(int cpu);

int riscv_hartid_to_cpuid(unsigned long hartid);

/* Set custom IPI operations */
void riscv_set_ipi_ops(const struct riscv_ipi_ops *ops);

/* Clear IPI for current CPU */
void riscv_clear_ipi(void);

/* Check other CPUs stop or not */
bool smp_crash_stop_failed(void);

#define MAX_DEVICES			1024

struct plic_priv {
	struct cpumask lmask;
	struct irq_domain *irqdomain;
	void __iomem *regs;
	unsigned long plic_quirks;
	unsigned int nr_irqs;
	u32 *priority_reg;

	resource_size_t phys_start;
	resource_size_t phys_size;
};

struct plic_handler {
	bool			present;
	void __iomem		*hart_base;
	unsigned long hartid;
	int context_idx;
	/*
	 * Protect mask operations on the registers given that we can't
	 * assume atomic memory operations work on them.
	 */
	raw_spinlock_t		enable_lock;
	void __iomem		*enable_base;
	struct plic_priv	*priv;
	u32 enable_reg[MAX_DEVICES / 32];
};

DECLARE_PER_CPU(struct plic_handler, plic_handlers);

void plic_irq_claim_handle_cpu_hwirq(int cpuid, irq_hw_number_t hwirq);
irq_hw_number_t plic_irq_claim_handle_cpu(int cpuid);
irq_hw_number_t plic_irq_claim_handle(void);

void plic_toggle(struct plic_handler *handler, int hwirq, int enable);

void plic_set_threshold(struct plic_handler *handler, u32 threshold);

/* Secondary hart entry */
asmlinkage void smp_callin(void);

/*
 * Obtains the hart ID of the currently executing task.  This relies on
 * THREAD_INFO_IN_TASK, but we define that unconditionally.
 */
#define raw_smp_processor_id() (current_thread_info()->cpu)

#if defined CONFIG_HOTPLUG_CPU
int __cpu_disable(void);
void __cpu_die(unsigned int cpu);
#endif /* CONFIG_HOTPLUG_CPU */

#else

static inline void show_ipi_stats(struct seq_file *p, int prec)
{
}

static inline int riscv_hartid_to_cpuid(unsigned long hartid)
{
	if (hartid == boot_cpu_hartid)
		return 0;

	return -1;
}
static inline unsigned long cpuid_to_hartid_map(int cpu)
{
	return boot_cpu_hartid;
}

static inline void riscv_set_ipi_ops(const struct riscv_ipi_ops *ops)
{
}

static inline void riscv_clear_ipi(void)
{
}

#endif /* CONFIG_SMP */

#if defined(CONFIG_HOTPLUG_CPU) && (CONFIG_SMP)
bool cpu_has_hotplug(unsigned int cpu);
#else
static inline bool cpu_has_hotplug(unsigned int cpu)
{
	return false;
}
#endif

struct beandip_info {
	u32 poll_count;
	u32 hwint_count;
	u32 kernel_poll_hits;
	u32 userspace_poll_hits;
};

DECLARE_PER_CPU(struct beandip_info, beandip_info);

u32 beandip_get_poll_count(unsigned int cpu_id);

u32 beandip_get_hwint_count(unsigned int cpu_id);

u32 beandip_get_kernel_poll_hits(unsigned int cpu_id);

u32 beandip_get_userspace_poll_hits(unsigned int cpu_id);

struct irq_domain *get_intc_domain(void);

#define BEANDIP_IS_SIFIVE 1

#endif /* _ASM_RISCV_SMP_H */
