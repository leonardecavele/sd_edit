#include "preset.h"

#include <ctype.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "effects.h"

#define MIN_RANDOMNESS 0
#define MAX_RANDOMNESS 10
#define MIN_MIX_PERCENT 25
#define MAX_MIX_PERCENT 100
#define DEFAULT_RANDOM_SAMPLE_RATE 44100
#define DEFAULT_RANDOM_CHANNELS 1
#define PRESET_NAME_SEPARATOR '|'
#define PRESET_STORE_LINE_LENGTH 4096

typedef struct
{
    unsigned int state;
} RecipePrng;

static char *duplicate_string(const char *text)
{
    size_t length;
    char *copy;

    if (text == NULL)
    {
        return NULL;
    }

    length = strlen(text);
    copy = malloc(length + 1);

    if (copy == NULL)
    {
        return NULL;
    }

    memcpy(copy, text, length + 1);
    return copy;
}

static char *trim_whitespace(char *text)
{
    char *end;

    if (text == NULL)
    {
        return NULL;
    }

    while (*text != '\0' && isspace((unsigned char)*text))
    {
        text++;
    }

    if (*text == '\0')
    {
        return text;
    }

    end = text + strlen(text) - 1;

    while (end > text && isspace((unsigned char)*end))
    {
        *end = '\0';
        end--;
    }

    return text;
}

static int append_segment(Preset *preset, Segment segment)
{
    Segment *resized_segments;

    if (preset == NULL)
    {
        return -1;
    }

    resized_segments = realloc(
        preset->segments,
        sizeof(Segment) * (size_t)(preset->nb_seg + 1)
    );

    if (resized_segments == NULL)
    {
        return -1;
    }

    preset->segments = resized_segments;
    preset->segments[preset->nb_seg] = segment;
    preset->nb_seg++;
    return 0;
}

static int parse_segment(const char *text, Segment *segment)
{
    char trailing;

    if (text == NULL || segment == NULL)
    {
        return -1;
    }

    if (sscanf(text, " %d , %d , %d , %d %c",
        &segment->name,
        &segment->prmtr,
        &segment->mix,
        &segment->length,
        &trailing) != 4)
    {
        return -1;
    }

    if (!effect_is_supported(segment->name))
    {
        return -1;
    }

    if (segment->mix < 0 || segment->mix > 100 || segment->length <= 0)
    {
        return -1;
    }

    return 0;
}

static int parse_integer_token(const char *text, int minimum, int maximum, int *value)
{
    char *end;
    long parsed;

    if (text == NULL || value == NULL)
    {
        return -1;
    }

    parsed = strtol(text, &end, 10);

    if (text == end ||
        *trim_whitespace(end) != '\0' ||
        parsed < minimum ||
        parsed > maximum)
    {
        return -1;
    }

    *value = (int)parsed;
    return 0;
}

static int parse_unsigned_token(const char *text, unsigned int *value)
{
    char *end;
    unsigned long parsed;

    if (text == NULL || value == NULL)
    {
        return -1;
    }

    parsed = strtoul(text, &end, 10);

    if (text == end ||
        *trim_whitespace(end) != '\0' ||
        parsed > UINT_MAX)
    {
        return -1;
    }

    *value = (unsigned int)parsed;
    return 0;
}

static unsigned int mix_seed(unsigned int seed, unsigned int value)
{
    seed ^= value + 0x9e3779b9u + (seed << 6) + (seed >> 2);
    return seed;
}

static unsigned int build_materialization_seed(const PresetRecipe *recipe, const SF_INFO *info)
{
    unsigned int seed;

    if (recipe == NULL || info == NULL)
    {
        return 0;
    }

    seed = recipe->seed;
    seed = mix_seed(seed, (unsigned int)info->frames);
    seed = mix_seed(seed, (unsigned int)(((uint64_t)info->frames) >> 32));
    seed = mix_seed(seed, (unsigned int)info->samplerate);
    seed = mix_seed(seed, (unsigned int)info->channels);
    return seed;
}

static void seed_prng(RecipePrng *prng, unsigned int seed)
{
    if (prng == NULL)
    {
        return;
    }

    prng->state = seed;
}

static unsigned int next_prng_value(RecipePrng *prng)
{
    if (prng == NULL)
    {
        return 0;
    }

    prng->state = prng->state * 1664525u + 1013904223u;
    return prng->state;
}

static unsigned long long next_prng_u64(RecipePrng *prng)
{
    return ((unsigned long long)next_prng_value(prng) << 32) | next_prng_value(prng);
}

static int prng_between_int(RecipePrng *prng, int minimum, int maximum)
{
    unsigned int span;

    if (maximum <= minimum)
    {
        return minimum;
    }

    span = (unsigned int)(maximum - minimum + 1);
    return minimum + (int)(next_prng_value(prng) % span);
}

static sf_count_t prng_between_frames(RecipePrng *prng, sf_count_t minimum, sf_count_t maximum)
{
    unsigned long long span;

    if (maximum <= minimum)
    {
        return minimum;
    }

    span = (unsigned long long)(maximum - minimum);
    return minimum + (sf_count_t)(next_prng_u64(prng) % (span + 1ULL));
}

static int round_positive_to_int(double value)
{
    if (value <= 0.0)
    {
        return 0;
    }

    if (value >= (double)INT_MAX)
    {
        return INT_MAX;
    }

    return (int)(value + 0.5);
}

static int clamp_int(int value, int minimum, int maximum)
{
    if (value < minimum)
    {
        return minimum;
    }

    if (value > maximum)
    {
        return maximum;
    }

    return value;
}

static int clamp_frame_value_to_int(sf_count_t value)
{
    if (value <= 0)
    {
        return 0;
    }

    if (value >= (sf_count_t)INT_MAX)
    {
        return INT_MAX;
    }

    return (int)value;
}

static sf_count_t audio_sample_count_for_segment(const SF_INFO *info, sf_count_t segment_frames)
{
    int channels;

    if (segment_frames <= 0)
    {
        return 0;
    }

    channels = info != NULL && info->channels > 0 ? info->channels : DEFAULT_RANDOM_CHANNELS;
    return segment_frames * channels;
}

static double audio_duration_seconds(const SF_INFO *info)
{
    if (info == NULL || info->frames <= 0 || info->samplerate <= 0)
    {
        return 0.0;
    }

    return (double)info->frames / (double)info->samplerate;
}

static void compute_segment_count_window(
    double duration_seconds,
    int randomness,
    int *minimum_segments,
    int *maximum_segments)
{
    double density;
    int base_segments;
    int spread;

    if (minimum_segments == NULL || maximum_segments == NULL)
    {
        return;
    }

    density = 0.35 + (0.13 * (double)randomness);
    base_segments = round_positive_to_int(duration_seconds * density);

    if (base_segments < 1)
    {
        base_segments = 1;
    }

    spread = randomness > 0
        ? round_positive_to_int(duration_seconds * ((double)randomness / 30.0))
        : 0;

    if (base_segments <= 2)
    {
        spread = 0;
    }
    else if (base_segments <= 4 && spread > 1)
    {
        spread = 1;
    }

    if (spread > base_segments - 1)
    {
        spread = base_segments - 1;
    }

    *minimum_segments = base_segments - spread;
    *maximum_segments = base_segments + spread;

    if (*minimum_segments < 1)
    {
        *minimum_segments = 1;
    }

    if (*maximum_segments < *minimum_segments)
    {
        *maximum_segments = *minimum_segments;
    }
}

static int pick_segment_count(const PresetRecipe *recipe, const SF_INFO *info, RecipePrng *prng)
{
    double duration_seconds;
    int minimum_segments;
    int maximum_segments;
    sf_count_t frame_limit;

    if (recipe == NULL || info == NULL || prng == NULL || info->frames <= 0)
    {
        return -1;
    }

    duration_seconds = audio_duration_seconds(info);
    compute_segment_count_window(
        duration_seconds,
        recipe->randomness,
        &minimum_segments,
        &maximum_segments);

    frame_limit = info->frames < (sf_count_t)INT_MAX ? info->frames : (sf_count_t)INT_MAX;

    if (frame_limit < 1)
    {
        return -1;
    }

    if (minimum_segments > (int)frame_limit)
    {
        minimum_segments = (int)frame_limit;
    }

    if (maximum_segments > (int)frame_limit)
    {
        maximum_segments = (int)frame_limit;
    }

    if (maximum_segments < minimum_segments)
    {
        maximum_segments = minimum_segments;
    }

    return prng_between_int(prng, minimum_segments, maximum_segments);
}

static int materialize_segment_lengths(Preset *preset, const SF_INFO *info, RecipePrng *prng)
{
    sf_count_t remaining_frames;
    int remaining_segments;

    if (preset == NULL ||
        info == NULL ||
        prng == NULL ||
        preset->nb_seg <= 0 ||
        preset->segments == NULL ||
        info->frames <= 0)
    {
        return -1;
    }

    if (info->frames > (sf_count_t)INT_MAX * (sf_count_t)preset->nb_seg)
    {
        return -1;
    }

    remaining_frames = info->frames;
    remaining_segments = preset->nb_seg;

    for (int index = 0; index < preset->nb_seg; index++)
    {
        sf_count_t minimum_length;
        sf_count_t maximum_length;
        sf_count_t base_length;
        sf_count_t jitter;
        sf_count_t preferred_minimum;
        sf_count_t preferred_maximum;
        sf_count_t chosen_length;

        minimum_length = remaining_frames - ((sf_count_t)INT_MAX * (remaining_segments - 1));

        if (minimum_length < 1)
        {
            minimum_length = 1;
        }

        maximum_length = remaining_frames - (remaining_segments - 1);

        if (maximum_length > INT_MAX)
        {
            maximum_length = INT_MAX;
        }

        if (maximum_length < minimum_length)
        {
            return -1;
        }

        if (remaining_segments == 1)
        {
            chosen_length = remaining_frames;
        }
        else
        {
            base_length = remaining_frames / remaining_segments;
            jitter = base_length / 3;

            if (jitter < 1)
            {
                jitter = 1;
            }

            preferred_minimum = base_length - jitter;
            preferred_maximum = base_length + jitter;

            if (preferred_minimum < minimum_length)
            {
                preferred_minimum = minimum_length;
            }

            if (preferred_maximum > maximum_length)
            {
                preferred_maximum = maximum_length;
            }

            if (preferred_maximum < preferred_minimum)
            {
                preferred_maximum = preferred_minimum;
            }

            chosen_length = prng_between_frames(prng, preferred_minimum, preferred_maximum);
        }

        preset->segments[index].length = (int)chosen_length;
        remaining_frames -= chosen_length;
        remaining_segments--;
    }

    return remaining_frames == 0 ? 0 : -1;
}

static int random_effect_parameter(
    RecipePrng *prng,
    int effect_id,
    const SF_INFO *info,
    sf_count_t segment_frames)
{
    int sample_rate;
    int channels;
    sf_count_t segment_samples;
    sf_count_t minimum_value;
    sf_count_t maximum_value;

    if (prng == NULL)
    {
        return 100;
    }

    sample_rate = info != NULL && info->samplerate > 0
        ? info->samplerate
        : DEFAULT_RANDOM_SAMPLE_RATE;
    channels = info != NULL && info->channels > 0
        ? info->channels
        : DEFAULT_RANDOM_CHANNELS;
    segment_samples = audio_sample_count_for_segment(info, segment_frames);

    switch (effect_id)
    {
        case EFFECT_PITCH_SHIFT:
            return prng_between_int(prng, 70, 140);

        case EFFECT_SHUFFLE_CHUNKS:
            minimum_value = ((sf_count_t)sample_rate / 100) * channels;
            maximum_value = ((sf_count_t)sample_rate / 8) * channels;

            if (segment_samples <= 1)
            {
                return 1;
            }

            if (maximum_value > segment_samples / 2)
            {
                maximum_value = segment_samples / 2;
            }

            if (minimum_value < 1)
            {
                minimum_value = 1;
            }

            if (minimum_value > maximum_value)
            {
                minimum_value = maximum_value;
            }

            return clamp_frame_value_to_int(
                prng_between_frames(prng, minimum_value, maximum_value));

        case EFFECT_GAIN:
            return prng_between_int(prng, 60, 180);

        case EFFECT_REVERSE:
            return 0;

        case EFFECT_ECHO:
            minimum_value = ((sf_count_t)sample_rate / 40) * channels;
            maximum_value = ((sf_count_t)sample_rate / 3) * channels;

            if (segment_samples <= 1)
            {
                return 1;
            }

            if (maximum_value > segment_samples - 1)
            {
                maximum_value = segment_samples - 1;
            }

            if (minimum_value < 1)
            {
                minimum_value = 1;
            }

            if (minimum_value > maximum_value)
            {
                minimum_value = maximum_value;
            }

            return clamp_frame_value_to_int(
                prng_between_frames(prng, minimum_value, maximum_value));

        case EFFECT_DISTORTION:
            return prng_between_int(prng, 120, 320);

        default:
            return 100;
    }
}

static int parse_named_preset_line(char *line, char **preset_name, PresetRecipe *recipe)
{
    char *name_separator;
    char *seed_separator;
    char *randomness_text;
    char *seed_text;

    if (line == NULL || preset_name == NULL || recipe == NULL)
    {
        return -1;
    }

    *preset_name = NULL;
    init_preset_recipe(recipe);
    line[strcspn(line, "\r\n")] = '\0';

    name_separator = strchr(line, PRESET_NAME_SEPARATOR);

    if (name_separator == NULL)
    {
        return -1;
    }

    *name_separator = '\0';
    seed_separator = strchr(name_separator + 1, PRESET_NAME_SEPARATOR);

    if (seed_separator == NULL)
    {
        return -1;
    }

    *seed_separator = '\0';
    *preset_name = trim_whitespace(line);
    randomness_text = trim_whitespace(name_separator + 1);
    seed_text = trim_whitespace(seed_separator + 1);

    if (**preset_name == '\0')
    {
        return -1;
    }

    if (parse_integer_token(randomness_text, MIN_RANDOMNESS, MAX_RANDOMNESS, &recipe->randomness) != 0)
    {
        return -1;
    }

    if (parse_unsigned_token(seed_text, &recipe->seed) != 0)
    {
        return -1;
    }

    return 0;
}

static unsigned int mint_recipe_seed(int randomness)
{
    unsigned int seed;

    seed = (unsigned int)time(NULL);
    seed ^= (unsigned int)clock();
    seed ^= (unsigned int)(uintptr_t)&seed;
    seed ^= (unsigned int)(randomness + 1) * 0x9e3779b9u;
    return seed;
}

void init_preset(Preset *preset)
{
    if (preset == NULL)
    {
        return;
    }

    preset->nb_seg = 0;
    preset->segments = NULL;
}

void free_preset(Preset *preset)
{
    if (preset == NULL)
    {
        return;
    }

    free(preset->segments);
    preset->segments = NULL;
    preset->nb_seg = 0;
}

int copy_preset(Preset *destination, const Preset *source)
{
    Segment *copy;

    if (destination == NULL || source == NULL)
    {
        return -1;
    }

    if (source->nb_seg <= 0 || source->segments == NULL)
    {
        free_preset(destination);
        return 0;
    }

    copy = malloc(sizeof(Segment) * (size_t)source->nb_seg);

    if (copy == NULL)
    {
        return -1;
    }

    memcpy(copy, source->segments, sizeof(Segment) * (size_t)source->nb_seg);
    free_preset(destination);
    destination->segments = copy;
    destination->nb_seg = source->nb_seg;
    return 0;
}

void init_preset_recipe(PresetRecipe *recipe)
{
    if (recipe == NULL)
    {
        return;
    }

    recipe->randomness = MIN_RANDOMNESS;
    recipe->seed = 0;
}

int copy_preset_recipe(PresetRecipe *destination, const PresetRecipe *source)
{
    if (destination == NULL || source == NULL)
    {
        return -1;
    }

    *destination = *source;
    return 0;
}

int preset_recipe_is_valid(const PresetRecipe *recipe)
{
    return recipe != NULL &&
        recipe->randomness >= MIN_RANDOMNESS &&
        recipe->randomness <= MAX_RANDOMNESS;
}

int parse_preset_definition(const char *definition, Preset *preset)
{
    Preset parsed;
    char *copy;
    char *cursor;

    if (definition == NULL || preset == NULL)
    {
        return -1;
    }

    init_preset(&parsed);
    copy = duplicate_string(definition);

    if (copy == NULL)
    {
        return -1;
    }

    cursor = strtok(copy, ";");

    while (cursor != NULL)
    {
        Segment segment;
        char *segment_text = trim_whitespace(cursor);

        if (*segment_text == '\0' || parse_segment(segment_text, &segment) != 0)
        {
            free(copy);
            free_preset(&parsed);
            return -1;
        }

        if (append_segment(&parsed, segment) != 0)
        {
            free(copy);
            free_preset(&parsed);
            return -1;
        }

        cursor = strtok(NULL, ";");
    }

    free(copy);

    if (parsed.nb_seg <= 0)
    {
        free_preset(&parsed);
        return -1;
    }

    free_preset(preset);
    *preset = parsed;
    return 0;
}

int save_named_preset(const char *file_path, const char *name, const PresetRecipe *recipe)
{
    FILE *file;

    if (file_path == NULL || name == NULL || !preset_recipe_is_valid(recipe))
    {
        return -1;
    }

    if (strchr(name, PRESET_NAME_SEPARATOR) != NULL || strchr(name, '\n') != NULL || strchr(name, '\r') != NULL)
    {
        return -1;
    }

    file = fopen(file_path, "a");

    if (file == NULL)
    {
        return -1;
    }

    fprintf(file, "%s|%d|%u\n", name, recipe->randomness, recipe->seed);
    fclose(file);
    return 0;
}

int load_named_preset(const char *file_path, const char *name, PresetRecipe *recipe)
{
    FILE *file;
    char line[PRESET_STORE_LINE_LENGTH];
    PresetRecipe latest_match;
    int found = 0;

    if (file_path == NULL || name == NULL || recipe == NULL)
    {
        return -1;
    }

    file = fopen(file_path, "r");

    if (file == NULL)
    {
        return -1;
    }

    init_preset_recipe(&latest_match);

    while (fgets(line, sizeof(line), file) != NULL)
    {
        char *preset_name;
        PresetRecipe parsed;

        if (parse_named_preset_line(line, &preset_name, &parsed) != 0)
        {
            continue;
        }

        if (strcmp(preset_name, name) != 0)
        {
            continue;
        }

        latest_match = parsed;
        found = 1;
    }

    fclose(file);

    if (!found)
    {
        return -1;
    }

    *recipe = latest_match;
    return 0;
}

static int append_preset_name(char ***names, int *count, int *capacity, const char *name)
{
    char **resized_names;
    char *name_copy;

    if (names == NULL || count == NULL || capacity == NULL || name == NULL)
    {
        return -1;
    }

    for (int index = 0; index < *count; index++)
    {
        if (strcmp((*names)[index], name) == 0)
        {
            return 0;
        }
    }

    if (*count >= *capacity)
    {
        int new_capacity = *capacity > 0 ? *capacity * 2 : 8;

        resized_names = realloc(*names, sizeof(char *) * (size_t)new_capacity);

        if (resized_names == NULL)
        {
            return -1;
        }

        *names = resized_names;
        *capacity = new_capacity;
    }

    name_copy = duplicate_string(name);

    if (name_copy == NULL)
    {
        return -1;
    }

    (*names)[*count] = name_copy;
    (*count)++;
    return 0;
}

int list_named_presets(const char *file_path, char ***names, int *count)
{
    FILE *file;
    char line[PRESET_STORE_LINE_LENGTH];
    char **found_names = NULL;
    int found_count = 0;
    int found_capacity = 0;

    if (file_path == NULL || names == NULL || count == NULL)
    {
        return -1;
    }

    *names = NULL;
    *count = 0;
    file = fopen(file_path, "r");

    if (file == NULL)
    {
        return 0;
    }

    while (fgets(line, sizeof(line), file) != NULL)
    {
        char *preset_name;
        PresetRecipe parsed;

        if (parse_named_preset_line(line, &preset_name, &parsed) != 0)
        {
            continue;
        }

        if (append_preset_name(&found_names, &found_count, &found_capacity, preset_name) != 0)
        {
            fclose(file);
            free_named_preset_list(found_names, found_count);
            return -1;
        }
    }

    fclose(file);
    *names = found_names;
    *count = found_count;
    return 0;
}

void free_named_preset_list(char **names, int count)
{
    if (names == NULL)
    {
        return;
    }

    for (int index = 0; index < count; index++)
    {
        free(names[index]);
    }

    free(names);
}

int generate_random_recipe(PresetRecipe *recipe, int randomness)
{
    if (recipe == NULL || randomness < MIN_RANDOMNESS || randomness > MAX_RANDOMNESS)
    {
        return -1;
    }

    recipe->randomness = randomness;
    recipe->seed = mint_recipe_seed(randomness);
    return 0;
}

int materialize_preset_from_recipe(Preset *preset, const PresetRecipe *recipe, const SF_INFO *info)
{
    Preset generated;
    const EffectDescriptor *catalog;
    size_t catalog_size;
    RecipePrng prng;
    int segment_count;

    if (preset == NULL ||
        !preset_recipe_is_valid(recipe) ||
        info == NULL ||
        info->frames <= 0 ||
        info->channels <= 0)
    {
        return -1;
    }

    catalog = get_effect_catalog(&catalog_size);

    if (catalog == NULL || catalog_size == 0)
    {
        return -1;
    }

    seed_prng(&prng, build_materialization_seed(recipe, info));
    segment_count = pick_segment_count(recipe, info, &prng);

    if (segment_count <= 0)
    {
        return -1;
    }

    init_preset(&generated);
    generated.segments = calloc((size_t)segment_count, sizeof(Segment));

    if (generated.segments == NULL)
    {
        return -1;
    }

    generated.nb_seg = segment_count;

    if (materialize_segment_lengths(&generated, info, &prng) != 0)
    {
        free_preset(&generated);
        return -1;
    }

    for (int index = 0; index < generated.nb_seg; index++)
    {
        size_t catalog_index = (size_t)(next_prng_value(&prng) % (unsigned int)catalog_size);
        Segment *segment = &generated.segments[index];

        segment->name = catalog[catalog_index].id;
        segment->prmtr = random_effect_parameter(&prng, segment->name, info, segment->length);
        segment->mix = prng_between_int(&prng, MIN_MIX_PERCENT, MAX_MIX_PERCENT);

        if (segment->name == EFFECT_REVERSE)
        {
            segment->mix = clamp_int(segment->mix, 0, 100);
        }
    }

    free_preset(preset);
    *preset = generated;
    return 0;
}
