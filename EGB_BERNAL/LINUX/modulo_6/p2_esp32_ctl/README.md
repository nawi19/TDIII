# Práctica 2 — Cliente de consola multi-hilo

A diferencia de `ledbtn_ctl`/`p2_ioctl_cli` del Módulo 5, este programa tiene **dos hilos**
(`pthread`, de vuelta el Módulo 3 para cerrar el bloque):

- Un hilo **monitor**, en loop bloqueante de `read()` sobre `/dev/esp32link`, que imprime cada
  línea que llega del ESP32-S3 -- respuestas a `get`, o cualquier cosa que el firmware decida
  mandar por su cuenta.
- El hilo **principal**, con un menú interactivo (`set`/`get`/`exec`) que hace
  `write()`/`ioctl()` según lo que carga el usuario.

Un mutex chico (`consola`) protege los `printf` de los dos hilos para que no se entrelacen las
salidas -- necesidad real, no forzada, porque los dos hilos escriben a la misma consola.

## Archivos

| Archivo          | Descripción                                                            |
|---------------------|---------------------------------------------------------------------------|
| `esp32_ctl.c`     | CLI multi-hilo: hilo monitor (`read`) + hilo principal (`write`/`ioctl`) |
| `esp32_link.h`    | Copia del header de comandos de `../p1_esp32_link/` (debe coincidir con la del driver) |

## Requisitos previos

El driver de la Práctica 1 debe estar cargado (`/dev/esp32link` tiene que existir).

## Compilación y ejecución

```bash
gcc -Wall -pthread -o esp32_ctl esp32_ctl.c
sudo ./esp32_ctl
```

En el prompt interactivo:

```
set temp_objetivo 25
get temp
exec toggle
salir
```

## Salida esperada (fragmento)

```
$ sudo ./esp32_ctl
get temp
[esp32] get temp
temp = 234
```

La línea `[esp32] get temp` es el hilo monitor mostrando lo que salió por el UART (útil para
depurar); `temp = 234` es el resultado que imprime el hilo principal cuando el `ioctl(ESP32_GET,
...)` bloqueado recibe la respuesta y retorna.
