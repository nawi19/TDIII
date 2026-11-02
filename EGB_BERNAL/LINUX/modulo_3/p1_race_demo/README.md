# Práctica 1 — Condición de carrera

Programa que demuestra el efecto de una condición de carrera en un contador compartido. Dos
hilos incrementan el mismo contador `ITERACIONES` veces cada uno. Sin mutex, el resultado
difiere del esperado; con mutex, es siempre correcto.

## Archivos

| Archivo       | Descripción                                                       |
|----------------|--------------------------------------------------------------------|
| `race_demo.c` | Dos hilos incrementan `counter` sin protección (mutex comentado)  |

## Compilación y ejecución

```bash
gcc -Wall -pthread -o race_demo race_demo.c
./race_demo
```

## Salida esperada sin mutex

```
Esperado: 2000000
Obtenido: 1243872      # valor incorrecto y no reproducible
```

El resultado varía entre ejecuciones porque depende exactamente de cuándo el scheduler
interrumpe cada hilo. Descomentando las dos líneas de mutex (`pthread_mutex_lock`/
`pthread_mutex_unlock` alrededor de `counter++`) el resultado siempre es `2000000`.

En algunos procesadores o con ciertas opciones del compilador, la condición de carrera puede
no manifestarse si `counter++` se compila a una instrucción atómica (por ejemplo, `INC` en
x86 con un único procesador). En la Raspberry Pi, con el compilador estándar y múltiples
núcleos, el efecto es reproducible.
