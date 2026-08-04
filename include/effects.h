#ifndef EFFECTS_H
#define EFFECTS_H

#include <sndfile.h>

typedef enum
{
    EFFECT_PITCH_SHIFT = 1,
    EFFECT_SHUFFLE_CHUNKS = 2,
    EFFECT_GAIN = 3,
    EFFECT_REVERSE = 4,
    EFFECT_ECHO = 5,
    EFFECT_DISTORTION = 6
} EffectId;

typedef struct
{
    int id;
    const char *name;
} EffectDescriptor;

void divide_volume(short *buffer, sf_count_t c, int divider);
void shuffle_chunks(short *buffer, sf_count_t c, int chunk_size);
void apply_gain(short *buffer, sf_count_t c, int gain_percent, int mix_percent);
void pitch_shift(short *buffer, sf_count_t c, int pitch_percent, int mix_percent);
void reverse_buffer(short *buffer, sf_count_t c);
void apply_echo(short *buffer, sf_count_t c, int delay_samples, int mix_percent);
void apply_distortion(short *buffer, sf_count_t c, int drive_percent, int mix_percent);
int apply_effect(short *buffer, sf_count_t c, int effect_id, int parameter, int mix_percent);
const EffectDescriptor *get_effect_catalog(size_t *count);
const char *effect_name(int effect_id);
int effect_is_supported(int effect_id);

#endif
