/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2021 Allwinner Ltd
 * Copyright (C) 2021 Western Digital Corporation or its affiliates.
 */

#ifndef _ASM_RISCV_CPUIDLE_H
#define _ASM_RISCV_CPUIDLE_H

#include <asm/barrier.h>
#include <asm/processor.h>
#include <asm/smp.h>
#include <linux/irq.h>
#include <linux/irqchip.h>
#include <linux/irqdomain.h>

static inline void cpu_do_idle(void)
{
	/*
	 * Add mb() here to ensure that all
	 * IO/MEM accesses are completed prior
	 * to entering WFI.
	 */
	mb();

	unsigned int hwirq;

	// pr_info("BEGIN CPU IDLE\n");

	// pr_info("Interrupt pending reg: %lx\n", csr_read(CSR_IP));
	// pr_info("Interrupt enable reg: %lx\n", csr_read(CSR_IE));

	hwirq = plic_irq_claim_handle();
	if (hwirq) {
		// pr_info("Polled PLIC hwirq: %d\n", hwirq);
		// if (hwirq == 10) {
		// 	csr_set(CSR_STATUS, SR_IE);
		// }
		return;
	}

	// int timer_hwirq = 5;
	// int external_hwirq = 9;
	// int timer_bit = 1 << timer_hwirq;
	// int external_bit = 1 << external_hwirq;

	// int interrupts_pending = csr_read(CSR_IP);

	// if (interrupts_pending & external_bit) {
	// 	pr_info("External interrupt pending\n");

	// 	struct irq_domain *intc_domain = get_intc_domain();

	// 	if (intc_domain) {
	// 		generic_handle_domain_irq(intc_domain, external_hwirq);
	// 		return;
	// 	} else {
	// 		pr_warn("Intc domain not yet initialized");
	// 	}
	// }

	// if (interrupts_pending & timer_bit) {
	// 	// pr_info("Timer interrupt pending\n");

	// 	struct irq_domain *intc_domain = get_intc_domain();

	// 	if (intc_domain) {
	// 		// irq_enter();
	// 		generic_handle_domain_irq(intc_domain, timer_hwirq);
	// 		// irq_exit();
	// 	} else {
	// 		pr_warn("Intc domain not yet initialized");
	// 	}

	// 	// pr_info("Original pending: %lx, Interrupt pending reg: %lx\n", interrupts_pending, csr_read(CSR_IP));

	// 	return;
	// }

	if (arch_irqs_disabled()) {
		return;
	}

	while (1) {
		hwirq = plic_irq_claim_handle();
		if (hwirq) {
			// pr_info("Polled hwirq: %d\n", hwirq);
			break;
		}
	}

	// pr_info("END CPU IDLE\n");

	// wait_for_interrupt();


	// if (beandip_is_ready()) {
	// 	unsigned int hwirq;

	// 	while (1) {
	// 		hwirq = plic_irq_claim_handle();
	// 		if (hwirq) {
	// 			pr_info("Polled hwirq: %d\n", hwirq);
	// 			break;
	// 		}
	// 	}
	// } else {
	// 	pr_info("Wait for irq\n");
	// 	wait_for_interrupt();
	// 	pr_info("Got irq\n");
	// }

	// pr_info("HERE2\n");
}

#endif
