#include "linux/cpumask.h"
#include "linux/fs.h"
#include "linux/percpu-defs.h"
#include "linux/slab.h"
#include "linux/uaccess.h"
#include <linux/init.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/proc_fs.h>
#include <linux/sched.h>
#include <linux/kprobes.h>
#include "resolver.h"

#define PROC_DIR_NAME "runqueues"

static ssize_t proc_read(struct file* file, char __user* buffer, size_t buffer_size, loff_t* offset);

static struct proc_dir_entry* proc_dir = NULL;
static struct proc_dir_entry** proc_entries = NULL;

static int cpus_online = 0;
static unsigned long runqueues_addr = 0;

static const struct proc_ops proc_ops = {
    .proc_read = proc_read,
};

static ssize_t proc_read(struct file* file, char __user* buffer, size_t buffer_size, loff_t* offset) {
    unsigned long cpu = (unsigned long)pde_data(file_inode(file));

    void* rq = per_cpu_ptr((void __percpu*)runqueues_addr, cpu);
    // nr_running is the first field of type unsigned int (kernel 7.1.3), it is not compatible between different kernel versoions :(
    const int nr_running = *(unsigned int*)rq;

    char buf[1 << 4];
    int buf_len = snprintf(buf, sizeof(buf), "%d", nr_running);

    if (*offset >= buf_len) {
        return 0; // EOF
    }

    size_t left_to_copy = buf_len - *offset;
    left_to_copy = min(left_to_copy, buffer_size);

    const size_t not_copied = copy_to_user(buffer, buf + *offset, left_to_copy);
    left_to_copy -= not_copied;

    *offset += left_to_copy;
    return left_to_copy;
}

static int __init queuer_init(void) {
    if (resolve_symbols() < 0) {
        pr_err("Could not resolve kallsyms_lookup_name\n");
        return -ENODATA;
    }

    runqueues_addr = kallsyms_lookup_name_func("runqueues");
    if (runqueues_addr == 0) {
        pr_err("Could not resolve runqueues symbol address\n");
        return -ENODATA;
    }

    cpus_online = num_online_cpus();
    proc_entries = kcalloc(cpus_online, sizeof(struct proc_dir_entry*), GFP_KERNEL);
    if (!proc_entries) {
        pr_err("Could not allocate memory\n");
        return -ENOMEM;
    }

    proc_dir = proc_mkdir(PROC_DIR_NAME, NULL);
    if (!proc_dir) {
        pr_err("Could not create a /proc directory '%s'\n", PROC_DIR_NAME);
        return -ENOMEM;
    }

    int failed_at = -1;
    for (int i = 0; i < cpus_online; ++i) {
        char buf[1 << 4];
        snprintf(buf, sizeof(buf), "%d", i);

        unsigned long cur_cpu = i;
        proc_entries[i] = proc_create_data(buf, 0644, proc_dir, &proc_ops, (void*)cur_cpu);
        if (!proc_entries[i]) {
            pr_err("/proc entry '%d' could not be created\n", i);

            failed_at = i;
            goto cleanup;
        }
    }

    return 0;
cleanup:
    for (int i = 0; i <= failed_at; ++i) {
        proc_remove(proc_entries[i]);
    }
    proc_remove(proc_dir);

    return -ENOMEM;
}

static void __exit queuer_exit(void) {
    for (int i = 0; i < cpus_online; ++i) {
        proc_remove(proc_entries[i]);
    }
    proc_remove(proc_dir);
}

module_init(queuer_init);
module_exit(queuer_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Let's you see how many tasks are in each runqueue");
