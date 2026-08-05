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

typedef struct
{
    int randomness;
    unsigned int seed;
} PresetRecipe;

void init_preset(Preset *preset);
void free_preset(Preset *preset);
int copy_preset(Preset *destination, const Preset *source);
int parse_preset_definition(const char *definition, Preset *preset);
void init_preset_recipe(PresetRecipe *recipe);
int copy_preset_recipe(PresetRecipe *destination, const PresetRecipe *source);
int preset_recipe_is_valid(const PresetRecipe *recipe);
int save_named_preset(const char *file_path, const char *name, const PresetRecipe *recipe);
int load_named_preset(const char *file_path, const char *name, PresetRecipe *recipe);
int list_named_presets(const char *file_path, char ***names, int *count);
void free_named_preset_list(char **names, int count);
int generate_random_recipe(PresetRecipe *recipe, int randomness);
int materialize_preset_from_recipe(Preset *preset, const PresetRecipe *recipe, const SF_INFO *info);

#endif
