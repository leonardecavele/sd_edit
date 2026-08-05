#ifndef PRESET_H
#define PRESET_H

#include <sndfile.h>

typedef struct
{
    int name;
    int prmtr;
    int mix;
    int length;
} Segment;

typedef struct
{
    int nb_seg;
    Segment *segments;
} Preset;

void init_preset(Preset *preset);
void free_preset(Preset *preset);
int copy_preset(Preset *destination, const Preset *source);
int parse_preset_definition(const char *definition, Preset *preset);
int save_named_preset(const char *file_path, const char *name, const Preset *preset);
int load_named_preset(const char *file_path, const char *name, Preset *preset);
int list_named_presets(const char *file_path, char ***names, int *count);
void free_named_preset_list(char **names, int count);
int generate_random_preset(Preset *preset, int randomness);

#endif
