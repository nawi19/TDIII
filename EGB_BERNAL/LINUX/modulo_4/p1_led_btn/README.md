# Práctica 1 — Controlar LED y leer botón por GPIO

Módulo que implementa un character device (`cdev`) desde espacio de kernel para interactuar con hardware físico. Utiliza la API `of_*` (`<linux/of.h>`) para buscar en el Device Tree el nodo compatible con `td3,ledbtn`, y la API moderna de descriptores GPIO (`<linux/gpio/consumer.h>`) para solicitar los pines del LED y el botón. Expone el nodo `/dev/p1_led_btn` permitiendo leer el estado del pulsador y controlar el LED mediante llamadas al sistema estándar desde espacio de usuario.

## Archivos

| Archivo                 | Descripción                                                       |
|-------------------------|-------------------------------------------------------------------|
| `p1_led_btn_pi4-overlay.dts`| Overlay: describe el hardware del LED y el botón                  |
| `p1_led_btn.c`          | Fuente del módulo: usa `file_operations`, `gpiod` y `char device` |
| `Makefile`              | Invoca Kbuild contra las cabeceras del kernel                     |

## Conexionado

Se requieren para esta práctica:
- LED junto a resistencia de 220&Omega;.
- Botón junto a resistencia de 10k&Omega;.

El LED se conecta el ánodo al GPIO 17 (pin físico 11) y el cátodo junto a la resistencia a GND (por ejemplo el pin físico 9).

Del botón se conecta un pin al GPIO 27 (pin físico 13) y el otro pin a GND, con una resistencia de pull-up entre el GPIO 27 y 3,3V.

## Compilación y carga del overlay

```bash
# Compilar el overlay
dtc -@ -I dts -O dtb -o p1_led_btn.dtbo p1_led_btn_pi4-overlay.dts

# Aplicar overlay por una vez
sudo dtoverlay -d . p1_led_btn

# Listar overlays cargados
dtoverlay -l

# Remover overlay
sudo dtoverlay -r p1_led_btn

# Aplicar overlay "permanente"
sudo cp p1_led_btn.dtbo /boot/firmware/overlays/

# Activar overlay
echo "dtoverlay=p1_led_btn" | sudo tee -a /boot/firmware/config.txt

# Aplicar cambios
sudo reboot
```

## Compilación, carga y prueba del módulo

```bash
# Compilar el módulo
make

# Cargar el módulo
sudo insmod p1_led_btn.ko  

#PUEDE ESTAR EN LA CARPETA BUILD. BUSCARLA CON
#   find . -name "*.ko"
#   veo el contenido de build con  ls build/ 2>/dev/null
#   Si está en build, lo corro con sudo insmod build/p1_led_btn.ko



# Verificar que se creó el nodo
ls -l /dev/p1_led_btn

# Prender LED
echo "1" | sudo tee /dev/p1_led_btn

# Apagar LED
echo "0" | sudo tee /dev/p1_led_btn

# Leer estado del botón
sudo cat /dev/p1_led_btn

# Ver los mensajes del módulo
dmesg | tail -5

# Descargar módulo
sudo rmmod p1_led_btn
```

## Salida esperada de ls -l /dev/p1_led_btn

```
crw-rw---- 1 root td3 238, 0 ago 9 10:00 /dev/p1_led_btn
```
