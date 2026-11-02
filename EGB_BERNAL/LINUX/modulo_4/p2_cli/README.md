# Práctica 2 — Interfaz de consola

Aplicación `CLI` (Command Line Interface) para realizar escrituras y lecturas sobre el char device creado en la práctica 1.

## Archivos

| Archivo     | Descripción                                                       |
|-------------|-------------------------------------------------------------------|
| `p2_cli.py` | Código fuente: hace lecturas y escrituras sobre `/dev/p1_led_btn` |

## Requisitos previos

```bash
# Dar permisos de lectura y escritura para todos los usuarios
sudo chmod 666 /dev/p1_led_btn
```

## Ejecución del programa

```bash
# Ejecutar código
python3 p2_cli.py
```
