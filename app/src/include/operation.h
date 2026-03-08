#ifndef OPERATION_H
#define OPERATION_H

#include <stdint.h>
/*
* Operation Mode Header
*
* Operations within Operation Mode:
* 1. Launch the ball based on entries in the set seuqence
* 2. User can stop the machine at any time
* 3. User can repeat the same set
* 4. User can shuffle the set sequence
*/

void operation_init();
void stop_machine();
void repeat_set();
void shuffle_set_sequence();

#endif // OPERATION_H