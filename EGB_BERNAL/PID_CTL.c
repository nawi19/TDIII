/*
 * PID_CTL
 *
 * Corre en la Raspberry Pi 4 y le habla al driver de kernel esp32_link
 * a traves del dispositivo /dev/esp32link. El driver se encarga de la UART
 * con el ESP32; este programa solo:
 *   - lee comandos escritos por teclado (o enviados por SSH desde la GUI),
 *   - los traduce a ioctl() / write() hacia el driver,
 *   - y muestra todo lo que el ESP32 manda de vuelta.
 *
 * Comandos:
 *   set:           Setea un angulo deseado             [ioctl ESP32_SET]
 *   get:           Se obtiene el angulo actual del eje [ioctl ESP32_GET]
 *   exec RAM/ESC   Modifica los perfiles de arranque   [write()]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/ioctl.h>

#include "esp32_link.h"   //struct esp32_var, ESP32_SET, ESP32_GET 

#define DEV_PATH "/dev/esp32link"   //Archivo de dispositivo que crea el driver en probe() con device_create()

static int fd_esp32;    //File descriptor del dispositivo: global porque lo usan main() y el hilo.

static pthread_mutex_t consola = PTHREAD_MUTEX_INITIALIZER; // Mutex para que el hilo receptor y principal no mezclen sus printf() en la terminal (ambos escriben por stdout).

static void *hilo_monitor(void *arg)    //Hilo receptor: loop bloqueante de read(), imprime cada linea que llega (respuestas a "get", o cualquier cosa que el firmware mande por su cuenta).
{
    char rx[128];      //buffer donde el driver deja lo que mando el ESP32 
    ssize_t leidos;    // cantidad de bytes devueltos por read() 

    //read() se duerme en el driver (wait_event_interruptible) hasta que llega una linea completa; devuelve <= 0 recien al cerrar el fd. 

    while ((leidos = read(fd_esp32, rx, sizeof(rx) - 1)) > 0) {
        rx[leidos] = '\0';   // read() no agrega terminador: lo ponemos nosotros 
        
        pthread_mutex_lock(&consola);
        
        printf("[esp32] %s", rx);

        if (rx[leidos - 1] != '\n') printf("\n");   // si no vino con salto de linea, lo agregamos 
            
        fflush(stdout);

        pthread_mutex_unlock(&consola);
    }
    return NULL;
}

static void uso(const char *argv0) //Muestra la ayuda de uso por stderr. 
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
    pthread_t hilo_receptor;            // hilo que imprime lo que llega del ESP32 
    struct esp32_var ajuste;            // par banda/nivel que viaja por ioctl 
    char comando[64];                   // linea completa leida de stdin 
    char modo[16];                      // nombre del preset para "exec" 
    char banda[ESP32_VAR_NAME_MAX];     // nombre de la banda: low / mid / high 
    int nivel;                          // valor numerico a fijar en la banda 
 
    if (argc > 1) { //Este programa es interactivo: no recibe argumentos. Si se pasa alguno, se muestra la ayuda y se sale. */
        uso(argv[0]);
        return 0;
    }

    fd_esp32 = open(DEV_PATH, O_RDWR);  // Abrir el dispositivo del driver (requiere permisos: se corre con sudo).

    if (fd_esp32 < 0) {
        perror("open");
        return 1;
    }

    pthread_create(&hilo_rx, NULL, hilo_monitor, NULL); //Lanzar el hilo receptor: asi las respuestas del ESP32 se ven apenas llegan, sin esperar a que el usuario escriba otro comando. 

    while (fgets(comando, sizeof(comando), stdin)) {    //Loop principal: una linea de stdin = un comando.
        if (!strncmp(comando, "salir", 5))
            break;

        if (sscanf(comando, "set %15s %d", banda, &nivel) == 2) {
            /* set BANDA NIVEL: se arma la estructura y se manda por ioctl.
             * El driver la convierte en "set BANDA NIVEL\n" por la UART. */
            strncpy(ajuste.nombre, banda, sizeof(ajuste.nombre) - 1);
            ajuste.nombre[sizeof(ajuste.nombre) - 1] = '\0';
            ajuste.valor = nivel;
            if (ioctl(fd_esp32, ESP32_SET, &ajuste) < 0)
                perror("ioctl ESP32_SET");

        } else if (sscanf(comando, "get %15s", banda) == 1) {
            /* get BANDA: el ioctl bloquea hasta que el ESP32 responde y deja
             * el valor leido en ajuste.valor (ESP32_GET es de ida y vuelta). */
            strncpy(ajuste.nombre, banda, sizeof(ajuste.nombre) - 1);
            ajuste.nombre[sizeof(ajuste.nombre) - 1] = '\0';
            if (ioctl(fd_esp32, ESP32_GET, &ajuste) == 0) {
                pthread_mutex_lock(&consola);
                printf("%s = %d\n", ajuste.nombre, ajuste.valor);
                pthread_mutex_unlock(&consola);
            } else {
                perror("ioctl ESP32_GET");
            }

        } else if (sscanf(comando, "exec %15s", modo) == 1) { //exec PRESET: no tiene ioctl propio, se manda como texto con write() y el driver lo reenvia tal cual al ESP32. */
            char orden[32];
            snprintf(orden, sizeof(orden), "exec %s\n", modo);

            if (write(fd_esp32, orden, strlen(orden)) < 0) perror("write");
                

        } else  uso(argv[0]);   //Comando no reconocido: mostrar ayuda.
           
    }

    close(fd_esp32);
    return 0;
}
