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

int add_vehicle(Interseccion *I, int lane, int pos) {
    if (I->numVehiculos >= I->capVehiculos) return -1;
    int id = I->numVehiculos;
    I->vehiculos[id] = (Vehiculo){ .id=id, .lane=lane, .pos=pos };
    I->numVehiculos++;

    int k = idx(lane, pos, I->L);
    if (I->occ[k] == -1) {
        I->occ[k] = id;
    } else {
        // buscar siguiente celda libre hacia adelante (simple)
        int p = pos, vueltas = 0;
        while (I->occ[idx(lane, p, I->L)] != -1) {
            p = (p + 1) % I->L;
            if (++vueltas > I->L) break; // carril lleno
        }
        I->vehiculos[id].pos = p;
        I->occ[idx(lane, p, I->L)] = id;
    }
    return id;
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


/* ---------- Lógica de vehículos (PARALELA) ---------- */
static inline int hay_alto_en(const Interseccion *I, int lane, int pos) {
    // [0]=N→S, [1]=E→W
    for (int i = 0; i < 2; ++i) {
        const Semaforo *s = &I->semaforos[i];
        if (s->laneGroup == lane && s->pos == pos) {
            return s->estado != GREEN; // alto si no está en verde
        }
    }
    return 0;
}

void move_vehicles_parallel(Interseccion *I) {
    int total = I->numCarriles * I->L;

    int *next_occ = (int*)malloc(sizeof(int) * (size_t)total);
    for (int k = 0; k < total; ++k) next_occ[k] = -1;

    // Un lock por celda para reclamar destinos sin carreras
    omp_lock_t *locks = (omp_lock_t*)malloc(sizeof(omp_lock_t) * (size_t)total);
    for (int k = 0; k < total; ++k) omp_init_lock(&locks[k]);

    // 1) Decidir y reservar destino en paralelo
    #pragma omp parallel for schedule(dynamic, 32)
    for (int id = 0; id < I->numVehiculos; ++id) {
        Vehiculo *v = &I->vehiculos[id];
        int cur = v->pos;
        int nxt = (cur + 1) % I->L;

        // Solo lecturas de I->occ e I->semaforos
        int ocupado = I->occ[idx(v->lane, nxt, I->L)] != -1;
        int alto    = hay_alto_en(I, v->lane, nxt);

        int dest = (ocupado || alto) ? cur : nxt;
        int k    = idx(v->lane, dest, I->L);

        // Resolver conflictos: gana el id menor
        omp_set_lock(&locks[k]);
        if (next_occ[k] == -1 || id < next_occ[k]) next_occ[k] = id;
        omp_unset_lock(&locks[k]);
    }

    // 2) Aplicar movimientos (puede ser paralelo)
    #pragma omp parallel for schedule(static)
    for (int k = 0; k < total; ++k) {
        int id = next_occ[k];
        I->occ[k] = id;
        if (id != -1) {
            int lane = k / I->L;
            int pos  = k % I->L;
            I->vehiculos[id].lane = lane;
            I->vehiculos[id].pos  = pos;
        }
    }

    for (int k = 0; k < total; ++k) omp_destroy_lock(&locks[k]);
    free(locks);
    free(next_occ);
}

/* ---------- Impresión ---------- */
void print_estado(const Interseccion *I, int iter) {
    printf("Iteración %d\n", iter);
    for (int i = 0; i < I->numVehiculos; ++i) {
        printf("Vehículo %d - Lane:%d Pos:%d\n",
               I->vehiculos[i].id, I->vehiculos[i].lane, I->vehiculos[i].pos);
    }
    for (int j = 0; j < 2; ++j) {
        printf("Semáforo %d (laneGroup=%d pos=%d) - Estado:%d Timer:%d\n",
               I->semaforos[j].id, I->semaforos[j].laneGroup, I->semaforos[j].pos,
               I->semaforos[j].estado, I->semaforos[j].timer);
    }
    puts("");
}

