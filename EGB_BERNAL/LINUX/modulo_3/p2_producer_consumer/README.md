# Práctica 2 — Productor-consumidor con buffer circular

El patrón productor-consumidor es la aplicación clásica de variables de condición. Un hilo
productor genera datos y los deposita en un buffer compartido; un hilo consumidor los retira
y los procesa. El buffer actúa como colchón que absorbe diferencias de velocidad entre ambos.

## Archivos

| Archivo                | Descripción                                                     |
|--------------------------|-------------------------------------------------------------------|
| `producer_consumer.c`  | Buffer circular protegido por mutex + dos variables de condición |

## Diseño del buffer circular

Buffer circular (`ring buffer`) de `BUF_SIZE` slots. `head` apunta al próximo slot libre
donde el productor escribe; `tail` apunta al próximo slot con dato para que el consumidor
lea; `count` registra cuántos ítems hay en el buffer en cada momento.

Se usan **dos variables de condición**: `no_lleno` (el productor espera aquí cuando el
buffer está lleno; el consumidor señaliza cuando consume) y `no_vacio` (el consumidor
espera aquí cuando el buffer está vacío; el productor señaliza cuando produce).

## Compilación y ejecución

```bash
gcc -Wall -pthread -o producer_consumer producer_consumer.c
./producer_consumer
```

## Salida esperada (fragmento)

```
[prod] item  0 producido  | en buffer: 1
[cons] item  0 consumido  | en buffer: 0
[prod] item  1 producido  | en buffer: 1
[prod] item  2 producido  | en buffer: 2
[prod] item  3 producido  | en buffer: 3
[cons] item  1 consumido  | en buffer: 2
[prod] item  4 producido  | en buffer: 3
...
```

El productor (50 ms entre items) es más rápido que el consumidor (120 ms por item), por lo
que el buffer se va llenando gradualmente. Cuando llega a `BUF_SIZE = 8`, el productor se
bloquea en `wait(no_lleno)` hasta que el consumidor retira al menos un ítem.
