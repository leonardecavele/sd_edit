#include <stdio.h>
#include <sndfile.h>
#include "effects.h"

int main() {
    
    printf("program started\n");
	
    SF_INFO info;
    SNDFILE *file = sf_open("audio/test.wav", SFM_READ, &info);

    if (file == NULL) {
        printf("Erreur : impossible d'ouvrir le fichier audio !\n");
        return 1;
    } else {
   	 printf("Format: %d, Canaux: %d, Sample rate: %d\n",
          	 info.format, info.channels, info.samplerate);
    }

       SNDFILE *modif_file = sf_open("audio/modified.wav", SFM_WRITE, &info);

    if (file == NULL) {
        printf("Erreur : impossible d'ouvrir le fichier audio !\n");
        return 1;
    }

    #define buffer_size 1024
    short buffer[buffer_size];

    sf_count_t c;
    while ((c = sf_read_short(file, buffer, buffer_size)) > 0) {
	   
		shuffle_chunks(buffer, c, 1000);
	    //divide_volume(buffer, c, 5);

	    sf_write_short(modif_file, buffer, c);
    }

    sf_close(file);
    sf_close(modif_file);
    return 0;
}
