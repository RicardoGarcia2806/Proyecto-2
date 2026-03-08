#ifndef BANCO_H
#define BANCO_H

#include <pthread.h>

#define MAX_PARAM_LINE 128
#define STRLEN_PARAM   32

typedef struct {
    int id;
    double llegada;
    double inicio_atencion;
    double fin_atencion;
    double servicio;
} Cliente;

typedef struct ClienteNode {
    Cliente cliente;
    struct ClienteNode *next;
} ClienteNode;

typedef struct {
    ClienteNode *front;
    ClienteNode *rear;
    int size;
} Cola;

/* Archivo de configuración */
typedef struct {
    int CAJEROS;
    int TCIERRE;
    double LAMBDA;
    double MU;
    int MAX_CLIENTES;
} Config;

void cola_init(Cola *cola);
void cola_enqueue(Cola *cola, Cliente cl);
int cola_dequeue(Cola *cola, Cliente *cl);
int cola_vacia(Cola *cola);

int leer_config(const char* filename, Config* cfg);
double rand_exp(double lambda);

#endif
