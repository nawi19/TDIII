# Práctica 3 — Inspeccionar el DTB de la Raspberry Pi

El Device Tree Blob (DTB) es el archivo binario que el bootloader carga en memoria y pasa
al kernel al arrancar. Con `dtc` se puede descompilar de vuelta a texto legible (DTS) y
explorar qué periféricos describe el hardware de la Pi.

Esta práctica no tiene archivos fuente: toda la actividad se hace con herramientas del
sistema.

## Requisitos previos

```bash
sudo apt install device-tree-compiler
```

## Descompilar el DTB activo

```bash
# Para Raspberry Pi 4
dtc -I dtb -O dts /boot/firmware/bcm2711-rpi-4-b.dtb 2>/dev/null | less

# Para Raspberry Pi 5
dtc -I dtb -O dts /boot/firmware/bcm2712-rpi-5-b.dtb 2>/dev/null | less
```

Opciones de `dtc`:
- `-I dtb` — la entrada es un DTB binario
- `-O dts` — la salida es DTS (texto legible)
- `2>/dev/null` — descarta advertencias normales de DTBs de producción

Navegación en `less`: `/` para buscar, `q` para salir.

## Exploración guiada

Con el DTS abierto en `less`, buscar los siguientes elementos:

```
/uart          <- nodos de UART (ver compatible y status)
/compatible    <- modelo de placa en el nodo raiz
```

Observar en los nodos UART:
- La propiedad `compatible` identifica el driver (`"arm,pl011"` en la Pi 4)
- La propiedad `status = "okay"` indica que el periférico está habilitado

## Overlays disponibles

```bash
ls /boot/firmware/overlays/ | head -20
```

Los overlays son DTBs parciales que modifican el Device Tree base en tiempo de arranque.
En el Módulo 6 se va a crear un overlay propio para habilitar un UART secundario.

## Acceso alternativo sin dtc

El Device Tree activo también está disponible como sistema de archivos en
`/sys/firmware/devicetree/base/`: cada nodo es un directorio y cada propiedad un archivo.

```bash
# Leer el modelo de placa directamente
cat /sys/firmware/devicetree/base/model
```
