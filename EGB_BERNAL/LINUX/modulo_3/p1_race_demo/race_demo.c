#include <stdio.h>
#include <pthread.h>

#define ITERACIONES 1000000

static long counter = 0;
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

static void *incrementar(void *arg)
{
    for (int i = 0; i < ITERACIONES; i++) {
        /* Descomentar las dos lineas siguientes para activar la proteccion */
        pthread_mutex_lock(&mutex); 
        counter++;
        pthread_mutex_unlock(&mutex); 
    }
    return NULL;
}

int main(void)
{
    pthread_t t1, t2;

    pthread_create(&t1, NULL, incrementar, NULL);
    pthread_create(&t2, NULL, incrementar, NULL);
    pthread_join(t1, NULL);
    pthread_join(t2, NULL);

    printf("Esperado: %d\n", 2 * ITERACIONES);
    printf("Obtenido: %ld\n", counter);
    return 0;
}
