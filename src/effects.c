#include "effects.h"
#include <stdlib.h>
#include <string.h>

#define SAMPLE_MIN (-32768)
#define SAMPLE_MAX 32767
#define DEFAULT_GAIN_PERCENT 100
#define DEFAULT_PITCH_PERCENT 100
#define DEFAULT_CHUNK_SIZE 512
#define DEFAULT_ECHO_DELAY 4000
#define DEFAULT_DISTORTION_DRIVE 150
#define ECHO_DECAY_PERCENT 60
#define MAX_DISTORTION_DRIVE 800

static short clamp_sample(int value)
{
    if (value > SAMPLE_MAX)
    {
        return SAMPLE_MAX;
    }

    if (value < SAMPLE_MIN)
    {
        return SAMPLE_MIN;
    }

    return (short)value;
}

static int clamp_mix(int mix_percent)
{
    if (mix_percent < 0)
    {
        return 0;
    }

    if (mix_percent > 100)
    {
        return 100;
    }

    return mix_percent;
}

static short mix_samples(short dry, short wet, int mix_percent)
{
    int wet_mix = clamp_mix(mix_percent);
    int dry_mix = 100 - wet_mix;
    int mixed = ((int)dry * dry_mix + (int)wet * wet_mix) / 100;

    return clamp_sample(mixed);
}

static short interpolate_sample(short a, short b, float fraction)
{
    float value = (float)a + ((float)b - (float)a) * fraction;

    if (value >= 0.0f)
    {
        return clamp_sample((int)(value + 0.5f));
    }

    return clamp_sample((int)(value - 0.5f));
}

void divide_volume(short *buffer, sf_count_t c, int divider)
{
    if (buffer == NULL || c <= 0 || divider == 0)
    {
        return;
    }

    for (sf_count_t i = 0; i < c; i++)
    {
        buffer[i] = clamp_sample((int)buffer[i] / divider);
    }
}

void shuffle_chunks(short *buffer, sf_count_t c, int chunk_size)
{
    short *copy = NULL;
    int *order = NULL;
    int chunk_count;

    if (buffer == NULL || c <= 0 || chunk_size <= 0)
    {
        return;
    }

    chunk_count = (int)(c / chunk_size);

    if (chunk_count < 2)
    {
        return;
    }

    copy = malloc(sizeof(short) * (size_t)c);
    order = malloc(sizeof(int) * (size_t)chunk_count);

    if (copy == NULL || order == NULL)
    {
        free(copy);
        free(order);
        return;
    }

    memcpy(copy, buffer, sizeof(short) * (size_t)c);

    for (int i = 0; i < chunk_count; i++)
    {
        order[i] = i;
    }

    for (int i = chunk_count - 1; i > 0; i--)
    {
        int j = rand() % (i + 1);
        int tmp = order[i];
        order[i] = order[j];
        order[j] = tmp;
    }

    for (int i = 0; i < chunk_count; i++)
    {
        memcpy(
            buffer + (i * chunk_size),
            copy + (order[i] * chunk_size),
            sizeof(short) * (size_t)chunk_size
        );
    }

    if ((sf_count_t)(chunk_count * chunk_size) < c)
    {
        memcpy(
            buffer + (chunk_count * chunk_size),
            copy + (chunk_count * chunk_size),
            sizeof(short) * (size_t)(c - (chunk_count * chunk_size))
        );
    }

    free(copy);
    free(order);
}

void apply_gain(short *buffer, sf_count_t c, int gain_percent, int mix_percent)
{
    if (buffer == NULL || c <= 0)
    {
        return;
    }

    if (gain_percent < 0)
    {
        gain_percent = 0;
    }

    for (sf_count_t i = 0; i < c; i++)
    {
        short dry = buffer[i];
        short wet = clamp_sample(((int)dry * gain_percent) / 100);
        buffer[i] = mix_samples(dry, wet, mix_percent);
    }
}

void pitch_shift(short *buffer, sf_count_t c, int pitch_percent, int mix_percent)
{
    short *copy = NULL;
    float source_index = 0.0f;
    float step;

    if (buffer == NULL || c <= 1)
    {
        return;
    }

    if (pitch_percent <= 0)
    {
        pitch_percent = DEFAULT_PITCH_PERCENT;
    }

    copy = malloc(sizeof(short) * (size_t)c);

    if (copy == NULL)
    {
        return;
    }

    memcpy(copy, buffer, sizeof(short) * (size_t)c);
    step = (float)pitch_percent / 100.0f;

    for (sf_count_t i = 0; i < c; i++)
    {
        sf_count_t base_index;
        sf_count_t next_index;
        float fraction;
        short wet;

        while (source_index >= (float)c)
        {
            source_index -= (float)c;
        }

        base_index = (sf_count_t)source_index;
        next_index = base_index + 1;

        if (next_index >= c)
        {
            next_index = 0;
        }

        fraction = source_index - (float)base_index;
        wet = interpolate_sample(copy[base_index], copy[next_index], fraction);
        buffer[i] = mix_samples(copy[i], wet, mix_percent);
        source_index += step;
    }

    free(copy);
}

void reverse_buffer(short *buffer, sf_count_t c)
{
    if (buffer == NULL || c <= 1)
    {
        return;
    }

    for (sf_count_t i = 0, j = c - 1; i < j; i++, j--)
    {
        short tmp = buffer[i];
        buffer[i] = buffer[j];
        buffer[j] = tmp;
    }
}

void apply_echo(short *buffer, sf_count_t c, int delay_samples, int mix_percent)
{
    short *copy = NULL;

    if (buffer == NULL || c <= 1 || delay_samples <= 0 || delay_samples >= c)
    {
        return;
    }

    copy = malloc(sizeof(short) * (size_t)c);

    if (copy == NULL)
    {
        return;
    }

    memcpy(copy, buffer, sizeof(short) * (size_t)c);

    for (sf_count_t i = 0; i < c; i++)
    {
        int wet = copy[i];

        if (i >= delay_samples)
        {
            wet += ((int)copy[i - delay_samples] * ECHO_DECAY_PERCENT) / 100;
        }

        buffer[i] = mix_samples(copy[i], clamp_sample(wet), mix_percent);
    }

    free(copy);
}

void apply_distortion(short *buffer, sf_count_t c, int drive_percent, int mix_percent)
{
    int threshold;

    if (buffer == NULL || c <= 0)
    {
        return;
    }

    if (drive_percent < DEFAULT_GAIN_PERCENT)
    {
        drive_percent = DEFAULT_GAIN_PERCENT;
    }

    if (drive_percent > MAX_DISTORTION_DRIVE)
    {
        drive_percent = MAX_DISTORTION_DRIVE;
    }

    threshold = (SAMPLE_MAX * DEFAULT_GAIN_PERCENT) / drive_percent;

    if (threshold < 2048)
    {
        threshold = 2048;
    }

    for (sf_count_t i = 0; i < c; i++)
    {
        int driven = ((int)buffer[i] * drive_percent) / DEFAULT_GAIN_PERCENT;
        short wet;

        if (driven > threshold)
        {
            driven = threshold;
        }
        else if (driven < -threshold)
        {
            driven = -threshold;
        }

        driven = (driven * SAMPLE_MAX) / threshold;
        wet = clamp_sample(driven);
        buffer[i] = mix_samples(buffer[i], wet, mix_percent);
    }
}

int apply_effect(short *buffer, sf_count_t c, int effect_id, int parameter, int mix_percent)
{
    switch (effect_id)
    {
        case EFFECT_PITCH_SHIFT:
            pitch_shift(buffer, c, parameter > 0 ? parameter : DEFAULT_PITCH_PERCENT, mix_percent);
            return 0;

        case EFFECT_SHUFFLE_CHUNKS:
            shuffle_chunks(buffer, c, parameter > 0 ? parameter : DEFAULT_CHUNK_SIZE);
            return 0;

        case EFFECT_GAIN:
            apply_gain(buffer, c, parameter > 0 ? parameter : DEFAULT_GAIN_PERCENT, mix_percent);
            return 0;

        case EFFECT_REVERSE:
            reverse_buffer(buffer, c);
            return 0;

        case EFFECT_ECHO:
            apply_echo(buffer, c, parameter > 0 ? parameter : DEFAULT_ECHO_DELAY, mix_percent);
            return 0;

        case EFFECT_DISTORTION:
            apply_distortion(buffer, c, parameter > 0 ? parameter : DEFAULT_DISTORTION_DRIVE, mix_percent);
            return 0;

        default:
            return -1;
    }
}

