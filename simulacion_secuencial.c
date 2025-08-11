typedef struct {
    int id;
    int pos;
    int lane;
} Vehiculo;

typedef struct {
    int id;
    int estado;
    int laneGroup;
    int timer, R, Y, G;
    int pos;
} Semaforo;

typedef struct {
    int id;
    int L;
    int numCarriles;
    int *occ;

    Vehiculo * vehiculos;
    int numVehiculos;
    int capVehiculos;

    Semaforo semaforos[2];
} Interseccion;
