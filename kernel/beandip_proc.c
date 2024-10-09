#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <asm/smp.h>

static int beandip_proc_show(struct seq_file *m, void *v) {
  int cpu;

	for_each_online_cpu(cpu) {
    u32 poll_count = beandip_get_poll_count(cpu);
    u32 hwint_count = beandip_get_hwint_count(cpu);
    seq_printf(m, "CPU %d polls: %d, hwints: %d\n", cpu, poll_count, hwint_count);
  }
  // seq_printf(m, "Hello proc!\n");
  return 0;
}

static int beandip_proc_open(struct inode *inode, struct  file *file) {
  return single_open(file, beandip_proc_show, NULL);
}

static const struct proc_ops beandip_proc_fops = {
  .proc_open = beandip_proc_open,
  .proc_read = seq_read,
  .proc_lseek = seq_lseek,
  .proc_release = single_release,
};

static int __init beandip_proc_init(void) {
  proc_create("beandip_proc", 0, NULL, &beandip_proc_fops);
  return 0;
}

static void __exit beandip_proc_exit(void) {
  remove_proc_entry("beandip_proc", NULL);
}

MODULE_LICENSE("GPL");
module_init(beandip_proc_init);
module_exit(beandip_proc_exit);
