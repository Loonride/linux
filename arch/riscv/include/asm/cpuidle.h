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

	// wait_for_interrupt();

	// return;

	unsigned int hwirq;

	hwirq = plic_irq_claim_handle();
	if (hwirq) {
		return;
	}

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
}

#endif
