# Práctica 1 — `read`/`write` en profundidad e `ioctl`

Extiende el char device de la Práctica 1 del Módulo 4 (`../../modulo_4/p1_led_btn/`). No se
reescribe el driver: se generalizan `read`/`write` (manejo correcto de `*off`, buffers de
tamaño arbitrario en vez de un solo byte) y se agrega `ioctl` con cinco comandos, cubriendo las
cuatro variantes de macro (`_IO`, `_IOR`, `_IOW`, `_IOWR`). El registro del character device
(`alloc_chrdev_region`/`cdev_init`/`cdev_add`/`class_create`/`device_create`) y la obtención de
los GPIO (`fwnode_gpiod_get_index`) quedan exactamente igual que en el Módulo 4.

## Archivos

| Archivo                 | Descripción                                                        |
|--------------------------|---------------------------------------------------------------------|
| `p1_led_btn_ioctl.c`    | Driver: `read`/`write` extendidos + `ioctl` con cinco comandos     |
| `ledbtn.h`              | Comandos `ioctl` compartidos entre el driver y `p2_ioctl_cli`      |
| `Makefile`              | Invoca Kbuild contra las cabeceras del kernel                      |

## Requisitos previos

Mismo hardware, mismo overlay que el Módulo 4 — no hay nada que volver a cablear ni a
compilar del lado del Device Tree:

- LED en GPIO17 (pin físico 11) con resistencia de 220&Omega; a GND.
- Botón en GPIO27 (pin físico 13) con resistencia de pull-up de 10k&Omega; a 3,3V.
- Overlay `td3,ledbtn` ya aplicado (`../../modulo_4/p1_led_btn/p1_led_btn_pi4-overlay.dts` o
  `..._pi5-overlay.dts`, según el modelo de Raspberry Pi).

## `read`/`write` extendidos

`/dev/p1_led_btn` ahora entrega el estado del botón como dos bytes de texto (`"0\n"`/`"1\n"`)
en lugar de un byte crudo, y acepta `"0"`/`"1"`/`"on"`/`"off"` en la escritura. `*off` se
actualiza correctamente: una segunda lectura sobre el mismo descriptor devuelve fin de archivo
en vez de repetir el mismo byte.

## Los cinco comandos `ioctl`

| Comando                  | Macro    | Dirección         | Qué hace                                                        |
|---------------------------|----------|--------------------|-------------------------------------------------------------------|
| `LEDBTN_RESET`            | `_IO`    | ninguna            | Apaga el LED sin pasar por `write`.                              |
| `LEDBTN_GET_BOTON`        | `_IOR`   | kernel→usuario     | Lee el estado del botón vía `ioctl`, alternativa a `read`.       |
| `LEDBTN_SET_POLARIDAD`    | `_IOW`   | usuario→kernel     | Recibe un `int`: si es distinto de 0, invierte la polaridad del LED. |
| `LEDBTN_SET_LED`          | `_IOW`   | usuario→kernel     | Recibe un `int` (0/1) y prende/apaga el LED directo, alternativa a `write`. |
| `LEDBTN_XPOLARIDAD`       | `_IOWR`  | bidireccional      | Fija la nueva polaridad y devuelve la anterior, en la misma llamada. |

`echo`/`cat` no pueden invocar `ioctl()`. Para probar estos cinco comandos hace falta el
programa de la Práctica 2, `../p2_ioctl_cli/`.

## Compilación, carga y prueba

```bash
# Compilar el modulo
make

# Cargar el modulo (el overlay del Modulo 4 ya debe estar aplicado)
sudo insmod build/p1_led_btn_ioctl.ko

# Verificar que el nodo se creo solo
ls -l /dev/p1_led_btn

# read/write extendidos: ahora tambien aceptan "on"/"off"
echo "on"  | sudo tee /dev/p1_led_btn
echo "off" | sudo tee /dev/p1_led_btn
# imprime "0" o "1" seguido de salto de linea,, OJOOO necesita             sudo chmod 666 /dev/p1_led_btn


cat /dev/p1_led_btn          

# Los cinco comandos ioctl (ver ../p2_ioctl_cli/)
sudo ../p2_ioctl_cli/p2_ioctl_cli reset
sudo ../p2_ioctl_cli/p2_ioctl_cli get-boton
sudo ../p2_ioctl_cli/p2_ioctl_cli set-led 1
sudo ../p2_ioctl_cli/p2_ioctl_cli set-pol 1
sudo ../p2_ioctl_cli/p2_ioctl_cli x-pol 0

# Ver los mensajes del modulo
dmesg | tail -5

# Descargar el modulo
sudo rmmod p1_led_btn_ioctl
```

## Salida esperada de `ls -l /dev/p1_led_btn`

```
crw-rw---- 1 root td3 238, 0 ago 9 10:00 /dev/p1_led_btn
```
