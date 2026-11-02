# Práctica 4 — Leer el Device Tree desde un módulo de kernel

Módulo que atraviesa el Device Tree desde espacio de kernel usando la API `of_*`
(`<linux/of.h>`), la misma que usa cualquier driver real cuando se inicializa. Lee el
modelo de placa desde el nodo raíz y busca el primer nodo UART compatible con `arm,pl011`.

## Archivos

| Archivo        | Descripción                                          |
|----------------|------------------------------------------------------|
| `dt_reader.c`  | Fuente del módulo: usa `of_find_node_by_path` y `of_find_compatible_node` |
| `Makefile`     | Invoca Kbuild contra las cabeceras del kernel        |

## Compilación y uso

```bash
make
sudo insmod dt_reader.ko
dmesg | tail -5
sudo rmmod dt_reader
make clean
```

## Salida esperada en dmesg

```
dt_reader: modelo: Raspberry Pi 4 Model B
dt_reader: nodo: /soc/serial@7e201000
dt_reader: habilitado: si
```

El nodo `/soc/serial@7e201000` es el mismo que se vio al descompilar el DTB con `dtc` en
la Práctica 3. El estado `habilitado: si` corresponde a `status = "okay"` en el DTS.

## Funciones de la API of_* usadas

| Función                                      | Qué hace                                          |
|----------------------------------------------|---------------------------------------------------|
| `of_find_node_by_path("/")`                  | Devuelve el nodo en la ruta indicada              |
| `of_find_compatible_node(NULL, NULL, compat)`| Busca el primer nodo con ese string en `compatible` |
| `of_property_read_string(node, prop, &out)`  | Lee una propiedad de tipo string                  |
| `of_device_is_available(node)`               | Devuelve `true` si `status = "okay"`              |
| `of_node_put(node)`                          | Libera la referencia al nodo (obligatorio)        |

El especificador `%pOF` en `printk` imprime la ruta completa del nodo en el árbol. Es una
extensión exclusiva del kernel, no disponible en `printf` de espacio de usuario.
