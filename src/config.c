#include "config.h"
#include "stream.h"

#define MAX_CONFIG_TABLES 64
#define CONFIG_TABLE_LEN 256

struct psx_config g_config;

static void load_default_config(void)
{

}

typedef enum config_token_type
{
    INVALID,
    LBRACKET,
    RBRACKET,
    NUMBER,
    IDENTIFIER,
    EQUALS,
    NEWLINE
} config_token_type;

typedef struct config_token
{
    const char *value;
    u32 length;
    config_token_type type;
} config_token;

struct config_table_entry
{
    u32 key_len;
    u32 value_len;
    const char *key;
    const char *value;
};

struct config_table
{
    const char *name;
    u32 name_length;
    u32 table_length;
    struct config_table_entry entries[CONFIG_TABLE_LEN];
};

typedef struct config_parser
{
    const char *at;
    config_token token;
    struct config_table *hash_table;
    struct config_table *current_table;
    b8 done;
} config_parser;

static inline b8 is_alpha(char c)
{
    return ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_');
}

static inline b8 is_digit(char c)
{
    return (c >= '0' && c <= '9');
}

static u32 get_identifier_len(config_parser *parser)
{
    const char *p = parser->at;
    char c;
    do
    {
        c = *++p;
    } while (is_alpha(c) || is_digit(c));
    u32 length = p - parser->at;
    return length;
}

static config_token consume_token(config_parser *parser, u32 length, config_token_type type)
{
    config_token result;
    result.value = parser->at;
    result.length = length;
    result.type = type;

    parser->token = result;
    parser->at += length;

    return result;
}

static inline b8 is_space(char c)
{
    return (c == ' ' || c == '\t' || c == '\r');
}

static inline b8 is_eol(char c)
{
    return (c == '\n' || c == '\r');
}

static config_token next_token(config_parser *parser)
{
    while (*parser->at)
    {
        char c = *parser->at;
        if (is_space(c))
        {
            ++parser->at;
        }
        else if (c == '#')
        {
            do
            {
                ++parser->at;
            } while (*parser->at != '\n');
        }
        else if (is_alpha(c))
        {
            u32 len = get_identifier_len(parser);
            return consume_token(parser, len, IDENTIFIER);
        }
        else if (c == '=')
        {
            return consume_token(parser, 1, EQUALS);
        }
        else if (c == '\n')
        {
            return consume_token(parser, 1, NEWLINE);
        }
        else if (c == '[')
        {
            return consume_token(parser, 1, LBRACKET);
        }
        else if (c == ']')
        {
            return consume_token(parser, 1, RBRACKET);
        }
        else
        {
            printf("'%c' was unexpected.\n", c);
            return consume_token(parser, 1, INVALID);
        }
    }
    parser->done = true;
    return parser->token;
}

static b8 expect_token(config_parser *parser, config_token_type type)
{
    if (next_token(parser).type == type) 
    {
        return true;
    }
    else 
    {
        parser->done = true;
        return false;
    }
}

static inline struct config_table *put_table(struct config_table *hash_table, const char *key, u32 length)
{
    u32 hash = fnv1a_n(key, length);
    u32 start_index = hash & (MAX_CONFIG_TABLES - 1);
    u32 index = start_index;
loop:
    struct config_table *table = &hash_table[index];
    if (table->name)
    {
        if (strncmp(key, table->name, length) == 0)
        {
            return NULL;
        }
        index = (index + 1) & (MAX_CONFIG_TABLES - 1);
        if (index == start_index)
        {
            return NULL;
        }
        goto loop;
    }
    table->name = key;
    table->name_length = length;
    return table;
}

static inline struct config_table *get_table(struct config_table *hash_table, const char *key)
{
    u32 hash = fnv1a(key);
    u32 start_index = hash & (MAX_CONFIG_TABLES - 1);
    u32 index = start_index;
loop:
    struct config_table *table = &hash_table[index];
    if (table->name)
    {
        if (strncmp(key, table->name, table->name_length) == 0)
        {
            return table;
        }
        index = (index + 1) & (MAX_CONFIG_TABLES - 1);
        if (index == start_index)
        {
            return NULL;
        }
        goto loop;
    }
    return NULL;
}

static const char *parser_get_value(config_parser *parser, u32 *len)
{
    while (is_space(*parser->at))
        ++parser->at;

    const char *start = parser->at;
    while (!is_eol(*parser->at) && *parser->at)
        ++parser->at;
    *len = parser->at - start;
    return start;
}

static struct config_table_entry *table_find(struct config_table *table, const char *key, u32 key_len)
{
    for (u32 i = 0; i < table->table_length; ++i)
    {
        if (strncmp(key, table->entries[i].key, key_len) == 0)
        {
            return &table->entries[i];
        }
    }
    return NULL;
}

static b8 parse_table(config_parser *parser)
{
    if (expect_token(parser, IDENTIFIER))
    {
        config_token key = parser->token;
        if (expect_token(parser, RBRACKET))
        {
            if (expect_token(parser, NEWLINE) || parser->done)
            {
                parser->current_table = put_table(parser->hash_table, key.value, key.length);
                if (parser->current_table)
                {
                    return true;
                }
                printf("failed to place table: %.*s\n", key.length, key.value);
            }
        }
    }
    return false;
}

static struct config_table *parse_config(const char *contents)
{
    config_parser parser = {.at = contents};
    parser.hash_table = malloc(sizeof(struct config_table) * MAX_CONFIG_TABLES);
    
    while (!parser.done)
    {
        config_token token = next_token(&parser);

        switch (token.type)
        {
        case LBRACKET:
        {
            if (!parse_table(&parser))
                goto error;
            break;
        }
        case IDENTIFIER:
        {
            if (parser.current_table)
            {
                config_token key = parser.token;
                if (table_find(parser.current_table, key.value, key.length))
                {
                    printf("key: %.*s, duplicate found.\n", key.length, key.value);
                    goto error;
                }
                if (expect_token(&parser, EQUALS))
                {
                    u32 len;
                    const char *value = parser_get_value(&parser, &len);
                    if (!len)
                    {
                        printf("key/value pair must not be empty.\n");
                        goto error;
                    }
                    if (parser.current_table->table_length >= CONFIG_TABLE_LEN)
                    {
                        printf("cannot add more than 256 entries in a table.\n");
                        goto error;
                    }
                    struct config_table_entry *entry = &parser.current_table->entries[parser.current_table->table_length++];
                    entry->key = key.value;
                    entry->key_len = key.length;
                    entry->value = value;
                    entry->value_len = len;
                }
            }
            break;
        }
        case NEWLINE:
        {
            break;
        }
        default:
            printf("unexpected token: '%.*s'\n", token.length, token.value);
            parser.done = true;
            break;
        }
    }

    return parser.hash_table;

error:
    free(parser.hash_table);
    return NULL;
}

static b8 key_equals(struct config_table_entry *entry, const char *key)
{
    return (strncmp(entry->key, key, entry->key_len) == 0);
}

static b8 config_get_bool(struct config_table_entry *entry, b8 *result)
{
    if (strncmp(entry->value, "true", entry->value_len) == 0)
    {
        *result = true;
    }
    else if (strncmp(entry->value, "false", entry->value_len) == 0)
    {
        *result = false;
    }
    else
    {
        return false;
    }
    return true;
}

b8 load_config(void)
{
    load_default_config();
    struct file_dat file;
    if (allocate_and_read_file_null_terminated("sunny.cfg", &file))
    {
        struct config_table *table = parse_config(file.memory);
        if (!table)
            return false;

        struct config_table *settings = get_table(table, "settings");
        if (settings)
        {
            for (u32 i = 0; i < settings->table_length; ++i)
            {
                struct config_table_entry *entry = &settings->entries[i];
                if (key_equals(entry, "bios_path"))
                {
                    strncpy_s(g_config.bios_path, MAX_CONFIG_PATH, entry->value, entry->value_len);
                }
                else if (key_equals(entry, "boot_file"))
                {
                    strncpy_s(g_config.boot_file, MAX_CONFIG_PATH, entry->value, entry->value_len);
                }
                else if (key_equals(entry, "software_rendering"))
                {
                    config_get_bool(entry, &g_config.software_rendering);
                }
            }
        }

        free(table);
        return true;
    }
    return false;
}
