// CI3825: Sistemas de Operacion I
//Proyecto 2: Simulacion concurrente de colas de banco
//Integrantes:
/*
Brian Orta 21-10447
Ricardo Garcia 2010274              
*/
//Se solicito hace un programa en C que, mediante el uso de hilos y multithreading
//Para ello usaremos la libreria de POSIX, llamada pthread.h

//estas son las librerias que se van a usar 
#include "banco.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <math.h>
#include <errno.h>
#include <time.h>

//en esta parte se define el numero maximo de clientes
#define MAX_CLIENTES_TOTAL 1024

//aqui se definen los tipos de datos y las estructuras que se van a usar, 
//estos datos estan bien definidos en banco.h
static Cola cola;

//creo los mutex, de phtread que vamos a usar 
static pthread_mutex_t mutex_cola = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  cond_cola  = PTHREAD_COND_INITIALIZER;
static int banco_cerrado = 0;

static Config config;
static Cliente clientes[MAX_CLIENTES_TOTAL];
static int N_clientes = 0;

typedef struct {
    int cajero_id;
    double tiempo_libre;
} ArgsCajero;

//Metricas y estadisticas acerca de los clientes
static double suma_Wq = 0, suma_W = 0, max_Wq = 0, tiempo_total = 0;
static int clientes_atendidos = 0;
pthread_mutex_t mutex_stats = PTHREAD_MUTEX_INITIALIZER;

//Esta es la cola de clientes, o de procesos en espera 
void cola_init(Cola *c) {
    c->front = c->rear = NULL;
    c->size = 0;
}

//esto es para encolar los procesos, cuando los mandamos a esperar
void cola_enqueue(Cola *c, Cliente cl) {
    ClienteNode *n = malloc(sizeof(ClienteNode));
    if (!n) { perror("malloc"); exit(1); }
    n->cliente = cl;
    n->next = NULL;
    if (c->rear) c->rear->next = n;
    else c->front = n;
    c->rear = n;
    c->size++;
}

//aqui desencolamos un proceso, cuando un cliente va a pasar al cajero
int cola_dequeue(Cola *c, Cliente *cl) {
    if (!c->front) return 0;
    ClienteNode *n = c->front;
    *cl = n->cliente;
    c->front = n->next;
    if (!c->front) c->rear = NULL;
    c->size--;
    free(n);
    return 1;
}

//esta funcion solo verifica si la cola de clientes esta vacia 
int cola_vacia(Cola *c) {
    return c->size == 0;
}

//esta parte deberia leer el archivo y verificar que la entrada sea valida, sino, lanza un error
//hay que verificar que esto corra bien (Brian borra el comentario cuando esta parte sirva)
int leer_config(const char *filename, Config *cfg) {

    //esto hace que el archivo se abra en modo lectura 
    FILE *f = fopen(filename, "r");

    //si hay un error al leer el archivo, salta este error
    if (!f) { perror("Error abriendo archivo de configuración"); return -1; }

    //esoacio de variables:
    char linea[MAX_PARAM_LINE];
    int found[5] = {0};

    // mientras que se encuentre una linea en f, se va a ejecutar el while, esto hasta que lea todas 
    //las lineas del archivo. 
    while (fgets(linea, sizeof(linea), f)) {
        char key[STRLEN_PARAM], val[STRLEN_PARAM];
        if (linea[0] == '#' || strlen(linea) <= 2) continue;
        if (sscanf(linea, "%31[^=]=%31s", key, val) != 2) continue;

        /*
        esta parte del codigo maneja el funcionamiento de los cajeros, el tiempo de cierre
        , el lambda, el mu (no se como colocar las letras griegas), el numero maximo de clientes 
        o detectar si hay un parametro desconocido
        */
        if (!strcmp(key, "CAJEROS")) {
            cfg->CAJEROS = atoi(val); found[0]=1;
            if (cfg->CAJEROS < 1) goto inv;
        } else if (!strcmp(key, "TCIERRE")) {
            cfg->TCIERRE = atoi(val); found[1]=1;
            if (cfg->TCIERRE <= 0) goto inv;
        } else if (!strcmp(key, "LAMBDA")) {
            cfg->LAMBDA = atof(val); found[2]=1;
            if (cfg->LAMBDA <= 0) goto inv;
        } else if (!strcmp(key, "MU")) {
            cfg->MU = atof(val); found[3]=1;
            if (cfg->MU <= 0) goto inv;
        } else if (!strcmp(key, "MAX_CLIENTES")) {
            cfg->MAX_CLIENTES = atoi(val); found[4]=1;
            if (cfg->MAX_CLIENTES < 1) goto inv;
        } else {
            fprintf(stderr,"Warning: parámetro desconocido \"%s\"\n", key);
        }
        continue;
    inv:
        fprintf(stderr,"Error: Parámetro %s inválido o fuera de rango\n", key);
        fclose(f);
        return -1;
    }

    //cierra el archivo 
    fclose(f);
    for (int i=0; i<5; i++) if (!found[i]) {
        fprintf(stderr,"Error: Faltan parámetros requeridos en archivo configuración\n");
        return -1;
    }
    return 0;
}

//esto retorna un numero "aleatorio"  entre 0 y 1, son incluirlos 
double _randU() {
    return ((double)(rand())+1.0)/((double)RAND_MAX+2.0);
}

//se el numero para la exponencial, el lambda 
double rand_exp(double lambda) {
    double u;
    do { u = _randU(); } while (u <= 0.0 || u >= 1.0);
    return -log(u)/lambda;
}

//funcion que hace de cajero
void *funcion_cajero(void *arg) {
    ArgsCajero *args = (ArgsCajero*)arg;
    double tiempo_libre = 0.0;

    while (1) {
        Cliente cl;
        pthread_mutex_lock(&mutex_cola);
        while (cola_vacia(&cola) && !banco_cerrado)
            pthread_cond_wait(&cond_cola, &mutex_cola);
        if (cola_vacia(&cola) && banco_cerrado) {
            pthread_mutex_unlock(&mutex_cola);
            break;
        }
        int ok = cola_dequeue(&cola, &cl);
        pthread_mutex_unlock(&mutex_cola);
        if (!ok) continue;

        double Bi = cl.llegada > tiempo_libre ? cl.llegada : tiempo_libre;
        cl.inicio_atencion = Bi;

        pthread_mutex_lock(&mutex_stats);
        printf("[t=%7.2f] Cliente %d inicia atención en Cajero %d\n", Bi, cl.id, args->cajero_id+1);
        pthread_mutex_unlock(&mutex_stats);

        cl.servicio = rand_exp(config.MU);
        cl.fin_atencion = cl.inicio_atencion + cl.servicio;

        pthread_mutex_lock(&mutex_stats);
        printf("[t=%7.2f] Cliente %d finaliza atención en Cajero %d\n", cl.fin_atencion, cl.id, args->cajero_id+1);

        double Wq = cl.inicio_atencion - cl.llegada;
        double Wi = cl.fin_atencion - cl.llegada;
        suma_Wq += Wq;
        suma_W  += Wi;
        max_Wq = Wq > max_Wq ? Wq : max_Wq;
        tiempo_total = cl.fin_atencion > tiempo_total ? cl.fin_atencion : tiempo_total;
        clientes_atendidos++;

        pthread_mutex_unlock(&mutex_stats);

        tiempo_libre = cl.fin_atencion;
    }
    return NULL;
}


// esta funcion calcula el valor del factorial y la sumatoria
double log_factorial(int n) {
    double sum = 0.0;
    for (int k=2;k<=n;k++) sum += log((double)k);
    return sum;
}

//calcula el valor teorico del lambda, el mu y el rho 
void calcular_teorico(int cajeros, double lambda, double mu, double *rho, double *Wq, double *W) {
    double a = lambda / mu;
    *rho = a / cajeros;

    if ((*rho) >= 1.0) return;

    // Sumatorio de k = 0 a c-1: sum += (a^k)/k! usando log para evitar overflows
    double log_a = log(a);
    double sum = 0.0;

    for (int k = 0; k < cajeros; k++) {
        double term_log = k * log_a - log_factorial(k);
        sum += exp(term_log);
    }

    // Calcular en log: log(a^c / c!) = c*log_a - log_factorial(c)
    double log_a_c_over_fact = cajeros * log_a - log_factorial(cajeros);
    double a_c_over_fact = exp(log_a_c_over_fact);

    double Cc = a_c_over_fact / (1 - *rho); // factor en el numerador
    double P_wait = Cc / (sum + Cc);

    *Wq = P_wait / (cajeros * mu - lambda);
    *W = *Wq + 1 / mu;
}

//esta es la funcion principal, aqui se ejecuta toda la logica del banco
int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr,"Uso: %s archivo_config.txt\n", argv[0]);
        return 1;
    }
    srand(time(NULL));
    cola_init(&cola);

    if (leer_config(argv[1], &config) != 0)
        return 2;

    /* Aqui se genern los clientes, teniendo en cuenta la restriccion del numero maximo de clientes  */
    double acumulado = 0.0;
    int N=0, truncado=0;
    while (N < config.MAX_CLIENTES) {
        double Ti = rand_exp(config.LAMBDA);
        acumulado += Ti;
        if (acumulado >= config.TCIERRE) break;
        clientes[N].id = N+1;
        clientes[N].llegada = acumulado;
        clientes[N].inicio_atencion = 0.0;
        clientes[N].fin_atencion = 0.0;
        clientes[N].servicio = 0.0;
        printf("[t=%7.2f] Cliente %d llega al banco\n", acumulado, N+1);
        N++;
    }

    //por temas de legibilidad del codigo, se le asigna el nombre N_clientes, para referirnos al numero de clientes
    N_clientes = N;

    //si hay mas clientes de los maximos establecidos, entonces a truncado se le asigna el valor de 1 
    if (N >= config.MAX_CLIENTES) truncado=1;

    /*  Ahora ponemos a todos los clientes en la cola de espera, donde se iran sacando a medida que sean atendidos */
    for (int i=0; i<N_clientes; i++)
        cola_enqueue(&cola, clientes[i]);

    /* ahora creamos los cajeros, donde seran atenidos los clientes  */
    pthread_t cajeros[config.CAJEROS];
    ArgsCajero args[config.CAJEROS];
    for (int i=0;i<config.CAJEROS;i++) {
        args[i].cajero_id = i;
        args[i].tiempo_libre = 0.0;
        pthread_create(&cajeros[i], NULL, funcion_cajero, &args[i]);
    }

    /* ahora despertamos a los hilos que estan en cola, para que vayan iendo atendidos en los cajeros */
    pthread_mutex_lock(&mutex_cola);
    banco_cerrado = 1;
    pthread_cond_broadcast(&cond_cola);
    pthread_mutex_unlock(&mutex_cola);

    for (int i=0;i<config.CAJEROS;i++)
        pthread_join(cajeros[i], NULL);

    /* Ahora, aqui se calculan las metricas simuladas, el valor real de lo que sucedio mientras ejecutamos los procesos  */
    double Wq_sim = clientes_atendidos? suma_Wq/clientes_atendidos : 0.0;
    double W_sim  = clientes_atendidos? suma_W /clientes_atendidos : 0.0;

    /* Aqui se calculan las metricas teoricas de lo que sucedio mientras ejecutabamos los procesos*/
    double rho, Wq_teo, W_teo;
    calcular_teorico(config.CAJEROS, config.LAMBDA, config.MU, &rho, &Wq_teo, &W_teo);
    int estable = rho<1.0;

    /* diagnostic output */
    printf("DEBUG: Wq_sim=%.6f Wq_teo=%.6f W_sim=%.6f W_teo=%.6f rho=%.4f\n",
           Wq_sim, Wq_teo, W_sim, W_teo, rho);

    /* Este es el informe, o resumen, de lo que sucedio en la simulacion del cajero  */
    printf("==================================================\n");
    printf("RESUMEN FINAL\n");
    printf("==================================================\n");
    printf("Parametros:\n");
    printf("CAJEROS: %d\n", config.CAJEROS);
    printf("TCIERRE: %d\n", config.TCIERRE);
    printf("LAMBDA: %.4f\n", config.LAMBDA);
    printf("MU: %.4f\n", config.MU);
    printf("MAX_CLIENTES: %d\n", config.MAX_CLIENTES);
    printf("Resultados Simulados:\n");
    printf("Clientes atendidos: %d\n", clientes_atendidos);
    printf("Truncado por MAX_CLIENTES: %s\n", truncado ? "SI" : "NO");
    printf("Tiempo promedio de espera (Wq): %.2f\n", Wq_sim);
    printf("Tiempo promedio en sistema (W): %.2f\n", W_sim);
    printf("Tiempo maximo de espera: %.2f\n", max_Wq);
    printf("Tiempo total hasta ultimo cliente: %.2f\n", tiempo_total);

    if (estable) {
        printf("Resultados Teoricos (M/M/c):\n");
        printf("Utilizacion (rho): %.4f\n", rho);
        printf("Tiempo promedio de espera teorico: %.2f\n", Wq_teo);
        printf("Tiempo promedio en sistema teorico: %.2f\n", W_teo);
        {
            double errWq = Wq_teo > 0.0 ? 100.0 * fabs(Wq_teo - Wq_sim) / Wq_sim : 0.0;
            double errW  = W_teo > 0.0  ? 100.0 * fabs(W_teo  - W_sim)  / W_sim  : 0.0;
            printf("Error relativo Wq: %.1f %%\n", errWq);
            printf("Error relativo W: %.1f %%\n", errW);
        }
        printf("Estado del sistema:\n");
        printf("rho = %.4f < 1 → Sistema estable\n", rho);
    } else {
        printf("Estado del sistema:\n");
        printf("rho = %.4f >= 1 → Sistema INESTABLE\n", rho);
        printf("Métricas teóricas omitidas (sistema inestable)\n");
    }
    printf("==================================================\n");

    return 0;
}