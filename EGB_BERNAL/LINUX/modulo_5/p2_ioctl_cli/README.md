# Práctica 2 — Cliente `ioctl` de línea de comandos

`echo`/`cat` sirven para probar `read`/`write`, pero no pueden invocar `ioctl()` — son
programas genéricos que solo llaman a las syscalls estándar de E/S. Este programa invoca
directamente los cinco comandos `ioctl` del driver de la Práctica 1
(`../p1_led_btn_ioctl/`) sobre `/dev/p1_led_btn`.

## Archivos

| Archivo           | Descripción                                                          |
|---------------------|------------------------------------------------------------------------|
| `p2_ioctl_cli.c`  | Invoca `LEDBTN_RESET`/`GET_BOTON`/`SET_LED`/`SET_POLARIDAD`/`XPOLARIDAD` según `argv[1]` |
| `ledbtn.h`        | Copia del header de comandos de `../p1_led_btn_ioctl/` (debe coincidir con la del driver) |

## Requisitos previos

El módulo de la Práctica 1 debe estar cargado (`/dev/p1_led_btn` tiene que existir).

## Compilación y ejecución

```bash
gcc -Wall -o p2_ioctl_cli p2_ioctl_cli.c

sudo ./p2_ioctl_cli reset            # apaga el LED sin pasar por write
sudo ./p2_ioctl_cli get-boton        # lee el estado del boton via ioctl
sudo ./p2_ioctl_cli set-led 1        # prende el LED directo, sin pasar por write
sudo ./p2_ioctl_cli set-pol 1        # invierte la polaridad
sudo ./p2_ioctl_cli set-led 1        # con la polaridad invertida, esto apaga el LED
sudo ./p2_ioctl_cli x-pol 0          # vuelve a la polaridad normal, imprime la anterior
```

## Salida esperada (fragmento)

```
$ sudo ./p2_ioctl_cli get-boton
boton: 0
$ sudo ./p2_ioctl_cli x-pol 0
polaridad anterior: 1
```

`x-pol` hace visible el patrón "exchange" del lado de usuario: `valor` se pasa por dirección
con la polaridad *nueva* adentro, y al volver de `ioctl()` esa misma variable quedó
sobreescrita con la polaridad *anterior* — por eso el `printf` de después imprime lo que
`valor` tiene en ese momento, no lo que se pasó por `argv`.
