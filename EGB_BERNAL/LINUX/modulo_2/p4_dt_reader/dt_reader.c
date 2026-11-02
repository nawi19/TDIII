#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/of.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("TD3 -- UTN FRA");
MODULE_DESCRIPTION("Lectura de propiedades del Device Tree desde kernel");

static int __init dt_reader_init(void)
{
    struct device_node *node;
    const char *str;

    /* Leer el modelo de placa desde el nodo raiz */
    node = of_find_node_by_path("/");
    if (node) {
        if (of_property_read_string(node, "model", &str) == 0)
            printk(KERN_INFO "dt_reader: modelo: %s\n", str);
        of_node_put(node);
    }

    /* Buscar el primer nodo compatible con "arm,pl011" (UART) */
    node = of_find_compatible_node(NULL, NULL, "arm,pl011");
    if (node) {
        /* %pOF imprime la ruta completa del nodo en el arbol */
        printk(KERN_INFO "dt_reader: nodo: %pOF\n", node);
        printk(KERN_INFO "dt_reader: habilitado: %s\n",
               of_device_is_available(node) ? "si" : "no");
        of_node_put(node);
    } else {
        printk(KERN_INFO "dt_reader: no se encontro nodo arm,pl011\n");
    }

    return 0;
}

static void __exit dt_reader_exit(void)
{
    printk(KERN_INFO "dt_reader: modulo descargado\n");
}

module_init(dt_reader_init);
module_exit(dt_reader_exit);
