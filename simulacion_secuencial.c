#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

/* ---------- Helpers ---------- */
static inline int idx(int lane, int pos, int L) { return lane*L + pos; } // aplanar  lane=0 (N→S):  [ (0,0) (0,1) ... (0,L-1) ] --> idx

static inline int duracion_fase(const Semaforo *s) { // Devuelve cuántos ticks dura la fase actual del semáforo, según su estado.
    return (s->estado==RED) ? s->R : (s->estado==GREEN ? s->G : s->Y);
}

/* ---------- Inicialización ---------- */
void init_interseccion(Interseccion *I, int id, int L, int numCarriles, int capVehiculos) {
    I->id = id;
    I->L = L;
    I->numCarriles = numCarriles;

    I->occ = (int*)malloc(sizeof(int) * (size_t)(numCarriles * L));
    for (int i = 0; i < numCarriles*L; ++i) I->occ[i] = -1;

    I->capVehiculos = capVehiculos;
    I->numVehiculos = 0;
    I->vehiculos = (Vehiculo*)malloc(sizeof(Vehiculo) * (size_t)capVehiculos);
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
    if (I->occ[k] == -1) I->occ[k] = id;
    else {
        // Si ya está ocupado, busca la siguiente celda libre hacia adelante (opción simple)
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

/* Distribuye N vehículos sin choques: recorre celdas en orden (lane, pos) */
void init_vehiculos_round_robin(Interseccion *I, int N) {
    int capacidad = I->numCarriles * I->L;
    int n = (N > capacidad) ? capacidad : N;
    for (int i = 0; i < n; ++i) {
        int lane = i % I->numCarriles;
        int pos  = (i / I->numCarriles) % I->L;
        add_vehicle(I, lane, pos);
    }
}

/* ---------- Impresión de estado (útil para verificar) ---------- */
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

/* ---------- Paso 3: Comportamiento de Semáforos ---------- */
static inline void next_state(Semaforo *s) {
    // Ciclo: ROJO -> VERDE -> AMARILLO -> ROJO
    if (s->estado == RED)       s->estado = GREEN;
    else if (s->estado == GREEN) s->estado = YELLOW;
    else                         s->estado = RED;
    s->timer = duracion_fase(s);
}

void update_traffic_lights(Semaforo *L, int n) {
    // Versión secuencial: recorre y actualiza cada semáforo
    for (int i = 0; i < n; ++i) {
        // Seguridad mínima: evita timers no válidos si alguien puso R/G/Y <= 0
        if (L[i].R <= 0) L[i].R = 1;
        if (L[i].G <= 0) L[i].G = 1;
        if (L[i].Y <= 0) L[i].Y = 1;

        L[i].timer--;
        if (L[i].timer <= 0) {
            next_state(&L[i]);
        }
    }
}

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

void move_vehicles_once(Interseccion *I) {
    int total = I->numCarriles * I->L;

    // Doble buffer para aplicar todos los movimientos a la vez
    int *next_occ = (int*)malloc(sizeof(int) * (size_t)total);
    for (int k = 0; k < total; ++k) next_occ[k] = -1;

    // 1) Decidir destino de cada vehículo
    for (int id = 0; id < I->numVehiculos; ++id) {
        Vehiculo *v = &I->vehiculos[id];
        int cur = v->pos;
        int nxt = (cur + 1) % I->L;

        int ocupado = I->occ[idx(v->lane, nxt, I->L)] != -1;
        int alto    = hay_alto_en(I, v->lane, nxt);

        int dest = (ocupado || alto) ? cur : nxt;

        int k = idx(v->lane, dest, I->L);
        // Si chocan 2 en la misma celda, prioridad al id menor (regla simple)
        if (next_occ[k] == -1 || id < next_occ[k]) next_occ[k] = id;
    }

    // 2) Aplicar movimientos y actualizar occ/posiciones
    for (int k = 0; k < total; ++k) {
        int id = next_occ[k];
        I->occ[k] = id;
        if (id != -1) {
            int lane = k / I->L;
            int pos  = k % I->L;
            I->vehiculos[id].lane = lane;  // por si usas >1 carril
            I->vehiculos[id].pos  = pos;
        }
    }

    free(next_occ);
}

/* ---------- Limpieza ---------- */
void destroy_interseccion(Interseccion *I){
    free(I->occ);
    free(I->vehiculos);
    I->occ = NULL;
    I->vehiculos = NULL;
    I->numVehiculos = I->capVehiculos = 0;
}

/* ---------- Bucle principal (SECUENCIAL) ---------- */
int main(void) {
    // Parámetros de demo (ajústalos si quieres)
    const int L = 5;              // largo del carril
    const int numCarriles = 2;    // 0=N→S, 1=E→W
    const int N = 6;             // vehículos
    const int T = 8;              // iteraciones de simulación

    // Semáforos: posiciones y tiempos de fase
    const int posNS = 0;
    const int posEW = 3;
    const int R = 2, G = 2, Y = 1;     // duraciones
    const int estadoNS_ini = GREEN;    // estado inicial NS
    const int estadoEW_ini = YELLOW;   // estado inicial EW

    // 1) Inicializar mundo
    Interseccion I;
    init_interseccion(&I, /*id*/0, L, numCarriles, /*capVehiculos*/N);
    init_semaforos(&I, posNS, posEW, R, G, Y, estadoNS_ini, estadoEW_ini);

    // 2) Crear vehículos (sin choques iniciales)
    init_vehiculos_round_robin(&I, N);

    // (Opcional) Imprime estado inicial
    print_estado(&I, 0);

    // 3) Bucle de simulación: semáforos -> vehículos -> imprimir
    for (int t = 1; t <= T; ++t) {
        update_traffic_lights(I.semaforos, 2);  // Paso 3
        move_vehicles_once(&I);                 // Paso 4
        print_estado(&I, t);                    // salida por iteración
        // (Opcional) pequeña pausa visual:
        // #include <time.h>
        // struct timespec ts={0, 200*1000000}; nanosleep(&ts, NULL);
    }

    // 4) Limpieza
    destroy_interseccion(&I);
    return 0;
}
