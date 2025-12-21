# JSON Utilities

Specialized JSON parsing and manipulation tools for repoWatch index.json structure and report files.

## Core API

### JSON Parsing
```c
json_value_t* json_parse_file(const char* filename);    // Parse JSON from file
json_value_t* json_parse_string(const char* json_str); // Parse JSON from string
void json_free(json_value_t* value);                   // Free parsed JSON
```

### Value Access
```c
json_value_t* get_nested_value(json_value_t* root, const char* key_path); // Dot notation: "config.ui_refresh_rate"
```

### JSON Writing
```c
char* json_stringify(json_value_t* value);             // Convert to JSON string
int json_write_file(const char* filename, json_value_t* value); // Write to file
```

### Creation Functions
```c
json_value_t* json_create_null();
json_value_t* json_create_bool(int value);
json_value_t* json_create_number(double value);
json_value_t* json_create_string(const char* value);
json_value_t* json_create_array();
json_value_t* json_create_object();
```

### Index.json Functions
```c
json_value_t* index_json_load(const char* path);        // Load index.json from directory
char** index_json_get_children(const char* path, size_t* count); // Get children array
int index_json_update_children(const char* path, char** children, size_t count); // Update children
```

## Command-Line Tools

### get-value
Extract values from index.json files using dot notation.
```bash
./get-value <key_path> [directory]
./get-value children .                    # Get children array
./get-value config.ui_refresh_rate        # Get nested config value
```

### get-children
Get children array from index.json.
```bash
./get-children [directory]
./get-children .                          # Space-separated list
```

### set-value
Write values to index.json (placeholder implementation).
```bash
./set-value <key_path> <value> [directory]
./set-value children '["git-status","git-tui"]' .
```

### read-report
Read repoWatch report files (JSON files at repoWatch root).
```bash
./read-report <report_file> [key_path]
./read-report git-submodules.report summary.total_dirty_repositories
./read-report dirty-files-report.json repositories.0.name
./read-report dirty-files-report.json     # Print entire file
```

### test-parse
Test JSON parsing functionality.
```bash
./test-parse
```

## Compilation

### Static Library
```bash
gcc -c json-utils.c -o json-utils.o
ar rcs libjson-utils.a json-utils.o
```

### Individual Tools
```bash
gcc -o get-value get-value.c json-utils.o -lm
gcc -o set-value set-value.c json-utils.o -lm
gcc -o get-children get-children.c json-utils.o -lm
gcc -o read-report read-report.c json-utils.o -lm
gcc -o test-parse test-parse.c json-utils.o -lm
```

### Linking Components
```bash
gcc -o component component.c ../json-utils/json-utils.o -lm
```

## JSON Types

```c
typedef enum {
    JSON_NULL, JSON_BOOL, JSON_NUMBER, JSON_STRING, JSON_ARRAY, JSON_OBJECT
} json_type_t;
```

## Usage Examples

### Parse and Access JSON
```c
json_value_t* root = json_parse_file("index.json");
json_value_t* value = get_nested_value(root, "config.ui_refresh_rate");

if (value && value->type == JSON_STRING) {
    printf("Rate: %s\n", value->value.str_val);
}

json_free(root);
```

### Create JSON Object
```c
json_value_t* obj = json_create_object();
json_value_t* str = json_create_string("value");
json_object_set(obj, "key", str);

// Write to file
json_write_file("output.json", obj);

json_free(obj);
```

### Work with Index.json
```c
size_t count;
char** children = index_json_get_children(".", &count);

for (size_t i = 0; i < count; i++) {
    printf("%s ", children[i]);
    free(children[i]);
}
free(children);
```

## File Structure

```
json-utils/
├── json-utils.h          # Main header with all APIs
├── json-utils.c          # Core JSON parsing/writing implementation
├── get-value.c          # Nested value extraction utility
├── set-value.c          # Nested value setting utility (placeholder)
├── get-children.c       # Index.json children array reader
├── read-report.c        # Report file reader
├── test-parse.c         # JSON parsing test utility
├── libjson-utils.a      # Static library
└── index.json           # Module configuration
```

## Dependencies

- Standard C library only
- No external JSON libraries required
- Thread-safe for read operations

## Error Handling

All functions return NULL on error. Check return values before use. Memory is managed automatically with `json_free()`.
