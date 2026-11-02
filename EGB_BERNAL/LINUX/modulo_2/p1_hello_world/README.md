# Práctica 1 — Módulo Hello World

Módulo de kernel mínimo que imprime mensajes en el buffer del kernel al cargarse y al
descargarse. Permite verificar el flujo completo: compilación, carga, inspección de
mensajes y descarga.

## Archivos

| Archivo   | Descripción                                      |
|-----------|--------------------------------------------------|
| `hello.c` | Fuente del módulo: funciones `init` y `exit`     |
| `Makefile` | Invoca Kbuild contra las cabeceras del kernel   |

## Requisitos previos

Instalar las cabeceras del kernel en la Raspberry Pi (verificar primero si ya están):

```bash
ls /lib/modules/$(uname -r)/build/

# Si el directorio no existe o está vacío:
sudo apt update
sudo apt install raspberrypi-kernel-headers
```

## Compilación y uso

```bash
# Compilar
make

# Verificar el archivo generado
ls -lh hello.ko

# Ver los metadatos del módulo
modinfo hello.ko

# Cargar el módulo
sudo insmod hello.ko

# Ver el mensaje de inicialización
dmesg | tail -5

# Verificar que aparece en la lista de módulos cargados
lsmod | grep hello

# Descargar el módulo
sudo rmmod hello

# Ver el mensaje de limpieza
dmesg | tail -5

# Limpiar los archivos de compilación
make clean
```

## Salida esperada en dmesg

```
hello: modulo cargado
...
hello: modulo descargado
```

## Referencia rápida de comandos

| Comando              | Función                                              |
|----------------------|------------------------------------------------------|
| `insmod modulo.ko`   | Carga el módulo en el kernel                         |
| `rmmod modulo`       | Descarga el módulo (sin extensión `.ko`)             |
| `lsmod`              | Lista los módulos actualmente cargados               |
| `modinfo modulo.ko`  | Muestra metadatos: licencia, autor, descripción      |
| `dmesg | tail -10`   | Últimos 10 mensajes del buffer del kernel            |
| `dmesg -C`           | Limpia el buffer (requiere `sudo`)                   |
