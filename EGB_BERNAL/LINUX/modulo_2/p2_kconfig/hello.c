#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("TD3 -- UTN FRA");
MODULE_DESCRIPTION("Modulo de ejemplo con Kconfig");

static int __init hello_init(void)
{
    printk(KERN_INFO "hello: modulo cargado\n");
#ifdef CONFIG_HELLO_VERBOSE
    printk(KERN_INFO "hello: compilado con verbose activo\n");
#endif
    return 0;
}

static void __exit hello_exit(void)
{
    printk(KERN_INFO "hello: modulo descargado\n");
}

module_init(hello_init);
module_exit(hello_exit);
