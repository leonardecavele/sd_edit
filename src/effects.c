#include "effects.h"
#include <stdlib.h>

void divide_volume(short *buffer, sf_count_t c, int divider) 
{
	for (int i = 0; i < c; i++) 
	{
		buffer[i] = buffer[i] / divider;
	}
}

void shuffle_chunks(short *buffer, sf_count_t c, int chunk_size)
{
    int chunk_count = c / chunk_size;

    for (int i = chunk_count - 1; i > 0; i--)
    {
        int j = rand() % (i + 1);

        for (int k = 0; k < chunk_size; k++)
        {
            short tmp = buffer[i * chunk_size + k];
            buffer[i * chunk_size + k] = buffer[j * chunk_size + k];
            buffer[j * chunk_size + k] = tmp;
        }
    }
}

