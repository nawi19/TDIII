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

#include <ctype.h>
#include <stdarg.h>

#define DEV_PATH "/dev/esp32link"   //Archivo de dispositivo que crea el driver en probe() con device_create()

#define ANGULO_MIN 0.0f
#define ANGULO_MAX 360.0f

static int fd_esp32;    //File descriptor del dispositivo: global porque lo usan main() y el hilo.
static pthread_mutex_t consola = PTHREAD_MUTEX_INITIALIZER; // Mutex para que el hilo receptor y principal no mezclen sus printf() en la terminal (ambos escriben por stdout).

static void *hilo_monitor(void *arg)    //Hilo receptor: loop bloqueante de read(), imprime cada linea que llega (respuestas a "get", o cualquier cosa que el firmware mande por su cuenta).
{
    char rx[128];      //buffer donde el driver deja lo que mando el ESP32 
    ssize_t leidos;    // cantidad de bytes devueltos por read() 

    //read() se duerme en el driver (wait_event_interruptible) hasta que llega una linea completa; devuelve <= 0 recien al cerrar el fd. 
    
    (void)arg;  //para evitar warning de unused parameter

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


static int enviar(const char *fmt, ...) { //Arma la linea con printf-style, le agrega '\n' y la manda al driver.
    char linea[48];
    va_list ap;
    int n;
 
    va_start(ap, fmt);
    n = vsnprintf(linea, sizeof(linea) - 1, fmt, ap);
    va_end(ap);
 
    if (n < 0 || n >= (int)sizeof(linea) - 1) {
        fprintf(stderr, "Comando demasiado largo\n");
        return -1;
    }
 
    linea[n++] = '\n';
    if (write(fd_esp32, linea, n) < 0) {
        perror("write");
        return -1;
    }
    return 0;
}


int main(int argc, char *argv[])
{
    pthread_t hilo_receptor;    // hilo que imprime lo que llega del ESP32 
    char comando[64];           // linea completa leida de stdin 
    char modo[8];               // nombre del preset para "exec" 
    char *fin;
    float valor;                // valor numerico a fijar en la banda 

    fd_esp32 = open(DEV_PATH, O_RDWR);  // Abrir el dispositivo del driver (requiere permisos: se corre con sudo).

    if (fd_esp32 < 0) {
        perror("open");
        return 1;
    }

  if (pthread_create(&hilo_receptor, NULL, hilo_monitor, NULL) != 0) {
        perror("pthread_create");
        close(fd_esp32);
        return 1;
    }

    while (fgets(comando, sizeof(comando), stdin)) {
        comando[strcspn(comando, "\r\n")] = '\0';   // Quitar el salto de linea
        
        if (!(strcmp(comando, "salir"))) break;

        if (!strncmp(comando, "set ", 4)) {
            valor = strtof(comando + 4, &fin);

            if (fin == comando + 4) {
                fprintf(stderr, "Dato invalido\n");
                continue;
            }

            if (valor < ANGULO_MIN || valor > ANGULO_MAX) {
                fprintf(stderr, "Angulo fuera de rango (%.0f a %.0f)\n", ANGULO_MIN, ANGULO_MAX);
                continue;
            }

            enviar("set %.2f", valor);
 
        } else if (!strcmp(comando, "get")) {
            enviar("get");      // La respuesta la imprime el hilo receptor.
 
        } else if (sscanf(comando, "exec %7s", modo) == 1) {
            for (char *p = modo; *p; p++)
                *p = tolower((unsigned char)*p);
 
            if (strcmp(modo, "ram") && strcmp(modo, "esc")) {
                fprintf(stderr, "Accion invalida: usar ram o esc\n");
                continue;
            }
            enviar("exec %s", modo);
 
        } else if (comando [0] !='\0'){
            fprintf(stderr, "Comando invalido\n");
        }
    }
 
    pthread_cancel(hilo_receptor);
    pthread_join(hilo_receptor, NULL);
    close(fd_esp32);
    return 0;
}

