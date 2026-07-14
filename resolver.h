#include <linux/kprobes.h>

static struct kprobe kp = { .symbol_name = "kallsyms_lookup_name" };

typedef unsigned long (*kallsyms_lookup_name_t)(const char *name);
kallsyms_lookup_name_t kallsyms_lookup_name_func;

static int __init resolve_symbols(void)
{
    if (register_kprobe(&kp) < 0)
        return -1;
    kallsyms_lookup_name_func = (kallsyms_lookup_name_t) kp.addr;
    unregister_kprobe(&kp);
    return 0;
}
