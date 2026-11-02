#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/of.h>
#include <linux/gpio/consumer.h>
#include <linux/uaccess.h>
#include <linux/err.h>
#include <linux/string.h>

#include "ledbtn.h"

#define DEVICE_NAME "p1_led_btn"
#define CLASS_NAME "td3"
#define LEDBTN_BUF_MAX 8

MODULE_LICENSE("GPL");
MODULE_AUTHOR("TD3 -- UTN FRA");
MODULE_DESCRIPTION("Char device de ejemplo: LED + boton por GPIO real, con read/write en profundidad e ioctl");

static dev_t dev_num;
static struct cdev ledbtn_cdev;
static struct class *ledbtn_class;
static struct device *ledbtn_device;
static struct gpio_desc *led_gpio;
static struct gpio_desc *boton_gpio;
static bool polaridad_invertida;

static int ledbtn_open(struct inode *inode, struct file *file)
{
    return 0;
}

static int ledbtn_release(struct inode *inode, struct file *file)
{
    return 0;
}

static ssize_t ledbtn_read(struct file *file, char __user *buf, size_t len, loff_t *off)
{
    char estado[2];
    size_t restante;

    if (*off >= sizeof(estado))
        return 0; /* ya se entrego todo: fin de archivo */

    estado[0] = gpiod_get_value(boton_gpio) ? '1' : '0';
    estado[1] = '\n';

    restante = sizeof(estado) - *off;
    if (restante > len)
        restante = len;

    if (copy_to_user(buf, estado + *off, restante))
        return -EFAULT;

    *off += restante;
    return restante;
}

static void ledbtn_set_led(int encender)
{
    gpiod_set_value(led_gpio, polaridad_invertida ? !encender : encender);
}

static ssize_t ledbtn_write(struct file *file, const char __user *buf, size_t len, loff_t *off)
{
    char local[LEDBTN_BUF_MAX];
    size_t n = len;
    int encender;

    if (n >= sizeof(local))
        n = sizeof(local) - 1; /* nunca copiar mas de lo que entra en local[] */

    if (copy_from_user(local, buf, n))
        return -EFAULT;
    local[n] = '\0';

    if (n > 0 && local[n - 1] == '\n')
        local[n - 1] = '\0'; /* recortar el salto de linea que agrega "echo" */

    if (!strcmp(local, "1") || !strcmp(local, "on"))
        encender = 1;
    else if (!strcmp(local, "0") || !strcmp(local, "off"))
        encender = 0;
    else
        return -EINVAL;

    ledbtn_set_led(encender);

    return len; /* se consumio todo el buffer original */
}

static long ledbtn_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    int __user *argp = (int __user *)arg;
    int valor, anterior;

    switch (cmd) {
    case LEDBTN_RESET:
        ledbtn_set_led(0);
        break;

    case LEDBTN_GET_BOTON:
        valor = gpiod_get_value(boton_gpio);
        if (copy_to_user(argp, &valor, sizeof(valor)))
            return -EFAULT;
        break;

    case LEDBTN_SET_POLARIDAD:
        if (copy_from_user(&valor, argp, sizeof(valor)))
            return -EFAULT;
        polaridad_invertida = !!valor;
        break;

    case LEDBTN_SET_LED:
        if (copy_from_user(&valor, argp, sizeof(valor)))
            return -EFAULT;
        ledbtn_set_led(!!valor);
        break;

    case LEDBTN_XPOLARIDAD:
        if (copy_from_user(&valor, argp, sizeof(valor)))
            return -EFAULT;
        anterior = polaridad_invertida;
        polaridad_invertida = !!valor;
        if (copy_to_user(argp, &anterior, sizeof(anterior)))
            return -EFAULT;
        break;

    default:
        return -ENOTTY; /* comando no reconocido: convencion POSIX */
    }

    return 0;
}

static const struct file_operations ledbtn_fops = {
    .owner          = THIS_MODULE,
    .open           = ledbtn_open,
    .release        = ledbtn_release,
    .read           = ledbtn_read,
    .write          = ledbtn_write,
    .unlocked_ioctl = ledbtn_ioctl,
};

static int __init ledbtn_init(void)
{
    struct device_node *node;
    int ret;

    node = of_find_compatible_node(NULL, NULL, "td3,ledbtn");
    if (!node) {
        printk(KERN_ERR "ledbtn: no se encontro el nodo 'td3,ledbtn' en el Device Tree\n");
        return -ENODEV;
    }

    // led_gpio = gpiod_get_from_of_node(node, "led-gpios", 0, GPIOD_OUT_LOW, "led");
    led_gpio = fwnode_gpiod_get_index(of_fwnode_handle(node), "led", 0, GPIOD_OUT_LOW, "led");
    if (IS_ERR(led_gpio)) {
        printk(KERN_ERR "ledbtn: no se pudo obtener el GPIO del LED\n");
        ret = PTR_ERR(led_gpio);
        goto err_node;
    }

    // boton_gpio = gpiod_get_from_of_node(node, "boton-gpios", 0, GPIOD_IN, "boton");
    boton_gpio = fwnode_gpiod_get_index(of_fwnode_handle(node), "boton", 0, GPIOD_IN, "boton");
    if (IS_ERR(boton_gpio)) {
        printk(KERN_ERR "ledbtn: no se pudo obtener el GPIO del boton\n");
        ret = PTR_ERR(boton_gpio);
        goto err_led;
    }

    of_node_put(node);

    ret = alloc_chrdev_region(&dev_num, 0, 1, DEVICE_NAME);
    if (ret < 0) {
        printk(KERN_ERR "ledbtn: no se pudo reservar major/minor\n");
        goto err_boton;
    }

    cdev_init(&ledbtn_cdev, &ledbtn_fops);
    ledbtn_cdev.owner = THIS_MODULE;
    ret = cdev_add(&ledbtn_cdev, dev_num, 1);
    if (ret < 0) {
        printk(KERN_ERR "ledbtn: no se pudo registrar el cdev\n");
        goto err_chrdev;
    }

    ledbtn_class = class_create(CLASS_NAME);
    if (IS_ERR(ledbtn_class)) {
        printk(KERN_ERR "ledbtn: no se pudo crear la clase\n");
        ret = PTR_ERR(ledbtn_class);
        goto err_cdev;
    }

    ledbtn_device = device_create(ledbtn_class, NULL, dev_num, NULL, DEVICE_NAME);
    if (IS_ERR(ledbtn_device)) {
        printk(KERN_ERR "ledbtn: no se pudo crear el dispositivo\n");
        ret = PTR_ERR(ledbtn_device);
        goto err_class;
    }

    printk(KERN_INFO "ledbtn: modulo cargado, /dev/%s listo\n", DEVICE_NAME);
    return 0;

err_class:
    class_destroy(ledbtn_class);
err_cdev:
    cdev_del(&ledbtn_cdev);
err_chrdev:
    unregister_chrdev_region(dev_num, 1);
err_boton:
    gpiod_put(boton_gpio);
err_led:
    gpiod_put(led_gpio);
    return ret;
err_node:
    of_node_put(node);
    return ret;
}

static void __exit ledbtn_exit(void)
{
    device_destroy(ledbtn_class, dev_num);
    class_destroy(ledbtn_class);
    cdev_del(&ledbtn_cdev);
    unregister_chrdev_region(dev_num, 1);
    gpiod_put(boton_gpio);
    gpiod_put(led_gpio);
    printk(KERN_INFO "ledbtn: modulo descargado\n");
}

module_init(ledbtn_init);
module_exit(ledbtn_exit);
