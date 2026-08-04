#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <process.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <sndfile.h>

#include "effects.h"
#include "preset.h"

#define PRESET_STORE_PATH "presets.txt"
#define MAX_COMMAND_LENGTH 4096
#define MAX_OUTPUT_PATH 1024
#define MAX_PRESET_NAME 128
#define PLAY_COMMAND_PATH ".\\libsndfile-1.2.2-win64\\bin\\sndfile-play.exe"

typedef struct
{
    Preset current_preset;
    int has_current_preset;
} Session;

static void init_session(Session *session)
{
    if (session == NULL)
    {
        return;
    }

    init_preset(&session->current_preset);
    session->has_current_preset = 0;
}

static void free_session(Session *session)
{
    if (session == NULL)
    {
        return;
    }

    free_preset(&session->current_preset);
    session->has_current_preset = 0;
}

static int is_space_or_empty(const char *text)
{
    if (text == NULL)
    {
        return 1;
    }

    while (*text != '\0')
    {
        if (!isspace((unsigned char)*text))
        {
            return 0;
        }

        text++;
    }

    return 1;
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

static int equals_ignore_case(const char *left, const char *right)
{
    if (left == NULL || right == NULL)
    {
        return 0;
    }

    while (*left != '\0' && *right != '\0')
    {
        if (tolower((unsigned char)*left) != tolower((unsigned char)*right))
        {
            return 0;
        }

        left++;
        right++;
    }

    return *left == '\0' && *right == '\0';
}

static int has_wav_extension(const char *path)
{
    const char *extension;

    if (path == NULL)
    {
        return 0;
    }

    extension = strrchr(path, '.');

    if (extension == NULL)
    {
        return 0;
    }

    return equals_ignore_case(extension, ".wav");
}

static int split_command_line(char *line, char **tokens, int max_tokens)
{
    int count = 0;
    char *cursor = line;

    if (line == NULL || tokens == NULL || max_tokens <= 0)
    {
        return 0;
    }

    while (*cursor != '\0' && count < max_tokens)
    {
        char quote = '\0';

        while (*cursor != '\0' && isspace((unsigned char)*cursor))
        {
            cursor++;
        }

        if (*cursor == '\0')
        {
            break;
        }

        if (*cursor == '"' || *cursor == '\'')
        {
            quote = *cursor;
            cursor++;
        }

        tokens[count++] = cursor;

        if (quote != '\0')
        {
            while (*cursor != '\0' && *cursor != quote)
            {
                cursor++;
            }
        }
        else
        {
            while (*cursor != '\0' && !isspace((unsigned char)*cursor))
            {
                cursor++;
            }
        }

        if (*cursor == '\0')
        {
            break;
        }

        *cursor = '\0';
        cursor++;
    }

    return count;
}

static void print_preset(const Preset *preset)
{
    if (preset == NULL || preset->nb_seg <= 0 || preset->segments == NULL)
    {
        printf("No current preset.\n");
        return;
    }

    printf("Preset with %d segment(s):\n", preset->nb_seg);

    for (int index = 0; index < preset->nb_seg; index++)
    {
        Segment segment = preset->segments[index];

        printf(
            "  %d. effect=%d (%s), prmtr=%d, mix=%d, length=%d\n",
            index + 1,
            segment.name,
            effect_name(segment.name),
            segment.prmtr,
            segment.mix,
            segment.length
        );
    }
}

static void print_usage(void)
{
    size_t effect_count;
    const EffectDescriptor *catalog = get_effect_catalog(&effect_count);

    printf("Usage:\n");
    printf("  main.exe do <file.wav>\n");
    printf("  main.exe do <file.wav> <preset_name>\n");
    printf("  main.exe play <file.wav>\n");
    printf("  main.exe preset <effect,parameter,mix,length;...>\n");
    printf("  main.exe save preset <name>\n");
    printf("\n");
    printf("Interactive mode starts when no arguments are provided.\n");
    printf("Preset definition example:\n");
    printf("  preset 1,120,50,44100;2,2048,100,22050\n");
    printf("\n");
    printf("Available effects:\n");

    for (size_t index = 0; index < effect_count; index++)
    {
        printf("  %d = %s\n", catalog[index].id, catalog[index].name);
    }
}

static int sanitize_suffix(const char *text, char *output, size_t output_size)
{
    size_t output_index = 0;

    if (output == NULL || output_size == 0)
    {
        return -1;
    }

    if (text == NULL || *text == '\0')
    {
        text = "processed";
    }

    while (*text != '\0' && output_index + 1 < output_size)
    {
        unsigned char character = (unsigned char)*text;

        if (isalnum(character))
        {
            output[output_index++] = (char)tolower(character);
        }
        else if (character == '-' || character == '_')
        {
            output[output_index++] = (char)character;
        }
        else
        {
            output[output_index++] = '_';
        }

        text++;
    }

    if (output_index == 0)
    {
        if (output_size < 10)
        {
            return -1;
        }

        strcpy(output, "processed");
        return 0;
    }

    output[output_index] = '\0';
    return 0;
}

static int build_output_path(const char *input_path, const char *label, char *output_path, size_t output_size)
{
    const char *extension;
    char suffix[MAX_PRESET_NAME];

    if (input_path == NULL || output_path == NULL || output_size == 0 || !has_wav_extension(input_path))
    {
        return -1;
    }

    extension = strrchr(input_path, '.');

    if (extension == NULL)
    {
        return -1;
    }

    if (sanitize_suffix(label, suffix, sizeof(suffix)) != 0)
    {
        return -1;
    }

    if (snprintf(
        output_path,
        output_size,
        "%.*s_%s.wav",
        (int)(extension - input_path),
        input_path,
        suffix
    ) >= (int)output_size)
    {
        return -1;
    }

    return 0;
}

static int update_current_preset(Session *session, const Preset *preset)
{
    if (session == NULL || preset == NULL)
    {
        return -1;
    }

    if (copy_preset(&session->current_preset, preset) != 0)
    {
        return -1;
    }

    session->has_current_preset = 1;
    return 0;
}

static int load_audio_file(const char *input_path, short **samples, SF_INFO *info)
{
    SNDFILE *input_file;
    sf_count_t sample_count;
    short *buffer;
    sf_count_t read_count;

    if (input_path == NULL || samples == NULL || info == NULL)
    {
        return -1;
    }

    memset(info, 0, sizeof(*info));
    input_file = sf_open(input_path, SFM_READ, info);

    if (input_file == NULL)
    {
        fprintf(stderr, "Unable to open input file: %s\n", input_path);
        return -1;
    }

    if (info->frames <= 0 || info->channels <= 0)
    {
        fprintf(stderr, "Unsupported audio metadata in file: %s\n", input_path);
        sf_close(input_file);
        return -1;
    }

    if (info->frames > (SF_COUNT_MAX / info->channels))
    {
        fprintf(stderr, "Audio file is too large to process safely: %s\n", input_path);
        sf_close(input_file);
        return -1;
    }

    sample_count = info->frames * info->channels;

    if ((size_t)sample_count > (SIZE_MAX / sizeof(short)))
    {
        fprintf(stderr, "Audio buffer would exceed available memory: %s\n", input_path);
        sf_close(input_file);
        return -1;
    }

    buffer = malloc(sizeof(short) * (size_t)sample_count);

    if (buffer == NULL)
    {
        fprintf(stderr, "Unable to allocate memory for audio buffer.\n");
        sf_close(input_file);
        return -1;
    }

    read_count = sf_read_short(input_file, buffer, sample_count);
    sf_close(input_file);

    if (read_count != sample_count)
    {
        fprintf(stderr, "Unable to read the full audio buffer from: %s\n", input_path);
        free(buffer);
        return -1;
    }

    *samples = buffer;
    return 0;
}

static int save_audio_file(const char *output_path, const SF_INFO *info, const short *samples)
{
    SNDFILE *output_file;
    sf_count_t sample_count;
    sf_count_t written_count;

    if (output_path == NULL || info == NULL || samples == NULL)
    {
        return -1;
    }

    output_file = sf_open(output_path, SFM_WRITE, (SF_INFO *)info);

    if (output_file == NULL)
    {
        fprintf(stderr, "Unable to create output file: %s\n", output_path);
        return -1;
    }

    sample_count = info->frames * info->channels;
    written_count = sf_write_short(output_file, samples, sample_count);
    sf_close(output_file);

    if (written_count != sample_count)
    {
        fprintf(stderr, "Unable to write the full processed audio buffer to: %s\n", output_path);
        return -1;
    }

    return 0;
}

static int apply_preset_to_samples(short *samples, const SF_INFO *info, const Preset *preset)
{
    sf_count_t sample_offset = 0;
    sf_count_t total_samples;

    if (samples == NULL || info == NULL || preset == NULL || preset->nb_seg <= 0 || preset->segments == NULL)
    {
        return -1;
    }

    total_samples = info->frames * info->channels;

    for (int index = 0; index < preset->nb_seg && sample_offset < total_samples; index++)
    {
        Segment segment = preset->segments[index];
        sf_count_t segment_length = (sf_count_t)segment.length * info->channels;

        if (segment_length <= 0)
        {
            continue;
        }

        if (segment_length > total_samples - sample_offset)
        {
            segment_length = total_samples - sample_offset;
        }

        if (apply_effect(
            samples + sample_offset,
            segment_length,
            segment.name,
            segment.prmtr,
            segment.mix) != 0)
        {
            fprintf(stderr, "Unsupported effect id in preset: %d\n", segment.name);
            return -1;
        }

        sample_offset += segment_length;
    }

    return 0;
}

static int process_audio_file(const char *input_path, const Preset *preset, const char *output_label)
{
    SF_INFO info;
    short *samples = NULL;
    char output_path[MAX_OUTPUT_PATH];
    int result = -1;

    if (input_path == NULL || preset == NULL)
    {
        return -1;
    }

    if (!has_wav_extension(input_path))
    {
        fprintf(stderr, "Only .wav files are supported: %s\n", input_path);
        return -1;
    }

    if (build_output_path(input_path, output_label, output_path, sizeof(output_path)) != 0)
    {
        fprintf(stderr, "Unable to build an output path for: %s\n", input_path);
        return -1;
    }

    if (load_audio_file(input_path, &samples, &info) != 0)
    {
        return -1;
    }

    if (apply_preset_to_samples(samples, &info, preset) != 0)
    {
        goto cleanup;
    }

    if (save_audio_file(output_path, &info, samples) != 0)
    {
        goto cleanup;
    }

    printf("Saved processed audio to %s\n", output_path);
    result = 0;

cleanup:
    free(samples);
    return result;
}

static int handle_preset_command(Session *session, const char *definition)
{
    Preset parsed;

    if (session == NULL || definition == NULL || is_space_or_empty(definition))
    {
        fprintf(stderr, "Missing preset definition.\n");
        return -1;
    }

    init_preset(&parsed);

    if (parse_preset_definition(definition, &parsed) != 0)
    {
        fprintf(
            stderr,
            "Invalid preset definition. Expected format: effect,parameter,mix,length;effect,parameter,mix,length\n"
        );
        return -1;
    }

    if (update_current_preset(session, &parsed) != 0)
    {
        fprintf(stderr, "Unable to store the current preset in memory.\n");
        free_preset(&parsed);
        return -1;
    }

    free_preset(&parsed);
    printf("Current preset updated.\n");
    print_preset(&session->current_preset);
    return 0;
}

static int handle_save_preset_command(Session *session, const char *name)
{
    if (session == NULL || name == NULL || is_space_or_empty(name))
    {
        fprintf(stderr, "Missing preset name.\n");
        return -1;
    }

    if (!session->has_current_preset)
    {
        fprintf(stderr, "No current preset is loaded. Use 'preset ...' or 'do <file.wav>' first.\n");
        return -1;
    }

    if (save_named_preset(PRESET_STORE_PATH, name, &session->current_preset) != 0)
    {
        fprintf(stderr, "Unable to save preset '%s' to %s.\n", name, PRESET_STORE_PATH);
        return -1;
    }

    printf("Saved preset '%s' to %s\n", name, PRESET_STORE_PATH);
    return 0;
}

static int handle_do_command(Session *session, const char *input_path, const char *preset_name)
{
    Preset preset;
    short *samples = NULL;
    SF_INFO info;
    int result = -1;

    if (session == NULL || input_path == NULL)
    {
        return -1;
    }

    if (!has_wav_extension(input_path))
    {
        fprintf(stderr, "Only .wav files are supported: %s\n", input_path);
        return -1;
    }

    init_preset(&preset);

    if (preset_name == NULL)
    {
        if (load_audio_file(input_path, &samples, &info) != 0)
        {
            return -1;
        }

        if (generate_random_preset(&preset, info.frames, info.samplerate, info.channels) != 0)
        {
            fprintf(stderr, "Unable to generate a random preset.\n");
            goto cleanup;
        }

        free(samples);
        samples = NULL;
        printf("Generated random preset for %s\n", input_path);
    }
    else
    {
        if (load_named_preset(PRESET_STORE_PATH, preset_name, &preset) != 0)
        {
            fprintf(stderr, "Preset '%s' was not found in %s.\n", preset_name, PRESET_STORE_PATH);
            goto cleanup;
        }

        printf("Loaded preset '%s'.\n", preset_name);
    }

    print_preset(&preset);

    if (update_current_preset(session, &preset) != 0)
    {
        fprintf(stderr, "Unable to store the current preset in memory.\n");
        goto cleanup;
    }

    if (process_audio_file(input_path, &preset, preset_name != NULL ? preset_name : "random") != 0)
    {
        goto cleanup;
    }

    result = 0;

cleanup:
    free(samples);
    free_preset(&preset);
    return result;
}

static int handle_play_command(const char *input_path)
{
    char player_path[MAX_OUTPUT_PATH];
    char audio_path[MAX_OUTPUT_PATH];
    intptr_t result;

    if (input_path == NULL)
    {
        return -1;
    }

    if (!has_wav_extension(input_path))
    {
        fprintf(stderr, "Only .wav files are supported: %s\n", input_path);
        return -1;
    }

    if (_fullpath(player_path, PLAY_COMMAND_PATH, sizeof(player_path)) == NULL)
    {
        fprintf(stderr, "Unable to resolve player path: %s\n", PLAY_COMMAND_PATH);
        return -1;
    }

    if (_fullpath(audio_path, input_path, sizeof(audio_path)) == NULL)
    {
        fprintf(stderr, "Unable to resolve audio path: %s\n", input_path);
        return -1;
    }

    result = _spawnl(_P_WAIT, player_path, player_path, audio_path, NULL);

    if (result != 0)
    {
        fprintf(stderr, "Unable to play file with %s.\n", player_path);
        return -1;
    }

    return 0;
}

static int execute_tokens(Session *session, int argc, char **argv)
{
    if (argc <= 0 || argv == NULL)
    {
        return -1;
    }

    if (equals_ignore_case(argv[0], "help"))
    {
        print_usage();
        return 0;
    }

    if (equals_ignore_case(argv[0], "show") && argc == 2 && equals_ignore_case(argv[1], "preset"))
    {
        print_preset(session != NULL && session->has_current_preset ? &session->current_preset : NULL);
        return 0;
    }

    if (equals_ignore_case(argv[0], "do"))
    {
        if (argc == 2)
        {
            return handle_do_command(session, argv[1], NULL);
        }

        if (argc == 3)
        {
            return handle_do_command(session, argv[1], argv[2]);
        }

        fprintf(stderr, "Usage: do <file.wav> [preset_name]\n");
        return -1;
    }

    if (equals_ignore_case(argv[0], "play"))
    {
        if (argc != 2)
        {
            fprintf(stderr, "Usage: play <file.wav>\n");
            return -1;
        }

        return handle_play_command(argv[1]);
    }

    if (equals_ignore_case(argv[0], "preset"))
    {
        if (argc != 2)
        {
            fprintf(stderr, "Usage: preset <effect,parameter,mix,length;...>\n");
            return -1;
        }

        return handle_preset_command(session, argv[1]);
    }

    if (equals_ignore_case(argv[0], "save"))
    {
        if (argc == 3 && equals_ignore_case(argv[1], "preset"))
        {
            return handle_save_preset_command(session, argv[2]);
        }

        fprintf(stderr, "Usage: save preset <name>\n");
        return -1;
    }

    fprintf(stderr, "Unknown command.\n");
    print_usage();
    return -1;
}

static int run_interactive_console(Session *session)
{
    char line[MAX_COMMAND_LENGTH];

    printf("wav mini-console\n");
    printf("Type 'help' for usage and 'exit' to quit.\n");

    while (1)
    {
        char *tokens[4];
        int token_count;
        char *command;

        printf("wav> ");

        if (fgets(line, sizeof(line), stdin) == NULL)
        {
            putchar('\n');
            return 0;
        }

        line[strcspn(line, "\r\n")] = '\0';
        command = trim_whitespace(line);

        if (*command == '\0')
        {
            continue;
        }

        if (equals_ignore_case(command, "exit") || equals_ignore_case(command, "quit"))
        {
            return 0;
        }

        if (strncmp(command, "preset ", 7) == 0)
        {
            char *definition = trim_whitespace(command + 7);
            execute_tokens(session, 2, (char *[]) { "preset", definition });
            continue;
        }

        token_count = split_command_line(command, tokens, 4);

        if (token_count == 0)
        {
            continue;
        }

        execute_tokens(session, token_count, tokens);
    }
}

static int run_cli(Session *session, int argc, char **argv)
{
    char *command_argv[3];
    size_t total_length = 0;
    char *definition;
    int result;

    if (argc < 2)
    {
        return run_interactive_console(session);
    }

    if (equals_ignore_case(argv[1], "preset"))
    {
        if (argc < 3)
        {
            fprintf(stderr, "Usage: preset <effect,parameter,mix,length;...>\n");
            return -1;
        }

        for (int index = 2; index < argc; index++)
        {
            total_length += strlen(argv[index]) + 1;
        }

        definition = malloc(total_length + 1);

        if (definition == NULL)
        {
            fprintf(stderr, "Unable to allocate memory for preset definition.\n");
            return -1;
        }

        definition[0] = '\0';

        for (int index = 2; index < argc; index++)
        {
            strcat(definition, argv[index]);

            if (index + 1 < argc)
            {
                strcat(definition, " ");
            }
        }

        command_argv[0] = "preset";
        command_argv[1] = definition;
        result = execute_tokens(session, 2, command_argv);
        free(definition);
        return result;
    }

    return execute_tokens(session, argc - 1, argv + 1);
}

int main(int argc, char **argv)
{
    Session session;
    int exit_code;

    srand((unsigned int)time(NULL));
    init_session(&session);
    exit_code = run_cli(&session, argc, argv);
    free_session(&session);
    return exit_code == 0 ? 0 : 1;
}
