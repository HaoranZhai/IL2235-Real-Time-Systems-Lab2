#ifndef DIJKSTRA_H
#define DIJKSTRA_H

#include "input.h"

typedef unsigned char inputData_t[NUM_NODES][NUM_NODES];

void dijkstra_init( inputData_t* input_data );
void dijkstra_main( void );
int dijkstra_return( void );

#endif /* DIJKSTRA_H */