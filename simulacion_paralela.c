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