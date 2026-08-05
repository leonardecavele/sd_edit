#include <ctype.h>
#include <conio.h>
#include <errno.h>
#include <io.h>
#include <limits.h>
#include <process.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <sndfile.h>

#include "effects.h"
#include "preset.h"

#define PRESET_STORE_PATH "presets.txt"
#define MAX_COMMAND_LENGTH 4096
#define MAX_COMPLETION_TOKENS 4
#define MAX_COMMAND_HISTORY 100
#define MAX_OUTPUT_PATH 1024
#define MAX_PRESET_NAME 128
#define DEFAULT_RANDOMNESS 5
#define MIN_RANDOMNESS 0
#define MAX_RANDOMNESS 10
#define PROGRAM_NAME "sd_edit"
#define PROGRAM_EXECUTABLE_NAME "sd_edit.exe"
#define SNDFILE_DLL_RELATIVE_PATH "libsndfile-1.2.2-win64\\bin\\sndfile.dll"
#define PLAY_COMMAND_RELATIVE_PATH "libsndfile-1.2.2-win64\\bin\\sndfile-play.exe"

typedef SNDFILE *(*SfOpenFunction)(const char *path, int mode, SF_INFO *info);
typedef sf_count_t (*SfReadShortFunction)(SNDFILE *file, short *ptr, sf_count_t items);
typedef sf_count_t (*SfWriteShortFunction)(SNDFILE *file, const short *ptr, sf_count_t items);
typedef int (*SfCloseFunction)(SNDFILE *file);
typedef const char *(*SfStrerrorFunction)(SNDFILE *file);

typedef struct
{
    HMODULE module;
    char bundled_dll_path[MAX_OUTPUT_PATH];
    char error_message[MAX_OUTPUT_PATH];
    SfOpenFunction open_file;
    SfReadShortFunction read_short;
    SfWriteShortFunction write_short;
    SfCloseFunction close_file;
    SfStrerrorFunction strerror_file;
} SndfileRuntime;

typedef struct
{
    PresetRecipe current_recipe;
    int has_current_recipe;
    char current_preset_name[MAX_PRESET_NAME];
} Session;

static const char *g_command_catalog[] = {
    "help",
    "do",
    "play",
    "preset",
    "show",
    "exit",
    "quit"
};

static const char *g_preset_subcommand_catalog[] = {
    "random",
    "load",
    "save"
};

static const char *g_show_subcommand_catalog[] = {
    "preset"
};

static const char *g_randomness_catalog[] = {
    "0",
    "1",
    "2",
    "3",
    "4",
    "5",
    "6",
    "7",
    "8",
    "9",
    "10"
};

static SndfileRuntime g_sndfile_runtime;

static int get_executable_path(char *buffer, size_t buffer_size)
{
    DWORD copied_length;

    if (buffer == NULL || buffer_size == 0)
    {
        return -1;
    }

    copied_length = GetModuleFileNameA(NULL, buffer, (DWORD)buffer_size);

    if (copied_length == 0 || copied_length >= buffer_size)
    {
        return -1;
    }

    return 0;
}

static int build_path_relative_to_executable(const char *relative_path, char *buffer, size_t buffer_size)
{
    char executable_path[MAX_OUTPUT_PATH];
    char *directory_separator;

    if (relative_path == NULL || buffer == NULL || buffer_size == 0)
    {
        return -1;
    }

    if (get_executable_path(executable_path, sizeof(executable_path)) != 0)
    {
        return -1;
    }

    directory_separator = strrchr(executable_path, '\\');

    if (directory_separator == NULL)
    {
        directory_separator = strrchr(executable_path, '/');
    }

    if (directory_separator == NULL)
    {
        return -1;
    }

    *directory_separator = '\0';

    if (snprintf(buffer, buffer_size, "%s\\%s", executable_path, relative_path) >= (int)buffer_size)
    {
        return -1;
    }

    return 0;
}

static void unload_sndfile_runtime(void)
{
    if (g_sndfile_runtime.module != NULL)
    {
        FreeLibrary(g_sndfile_runtime.module);
    }

    memset(&g_sndfile_runtime, 0, sizeof(g_sndfile_runtime));
}

static int initialize_sndfile_runtime(void)
{
    HMODULE module;

    memset(&g_sndfile_runtime, 0, sizeof(g_sndfile_runtime));

    if (build_path_relative_to_executable(
        SNDFILE_DLL_RELATIVE_PATH,
        g_sndfile_runtime.bundled_dll_path,
        sizeof(g_sndfile_runtime.bundled_dll_path)) != 0)
    {
        snprintf(
            g_sndfile_runtime.error_message,
            sizeof(g_sndfile_runtime.error_message),
            "Unable to resolve the bundled sndfile.dll path relative to sd_edit.exe."
        );
        return -1;
    }

    module = LoadLibraryA(g_sndfile_runtime.bundled_dll_path);

    if (module == NULL)
    {
        module = LoadLibraryA("sndfile.dll");
    }

    if (module == NULL)
    {
        snprintf(
            g_sndfile_runtime.error_message,
            sizeof(g_sndfile_runtime.error_message),
            "Tried the bundled runtime first, then PATH, but sndfile.dll could not be loaded."
        );
        return -1;
    }

    g_sndfile_runtime.module = module;
    g_sndfile_runtime.open_file = (SfOpenFunction)GetProcAddress(module, "sf_open");
    g_sndfile_runtime.read_short = (SfReadShortFunction)GetProcAddress(module, "sf_read_short");
    g_sndfile_runtime.write_short = (SfWriteShortFunction)GetProcAddress(module, "sf_write_short");
    g_sndfile_runtime.close_file = (SfCloseFunction)GetProcAddress(module, "sf_close");
    g_sndfile_runtime.strerror_file = (SfStrerrorFunction)GetProcAddress(module, "sf_strerror");

    if (g_sndfile_runtime.open_file == NULL ||
        g_sndfile_runtime.read_short == NULL ||
        g_sndfile_runtime.write_short == NULL ||
        g_sndfile_runtime.close_file == NULL ||
        g_sndfile_runtime.strerror_file == NULL)
    {
        snprintf(
            g_sndfile_runtime.error_message,
            sizeof(g_sndfile_runtime.error_message),
            "Loaded sndfile.dll, but it does not export the libsndfile functions this program requires."
        );
        unload_sndfile_runtime();
        return -1;
    }

    return 0;
}

static void print_sndfile_runtime_error(void)
{
    fprintf(
        stderr,
        "Unable to start because the libsndfile runtime (sndfile.dll) is not available.\n"
        "Expected bundled runtime: %s\n"
        "The program first tries that bundled DLL, then PATH.\n"
        "Fix it by running 'make run' or by adding 'libsndfile-1.2.2-win64\\\\bin' to PATH.\n",
        g_sndfile_runtime.bundled_dll_path[0] != '\0'
            ? g_sndfile_runtime.bundled_dll_path
            : SNDFILE_DLL_RELATIVE_PATH
    );

    if (g_sndfile_runtime.error_message[0] != '\0')
    {
        fprintf(stderr, "Details: %s\n", g_sndfile_runtime.error_message);
    }
}

static SNDFILE *sndfile_open_file(const char *path, int mode, SF_INFO *info)
{
    if (g_sndfile_runtime.open_file == NULL)
    {
        return NULL;
    }

    return g_sndfile_runtime.open_file(path, mode, info);
}

static sf_count_t sndfile_read_short(SNDFILE *file, short *buffer, sf_count_t sample_count)
{
    if (g_sndfile_runtime.read_short == NULL)
    {
        return 0;
    }

    return g_sndfile_runtime.read_short(file, buffer, sample_count);
}

static sf_count_t sndfile_write_short(SNDFILE *file, const short *buffer, sf_count_t sample_count)
{
    if (g_sndfile_runtime.write_short == NULL)
    {
        return 0;
    }

    return g_sndfile_runtime.write_short(file, buffer, sample_count);
}

static int sndfile_close_file(SNDFILE *file)
{
    if (g_sndfile_runtime.close_file == NULL)
    {
        return 0;
    }

    return g_sndfile_runtime.close_file(file);
}

static const char *sndfile_error_string(SNDFILE *file)
{
    const char *detail;

    if (g_sndfile_runtime.strerror_file == NULL)
    {
        return NULL;
    }

    detail = g_sndfile_runtime.strerror_file(file);

    if (detail == NULL || detail[0] == '\0' || strncmp(detail, "No Error", 8) == 0)
    {
        return NULL;
    }

    return detail;
}

static void print_sndfile_operation_error(const char *message, const char *path, SNDFILE *file)
{
    const char *detail = sndfile_error_string(file);

    if (detail != NULL)
    {
        fprintf(stderr, "%s: %s (%s)\n", message, path, detail);
        return;
    }

    fprintf(stderr, "%s: %s\n", message, path);
}

static void init_session(Session *session)
{
    if (session == NULL)
    {
        return;
    }

    init_preset_recipe(&session->current_recipe);
    session->has_current_recipe = 0;
    session->current_preset_name[0] = '\0';
}

static void free_session(Session *session)
{
    if (session == NULL)
    {
        return;
    }

    init_preset_recipe(&session->current_recipe);
    session->has_current_recipe = 0;
    session->current_preset_name[0] = '\0';
}

static void set_current_preset_name(Session *session, const char *name)
{
    if (session == NULL)
    {
        return;
    }

    if (name == NULL || *name == '\0')
    {
        session->current_preset_name[0] = '\0';
        return;
    }

    snprintf(session->current_preset_name, sizeof(session->current_preset_name), "%s", name);
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

static int parse_integer_value(const char *text, int *value)
{
    char *end;
    long parsed;
    char *trimmed_end;

    if (text == NULL || value == NULL)
    {
        return -1;
    }

    errno = 0;
    parsed = strtol(text, &end, 10);
    trimmed_end = trim_whitespace(end);

    if (text == end ||
        errno != 0 ||
        trimmed_end == NULL ||
        *trimmed_end != '\0' ||
        parsed < INT_MIN ||
        parsed > INT_MAX)
    {
        return -1;
    }

    *value = (int)parsed;
    return 0;
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

typedef enum
{
    COMPLETE_NONE,
    COMPLETE_INVALID,
    COMPLETE_COMMAND,
    COMPLETE_DO_FILE,
    COMPLETE_PLAY_FILE,
    COMPLETE_PRESET_SUBCOMMAND,
    COMPLETE_PRESET_RANDOM_VALUE,
    COMPLETE_PRESET_LOAD_NAME,
    COMPLETE_PRESET_SAVE_NAME,
    COMPLETE_SHOW_SUBCOMMAND,
    COMPLETE_SHOW_PRESET_NAME
} CompletionContext;

typedef struct
{
    size_t start;
    size_t end;
    size_t after_end;
    char quote;
} CompletionToken;

typedef struct
{
    int token_count;
    int ends_with_whitespace;
    size_t current_token_start;
    size_t current_token_end;
    size_t current_token_after_end;
    char token_quote;
    char active_quote;
    CompletionContext context;
} CompletionParseResult;

typedef struct
{
    int active;
    CompletionContext context;
    size_t token_start;
    size_t token_end;
    size_t token_after_end;
    char token_quote;
    char **matches;
    int match_count;
    int cycle_index;
    char last_buffer[MAX_COMMAND_LENGTH];
} CompletionSession;

typedef struct
{
    char *entries[MAX_COMMAND_HISTORY];
    int count;
    int browse_index;
    int has_draft;
    char draft[MAX_COMMAND_LENGTH];
} CommandHistory;

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

static void set_command_buffer(char *buffer, size_t buffer_size, const char *text)
{
    if (buffer == NULL || buffer_size == 0)
    {
        return;
    }

    if (text == NULL)
    {
        buffer[0] = '\0';
        return;
    }

    snprintf(buffer, buffer_size, "%s", text);
}

static void reset_command_history_navigation(CommandHistory *history)
{
    if (history == NULL)
    {
        return;
    }

    history->browse_index = -1;
    history->has_draft = 0;
    history->draft[0] = '\0';
}

static void initialize_command_history(CommandHistory *history)
{
    if (history == NULL)
    {
        return;
    }

    memset(history, 0, sizeof(*history));
    history->browse_index = -1;
}

static void free_command_history(CommandHistory *history)
{
    if (history == NULL)
    {
        return;
    }

    for (int index = 0; index < history->count; index++)
    {
        free(history->entries[index]);
        history->entries[index] = NULL;
    }

    history->count = 0;
    reset_command_history_navigation(history);
}

static int add_command_history_entry(CommandHistory *history, const char *line)
{
    char *entry;

    if (history == NULL || line == NULL || *line == '\0')
    {
        return 0;
    }

    if (history->count > 0 && strcmp(history->entries[history->count - 1], line) == 0)
    {
        reset_command_history_navigation(history);
        return 0;
    }

    entry = duplicate_string(line);

    if (entry == NULL)
    {
        return -1;
    }

    if (history->count == MAX_COMMAND_HISTORY)
    {
        free(history->entries[0]);
        memmove(
            &history->entries[0],
            &history->entries[1],
            sizeof(history->entries[0]) * (MAX_COMMAND_HISTORY - 1)
        );
        history->count--;
        history->entries[history->count] = NULL;
    }

    history->entries[history->count] = entry;
    history->count++;
    reset_command_history_navigation(history);
    return 0;
}

static int navigate_command_history(
    CommandHistory *history,
    int direction,
    const char *current_buffer,
    char *buffer,
    size_t buffer_size)
{
    if (history == NULL || buffer == NULL || buffer_size == 0 || history->count <= 0)
    {
        return 0;
    }

    if (direction < 0)
    {
        if (history->browse_index < 0)
        {
            set_command_buffer(history->draft, sizeof(history->draft), current_buffer);
            history->has_draft = 1;
            history->browse_index = history->count - 1;
        }
        else if (history->browse_index > 0)
        {
            history->browse_index--;
        }

        set_command_buffer(buffer, buffer_size, history->entries[history->browse_index]);
        return 1;
    }

    if (direction > 0)
    {
        if (history->browse_index < 0)
        {
            return 0;
        }

        if (history->browse_index + 1 < history->count)
        {
            history->browse_index++;
            set_command_buffer(buffer, buffer_size, history->entries[history->browse_index]);
        }
        else
        {
            history->browse_index = -1;
            set_command_buffer(buffer, buffer_size, history->has_draft ? history->draft : "");
            history->has_draft = 0;
            history->draft[0] = '\0';
        }

        return 1;
    }

    return 0;
}

static int starts_with_ignore_case(const char *text, const char *prefix)
{
    if (text == NULL || prefix == NULL)
    {
        return 0;
    }

    while (*prefix != '\0')
    {
        if (*text == '\0')
        {
            return 0;
        }

        if (tolower((unsigned char)*text) != tolower((unsigned char)*prefix))
        {
            return 0;
        }

        text++;
        prefix++;
    }

    return 1;
}

static void free_completion_matches(char **matches, int count)
{
    if (matches == NULL)
    {
        return;
    }

    for (int index = 0; index < count; index++)
    {
        free(matches[index]);
    }

    free(matches);
}

static void reset_completion_session(CompletionSession *session)
{
    if (session == NULL)
    {
        return;
    }

    free_completion_matches(session->matches, session->match_count);
    memset(session, 0, sizeof(*session));
}

static int append_completion_match(char ***matches, int *count, int *capacity, const char *value)
{
    char **resized_matches;
    char *value_copy;

    if (matches == NULL || count == NULL || capacity == NULL || value == NULL)
    {
        return -1;
    }

    if (*count >= *capacity)
    {
        int new_capacity = *capacity > 0 ? *capacity * 2 : 8;

        resized_matches = realloc(*matches, sizeof(char *) * (size_t)new_capacity);

        if (resized_matches == NULL)
        {
            return -1;
        }

        *matches = resized_matches;
        *capacity = new_capacity;
    }

    value_copy = duplicate_string(value);

    if (value_copy == NULL)
    {
        return -1;
    }

    (*matches)[*count] = value_copy;
    (*count)++;
    return 0;
}

static int token_equals_ignore_case(const char *line, const CompletionToken *token, const char *value)
{
    size_t index;
    size_t length;

    if (line == NULL || token == NULL || value == NULL)
    {
        return 0;
    }

    length = strlen(value);

    if ((token->end - token->start) != length)
    {
        return 0;
    }

    for (index = 0; index < length; index++)
    {
        if (tolower((unsigned char)line[token->start + index]) != tolower((unsigned char)value[index]))
        {
            return 0;
        }
    }

    return 1;
}

static int copy_token_text(
    const char *line,
    size_t start,
    size_t end,
    char *output,
    size_t output_size)
{
    size_t length;

    if (line == NULL || output == NULL || output_size == 0 || end < start)
    {
        return -1;
    }

    length = end - start;

    if (length + 1 > output_size)
    {
        return -1;
    }

    memcpy(output, line + start, length);
    output[length] = '\0';
    return 0;
}

static int parse_completion_state(const char *line, CompletionParseResult *result)
{
    CompletionToken tokens[MAX_COMPLETION_TOKENS];
    size_t line_length;
    size_t cursor = 0;
    int token_count = 0;
    int current_token_index;
    int token_overflow = 0;

    if (line == NULL || result == NULL)
    {
        return -1;
    }

    memset(result, 0, sizeof(*result));
    line_length = strlen(line);
    result->current_token_start = line_length;
    result->current_token_end = line_length;
    result->current_token_after_end = line_length;
    result->context = COMPLETE_COMMAND;

    while (line[cursor] != '\0')
    {
        char quote = '\0';

        while (line[cursor] != '\0' && isspace((unsigned char)line[cursor]))
        {
            cursor++;
        }

        if (line[cursor] == '\0')
        {
            break;
        }

        if (token_count >= MAX_COMPLETION_TOKENS)
        {
            token_overflow = 1;
            break;
        }

        if (line[cursor] == '"' || line[cursor] == '\'')
        {
            quote = line[cursor];
            cursor++;
        }

        tokens[token_count].start = cursor;
        tokens[token_count].quote = quote;

        if (quote != '\0')
        {
            while (line[cursor] != '\0' && line[cursor] != quote)
            {
                cursor++;
            }

            tokens[token_count].end = cursor;
            tokens[token_count].after_end = cursor;

            if (line[cursor] == quote)
            {
                tokens[token_count].after_end = cursor + 1;
                cursor++;
            }
        }
        else
        {
            while (line[cursor] != '\0' && !isspace((unsigned char)line[cursor]))
            {
                cursor++;
            }

            tokens[token_count].end = cursor;
            tokens[token_count].after_end = cursor;
        }

        token_count++;
    }

    result->token_count = token_overflow ? MAX_COMPLETION_TOKENS + 1 : token_count;
    result->ends_with_whitespace =
        line_length > 0 && isspace((unsigned char)line[line_length - 1]);

    if (token_overflow)
    {
        result->context = COMPLETE_INVALID;
        return 0;
    }

    if (token_count == 0 || result->ends_with_whitespace)
    {
        current_token_index = token_count;
    }
    else
    {
        CompletionToken *current_token = &tokens[token_count - 1];

        result->current_token_start = current_token->start;
        result->current_token_end = current_token->end;
        result->current_token_after_end = current_token->after_end;
        result->token_quote = current_token->quote;

        if (current_token->quote != '\0' && current_token->after_end == current_token->end)
        {
            result->active_quote = current_token->quote;
        }

        current_token_index = token_count - 1;
    }

    if (token_count == 0)
    {
        result->context = COMPLETE_COMMAND;
        return 0;
    }

    if (current_token_index == 0)
    {
        result->context = COMPLETE_COMMAND;
        return 0;
    }

    if (token_equals_ignore_case(line, &tokens[0], "do"))
    {
        result->context = current_token_index == 1 ? COMPLETE_DO_FILE : COMPLETE_INVALID;
        return 0;
    }

    if (token_equals_ignore_case(line, &tokens[0], "play"))
    {
        result->context = current_token_index == 1 ? COMPLETE_PLAY_FILE : COMPLETE_INVALID;
        return 0;
    }

    if (token_equals_ignore_case(line, &tokens[0], "preset"))
    {
        if (current_token_index == 1)
        {
            result->context = COMPLETE_PRESET_SUBCOMMAND;
            return 0;
        }

        if (token_count >= 2 && token_equals_ignore_case(line, &tokens[1], "random"))
        {
            result->context = current_token_index == 2 ? COMPLETE_PRESET_RANDOM_VALUE : COMPLETE_INVALID;
            return 0;
        }

        if (token_count >= 2 && token_equals_ignore_case(line, &tokens[1], "load"))
        {
            result->context = current_token_index == 2 ? COMPLETE_PRESET_LOAD_NAME : COMPLETE_INVALID;
            return 0;
        }

        if (token_count >= 2 && token_equals_ignore_case(line, &tokens[1], "save"))
        {
            result->context = current_token_index == 2 ? COMPLETE_PRESET_SAVE_NAME : COMPLETE_INVALID;
            return 0;
        }

        result->context = COMPLETE_INVALID;
        return 0;
    }

    if (token_equals_ignore_case(line, &tokens[0], "show"))
    {
        if (current_token_index == 1)
        {
            result->context = COMPLETE_SHOW_SUBCOMMAND;
            return 0;
        }

        if (token_count >= 2 && token_equals_ignore_case(line, &tokens[1], "preset"))
        {
            result->context = current_token_index == 2 ? COMPLETE_SHOW_PRESET_NAME : COMPLETE_INVALID;
            return 0;
        }

        result->context = COMPLETE_INVALID;
        return 0;
    }

    if (token_equals_ignore_case(line, &tokens[0], "help") ||
        token_equals_ignore_case(line, &tokens[0], "exit") ||
        token_equals_ignore_case(line, &tokens[0], "quit"))
    {
        result->context = COMPLETE_NONE;
        return 0;
    }

    result->context = COMPLETE_INVALID;
    return 0;
}

static int completion_candidate_is_directory(const char *candidate)
{
    size_t length;

    if (candidate == NULL)
    {
        return 0;
    }

    length = strlen(candidate);
    return length > 0 && (candidate[length - 1] == '\\' || candidate[length - 1] == '/');
}

static int completion_should_append_space(CompletionContext context, const char *candidate)
{
    if (candidate == NULL || completion_candidate_is_directory(candidate))
    {
        return 0;
    }

    switch (context)
    {
        case COMPLETE_COMMAND:
        case COMPLETE_DO_FILE:
        case COMPLETE_PLAY_FILE:
        case COMPLETE_PRESET_SUBCOMMAND:
        case COMPLETE_PRESET_RANDOM_VALUE:
        case COMPLETE_PRESET_LOAD_NAME:
        case COMPLETE_PRESET_SAVE_NAME:
        case COMPLETE_SHOW_SUBCOMMAND:
        case COMPLETE_SHOW_PRESET_NAME:
            return 1;

        case COMPLETE_NONE:
        case COMPLETE_INVALID:
        default:
            return 0;
    }
}

static int replace_completion_token(
    char *buffer,
    size_t buffer_size,
    size_t token_start,
    size_t token_end,
    size_t token_after_end,
    char token_quote,
    const char *replacement,
    int close_quote,
    int append_space,
    size_t *new_token_end,
    size_t *new_token_after_end)
{
    char rebuilt[MAX_COMMAND_LENGTH];
    size_t line_length;
    size_t replacement_length;
    size_t suffix_length;
    size_t cursor;
    const char *suffix;
    int keep_quote;

    if (buffer == NULL ||
        replacement == NULL ||
        token_end < token_start ||
        token_after_end < token_end ||
        buffer_size == 0)
    {
        return -1;
    }

    line_length = strlen(buffer);

    if (token_after_end > line_length || buffer_size > sizeof(rebuilt))
    {
        return -1;
    }

    replacement_length = strlen(replacement);
    suffix = buffer + token_after_end;
    suffix_length = strlen(suffix);
    keep_quote = token_quote != '\0' && (close_quote || token_after_end > token_end);
    cursor = token_start;

    if (token_start > 0)
    {
        memcpy(rebuilt, buffer, token_start);
    }

    if (cursor + replacement_length + 1 > buffer_size)
    {
        return -1;
    }

    memcpy(rebuilt + cursor, replacement, replacement_length);
    cursor += replacement_length;

    if (keep_quote)
    {
        if (cursor + 2 > buffer_size)
        {
            return -1;
        }

        rebuilt[cursor++] = token_quote;
    }

    if (append_space && (suffix[0] == '\0' || !isspace((unsigned char)suffix[0])))
    {
        if (cursor + 2 > buffer_size)
        {
            return -1;
        }

        rebuilt[cursor++] = ' ';
    }

    if (cursor + suffix_length + 1 > buffer_size)
    {
        return -1;
    }

    memcpy(rebuilt + cursor, suffix, suffix_length + 1);
    memcpy(buffer, rebuilt, cursor + suffix_length + 1);

    if (new_token_end != NULL)
    {
        *new_token_end = token_start + replacement_length;
    }

    if (new_token_after_end != NULL)
    {
        *new_token_after_end = token_start + replacement_length + (keep_quote ? 1 : 0);
    }

    return 0;
}

static size_t longest_common_prefix_length(char **matches, int match_count)
{
    size_t prefix_length = 0;

    if (matches == NULL || match_count <= 0 || matches[0] == NULL)
    {
        return 0;
    }

    while (matches[0][prefix_length] != '\0')
    {
        char reference = (char)tolower((unsigned char)matches[0][prefix_length]);

        for (int index = 1; index < match_count; index++)
        {
            if (matches[index] == NULL ||
                matches[index][prefix_length] == '\0' ||
                (char)tolower((unsigned char)matches[index][prefix_length]) != reference)
            {
                return prefix_length;
            }
        }

        prefix_length++;
    }

    return prefix_length;
}

static int collect_word_matches(
    const char *prefix,
    const char *const *words,
    size_t word_count,
    char ***matches,
    int *match_count,
    int *match_capacity)
{
    if (prefix == NULL || words == NULL || matches == NULL || match_count == NULL || match_capacity == NULL)
    {
        return -1;
    }

    for (size_t index = 0; index < word_count; index++)
    {
        if (starts_with_ignore_case(words[index], prefix) &&
            append_completion_match(matches, match_count, match_capacity, words[index]) != 0)
        {
            return -1;
        }
    }

    return 0;
}

static int collect_named_preset_matches(
    const char *prefix,
    char ***matches,
    int *match_count,
    int *match_capacity)
{
    char **names = NULL;
    int name_count = 0;
    int status;

    if (prefix == NULL || matches == NULL || match_count == NULL || match_capacity == NULL)
    {
        return -1;
    }

    status = list_named_presets(PRESET_STORE_PATH, &names, &name_count);

    if (status != 0)
    {
        return -1;
    }

    for (int index = 0; index < name_count; index++)
    {
        if (starts_with_ignore_case(names[index], prefix) &&
            append_completion_match(matches, match_count, match_capacity, names[index]) != 0)
        {
            free_named_preset_list(names, name_count);
            return -1;
        }
    }

    free_named_preset_list(names, name_count);
    return 0;
}

static void normalize_path_separators(char *path)
{
    if (path == NULL)
    {
        return;
    }

    while (*path != '\0')
    {
        if (*path == '/')
        {
            *path = '\\';
        }

        path++;
    }
}

static int build_directory_search_pattern(
    const char *directory,
    char *pattern,
    size_t pattern_size)
{
    size_t directory_length;

    if (directory == NULL || pattern == NULL || pattern_size == 0)
    {
        return -1;
    }

    if (directory[0] == '\0')
    {
        return snprintf(pattern, pattern_size, ".\\*") >= (int)pattern_size ? -1 : 0;
    }

    directory_length = strlen(directory);

    if (directory_length > 0 &&
        (directory[directory_length - 1] == '\\' || directory[directory_length - 1] == '/'))
    {
        return snprintf(pattern, pattern_size, "%s*", directory) >= (int)pattern_size ? -1 : 0;
    }

    return snprintf(pattern, pattern_size, "%s\\*", directory) >= (int)pattern_size ? -1 : 0;
}

static int collect_wav_path_matches(
    const char *prefix,
    char ***matches,
    int *match_count,
    int *match_capacity)
{
    WIN32_FIND_DATAA find_data;
    HANDLE find_handle;
    char normalized_prefix[MAX_COMMAND_LENGTH];
    char directory_prefix[MAX_COMMAND_LENGTH];
    char search_directory[MAX_COMMAND_LENGTH];
    char search_pattern[MAX_COMMAND_LENGTH];
    char *last_separator;

    if (prefix == NULL || matches == NULL || match_count == NULL || match_capacity == NULL)
    {
        return -1;
    }

    if (snprintf(normalized_prefix, sizeof(normalized_prefix), "%s", prefix) >= (int)sizeof(normalized_prefix))
    {
        return -1;
    }

    normalize_path_separators(normalized_prefix);
    directory_prefix[0] = '\0';
    search_directory[0] = '\0';
    last_separator = strrchr(normalized_prefix, '\\');

    if (last_separator != NULL)
    {
        size_t directory_length = (size_t)(last_separator - normalized_prefix) + 1;

        if (directory_length + 1 > sizeof(directory_prefix))
        {
            return -1;
        }

        memcpy(directory_prefix, normalized_prefix, directory_length);
        directory_prefix[directory_length] = '\0';

        if (snprintf(search_directory, sizeof(search_directory), "%s", directory_prefix) >= (int)sizeof(search_directory))
        {
            return -1;
        }
    }

    if (build_directory_search_pattern(search_directory, search_pattern, sizeof(search_pattern)) != 0)
    {
        return -1;
    }

    find_handle = FindFirstFileA(search_pattern, &find_data);

    if (find_handle == INVALID_HANDLE_VALUE)
    {
        return 0;
    }

    do
    {
        int is_directory = (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        char candidate[MAX_COMMAND_LENGTH];

        if (strcmp(find_data.cFileName, ".") == 0 || strcmp(find_data.cFileName, "..") == 0)
        {
            continue;
        }

        if (!is_directory && !has_wav_extension(find_data.cFileName))
        {
            continue;
        }

        if (snprintf(
            candidate,
            sizeof(candidate),
            "%s%s%s",
            directory_prefix,
            find_data.cFileName,
            is_directory ? "\\" : "") >= (int)sizeof(candidate))
        {
            continue;
        }

        if (starts_with_ignore_case(candidate, normalized_prefix) &&
            append_completion_match(matches, match_count, match_capacity, candidate) != 0)
        {
            FindClose(find_handle);
            return -1;
        }
    }
    while (FindNextFileA(find_handle, &find_data) != 0);

    FindClose(find_handle);
    return 0;
}

static int collect_completion_matches(
    const char *buffer,
    const CompletionParseResult *parse,
    char ***matches,
    int *match_count)
{
    char prefix[MAX_COMMAND_LENGTH];
    int match_capacity = 0;

    if (buffer == NULL || parse == NULL || matches == NULL || match_count == NULL)
    {
        return -1;
    }

    *matches = NULL;
    *match_count = 0;

    if (copy_token_text(
        buffer,
        parse->current_token_start,
        parse->current_token_end,
        prefix,
        sizeof(prefix)) != 0)
    {
        return -1;
    }

    switch (parse->context)
    {
        case COMPLETE_COMMAND:
            return collect_word_matches(
                prefix,
                g_command_catalog,
                sizeof(g_command_catalog) / sizeof(g_command_catalog[0]),
                matches,
                match_count,
                &match_capacity
            );

        case COMPLETE_DO_FILE:
        case COMPLETE_PLAY_FILE:
            return collect_wav_path_matches(prefix, matches, match_count, &match_capacity);

        case COMPLETE_PRESET_LOAD_NAME:
        case COMPLETE_PRESET_SAVE_NAME:
        case COMPLETE_SHOW_PRESET_NAME:
            return collect_named_preset_matches(prefix, matches, match_count, &match_capacity);

        case COMPLETE_PRESET_SUBCOMMAND:
            return collect_word_matches(
                prefix,
                g_preset_subcommand_catalog,
                sizeof(g_preset_subcommand_catalog) / sizeof(g_preset_subcommand_catalog[0]),
                matches,
                match_count,
                &match_capacity
            );

        case COMPLETE_PRESET_RANDOM_VALUE:
            return collect_word_matches(
                prefix,
                g_randomness_catalog,
                sizeof(g_randomness_catalog) / sizeof(g_randomness_catalog[0]),
                matches,
                match_count,
                &match_capacity
            );

        case COMPLETE_SHOW_SUBCOMMAND:
            return collect_word_matches(
                prefix,
                g_show_subcommand_catalog,
                sizeof(g_show_subcommand_catalog) / sizeof(g_show_subcommand_catalog[0]),
                matches,
                match_count,
                &match_capacity
            );

        case COMPLETE_NONE:
        case COMPLETE_INVALID:
        default:
            return 0;
    }
}

static int apply_completion(char *buffer, size_t buffer_size, CompletionSession *session)
{
    CompletionParseResult parse;
    char **matches = NULL;
    int match_count = 0;
    size_t new_token_end = 0;
    size_t new_token_after_end = 0;

    if (buffer == NULL || session == NULL || buffer_size == 0)
    {
        return 0;
    }

    if (session->active &&
        session->match_count > 1 &&
        strcmp(buffer, session->last_buffer) == 0)
    {
        const char *candidate = session->matches[(session->cycle_index + 1) % session->match_count];
        int append_space = completion_should_append_space(session->context, candidate);
        int close_quote =
            session->token_quote != '\0' &&
            session->token_after_end == session->token_end &&
            append_space;

        if (replace_completion_token(
            buffer,
            buffer_size,
            session->token_start,
            session->token_end,
            session->token_after_end,
            session->token_quote,
            candidate,
            close_quote,
            append_space,
            &new_token_end,
            &new_token_after_end) != 0)
        {
            reset_completion_session(session);
            return 0;
        }

        session->token_end = new_token_end;
        session->token_after_end = new_token_after_end;
        session->cycle_index = (session->cycle_index + 1) % session->match_count;
        snprintf(session->last_buffer, sizeof(session->last_buffer), "%s", buffer);
        return 1;
    }

    reset_completion_session(session);

    if (parse_completion_state(buffer, &parse) != 0 ||
        parse.context == COMPLETE_NONE ||
        parse.context == COMPLETE_INVALID)
    {
        return 0;
    }

    if (collect_completion_matches(buffer, &parse, &matches, &match_count) != 0)
    {
        free_completion_matches(matches, match_count);
        return 0;
    }

    if (match_count <= 0)
    {
        free_completion_matches(matches, match_count);
        return 0;
    }

    if (match_count == 1)
    {
        const char *candidate = matches[0];
        int append_space = completion_should_append_space(parse.context, candidate);
        int close_quote =
            parse.token_quote != '\0' &&
            parse.current_token_after_end == parse.current_token_end &&
            append_space;
        int changed;

        changed = replace_completion_token(
            buffer,
            buffer_size,
            parse.current_token_start,
            parse.current_token_end,
            parse.current_token_after_end,
            parse.token_quote,
            candidate,
            close_quote,
            append_space,
            NULL,
            NULL) == 0;
        free_completion_matches(matches, match_count);
        return changed;
    }

    {
        size_t prefix_length = parse.current_token_end - parse.current_token_start;
        size_t common_prefix_length = longest_common_prefix_length(matches, match_count);

        if (common_prefix_length > prefix_length)
        {
            char common_prefix[MAX_COMMAND_LENGTH];

            if (common_prefix_length + 1 > sizeof(common_prefix))
            {
                free_completion_matches(matches, match_count);
                return 0;
            }

            memcpy(common_prefix, matches[0], common_prefix_length);
            common_prefix[common_prefix_length] = '\0';

            if (replace_completion_token(
                buffer,
                buffer_size,
                parse.current_token_start,
                parse.current_token_end,
                parse.current_token_after_end,
                parse.token_quote,
                common_prefix,
                0,
                0,
                &new_token_end,
                &new_token_after_end) != 0)
            {
                free_completion_matches(matches, match_count);
                return 0;
            }
        }
        else
        {
            new_token_end = parse.current_token_end;
            new_token_after_end = parse.current_token_after_end;
        }
    }

    session->active = 1;
    session->context = parse.context;
    session->token_start = parse.current_token_start;
    session->token_end = new_token_end;
    session->token_after_end = new_token_after_end;
    session->token_quote = parse.token_quote;
    session->matches = matches;
    session->match_count = match_count;
    session->cycle_index = -1;
    snprintf(session->last_buffer, sizeof(session->last_buffer), "%s", buffer);
    return 1;
}

static int prompt_supports_completion(void)
{
    HANDLE input_handle;
    HANDLE output_handle;
    DWORD input_mode;
    DWORD output_mode;

    if (_fileno(stdin) < 0 || _fileno(stdout) < 0 || !_isatty(_fileno(stdin)) || !_isatty(_fileno(stdout)))
    {
        return 0;
    }

    input_handle = GetStdHandle(STD_INPUT_HANDLE);
    output_handle = GetStdHandle(STD_OUTPUT_HANDLE);

    if (input_handle == NULL ||
        input_handle == INVALID_HANDLE_VALUE ||
        output_handle == NULL ||
        output_handle == INVALID_HANDLE_VALUE)
    {
        return 0;
    }

    return GetConsoleMode(input_handle, &input_mode) != 0 &&
        GetConsoleMode(output_handle, &output_mode) != 0;
}

static const char *current_preset_display_name(const Session *session)
{
    if (session == NULL || !session->has_current_recipe)
    {
        return NULL;
    }

    if (session->current_preset_name[0] != '\0')
    {
        return session->current_preset_name;
    }

    return "preset";
}

static int format_recipe_summary(const PresetRecipe *recipe, char *buffer, size_t buffer_size)
{
    if (!preset_recipe_is_valid(recipe) ||
        buffer == NULL ||
        buffer_size == 0)
    {
        return -1;
    }

    if (snprintf(
        buffer,
        buffer_size,
        "randomness=%d seed=%u",
        recipe->randomness,
        recipe->seed) >= (int)buffer_size)
    {
        return -1;
    }

    return 0;
}

static int build_prompt_text(const Session *session, char *buffer, size_t buffer_size)
{
    const char *preset_name;
    char summary[128];

    if (buffer == NULL || buffer_size == 0)
    {
        return -1;
    }

    preset_name = current_preset_display_name(session);

    if (preset_name == NULL ||
        format_recipe_summary(&session->current_recipe, summary, sizeof(summary)) != 0)
    {
        return snprintf(buffer, buffer_size, "%s> ", PROGRAM_NAME) >= (int)buffer_size ? -1 : 0;
    }

    return snprintf(
        buffer,
        buffer_size,
        "%s [%s] %s> ",
        preset_name,
        summary,
        PROGRAM_NAME) >= (int)buffer_size ? -1 : 0;
}

static void write_prompt_text_plain(const Session *session)
{
    char prompt[MAX_COMMAND_LENGTH];

    if (build_prompt_text(session, prompt, sizeof(prompt)) != 0)
    {
        printf("%s> ", PROGRAM_NAME);
        return;
    }

    printf("%s", prompt);
}

static void redraw_prompt_line(const Session *session, const char *buffer, size_t *previous_length)
{
    char prompt[MAX_COMMAND_LENGTH];
    size_t current_length;

    if (buffer == NULL || previous_length == NULL)
    {
        return;
    }

    if (build_prompt_text(session, prompt, sizeof(prompt)) != 0)
    {
        snprintf(prompt, sizeof(prompt), "%s> ", PROGRAM_NAME);
    }

    current_length = strlen(prompt) + strlen(buffer);
    printf("\r");
    write_prompt_text_plain(session);
    printf("%s", buffer);

    if (*previous_length > current_length)
    {
        for (size_t index = current_length; index < *previous_length; index++)
        {
            putchar(' ');
        }

        printf("\r");
        write_prompt_text_plain(session);
        printf("%s", buffer);
    }

    fflush(stdout);
    *previous_length = current_length;
}

static int read_interactive_line(
    char *buffer,
    size_t buffer_size,
    const Session *session,
    CommandHistory *history)
{
    CompletionSession completion_session;

    if (buffer == NULL || buffer_size == 0)
    {
        return -1;
    }

    if (!prompt_supports_completion())
    {
        write_prompt_text_plain(session);
        fflush(stdout);

        if (fgets(buffer, (int)buffer_size, stdin) == NULL)
        {
            putchar('\n');
            return 0;
        }

        buffer[strcspn(buffer, "\r\n")] = '\0';
        return 1;
    }

    memset(&completion_session, 0, sizeof(completion_session));
    buffer[0] = '\0';

    {
        size_t previous_length = 0;

        redraw_prompt_line(session, buffer, &previous_length);

        while (1)
        {
            int character = _getch();
            size_t length = strlen(buffer);

            if (character == 0 || character == 224)
            {
                int special_key = _getch();

                if (special_key == 72 || special_key == 80)
                {
                    int direction = special_key == 72 ? -1 : 1;

                    if (navigate_command_history(history, direction, buffer, buffer, buffer_size))
                    {
                        reset_completion_session(&completion_session);
                        redraw_prompt_line(session, buffer, &previous_length);
                    }
                }

                continue;
            }

            if (character == '\r')
            {
                reset_completion_session(&completion_session);
                putchar('\n');
                return 1;
            }

            if (character == '\t')
            {
                if (apply_completion(buffer, buffer_size, &completion_session))
                {
                    reset_command_history_navigation(history);
                    redraw_prompt_line(session, buffer, &previous_length);
                }

                continue;
            }

            if (character == '\b')
            {
                if (length > 0)
                {
                    buffer[length - 1] = '\0';
                    reset_completion_session(&completion_session);
                    reset_command_history_navigation(history);
                    redraw_prompt_line(session, buffer, &previous_length);
                }

                continue;
            }

            if (character >= 32 && character != 127)
            {
                if (length + 1 < buffer_size)
                {
                    buffer[length] = (char)character;
                    buffer[length + 1] = '\0';
                    reset_completion_session(&completion_session);
                    reset_command_history_navigation(history);
                    redraw_prompt_line(session, buffer, &previous_length);
                }
            }
        }
    }
}

static void print_recipe(const PresetRecipe *recipe, const char *preset_name)
{
    if (!preset_recipe_is_valid(recipe))
    {
        printf("No current preset.\n");
        return;
    }

    if (preset_name != NULL && *preset_name != '\0')
    {
        printf("Preset recipe '%s':\n", preset_name);
    }
    else
    {
        printf("Preset recipe:\n");
    }

    printf("  randomness=%d\n", recipe->randomness);
    printf("  seed=%u\n", recipe->seed);
    printf("  materialization=derived at do-time from input duration and recipe seed\n");
}

static void print_current_preset(const Session *session)
{
    if (session == NULL || !session->has_current_recipe)
    {
        printf("No current preset.\n");
        return;
    }

    print_recipe(&session->current_recipe, current_preset_display_name(session));
}

static void print_usage(void)
{
    size_t effect_count;
    const EffectDescriptor *catalog = get_effect_catalog(&effect_count);

    printf("%s commands:\n", PROGRAM_NAME);
    printf("  help\n");
    printf("  do <file.wav>\n");
    printf("  play <file.wav>\n");
    printf("  preset random [randomness]\n");
    printf("  preset load <name>\n");
    printf("  preset save <name>\n");
    printf("  show preset [name]\n");
    printf("  exit\n");
    printf("  quit\n");
    printf("\n");
    printf("Run %s with no arguments to open the prompt.\n", PROGRAM_EXECUTABLE_NAME);
    printf("In prompt mode, press Tab to complete commands, .wav paths, subcommands, preset names, and randomness values.\n");
    printf("Direct invocation is also supported, for example:\n");
    printf("  %s help\n", PROGRAM_EXECUTABLE_NAME);
    printf("  %s show preset demo\n", PROGRAM_EXECUTABLE_NAME);
    printf("\n");
    printf("Randomness values must be integers from %d to %d. Omitting the value uses %d.\n",
        MIN_RANDOMNESS,
        MAX_RANDOMNESS,
        DEFAULT_RANDOMNESS);
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

static int path_exists(const char *path)
{
    DWORD attributes;

    if (path == NULL || *path == '\0')
    {
        return 0;
    }

    attributes = GetFileAttributesA(path);
    return attributes != INVALID_FILE_ATTRIBUTES;
}

static int build_output_path_candidate(
    const char *input_path,
    const char *suffix,
    int collision_index,
    char *output_path,
    size_t output_size)
{
    const char *extension;

    if (input_path == NULL || output_path == NULL || output_size == 0 || !has_wav_extension(input_path))
    {
        return -1;
    }

    extension = strrchr(input_path, '.');

    if (extension == NULL)
    {
        return -1;
    }

    if (snprintf(
        output_path,
        output_size,
        collision_index > 0 ? "%.*s_%s%d.wav" : "%.*s_%s.wav",
        (int)(extension - input_path),
        input_path,
        suffix,
        collision_index
    ) >= (int)output_size)
    {
        return -1;
    }

    return 0;
}

static int build_output_path(const char *input_path, const char *label, char *output_path, size_t output_size)
{
    char suffix[MAX_PRESET_NAME];

    if (input_path == NULL || output_path == NULL || output_size == 0 || !has_wav_extension(input_path))
    {
        return -1;
    }

    if (sanitize_suffix(label, suffix, sizeof(suffix)) != 0)
    {
        return -1;
    }

    for (int collision_index = 0; collision_index < INT_MAX; collision_index++)
    {
        if (build_output_path_candidate(
            input_path,
            suffix,
            collision_index,
            output_path,
            output_size) != 0)
        {
            return -1;
        }

        if (!path_exists(output_path))
        {
            return 0;
        }
    }

    return -1;
}

static int update_current_recipe(Session *session, const PresetRecipe *recipe, const char *preset_name)
{
    if (session == NULL || recipe == NULL)
    {
        return -1;
    }

    if (copy_preset_recipe(&session->current_recipe, recipe) != 0)
    {
        return -1;
    }

    session->has_current_recipe = 1;
    set_current_preset_name(session, preset_name);
    return 0;
}

static int load_audio_file(const char *input_path, short **samples, SF_INFO *info)
{
    SNDFILE *input_file;
    sf_count_t sample_count;
    short *buffer;
    sf_count_t read_count;
    const char *read_error;

    if (input_path == NULL || samples == NULL || info == NULL)
    {
        return -1;
    }

    memset(info, 0, sizeof(*info));
    input_file = sndfile_open_file(input_path, SFM_READ, info);

    if (input_file == NULL)
    {
        print_sndfile_operation_error("Unable to open input file", input_path, NULL);
        return -1;
    }

    if (info->frames <= 0 || info->channels <= 0)
    {
        fprintf(stderr, "Unsupported audio metadata in file: %s\n", input_path);
        sndfile_close_file(input_file);
        return -1;
    }

    if (info->frames > (SF_COUNT_MAX / info->channels))
    {
        fprintf(stderr, "Audio file is too large to process safely: %s\n", input_path);
        sndfile_close_file(input_file);
        return -1;
    }

    sample_count = info->frames * info->channels;

    if ((size_t)sample_count > (SIZE_MAX / sizeof(short)))
    {
        fprintf(stderr, "Audio buffer would exceed available memory: %s\n", input_path);
        sndfile_close_file(input_file);
        return -1;
    }

    buffer = malloc(sizeof(short) * (size_t)sample_count);

    if (buffer == NULL)
    {
        fprintf(stderr, "Unable to allocate memory for audio buffer.\n");
        sndfile_close_file(input_file);
        return -1;
    }

    read_count = sndfile_read_short(input_file, buffer, sample_count);
    read_error = sndfile_error_string(input_file);
    sndfile_close_file(input_file);

    if (read_count != sample_count)
    {
        if (read_error != NULL)
        {
            fprintf(stderr, "Unable to read the full audio buffer from: %s (%s)\n", input_path, read_error);
        }
        else
        {
            fprintf(stderr, "Unable to read the full audio buffer from: %s\n", input_path);
        }
        free(buffer);
        return -1;
    }

    *samples = buffer;
    return 0;
}

static int save_audio_file(const char *output_path, const SF_INFO *info, const short *samples)
{
    SNDFILE *output_file;
    SF_INFO output_info;
    sf_count_t sample_count;
    sf_count_t written_count;
    const char *write_error;
    int close_status;

    if (output_path == NULL || info == NULL || samples == NULL)
    {
        return -1;
    }

    if (info->frames <= 0 || info->channels <= 0)
    {
        fprintf(stderr, "Invalid output audio metadata for: %s\n", output_path);
        return -1;
    }

    if (info->frames > (SF_COUNT_MAX / info->channels))
    {
        fprintf(stderr, "Output audio metadata is too large to save safely: %s\n", output_path);
        return -1;
    }

    sample_count = info->frames * info->channels;
    output_info = *info;

    output_file = sndfile_open_file(output_path, SFM_WRITE, &output_info);

    if (output_file == NULL)
    {
        print_sndfile_operation_error("Unable to create output file", output_path, NULL);
        return -1;
    }

    written_count = sndfile_write_short(output_file, samples, sample_count);
    write_error = sndfile_error_string(output_file);
    close_status = sndfile_close_file(output_file);

    if (written_count != sample_count)
    {
        if (write_error != NULL)
        {
            fprintf(stderr, "Unable to write the full processed audio buffer to: %s (%s)\n", output_path, write_error);
        }
        else
        {
            fprintf(stderr, "Unable to write the full processed audio buffer to: %s\n", output_path);
        }
        return -1;
    }

    if (close_status != 0)
    {
        fprintf(stderr, "Unable to finalize the output audio file: %s\n", output_path);
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

static int process_audio_file(const char *input_path, const PresetRecipe *recipe, const char *output_label)
{
    SF_INFO info;
    Preset preset;
    short *samples = NULL;
    char output_path[MAX_OUTPUT_PATH];
    int result = -1;

    if (input_path == NULL || recipe == NULL)
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

    init_preset(&preset);

    if (load_audio_file(input_path, &samples, &info) != 0)
    {
        return -1;
    }

    if (materialize_preset_from_recipe(&preset, recipe, &info) != 0)
    {
        fprintf(stderr, "Unable to materialize a concrete preset for: %s\n", input_path);
        goto cleanup;
    }

    printf(
        "Materialized %d segment(s) for %.3f second(s) of audio from recipe '%s'.\n",
        preset.nb_seg,
        info.samplerate > 0 ? (double)info.frames / (double)info.samplerate : 0.0,
        output_label != NULL ? output_label : "preset");

    if (apply_preset_to_samples(samples, &info, &preset) != 0)
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
    free_preset(&preset);
    free(samples);
    return result;
}

static void build_random_preset_name(int randomness, char *buffer, size_t buffer_size)
{
    if (buffer == NULL || buffer_size == 0)
    {
        return;
    }

    snprintf(buffer, buffer_size, "random_%d", randomness);
}

static int handle_preset_random_command(Session *session, const char *randomness_text)
{
    PresetRecipe recipe;
    int randomness = DEFAULT_RANDOMNESS;
    char preset_name[MAX_PRESET_NAME];

    if (session == NULL)
    {
        return -1;
    }

    if (randomness_text != NULL &&
        (parse_integer_value(randomness_text, &randomness) != 0 ||
            randomness < MIN_RANDOMNESS ||
            randomness > MAX_RANDOMNESS))
    {
        fprintf(stderr, "Usage: preset random [randomness]\n");
        fprintf(stderr, "Randomness must be an integer from %d to %d.\n", MIN_RANDOMNESS, MAX_RANDOMNESS);
        return -1;
    }

    init_preset_recipe(&recipe);

    if (generate_random_recipe(&recipe, randomness) != 0)
    {
        fprintf(stderr, "Unable to generate a random preset recipe.\n");
        return -1;
    }

    build_random_preset_name(randomness, preset_name, sizeof(preset_name));

    if (update_current_recipe(session, &recipe, preset_name) != 0)
    {
        fprintf(stderr, "Unable to store the current preset recipe in memory.\n");
        return -1;
    }

    printf("Generated preset recipe '%s'.\n", current_preset_display_name(session));
    print_current_preset(session);
    return 0;
}

static int handle_preset_load_command(Session *session, const char *name)
{
    PresetRecipe recipe;

    if (session == NULL || name == NULL || is_space_or_empty(name))
    {
        fprintf(stderr, "Missing preset name.\n");
        return -1;
    }

    init_preset_recipe(&recipe);

    if (load_named_preset(PRESET_STORE_PATH, name, &recipe) != 0)
    {
        fprintf(stderr, "Preset '%s' was not found in %s.\n", name, PRESET_STORE_PATH);
        return -1;
    }

    if (update_current_recipe(session, &recipe, name) != 0)
    {
        fprintf(stderr, "Unable to store the current preset recipe in memory.\n");
        return -1;
    }

    printf("Loaded preset '%s'.\n", name);
    print_current_preset(session);
    return 0;
}

static int handle_preset_save_command(Session *session, const char *name)
{
    if (session == NULL || name == NULL || is_space_or_empty(name))
    {
        fprintf(stderr, "Missing preset name.\n");
        return -1;
    }

    if (!session->has_current_recipe)
    {
        fprintf(stderr, "No current preset is loaded. Use 'preset random [randomness]' or 'preset load <name>' first.\n");
        return -1;
    }

    if (save_named_preset(PRESET_STORE_PATH, name, &session->current_recipe) != 0)
    {
        fprintf(stderr, "Unable to save preset '%s' to %s.\n", name, PRESET_STORE_PATH);
        return -1;
    }

    set_current_preset_name(session, name);
    printf("Saved current preset as '%s' to %s\n", name, PRESET_STORE_PATH);
    return 0;
}

static int handle_show_named_preset_command(const char *name)
{
    PresetRecipe recipe;

    if (name == NULL || is_space_or_empty(name))
    {
        fprintf(stderr, "Missing preset name.\n");
        return -1;
    }

    init_preset_recipe(&recipe);

    if (load_named_preset(PRESET_STORE_PATH, name, &recipe) != 0)
    {
        fprintf(stderr, "Preset '%s' was not found in %s.\n", name, PRESET_STORE_PATH);
        return -1;
    }

    print_recipe(&recipe, name);
    return 0;
}

static int handle_do_command(Session *session, const char *input_path)
{
    if (session == NULL || input_path == NULL)
    {
        return -1;
    }

    if (!session->has_current_recipe)
    {
        fprintf(stderr, "No current preset is loaded. Use 'preset random [randomness]' or 'preset load <name>' first.\n");
        return -1;
    }

    if (!has_wav_extension(input_path))
    {
        fprintf(stderr, "Only .wav files are supported: %s\n", input_path);
        return -1;
    }

    return process_audio_file(
        input_path,
        &session->current_recipe,
        current_preset_display_name(session));
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

    if (build_path_relative_to_executable(
        PLAY_COMMAND_RELATIVE_PATH,
        player_path,
        sizeof(player_path)) != 0)
    {
        fprintf(stderr, "Unable to resolve the bundled player path relative to %s.\n", PROGRAM_EXECUTABLE_NAME);
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

    if (equals_ignore_case(argv[0], "exit") || equals_ignore_case(argv[0], "quit"))
    {
        return 0;
    }

    if (equals_ignore_case(argv[0], "show"))
    {
        if (argc == 2 && equals_ignore_case(argv[1], "preset"))
        {
            print_current_preset(session);
            return 0;
        }

        if (argc == 3 && equals_ignore_case(argv[1], "preset"))
        {
            return handle_show_named_preset_command(argv[2]);
        }

        fprintf(stderr, "Usage: show preset [name]\n");
        return -1;
    }

    if (equals_ignore_case(argv[0], "do"))
    {
        if (argc == 2)
        {
            return handle_do_command(session, argv[1]);
        }

        fprintf(stderr, "Usage: do <file.wav>\n");
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
        if (argc == 2 && equals_ignore_case(argv[1], "random"))
        {
            return handle_preset_random_command(session, NULL);
        }

        if (argc == 3 && equals_ignore_case(argv[1], "random"))
        {
            return handle_preset_random_command(session, argv[2]);
        }

        if (argc == 3 && equals_ignore_case(argv[1], "load"))
        {
            return handle_preset_load_command(session, argv[2]);
        }

        if (argc == 3 && equals_ignore_case(argv[1], "save"))
        {
            return handle_preset_save_command(session, argv[2]);
        }

        fprintf(stderr, "Usage: preset random [randomness]\n");
        fprintf(stderr, "       preset load <name>\n");
        fprintf(stderr, "       preset save <name>\n");
        return -1;
    }

    fprintf(stderr, "Unknown command.\n");
    print_usage();
    return -1;
}

static int run_interactive_console(Session *session)
{
    CommandHistory history;
    char line[MAX_COMMAND_LENGTH];
    int result = 0;

    initialize_command_history(&history);

    printf("%s\n", PROGRAM_NAME);
    printf("Type 'help' for commands and 'exit' or 'quit' to leave.\n");

    while (1)
    {
        char *tokens[MAX_COMPLETION_TOKENS];
        int token_count;
        char *command;

        if (read_interactive_line(line, sizeof(line), session, &history) <= 0)
        {
            break;
        }

        command = trim_whitespace(line);

        if (*command == '\0')
        {
            continue;
        }

        if (add_command_history_entry(&history, command) != 0)
        {
            fprintf(stderr, "Warning: unable to store command history.\n");
        }

        if (equals_ignore_case(command, "exit") || equals_ignore_case(command, "quit"))
        {
            break;
        }

        token_count = split_command_line(command, tokens, MAX_COMPLETION_TOKENS);

        if (token_count == 0)
        {
            continue;
        }

        execute_tokens(session, token_count, tokens);
    }

    free_command_history(&history);
    return result;
}

static int run_direct_command_shortcut(Session *session, int argc, char **argv)
{
    if (argc <= 0 || argv == NULL)
    {
        return -1;
    }

    return execute_tokens(session, argc, argv);
}

static int run_cli(Session *session, int argc, char **argv)
{
    if (argc < 2)
    {
        return run_interactive_console(session);
    }

    return run_direct_command_shortcut(session, argc - 1, argv + 1);
}

int main(int argc, char **argv)
{
    Session session;
    int exit_code;

    if (initialize_sndfile_runtime() != 0)
    {
        print_sndfile_runtime_error();
        return 1;
    }

    init_session(&session);
    exit_code = run_cli(&session, argc, argv);
    free_session(&session);
    unload_sndfile_runtime();
    return exit_code == 0 ? 0 : 1;
}
