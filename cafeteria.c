#define _XOPEN_SOURCE 500 /* Necesario para algunas versiones de linux y usleep/strdup */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <pthread.h>
#include <unistd.h>
#include <semaphore.h>

//Estructuras
typedef struct {
    int num_clientes;
    int num_camareros;
    int num_sillas;
    int tiempo_min_llegada_cliente;
    int tiempo_max_llegada_cliente;
    int tiempo_min_prep;
    int tiempo_max_prep;
    int tiempo_min_consumo;
    int tiempo_max_consumo;
    int capacidad_cola;
}Cafeteria;

typedef struct {
    int *ids;
    int capacidad;
    int entrada;
    int salida;
}Cola;

//Variables Globales
Cafeteria cafeteria;
Cola cola;

FILE *fichero_salida = NULL;
int num_iteraciones = -1;

pthread_t *clientes;
pthread_t *camareros;

//Semáforos y Mutex
sem_t sem_sillas;
sem_t sem_huecos_cola;
sem_t sem_pedidos_listos;
sem_t *sem_pedido_cliente;

pthread_mutex_t mutex_escribir;
pthread_mutex_t mutex_cola;

//Funciones
int leer_config(char* nombre_fichero);
void mostrar_config();
void escribir_fichero(char *msg);
int inicializar_recursos();
void *ciclo_clientes(void *arg);
void *ciclo_camareros(void *arg);
void tiempo_espera(int min, int max);
void limpieza();

int main(int argc, char *argv[]) {
    int i;

    char nombre_salida[512] = "";
    int isDigit = 1;

    time_t t;
    struct tm *time_info;
    char fecha_salida[64];
    char *punto;

    int error;
    int *arg;

    //Vamos cambiando de semilla para obtener diferentes datos cada vez
    srand(time(NULL));

    //Comprobamos el número de argumentos y su contenido
    if (argc < 2) {
        printf("Uso: %s <fichero_configuracion> [fichero_salida] [iteraciones]\n", argv[0]);
        return 1;
    }

    if(leer_config(argv[1]) != 0){
        return 1;
    }

    if (argc == 3) {
        for(i = 0; i < strlen(argv[2]); i++) {
            if(!isdigit(argv[2][i])) {
                isDigit = 0;
                break;
            }
        }
        if(isDigit == 0) {
            strcpy(nombre_salida, argv[2]);
        }
        else{
            num_iteraciones = atoi(argv[2]);
        }
    }
    else if(argc >= 4) {
        num_iteraciones = atoi(argv[3]);
        strcpy(nombre_salida, argv[2]);
    }

    //Debemos agregar al nombre del archivo la fecha y hora, además del .txt al final
    t = time(NULL);
    time_info = localtime(&t);
    strftime(fecha_salida, 64, "%d%m%Y_%H%M%S", time_info);

    if(strlen(nombre_salida) == 0) {
        strcpy(nombre_salida, "sim_cafeteria");
    }

    punto = strstr(nombre_salida, ".txt");
    if(punto != NULL){
        *punto = '\0';
    }
    sprintf(nombre_salida, "%s_%s.txt", nombre_salida, fecha_salida);

    fichero_salida = fopen(nombre_salida, "w");
    if(fichero_salida == NULL) {
        perror("Error creando log");
        return 1;
    }

    //Mostramos la configuración inicial, además de escribirla en el fichero (si éste existe)
    mostrar_config();

    //Ponemos "a punto" todos las variables y demás del programa
    inicializar_recursos();

    //Crear y simular ciclo de vida de los clientes
    for(i = 0; i < cafeteria.num_clientes; i++) {
        arg = (int *)malloc(sizeof(int));
        *arg = i + 1;
	error = pthread_create(&clientes[i], NULL, ciclo_clientes, arg);
        if(error != 0){
            fprintf(stderr, "Error creando cliente %d: %s\n", i, strerror(error));
	    exit(1);
        }
    }

    //Crear y simular ciclo de vida de los camareros
    for(i = 0; i < cafeteria.num_camareros; i++) {
        arg = (int *)malloc(sizeof(int));
        *arg = i + 1;
	error = pthread_create(&camareros[i], NULL, ciclo_camareros, arg);
        if(error != 0){
            fprintf(stderr, "Error creando camarero %d: %s\n", i, strerror(error));
	    exit(1);
        }
    }

    limpieza();

    return 0;
}

int leer_config(char* nombre_fichero){
    FILE *f = fopen(nombre_fichero, "r");

    if(f == NULL){
        perror("Error al abrir el fichero de configuración");
        return -1;
    }

    fscanf(f, "%d", &cafeteria.num_clientes);
    fscanf(f, "%d", &cafeteria.num_camareros);
    fscanf(f, "%d", &cafeteria.num_sillas);
    fscanf(f, "%d", &cafeteria.capacidad_cola);
    fscanf(f, "%d", &cafeteria.tiempo_min_llegada_cliente);
    fscanf(f, "%d", &cafeteria.tiempo_max_llegada_cliente);
    fscanf(f, "%d", &cafeteria.tiempo_min_prep);
    fscanf(f, "%d", &cafeteria.tiempo_max_prep);
    fscanf(f, "%d", &cafeteria.tiempo_min_consumo);
    fscanf(f, "%d", &cafeteria.tiempo_max_consumo);

    fclose(f);
    return 0;
}

void mostrar_config(){
    char msg[1024];

    sprintf(msg, "Simulación Cafetería: CONFIGURACIÓN INICIAL\n"); escribir_fichero(msg);
    sprintf(msg, "Clientes: %d\n", cafeteria.num_clientes); escribir_fichero(msg);
    sprintf(msg, "Camareros: %d\n", cafeteria.num_camareros); escribir_fichero(msg);
    sprintf(msg, "Sillas en la barra: %d\n", cafeteria.num_sillas); escribir_fichero(msg);
    sprintf(msg, "Capacidad de la cola de pedidos: %d\n", cafeteria.capacidad_cola); escribir_fichero(msg);
    sprintf(msg, "Tiempo mínimo de llegada de un cliente: %d\n", cafeteria.tiempo_min_llegada_cliente); escribir_fichero(msg);
    sprintf(msg, "Tiempo máximo de llegada de un cliente: %d\n", cafeteria.tiempo_max_llegada_cliente); escribir_fichero(msg);
    sprintf(msg, "Tiempo mínimo de preparación de café: %d\n", cafeteria.tiempo_min_prep); escribir_fichero(msg);
    sprintf(msg, "Tiempo máximo de preparación de café: %d\n", cafeteria.tiempo_max_prep); escribir_fichero(msg);
    sprintf(msg, "Tiempo minimo de consumo de café: %d.\n", cafeteria.tiempo_min_consumo); escribir_fichero(msg);
    sprintf(msg, "Tiempo máximo de consumo de café: %d.\n", cafeteria.tiempo_max_consumo); escribir_fichero(msg);
    sprintf(msg, "SIMULACIÓN FUNCIONAMIENTO Cafetería\n"); escribir_fichero(msg);
}

void escribir_fichero(char *msg){
    printf("%s", msg);

    if(fichero_salida != NULL){
        fprintf(fichero_salida, "%s", msg);
        fflush(fichero_salida);
    }
}

int inicializar_recursos(){
    int i;

    //Inicializar semáforos a los valores idóneos
    sem_init(&sem_sillas, 0, cafeteria.num_sillas);
    sem_init(&sem_huecos_cola, 0, cafeteria.capacidad_cola);
    sem_init(&sem_pedidos_listos, 0, 0);

    sem_pedido_cliente = (sem_t *) malloc(sizeof(sem_t) * cafeteria.num_clientes);
    for(i = 0; i < cafeteria.num_clientes; i++){
        sem_init(&sem_pedido_cliente[i], 0, 0);
    }

    //Inicializar mutex
    pthread_mutex_init(&mutex_escribir, NULL);
    pthread_mutex_init(&mutex_cola, NULL);

    //Inicializar variables de las estructuras
    cola.capacidad = cafeteria.capacidad_cola;
    cola.entrada = 0;
    cola.salida = 0;
    cola.ids = (int*) malloc(sizeof(int) * cafeteria.capacidad_cola);
    if(cola.ids == NULL) {
        perror("Error creando cola");
        return 1;
    }

    //Inicializar hilos
    clientes = (pthread_t *) malloc(sizeof(pthread_t) * cafeteria.num_clientes);
    camareros = (pthread_t *) malloc(sizeof(pthread_t) * cafeteria.num_camareros);

    return 0;
}

void *ciclo_clientes(void *arg){
    int id = *(int *) arg;
    char msg[256];
    int iteraciones_restantes = num_iteraciones;

    tiempo_espera(cafeteria.tiempo_min_llegada_cliente, cafeteria.tiempo_max_llegada_cliente);

    while(iteraciones_restantes == -1 || iteraciones_restantes > 0){
        //Escribimos primeros mensajes
        pthread_mutex_lock(&mutex_escribir);
        sprintf(msg, "Cliente %d llega\n", id);
        escribir_fichero(msg);
        sprintf(msg, "Cliente %d intenta sentarse\n", id);
        escribir_fichero(msg);
        pthread_mutex_unlock(&mutex_escribir);

        //Espera hasta que haya una silla libre
        sem_wait(&sem_sillas);

        pthread_mutex_lock(&mutex_escribir);
        sprintf(msg, "Cliente %d consigue una silla\n", id);
        escribir_fichero(msg);
        pthread_mutex_unlock(&mutex_escribir);

        //Espera hasta que pueda hacer su pedido (cola de pedidos tiene hueco)
        sem_wait(&sem_huecos_cola);

        //Realiza el pedido (privatiza la cola para que no se produzcan problemas y suma 1 a la variable de pedidos para poder ser atendido)
        pthread_mutex_lock(&mutex_cola);

        cola.ids[cola.entrada] = id;
        cola.entrada = (cola.entrada + 1) % cola.capacidad;

        sem_post(&sem_pedidos_listos);

        pthread_mutex_lock(&mutex_escribir);
        sprintf(msg, "Cliente %d hace un pedido\n", id);
        escribir_fichero(msg);
        pthread_mutex_unlock(&mutex_escribir);

        pthread_mutex_unlock(&mutex_cola);

        //Espera a que su pedido sea realizado
        sem_wait(&sem_pedido_cliente[id - 1]);

	//El cliente recibe su café
	pthread_mutex_lock(&mutex_escribir);
	sprintf(msg, "Cliente %d recibe su café\n", id);
	escribir_fichero(msg);
	pthread_mutex_unlock(&mutex_escribir);

        //El cliente se toma su café
        pthread_mutex_lock(&mutex_escribir);
        sprintf(msg, "Cliente %d consume su café\n", id);
        escribir_fichero(msg);
        pthread_mutex_unlock(&mutex_escribir);
        tiempo_espera(cafeteria.tiempo_min_consumo, cafeteria.tiempo_max_consumo);

        //Cuando termina su iteración, "liberará" su silla
        pthread_mutex_lock(&mutex_escribir);
        sprintf(msg, "Cliente %d libera una silla\n", id);
        escribir_fichero(msg);
        pthread_mutex_unlock(&mutex_escribir);

        sem_post(&sem_sillas);

        //Actualizamos iteraciones restantes
        if(iteraciones_restantes > 0){
            iteraciones_restantes--;
        }
    }

    free(arg);

    return NULL;
}

void *ciclo_camareros(void *arg){
    int id = *(int *) arg;
    char msg[256];
    int id_pedido;

    while(1){
        //El camarero esperará a que se realice algún pedido
        sem_wait(&sem_pedidos_listos);

        //Privatizaremos la cola para que no más de un camarero coja el pedido
        pthread_mutex_lock(&mutex_cola);

        id_pedido = cola.ids[cola.salida];
        cola.salida = (cola.salida + 1) % cola.capacidad;

        pthread_mutex_unlock(&mutex_cola);

        //Actualizamos la cola de pedidos para que puedan hacerse más
        sem_post(&sem_huecos_cola);

        //Escribimos que ya está el camarero atendiendo el pedido del cliente
        pthread_mutex_lock(&mutex_escribir);
        sprintf(msg, "Camarero %d atiende pedido de Cliente %d\n", id, id_pedido);
        escribir_fichero(msg);
        pthread_mutex_unlock(&mutex_escribir);

        //Esperamos el tiempo de preparación
        tiempo_espera(cafeteria.tiempo_min_prep, cafeteria.tiempo_max_prep);

        //Ponemos como listo el pedido
        sem_post(&sem_pedido_cliente[id_pedido - 1]);
    }

    free(arg);

    return NULL;
}

void tiempo_espera(int min, int max){
    int tiempo = min + rand() % (max - min + 1);

    sleep(tiempo);
}

void limpieza(){
    int i;

    //De esta manera haremos que los hilos de los clientes se "unifiquen" en este punto y todos puedan terminar correctamente
    for(i = 0; i < cafeteria.num_clientes; i++) {
        pthread_join(clientes[i], NULL);
    }

    //No hará falta hacer nada para cerrar o eliminar a los camareros, ya que cuando el proceso termine ellos también lo harán

    //Destrucción de semáforos y mutex
    sem_destroy(&sem_sillas);
    sem_destroy(&sem_huecos_cola);
    sem_destroy(&sem_pedidos_listos);

    for(i = 0; i < cafeteria.num_clientes; i++) {
        sem_destroy(&sem_pedido_cliente[i]);
    }

    pthread_mutex_destroy(&mutex_escribir);
    pthread_mutex_destroy(&mutex_cola);

    free(cola.ids);
    free(clientes);
    free(camareros);
    free(sem_pedido_cliente);

    if(fichero_salida != NULL){
        fclose(fichero_salida);
    }
}






















