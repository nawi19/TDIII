// HOla soy facu 5/10/26 esp32_ctl

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/ioctl.h>

#include "esp32_link.h"

#define DEV_PATH "/dev/esp32link"

static int fd;
static pthread_mutex_t consola = PTHREAD_MUTEX_INITIALIZER;

/* Hilo monitor: loop bloqueante de read(), imprime cada linea que llega
 * (respuestas a "get", o cualquier cosa que el firmware mande por su cuenta). */
static void *hilo_monitor(void *arg)
{
    char buf[128];
    ssize_t n;

    while ((n = read(fd, buf, sizeof(buf) - 1)) > 0) {
        buf[n] = '\0';
        pthread_mutex_lock(&consola);
        printf("[esp32] %s", buf);
        if (buf[n - 1] != '\n')
            printf("\n");
        fflush(stdout);
        pthread_mutex_unlock(&consola);
    }
    return NULL;
}

static void uso(const char *argv0)
{
    fprintf(stderr,
            "Uso interactivo: %s\n"
            "  Despues, en el prompt:\n"
            "    set VARIABLE VALOR\n"
            "    get VARIABLE\n"
            "    exec ACCION\n"
            "    salir\n",
            argv0);
}

int main(int argc, char *argv[])
{
    pthread_t monitor;
    struct esp32_var var;
    char linea[64], accion[16], nombre[ESP32_VAR_NAME_MAX];
    int valor;

    if (argc > 1) {
        uso(argv[0]);
        return 0;
    }

    fd = open(DEV_PATH, O_RDWR);
    if (fd < 0) {
        perror("open");
        return 1;
    }

    pthread_create(&monitor, NULL, hilo_monitor, NULL);

    while (fgets(linea, sizeof(linea), stdin)) {
        if (!strncmp(linea, "salir", 5))
            break;

        if (sscanf(linea, "set %15s %d", nombre, &valor) == 2) {
            strncpy(var.nombre, nombre, sizeof(var.nombre) - 1);
            var.nombre[sizeof(var.nombre) - 1] = '\0';
            var.valor = valor;
            if (ioctl(fd, ESP32_SET, &var) < 0)
                perror("ioctl ESP32_SET");

        } else if (sscanf(linea, "get %15s", nombre) == 1) {
            strncpy(var.nombre, nombre, sizeof(var.nombre) - 1);
            var.nombre[sizeof(var.nombre) - 1] = '\0';
            if (ioctl(fd, ESP32_GET, &var) == 0) {
                pthread_mutex_lock(&consola);
                printf("%s = %d\n", var.nombre, var.valor);
                pthread_mutex_unlock(&consola);
            } else {
                perror("ioctl ESP32_GET");
            }

        } else if (sscanf(linea, "exec %15s", accion) == 1) {
            char msg[32];
            snprintf(msg, sizeof(msg), "exec %s\n", accion);
            if (write(fd, msg, strlen(msg)) < 0)
                perror("write");

        } else {
            uso(argv[0]);
        }
    }

    close(fd);
    return 0;
}
