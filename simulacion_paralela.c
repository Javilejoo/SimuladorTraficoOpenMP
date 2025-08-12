#include <omp.h>

enum { RED=0, GREEN=1, YELLOW=2 };

typedef struct {
    int id;
    int pos;
    int lane; // 0 = Norte a Sur, 1 = Este a Oeste
} Vehiculo;

typedef struct {
    int id;
    int estado;
    int laneGroup; // 0 = Norte a Sur, 1 = Este a Oeste
    int timer, R, Y, G;
    int pos;  // posicion del "alto"
} Semaforo;

typedef struct {
    int id;
    int L;
    int numCarriles;
    int *occ; // ocupacion  -1 libre, id de vehículo si ocupado (idx = lane*L + pos)

    Vehiculo * vehiculos;
    int numVehiculos;
    int capVehiculos;

    Semaforo semaforos[2];  // [0]=NS, [1]=EW
} Interseccion;

// simulacion_paralela.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <omp.h>

enum { RED=0, GREEN=1, YELLOW=2 };

typedef struct {
    int id;
    int pos;
    int lane; // 0 = Norte→Sur, 1 = Este→Oeste
} Vehiculo;

typedef struct {
    int id;
    int estado;    // RED/GREEN/YELLOW
    int laneGroup; // 0 = N→S, 1 = E→W
    int timer, R, Y, G;
    int pos;       // celda del "alto"
} Semaforo;

typedef struct {
    int id;
    int L;
    int numCarriles;
    int *occ; // ocupación: -1 libre, o id de vehículo (idx = lane*L + pos)

    Vehiculo *vehiculos;
    int numVehiculos;
    int capVehiculos;

    Semaforo semaforos[2];  // [0]=NS, [1]=EW
} Interseccion;

/* ---------- Helpers ---------- */
static inline int idx(int lane, int pos, int L) { return lane * L + pos; }

static inline int duracion_fase(const Semaforo *s) {
    switch (s->estado) {
        case RED:     return s->R;
        case GREEN:   return s->G;
        case YELLOW:  return s->Y;
        default:      return s->R;
    }
}

static inline void next_state(Semaforo *s) {
    if (s->estado == RED)      s->estado = GREEN;
    else if (s->estado == GREEN) s->estado = YELLOW;
    else                         s->estado = RED;
    s->timer = duracion_fase(s);
}

/* ---------- Inicialización / Utilidades ---------- */
void init_interseccion(Interseccion *I, int id, int L, int numCarriles, int capVehiculos) {
    I->id = id;
    I->L = L;
    I->numCarriles = numCarriles;

    I->occ = (int*)malloc(sizeof(int) * (size_t)(numCarriles * L));
    for (int i = 0; i < numCarriles * L; ++i) I->occ[i] = -1;

    I->capVehiculos = capVehiculos;
    I->numVehiculos = 0;
    I->vehiculos = (Vehiculo*)malloc(sizeof(Vehiculo) * (size_t)capVehiculos);
}

void destroy_interseccion(Interseccion *I){
    free(I->occ);
    free(I->vehiculos);
    I->occ = NULL;
    I->vehiculos = NULL;
    I->numVehiculos = I->capVehiculos = 0;
}

void init_semaforos(Interseccion *I,
                    int posNS, int posEW,
                    int R, int G, int Y,
                    int estadoNS, int estadoEW)
{
    // N→S
    I->semaforos[0].id = 0;
    I->semaforos[0].laneGroup = 0;
    I->semaforos[0].pos = posNS;
    I->semaforos[0].R = R; I->semaforos[0].G = G; I->semaforos[0].Y = Y;
    I->semaforos[0].estado = estadoNS;
    I->semaforos[0].timer  = duracion_fase(&I->semaforos[0]);

    // E→W
    I->semaforos[1].id = 1;
    I->semaforos[1].laneGroup = 1;
    I->semaforos[1].pos = posEW;
    I->semaforos[1].R = R; I->semaforos[1].G = G; I->semaforos[1].Y = Y;
    I->semaforos[1].estado = estadoEW;
    I->semaforos[1].timer  = duracion_fase(&I->semaforos[1]);
}



/* Distribuye N vehículos sin choques (round robin por carril y posición) */
void init_vehiculos_round_robin(Interseccion *I, int N) {
    int cap = I->numCarriles * I->L;
    int n = (N > cap) ? cap : N;
    for (int i = 0; i < n; ++i) {
        int lane = i % I->numCarriles;
        int pos  = (i / I->numCarriles) % I->L;
        add_vehicle(I, lane, pos);
    }
}




