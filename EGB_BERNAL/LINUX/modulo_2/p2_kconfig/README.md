# Práctica 2 — Kconfig aplicado a un módulo out-of-tree

Extiende el módulo Hello World para ver el pipeline completo de Kconfig: declarar una
opción en un archivo `Kconfig`, habilitarla en `.config`, y observar cómo cambia el
comportamiento del módulo en tiempo de ejecución sin modificar el fuente.

## Archivos

| Archivo    | Descripción                                              |
|------------|----------------------------------------------------------|
| `hello.c`  | Fuente del módulo con `#ifdef CONFIG_HELLO_VERBOSE`      |
| `Kconfig`  | Declaración de la opción `HELLO_VERBOSE`                 |
| `Makefile` | Lee `.config` y transmite el símbolo al compilador       |

## Flujo de la práctica

### Paso 1 — Compilar con la opción desactivada

Crear el `.config` con la opción desactivada y compilar:

```bash
echo "# CONFIG_HELLO_VERBOSE is not set" > .config
make
sudo insmod hello.ko
dmesg | tail -5
# Resultado esperado: solo "hello: modulo cargado"
sudo rmmod hello
```

### Paso 2 — Compilar con la opción activada

Cambiar el `.config`, recompilar y recargar:

```bash
echo "CONFIG_HELLO_VERBOSE=y" > .config
make clean && make
sudo insmod hello.ko
dmesg | tail -5
# Resultado esperado: "modulo cargado" + "compilado con verbose activo"
sudo rmmod hello
```

El mismo fuente produce binarios distintos según el estado del `.config`.

## (Opcional) Usar menuconfig con kconfiglib

En lugar de editar el `.config` a mano se puede usar la interfaz interactiva de
`kconfiglib`. El `menuconfig` que viene con el kernel no está disponible en Raspberry Pi OS,
pero `kconfiglib` es una reimplementación en Python completamente compatible.

### Instalación

```bash
# Instalar pip si no está disponible
sudo apt install python3-pip

# Crear un entorno virtual (solo la primera vez)
# El nombre y la ubicación son libres
python3 -m venv ~/venvs/kconfig

# Activar el entorno (necesario en cada sesión nueva)
source ~/venvs/kconfig/bin/activate

# Instalar kconfiglib dentro del entorno activo
pip install kconfiglib
```

Cuando el entorno está activo, el prompt muestra el nombre entre paréntesis:
`(kconfig) lse@lse-pi4-00:~/p2_kconfig$`

Para desactivarlo al terminar: `deactivate`

### Uso

Con el entorno activo, desde el directorio de la práctica:

```bash
KCONFIG_CONFIG=.config menuconfig Kconfig
```

- `KCONFIG_CONFIG=.config` — indica dónde leer y guardar la configuración
- `menuconfig` — abre la interfaz de texto interactiva
- `Kconfig` — archivo de declaraciones a cargar

### Atajos de teclado de la interfaz

| Tecla         | Acción                                                      |
|---------------|-------------------------------------------------------------|
| Flechas ↑/↓   | Moverse entre opciones                                      |
| `Space`/`Enter` | Activar/desactivar una opción `bool`; entrar a un submenú |
| `ESC`         | Volver al nivel anterior                                    |
| `S`           | Guardar el `.config` sin salir                              |
| `Q`           | Salir (pregunta si guardar)                                 |
| `?`           | Ver detalle de la opción seleccionada                       |

Al salir con `Q` y confirmar, el `.config` queda actualizado. Recompilar normalmente
con `make clean && make`.
