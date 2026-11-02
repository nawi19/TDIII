#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>

#include "ledbtn.h"

#define DEV_PATH "/dev/p1_led_btn"

static void uso(const char *argv0)
{
    fprintf(stderr,
            "Uso: %s reset|get-boton|set-led <0|1>|set-pol <0|1>|x-pol <0|1>\n",
            argv0);
    exit(1);
}

int main(int argc, char *argv[])
{
    int fd, valor;

    if (argc < 2)
        uso(argv[0]);

    fd = open(DEV_PATH, O_RDWR);
    if (fd < 0) {
        perror("open");
        return 1;
    }

    if (!strcmp(argv[1], "reset")) {
        if (ioctl(fd, LEDBTN_RESET) < 0)
            perror("ioctl LEDBTN_RESET");

    } else if (!strcmp(argv[1], "get-boton")) {
        if (ioctl(fd, LEDBTN_GET_BOTON, &valor) < 0)
            perror("ioctl LEDBTN_GET_BOTON");
        else
            printf("boton: %d\n", valor);

    } else if (!strcmp(argv[1], "set-led") && argc == 3) {
        valor = atoi(argv[2]);
        if (ioctl(fd, LEDBTN_SET_LED, &valor) < 0)
            perror("ioctl LEDBTN_SET_LED");

    } else if (!strcmp(argv[1], "set-pol") && argc == 3) {
        valor = atoi(argv[2]);
        if (ioctl(fd, LEDBTN_SET_POLARIDAD, &valor) < 0)
            perror("ioctl LEDBTN_SET_POLARIDAD");

    } else if (!strcmp(argv[1], "x-pol") && argc == 3) {
        valor = atoi(argv[2]);
        if (ioctl(fd, LEDBTN_XPOLARIDAD, &valor) < 0)
            perror("ioctl LEDBTN_XPOLARIDAD");
        else
            printf("polaridad anterior: %d\n", valor); /* valor fue sobreescrito */

    } else {
        uso(argv[0]);
    }

    close(fd);
    return 0;
}
