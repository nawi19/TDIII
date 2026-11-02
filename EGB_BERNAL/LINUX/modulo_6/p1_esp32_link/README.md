# Práctica 1 — UART real con `serdev` y protocolo con el ESP32-S3

Driver que reemplaza la búsqueda manual de `of_find_compatible_node` (Módulos 4-5) por el
mecanismo real de binding automático, aplicado a un periférico real: un UART secundario
dedicado de la Raspberry Pi. En vez de exponer un `/dev/ttyAMAx` genérico, el driver se registra
como cliente del bus `serdev` y es dueño exclusivo del puerto, hablando un protocolo de texto
simple (`set`/`get`/`exec`) con una placa ESP32-S3 corriendo FreeRTOS. Expone `/dev/esp32link`
con `read`/`write`/`ioctl`.

## Archivos

| Archivo                     | Descripción                                                        |
|-------------------------------|-----------------------------------------------------------------------|
| `esp32_link.c`               | Driver: bus `serdev`, `kfifo` (productor-consumidor), `ioctl` con `completion` |
| `esp32_link.h`               | Comandos `ioctl` compartidos con `../p2_esp32_ctl/`                |
| `esp32-link-pi4-overlay.dts` | Overlay para Pi 4 (BCM2711): `pinctrl` propio de GPIO4/GPIO5 + habilita UART3 + nodo hijo ESP32-S3 |
| `esp32-link-pi5-overlay.dts` | Overlay para Pi 5 (BCM2712/RP1): habilita UART3 + nodo hijo ESP32-S3, reusa el `pinctrl` `uart3_pins` del árbol base |
| `test-uart3-pi4-overlay.dts` | Overlay de diagnóstico (Pi 4): habilita UART3 sin nodo hijo, para probar la tty pelada sin el driver |
| `test-uart3-pi5-overlay.dts` | Overlay de diagnóstico (Pi 5): ídem, para RP1 |
| `Makefile`                   | Invoca Kbuild contra las cabeceras del kernel                      |

## Requisitos previos

### Conexionado

**Pi 4 y Pi 5 usan pines físicos distintos** para UART3 (verificado contra hardware real, no
solo por documentación):

- **Pi 4** -- `GPIO4` (TXD3, pin físico 7) → RX del ESP32-S3; `GPIO5` (RXD3, pin físico 29) ←
  TX del ESP32-S3.
- **Pi 5** -- `GPIO8` (TXD3, pin físico 24) → RX del ESP32-S3; `GPIO9` (RXD3, pin físico 21) ←
  TX del ESP32-S3. Confirmado leyendo el árbol de dispositivos en vivo
  (`uart3_pins = ".../rp1_uart3_8_9"`, con `pin_txd { pins = "gpio8"; }` y
  `pin_rxd { pins = "gpio9"; }`).
- GND común entre las dos placas (¡esto también hay que probarlo con multímetro si algo no
  anda, no alcanza con "parece estar conectado"!).
- Ambas trabajan a 3.3V: conexión directa, sin traductor de niveles.

Se eligió **UART3** en vez de UART2 (`GPIO0`/`GPIO1` en Pi4, reservado para la detección de
HATs con EEPROM) o el UART primario (consola serie del sistema).

### El overlay: dos variantes, Pi 4 y Pi 5

**La Pi 4 (BCM2711) y la Pi 5 (BCM2712) no comparten el mismo mecanismo de `pinctrl`.** En la
Pi 5 el GPIO/UART lo maneja un chip aparte, el **RP1** (conectado por PCIe), con su propio
controlador de pines -- la sintaxis `brcm,pins`/`brcm,function` de la Pi 4 no aplica ahí.

- **Pi 4** -- `esp32-link-pi4-overlay.dts` declara su propio grupo de `pinctrl`
  (`GPIO4`/`GPIO5` en función alternativa ALT4 = `TXD3`/`RXD3`, verificado contra la tabla 94
  del datasheet del BCM2711) y lo conecta a `uart3` él mismo, sin depender del overlay oficial
  de Raspberry Pi (que solo hace `status = "okay"`, confiando en que el mapeo de pines ya está
  en el árbol base).
- **Pi 5** -- `esp32-link-pi5-overlay.dts` referencia el grupo `uart3_pins` que ya viene
  correcto en el árbol base de la Pi 5 (mismo patrón que el overlay oficial
  `uart3-pi5-overlay.dts` de Raspberry Pi) -- no armamos el grupo a mano en este caso porque
  todavía no está verificada la codificación numérica del pinmux de RP1.

```bash
# Pi 4:
dtc -@ -I dts -O dtb -o esp32-link.dtbo esp32-link-pi4-overlay.dts
# Pi 5:
dtc -@ -I dts -O dtb -o esp32-link.dtbo esp32-link-pi5-overlay.dts

sudo dtoverlay -d . esp32-link

# Verificar que el nodo quedo en el arbol en vivo
ls /sys/firmware/devicetree/base/soc*/serial*/esp32-link/ 2>/dev/null || \
    find /sys/firmware/devicetree/base -name esp32-link

# Verificar que el pin haya quedado en la funcion correcta (Pi 4: espera ALT4 en GPIO4/GPIO5)
raspi-gpio get 4
raspi-gpio get 5

# En Pi 5, si no tenes raspi-gpio, se puede volcar el arbol en vivo y buscar el grupo:
sudo dtc -I fs -O dts /sys/firmware/devicetree/base 2>/dev/null | grep -A15 'rp1_uart3'
```

## El protocolo

Texto plano, una línea por mensaje, terminada en `\n`:

| Verbo | Formato | Respuesta esperada |
|-------|---------|---------------------|
| `set` | `set VARIABLE VALOR\n` | ninguna |
| `get` | `get VARIABLE\n` | `VARIABLE=VALOR\n` |
| `exec` | `exec ACCION\n` | ninguna |

El firmware del ESP32-S3 que implementa el lado que responde **no se provee acá** -- cada
alumno tiene el suyo. Lo único fijo es el formato de arriba, en particular la respuesta a `get`.

## Probar sin un ESP32-S3: una terminal serie en la PC

Como el protocolo es texto plano sobre UART, se puede probar el driver completo (`kfifo`,
`ioctl` con timeout, la CLI) sin tener el firmware del ESP32-S3 listo -- alcanza con hacer de
cuenta que la PC es el ESP32-S3, contestando a mano:

1. Conectar un adaptador USB-serie **de 3.3V** (no de 5V) a la PC.
2. Cablear cruzado contra los mismos pines de la Pi (ver "Conexionado" arriba, distintos en
   Pi 4 y Pi 5): TX del adaptador → RX de la Pi, RX del adaptador ← TX de la Pi, GND común.
3. Abrir una terminal serie en la PC, por ejemplo:
   ```bash
   picocom -b 115200 /dev/ttyUSB0
   # o: minicom -D /dev/ttyUSB0 -b 115200
   # o: screen /dev/ttyUSB0 115200
   ```
4. Cuando desde la Pi se haga `get temp` (por `ioctl` o por `esp32_ctl`), en la terminal de la
   PC va a aparecer la línea `get temp`. Escribir a mano `temp=234` y Enter -- eso completa el
   `ioctl(ESP32_GET, ...)` que está bloqueado del lado de la Pi.
5. `set`/`exec` se ven aparecer igual en la terminal, sin que haga falta contestar nada.

Esto sirve para validar el driver de forma independiente de tener el firmware FreeRTOS
terminado, y para depurar el protocolo desde los dos lados por separado.

## Compilación, carga y prueba

```bash
# Compilar el modulo
make

# Cargar el modulo (el overlay ya debe estar aplicado)
sudo insmod build/esp32_link.ko

# Verificar que el nodo se creo solo
ls -l /dev/esp32link

# exec y monitoreo crudo con herramientas genericas
echo "exec toggle" | sudo tee /dev/esp32link
cat /dev/esp32link &

# Los comandos ioctl (ver ../p2_esp32_ctl/)
sudo ../p2_esp32_ctl/esp32_ctl

# Ver los mensajes del modulo
dmesg | tail -10

# Descargar el modulo
sudo rmmod esp32_link
```

## Troubleshooting -- problemas reales encontrados probando contra hardware

Esta práctica ya se debuggeó de punta a punta contra una Pi 5 real con un adaptador USB-serie
haciendo de ESP32-S3. Tres problemas reales aparecieron, en este orden -- documentados acá para
no repetir la sesión de debug entera:

1. **`/dev/esp32link` puede terminar siendo un archivo común, no el dispositivo.** Si en algún
   momento se le escribió con `tee`/`echo`/redirección de la shell mientras el módulo **no**
   estaba cargado, la shell simplemente crea un archivo de texto normal con ese nombre --
   `udev` no lo pisa después al crear el nodo real. Se nota con `ls -l /dev/esp32link`: el nodo
   real arranca con `c` y muestra major/minor (`crw------- ... 509, 0 ...`); un archivo común
   arranca con `-` y muestra un tamaño en bytes. Si pasa: `sudo rm -f /dev/esp32link` y volver
   a cargar el módulo.

2. **`serdev_device_write()` exige un `write_wakeup` registrado si se lo llama con un
   `timeout` distinto de cero** -- sin él, devuelve `-EINVAL` siempre, sin transmitir un solo
   byte, sin que el resto del driver tenga nada malo. Ya está corregido en `esp32_link.c`
   (`esp32_write_wakeup`, un callback vacío, alcanza con que exista).

3. **Nunca ignorar el valor de retorno de `serdev_device_write()`** -- puede devolver menos
   bytes de los pedidos (o `0`) sin que sea un error negativo. Ya está corregido: `esp32_write`
   y el `ioctl` ahora revisan el resultado y lo propagan.

Si `/dev/esp32link` es un nodo de carácter real, el módulo tiene los tres fixes de arriba, y
sigue sin verse nada del otro lado: revisar primero el UART pelado con
`test-uart3-piN-overlay.dts` (sin nuestro driver de por medio) antes de sospechar del código --
en la sesión real, el `pinctrl`/cableado terminaron estando bien y el bug estaba en el driver,
pero podría ser al revés.

## Otra nota

Si el driver `ledbtn` de los Módulos 4-5 sigue cargado, `class_create("td3")` va a fallar acá
porque ese nombre de clase ya está tomado en `sysfs` -- hace falta `sudo rmmod ledbtn` antes de
cargar `esp32_link`, o cambiar `CLASS_NAME` en el código.
