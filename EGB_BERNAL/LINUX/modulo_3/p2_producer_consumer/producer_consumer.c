#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>

#define BUF_SIZE  8
#define N_ITEMS  20

/* --- Estructura del item --- */
typedef struct {
    int id;
    struct timespec ts;  /* timestamp de produccion */
} item_t;

/* --- Buffer compartido --- */
static item_t  buffer[BUF_SIZE];
static int     head  = 0;
static int     tail  = 0;
static int     count = 0;

/* --- Primitivas de sincronizacion --- */
static pthread_mutex_t mutex     = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  no_lleno  = PTHREAD_COND_INITIALIZER;
static pthread_cond_t  no_vacio  = PTHREAD_COND_INITIALIZER;

/* --- Hilo productor --- */
static void *productor(void *arg)
{
    for (int i = 0; i < N_ITEMS; i++) {
        item_t item;
        item.id = i;
        clock_gettime(CLOCK_MONOTONIC, &item.ts);

        pthread_mutex_lock(&mutex);

        /* Esperar si el buffer esta lleno */
        while (count == BUF_SIZE)
            pthread_cond_wait(&no_lleno, &mutex);

        /* Depositar el item */
        buffer[head] = item;
        head = (head + 1) % BUF_SIZE;
        count++;

        printf("[prod] item %2d producido  | en buffer: %d\n", item.id, count);
        pthread_cond_signal(&no_vacio);
        pthread_mutex_unlock(&mutex);

        usleep(50000);  /* 50 ms entre producciones */
    }
    return NULL;
}

/* --- Hilo consumidor --- */
static void *consumidor(void *arg)
{
    for (int i = 0; i < N_ITEMS; i++) {
        pthread_mutex_lock(&mutex);

        /* Esperar si el buffer esta vacio */
        while (count == 0)
            pthread_cond_wait(&no_vacio, &mutex);

        /* Retirar el item */
        item_t item = buffer[tail];
        tail = (tail + 1) % BUF_SIZE;
        count--;

        printf("[cons] item %2d consumido  | en buffer: %d\n", item.id, count);
        pthread_cond_signal(&no_lleno);
        pthread_mutex_unlock(&mutex);

        usleep(120000); /* 120 ms procesando (mas lento que el productor) */
    }
    return NULL;
}

int main(void)
{
    pthread_t prod, cons;

    pthread_create(&prod, NULL, productor,  NULL);
    pthread_create(&cons, NULL, consumidor, NULL);

    pthread_join(prod, NULL);
    pthread_join(cons, NULL);

    return 0;
}
