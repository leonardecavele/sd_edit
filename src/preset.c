#include "preset.h"

#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "effects.h"

#define MIN_RANDOM_SEGMENTS 1
#define MAX_RANDOM_SEGMENTS 4
#define MIN_MIX_PERCENT 25
#define MAX_MIX_PERCENT 100
#define PRESET_NAME_SEPARATOR '|'
#define PRESET_STORE_LINE_LENGTH 4096

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

static int random_between(int minimum, int maximum)
{
    if (maximum <= minimum)
    {
        return minimum;
    }

    return minimum + (rand() % (maximum - minimum + 1));
}

static int random_effect_parameter(int effect_id, int sample_rate, int channels)
{
    int effective_channels = channels > 0 ? channels : 1;
    int effective_rate = sample_rate > 0 ? sample_rate : 44100;

    switch (effect_id)
    {
        case EFFECT_PITCH_SHIFT:
            return random_between(70, 140);

        case EFFECT_SHUFFLE_CHUNKS:
            return random_between(effective_rate / 100, effective_rate / 8) * effective_channels;

        case EFFECT_GAIN:
            return random_between(60, 180);

        case EFFECT_REVERSE:
            return 0;

        case EFFECT_ECHO:
            return random_between(effective_rate / 40, effective_rate / 3) * effective_channels;

        case EFFECT_DISTORTION:
            return random_between(120, 320);

        default:
            return 100;
    }
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

static int parse_named_preset_line(char *line, char **preset_name, Preset *preset)
{
    char *name_separator;
    char *count_separator;
    char *count_text;
    char *definition_text;
    char *end;
    long expected_count;

    if (line == NULL || preset_name == NULL || preset == NULL)
    {
        return -1;
    }

    *preset_name = NULL;
    init_preset(preset);
    line[strcspn(line, "\r\n")] = '\0';

    name_separator = strchr(line, PRESET_NAME_SEPARATOR);

    if (name_separator == NULL)
    {
        return -1;
    }

    *name_separator = '\0';
    count_separator = strchr(name_separator + 1, PRESET_NAME_SEPARATOR);

    if (count_separator == NULL)
    {
        return -1;
    }

    *count_separator = '\0';
    *preset_name = trim_whitespace(line);
    count_text = trim_whitespace(name_separator + 1);
    definition_text = trim_whitespace(count_separator + 1);

    expected_count = strtol(count_text, &end, 10);

    if (**preset_name == '\0' ||
        *count_text == '\0' ||
        *trim_whitespace(end) != '\0' ||
        expected_count <= 0 ||
        expected_count > INT_MAX)
    {
        return -1;
    }

    if (parse_preset_definition(definition_text, preset) != 0)
    {
        return -1;
    }

    if (preset->nb_seg != (int)expected_count)
    {
        free_preset(preset);
        return -1;
    }

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

int save_named_preset(const char *file_path, const char *name, const Preset *preset)
{
    FILE *file;

    if (file_path == NULL || name == NULL || preset == NULL || preset->nb_seg <= 0 || preset->segments == NULL)
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

    fprintf(file, "%s|%d|", name, preset->nb_seg);

    for (int index = 0; index < preset->nb_seg; index++)
    {
        Segment segment = preset->segments[index];

        fprintf(
            file,
            "%d,%d,%d,%d",
            segment.name,
            segment.prmtr,
            segment.mix,
            segment.length
        );

        if (index + 1 < preset->nb_seg)
        {
            fputc(';', file);
        }
    }

    fputc('\n', file);
    fclose(file);
    return 0;
}

int load_named_preset(const char *file_path, const char *name, Preset *preset)
{
    FILE *file;
    char line[PRESET_STORE_LINE_LENGTH];
    Preset latest_match;
    int found = 0;

    if (file_path == NULL || name == NULL || preset == NULL)
    {
        return -1;
    }

    file = fopen(file_path, "r");

    if (file == NULL)
    {
        return -1;
    }

    init_preset(&latest_match);

    while (fgets(line, sizeof(line), file) != NULL)
    {
        char *preset_name;
        Preset parsed;
        if (parse_named_preset_line(line, &preset_name, &parsed) != 0)
        {
            continue;
        }

        if (strcmp(preset_name, name) != 0)
        {
            free_preset(&parsed);
            continue;
        }

        free_preset(&latest_match);
        latest_match = parsed;
        found = 1;
    }

    fclose(file);

    if (!found)
    {
        free_preset(&latest_match);
        return -1;
    }

    free_preset(preset);
    *preset = latest_match;
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
        Preset parsed;

        if (parse_named_preset_line(line, &preset_name, &parsed) != 0)
        {
            continue;
        }

        free_preset(&parsed);

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

int generate_random_preset(Preset *preset, sf_count_t total_frames, int sample_rate, int channels)
{
    Preset generated;
    const EffectDescriptor *catalog;
    size_t catalog_size;
    int segment_count;
    int remaining_frames;

    if (preset == NULL || total_frames <= 0 || total_frames > INT_MAX)
    {
        return -1;
    }

    catalog = get_effect_catalog(&catalog_size);

    if (catalog == NULL || catalog_size == 0)
    {
        return -1;
    }

    init_preset(&generated);
    segment_count = random_between(MIN_RANDOM_SEGMENTS, MAX_RANDOM_SEGMENTS);

    if (total_frames < segment_count)
    {
        segment_count = (int)total_frames;
    }

    remaining_frames = (int)total_frames;

    for (int index = 0; index < segment_count; index++)
    {
        Segment segment;
        int remaining_segments = segment_count - index - 1;
        int maximum_length = remaining_frames - remaining_segments;

        segment.name = catalog[rand() % catalog_size].id;
        segment.prmtr = random_effect_parameter(segment.name, sample_rate, channels);
        segment.mix = random_between(MIN_MIX_PERCENT, MAX_MIX_PERCENT);

        if (index + 1 == segment_count)
        {
            segment.length = remaining_frames;
        }
        else
        {
            int minimum_length = 1;

            if (sample_rate > 0 && maximum_length > sample_rate / 10)
            {
                minimum_length = sample_rate / 10;
            }

            if (minimum_length > maximum_length)
            {
                minimum_length = maximum_length;
            }

            segment.length = random_between(minimum_length, maximum_length);
        }

        if (append_segment(&generated, segment) != 0)
        {
            free_preset(&generated);
            return -1;
        }

        remaining_frames -= segment.length;
    }

    free_preset(preset);
    *preset = generated;
    return 0;
}
