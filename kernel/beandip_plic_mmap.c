#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/cdev.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/types.h>
#include <linux/mm_types.h>
#include <linux/mm.h>
#include <linux/highmem.h>
#include <linux/sched/task_stack.h>
#include <linux/io.h>

// #include <linux/sched.h>

#include <asm/io.h>
#include <asm/fixmap.h>
#include <asm/sbi.h>
#include <asm/smp.h>
// #include <asm/apicdef.h>

#include <asm-generic/fixmap.h>

#include <uapi/asm-generic/errno-base.h>

// Needed for measurement purposes. In handle_page_fault, we'll
// want to be able to know if we're in this process


/*
extern pid_t beandip_pid;
extern void __user * beandip_user_page_fault_indicator;
extern void __user * beandip_user_syscall_indicator;
*/

#define MAJOR_NUM 17
#define MAX_MINORS 1

#define CONTEXT_CLAIM 0x04

#define __APIC_BASE_MSR 0x0000001B
// #define APIC_MAP_SIZE 0x600000

#define MY_APIC_BASE_MSR 0x0000001B
#define MY_APIC_SIZE 0x530

#define MY_APIC_PF_INDICATOR 0xBDBD0001
#define MY_APIC_SYSCALL_INDICATOR 0xBDBD0002
#define MY_APIC_HIT_INDICATOR 0xBDBD0003

#define APIC_ENABLED_OFFSET 11
#define APIC_DEV_CLASS_MODE 0444

uint64_t APIC_BASE_PFN = 0;

// static void read_msr(uint32_t msr, uint32_t *lo, uint32_t *hi) {
// 	asm __volatile__("rdmsr" : "=a"(*lo), "=d"(*hi) : "c"(msr));
// }


#define Op0_shift	19
#define Op0_mask	0x3
#define Op1_shift	16
#define Op1_mask	0x7
#define CRn_shift	12
#define CRn_mask	0xf
#define CRm_shift	8
#define CRm_mask	0xf
#define Op2_shift	5
#define Op2_mask	0x7

// #define sys_reg(op0, op1, crn, crm, op2) \
// 	(((op0) << Op0_shift) | ((op1) << Op1_shift) | \
// 	 ((crn) << CRn_shift) | ((crm) << CRm_shift) | \
// 	 ((op2) << Op2_shift))

// #define read_sysreg_s(r) ({						\
// 	u64 __val;							\
// 	asm volatile(__mrs_s("%0", r) : "=r" (__val));			\
// 	__val;								\
// })

// #define SYS_ICC_SRE_EL1			sys_reg(3, 0, 12, 12, 5)
// #define SYS_ICC_HPPIR0_EL1		sys_reg(3, 0, 12, 8, 2)

/*
 * Page Table Debugging Stuff
 */

#define SV39_NEXT_TABLE_MASK  0x003FFFFFFFFFFC00ULL
#define SV39_NEXT_TABLE_SHIFT 2

#define SV39_NUM_LEVELS 3

#define SV39_V (1ULL<<0)
#define SV39_R (1ULL<<1)
#define SV39_W (1ULL<<2)
#define SV39_X (1ULL<<3)
#define SV39_U (1ULL<<4)
#define SV39_G (1ULL<<5)
#define SV39_A (1ULL<<6)
#define SV39_D (1ULL<<7)

#define ERROR(...) pr_err(__VA_ARGS__)
#define INFO(...) printk(__VA_ARGS__)

int
walk_sv39_table(
        uint64_t vaddr,
        uint64_t table_paddr,
        int level
        )
{
    INFO("Level(%d): table-paddr=0x%lx table-vaddr=0x%lx\n", 
            level,
            table_paddr,
            __va(table_paddr));

    if(level < 0) {
        ERROR("Reached Page Table Level: (%d)!\n",
                level);
        return -1;
    }

    uint64_t *table = (uint64_t*)(uint64_t)__va(table_paddr);
   
    size_t index = (vaddr >> ((level * 9) + 12)) & 0x1FFULL;
    printk("Index=0x%lx, translated-vaddr=0x%lx\n", index, vaddr);

    uint64_t entry = table[index];

    if((entry & SV39_V) == 0) {
        INFO("[0x%lx] INVALID\n",
                (void*)entry);
        return -1;
    }

    if(entry & SV39_R || entry & SV39_X) {
        INFO("[0x%lx] paddr=0x%lx %s%s%s%s%s%s%s LEAF\n",
                (void*)entry,
                (entry & SV39_NEXT_TABLE_MASK)<<SV39_NEXT_TABLE_SHIFT,
                (entry & SV39_R) ? "[READ]" : "",
                (entry & SV39_W) ? "[WRITE]" : "",
                (entry & SV39_X) ? "[EXEC]" : "",
                (entry & SV39_U) ? "[USER]" : "",
                (entry & SV39_G) ? "[GLOBAL]" : "",
                (entry & SV39_A) ? "[ACCESSED]" : "",
                (entry & SV39_D) ? "[DIRTY]" : ""
                );
        return 0;
    } else { 
        INFO("[0x%lx] %s%s%s%s%s%s%s TABLE\n",
                (void*)entry,
                (entry & SV39_R) ? "[READ]" : "",
                (entry & SV39_W) ? "[WRITE]" : "",
                (entry & SV39_X) ? "[EXEC]" : "",
                (entry & SV39_U) ? "[USER]" : "",
                (entry & SV39_G) ? "[GLOBAL]" : "",
                (entry & SV39_A) ? "[ACCESSED]" : "",
                (entry & SV39_D) ? "[DIRTY]" : ""
                );
        return walk_sv39_table(
                vaddr,
                (entry & SV39_NEXT_TABLE_MASK)<<SV39_NEXT_TABLE_SHIFT,
                level-1);
    }
}

static int
sv39_walk_assert_mapping(
        uint64_t vaddr,
        uint64_t paddr,
        uint64_t table_paddr,
        int level
        )
{
    if(level < 0) {
        ERROR("sv39_walk_assert_mapping: Reached Page Table Level: (%d)!\n",
                level);
        return -1;
    }

    uint64_t *table = (uint64_t*)(uint64_t)__va(table_paddr);
   
    size_t index = (vaddr >> ((level * 9) + 12)) & 0x1FFULL;

    uint64_t entry = table[index];

    if((entry & SV39_V) == 0) {
        ERROR("sv39_walk_assert_mapping: [0x%lx] INVALID\n",
                (void*)entry);
        return -1;
    }
 
    if(entry & SV39_R || entry & SV39_X) {
        uint64_t leaf_addr = (entry & SV39_NEXT_TABLE_MASK) << SV39_NEXT_TABLE_SHIFT;
        uint64_t offset_mask = (1ULL << (level * 9) + 12) - 1;
        if(leaf_addr != (paddr & ~offset_mask)) {
            ERROR("sv39_walk_assert_mapping: FAILED Kernel Mapped (0x%lx -> 0x%lx) instead of (0x%lx -> 0x%lx)\n",
                    vaddr, leaf_addr, vaddr, paddr & ~offset_mask);
            return -1;
        }
        return 0;
    } else { 
        return sv39_walk_assert_mapping(
                vaddr,
                paddr,
                (entry & SV39_NEXT_TABLE_MASK)<<SV39_NEXT_TABLE_SHIFT,
                level-1);
    }
}

static int
assert_mapping(
        uint64_t vaddr,
        uint64_t paddr)
{
    uint64_t root_paddr = (csr_read(CSR_SATP) & 0xFFFFFFFFFF) << 12;

    return sv39_walk_assert_mapping(
            vaddr,
            paddr,
            root_paddr,
            2
            );
}

static int
milkv_patch_sv39_table(
        uint64_t vaddr,
        uint64_t table_paddr,
        int level
        )
{
    if(level < 0) {
        ERROR("Reached Page Table Level: (%d)!\n",
                level);
        return -1;
    }

    uint64_t *table = (uint64_t*)(uint64_t)__va(table_paddr);
   
    size_t index = (vaddr >> ((level * 9) + 12)) & 0x1FFULL;

    uint64_t entry = table[index];

    if((entry & SV39_V) == 0) {
        ERROR("[0x%lx] INVALID\n",
                (void*)entry);
        return -1;
    }
    
    if(level == 0) {
        //table[index] = table[index] & 0x003FFFFFFFFFFFFFULL;
        table[index] &= ~(0xFull << 60); // clear insane linux reserved bit
        table[index] |= (0b1001ull << 60); // set pmbt to I/O
        entry = table[index];
        asm volatile (
                "sfence.vma"
                ::: "memory");
    }
    if(entry & SV39_R || entry & SV39_X) {
        return 0;
    } else { 
        return milkv_patch_sv39_table(
                vaddr,
                (entry & SV39_NEXT_TABLE_MASK)<<SV39_NEXT_TABLE_SHIFT,
                level-1);
    }
}

int
milkv_patch_sv39_range(
        uint64_t vbase,
        uint64_t pbase,
        uint64_t size,
        uint64_t table_paddr)
{
    int res;
    for(uint64_t offset = 0; offset < size; offset += 0x1000) {
        assert_mapping(vbase + offset, pbase + offset);
        res = milkv_patch_sv39_table(
                vbase + offset,
                table_paddr,
                2);
        if(res) {
            ERROR("milkv_patch_sv39_range: milkv_patch_sv39_table failed (vaddr=0x%lx)\n",
                    vbase + offset);
            return res;
        }
    }
    return 0;
}

/* 
=====================
Device Driver Stuff
=====================
*/

struct beandip_hit_params {
    int hartid;
    int hwirq;
};

struct beandip_hit_cpu {
    int cpuid;
    int hwirq;
};

static struct cdev apic_cdev;
static struct class *apic_class;

static int apic_open(struct inode *inode, struct file *file)
{
	return 0;
}

static int apic_release(struct inode *inode, struct file *file)
{
	return 0;
}

static char* apic_devnode(struct device *dev, umode_t *mode)
{
	if (mode) {
		*mode = APIC_DEV_CLASS_MODE;
	}
	return NULL;
}

static int apic_mmap(struct file *filp, struct vm_area_struct *vma)
{
	unsigned long requested_size = vma->vm_end - vma->vm_start;
	int err;
	struct pt_regs *regs;

// #ifdef CONFIG_X86_64
	// if (requested_size < APIC_MAP_SIZE) {
	// 	printk("apic_dev_kmod: start 0x%lx, end 0x%lx\n", vma->vm_start, vma->vm_end);
	// 	printk("apic_dev_kmod: incorrect mapping size for APIC\n");
	// 	return -1;
	// }

	// printk("Data: %d\n", readl((void __iomem *)(APIC_BASE_PFN + 0x04)));

	// Linux 6.4
	// vm_flags_set(vma, VM_IO);
	vma->vm_flags |= VM_IO;
    printk("pgoff=0x%lx\n", vma->vm_pgoff);
    uint64_t plic_base_pfn = APIC_BASE_PFN + vma->vm_pgoff;
    uint64_t plic_base = plic_base_pfn << 12;
	err = io_remap_pfn_range(vma, vma->vm_start, plic_base_pfn, requested_size, vma->vm_page_prot);
	if (err) {
		printk("apic_dev_kmod: apic_mmap [remap_page_range] failed!\n");
		return -1;
	}
	printk("apic_dev_kmod: hopefully mmaped! base: %lx\n", plic_base);

    uint64_t mmap_vaddr = vma->vm_start;
    uint64_t root_paddr = (csr_read(CSR_SATP) & 0xFFFFFFFFFF) << 12;

    printk("mmap_vaddr=0x%lx, root_paddr=0x%lx\n",
            mmap_vaddr,
            root_paddr);

    err = milkv_patch_sv39_range(
            mmap_vaddr,
            plic_base,
            requested_size,
            root_paddr
            );

    if(err) {
        pr_err("milkv_patch_sv39_range(vaddr=0x%lx, paddr=0x%lx) -> %d\n",
                mmap_vaddr,
                root_paddr,
                err);
    }

    printk("Hopefully patched page tables correctly\n");

    printk("Enabled Bitmap\n");

    // void __iomem *kmap = ioremap(plic_base, 0x400000);
    // if(kmap == NULL) {
    //     pr_err("Failed to ioremap the PLIC!\n");
    // } else {

    //     for(int i = 0; i < 0x20; i++) {
    //         uint64_t context = 65;
    //         uint64_t offset = 0x2000 + (context * 0x80) + (i * 4);
    //         uint32_t __iomem *reg = kmap + offset;
    //         assert_mapping(kmap + offset, plic_base + offset);
    //         uint32_t value = readl(reg);
    //         printk("offset=0x%x, [0x%08x]\n", offset, value);
    //     }
    // }

	// printk("SCOUNTEREN: %lx\n", csr_read(CSR_SCOUNTEREN));

	// unsigned long init_val = 0;
	// unsigned long flag = SBI_PMU_START_FLAG_SET_INIT_VALUE;

	// struct sbiret ret;

	// for (int idx = 0; idx < 10; idx++) {
	// 	ret = sbi_ecall(SBI_EXT_PMU, SBI_EXT_PMU_COUNTER_START, idx,
	// 		1, flag, init_val, 0, 0);
		
	// 	pr_info("idx: %d, Error: %d\n", idx, ret.error);
	// }

	// regs = task_pt_regs(current);
	// // IF flags bit
	// regs->flags &= ~(1 << 9);

// #else // ARM
//   printk("DID MMAP\n");
// 	// regs = task_pt_regs(current);

// 	// // mask out IRQ
// 	// regs->pstate |= 1 << 7;
// 	// // mask out FIQ
// 	// regs->pstate |= 1 << 6;
// 	// // set exception level to EL1t
// 	// regs->pstate |= 1 << 2;

// 	// printk("PSTATE: %llx\n", regs->pstate);
// #endif

	// sched_apic_kmod();

	return 0;
}

static void handle_irq_other_cpu(void *info) {
	struct beandip_info *bi;
	struct beandip_hit_cpu *hit_info = (struct beandip_hit_cpu *)info;

	bi = this_cpu_ptr(&beandip_info);
	bi->userspace_poll_hits++;

	plic_irq_claim_handle_cpu_hwirq(hit_info->cpuid, hit_info->hwirq);

	kfree(hit_info);

	while(plic_irq_claim_handle()) {
		bi->kernel_loop_poll_hits++;
		continue;
	}
}

static long apic_ioctl(struct file * fp, unsigned int cmd, unsigned long arg) {
	struct beandip_hit_params params;
	struct beandip_info *bi;

	switch(cmd) {
		case MY_APIC_PF_INDICATOR:
			printk("Registering page fault indicator\n");
			// current->user_page_fault_indicator = (void __user *) arg;
			// current->beandipped = true;
			break;
		case MY_APIC_SYSCALL_INDICATOR:
			printk("Registering syscall indicator\n");
			// current->user_syscall_indicator = (void __user *) arg;
			// current->beandipped = true;
			break;
		case MY_APIC_HIT_INDICATOR:
			if (copy_from_user(&params, (struct beandip_hit_params *)arg, sizeof(struct beandip_hit_params))) {
                return -EFAULT;
            }

			int hartid = params.hartid + BEANDIP_IS_SIFIVE;

			int cpuid = riscv_hartid_to_cpuid(hartid);

			int curr_cpuid = smp_processor_id();

			// printk(KERN_INFO "Received poll hit ioctl with hartid: %d, hwirq: %d, cpuid: %d\n", hartid, params.hwirq, cpuid);

			if (cpuid == curr_cpuid) {
				bi = this_cpu_ptr(&beandip_info);
				bi->userspace_poll_hits++;
				plic_irq_claim_handle_cpu_hwirq(cpuid, params.hwirq);

				while(plic_irq_claim_handle()) {
					bi->kernel_loop_poll_hits++;
					continue;
				}
			} else {
				struct beandip_hit_cpu *hit_info = kmalloc(sizeof(struct beandip_hit_cpu), GFP_KERNEL);
				hit_info->cpuid = cpuid;
				hit_info->hwirq = params.hwirq;

				// do not wait for this to return (which is why we need to malloc the hit info)
				smp_call_function_single(cpuid, handle_irq_other_cpu, (void *)hit_info, 0);
			}

			// current->user_syscall_indicator = (void __user *) arg;
			// current->beandipped = true;
			break;
		default:
			printk("Hit default ioctl case\n");
			break;
	}
	return 0;
}

static struct file_operations fops = 
{
	.owner			= THIS_MODULE,
	.open			= apic_open,
	.mmap			= apic_mmap,
	.unlocked_ioctl = apic_ioctl,
	.release		= apic_release
};

int setup_chrdev(void) {
	int err;
	err = register_chrdev_region(MKDEV(MAJOR_NUM, 0), MAX_MINORS, "apic_device_driver");

	if (err != 0){
		printk("apic: failed to register chrdev\n");
		return err;
	}

	cdev_init(&apic_cdev, &fops);
	// Linux 6.4
	// apic_class = class_create("apic_class");
	apic_class = class_create(THIS_MODULE, "apic_class");
	apic_class->devnode = apic_devnode;
	device_create(apic_class, NULL, MKDEV(MAJOR_NUM, 0), NULL, "apic_dev");
	cdev_add(&apic_cdev, MKDEV(MAJOR_NUM, 0), 1);
	
	return 0;
}

int teardown_chrdev(void) {
	device_destroy(apic_class, MKDEV(MAJOR_NUM, 0));
	class_destroy(apic_class);
	cdev_del(&apic_cdev);
	unregister_chrdev_region(MKDEV(MAJOR_NUM, 0), MAX_MINORS);
	printk("Unregistered beandip_userspace\n");
	return 0;
}

static int __init beandip_plic_mmap_init(void)
{
	uint32_t eax, edx;
	uint64_t apic_base_reg;
	int err;
	// uint64_t apic_virt_addr;
	// int i;

#ifdef CONFIG_X86_64
	// Check if APIC enabled + read physical APIC address
	read_msr(__APIC_BASE_MSR, &eax, &edx);
	
	apic_base_reg = edx;
	apic_base_reg = (apic_base_reg << 32) | eax;

	if (!((1UL << APIC_ENABLED_OFFSET) & apic_base_reg)) {
		printk("apic_dev_kmod: APIC not enabled!\n");
		return -1;
	}
	APIC_BASE_PFN = (apic_base_reg & 0xFFFFFFFFFF000) >> 12;
	printk("apic_dev_kmod: APIC base pfn == 0x%llx\n", APIC_BASE_PFN);

    
	// apic_virt_addr = APIC_BASE;
	// if (apic_virt_addr) {
	// 	printk("apic_virt_addr: %llx\n", apic_virt_addr);
	// 	for (i = 0; i < 0x530; i += 8) {
	// 		printk("%lx: %x\n", __pa(apic_virt_addr + i), *(uint32_t *)(apic_virt_addr + i));
	// 	}
	// }

#else // RISC-V
	printk("Hello from RISC-V beandip PLIC mmap kernel module!\n");

	// enable all performance counters
	csr_write(CSR_SCOUNTEREN, 0xFFFFFFFF);

    int cpu;

    uint64_t min_context_base = 0;
    bool min_context_base_set = false;
    uint64_t max_context_base = 0;

	resource_size_t phys_start = 0;
	resource_size_t phys_size = 0;

    for_each_online_cpu(cpu) {
        struct plic_handler *handler = per_cpu_ptr(&plic_handlers, cpu);
        void __iomem *claim = handler->hart_base;

		phys_start = handler->priv->phys_start;
		phys_size = handler->priv->phys_size;

        uint64_t context_base = (uint64_t)claim;

        if (!min_context_base_set || context_base < min_context_base) {
            min_context_base_set = true;
            min_context_base = context_base;
        }

        if (context_base > max_context_base) {
            max_context_base = context_base;
        }

        printk("CPU: %d, PLIC context base location: %lx\n", cpu, claim);
    }

    uint64_t context_diff = max_context_base - min_context_base;

    printk("Min PLIC context base: %lx\n", min_context_base);
    printk("Max PLIC context base: %lx\n", max_context_base);
    printk("PLIC Context diff: %lx\n", context_diff);

	printk("PLIC phys start: %lx, size: %lx\n", phys_start, phys_size);
    // shift to page
    APIC_BASE_PFN = (uint64_t)phys_start >> 12;
#endif
	
	// setup /dev/apic_dev
	err = setup_chrdev();
	if (err) {
		printk("apic_dev_kmod: failed to register device!\n");
		return err;
	}

	return 0;
}


static void __exit beandip_plic_mmap_exit(void)
{
	/* unregister the device */
	teardown_chrdev();
}

MODULE_LICENSE("GPL");
module_init(beandip_plic_mmap_init);
module_exit(beandip_plic_mmap_exit);
