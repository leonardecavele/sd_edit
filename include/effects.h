#ifndef EFFECTS_H
#define EFFECTS_H

#include <sndfile.h>

void divide_volume(short *buffer, sf_count_t c, int divider);
void shuffle_chunks(short *buffer, sf_count_t c, int chunk_size);

#endif
