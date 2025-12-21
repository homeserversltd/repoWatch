#include "../three-pane-tui.h"

// Tree node structure for hierarchical display
typedef struct tree_node {
    char* name;
    struct tree_node** children;
    size_t child_count;
    int is_file;
} tree_node_t;

// Helper function to check if a filename is a known submodule
static int is_submodule(const char* filename, char** submodules, size_t submodule_count) {
    if (!filename || !submodules) return 0;
    for (size_t i = 0; i < submodule_count; i++) {
        if (strcmp(filename, submodules[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

// Cleanup tree node recursively
static void cleanup_tree_node(tree_node_t* node) {
    if (!node) return;

    for (size_t i = 0; i < node->child_count; i++) {
        cleanup_tree_node(node->children[i]);
    }

    free(node->children);
    free(node->name);
    free(node);
}

// Build file tree from flat file paths
static tree_node_t* build_file_tree(char** files, size_t file_count) {
    tree_node_t* root = calloc(1, sizeof(tree_node_t));
    if (!root) return NULL;

    root->name = strdup("/");
    root->is_file = 0;

    for (size_t i = 0; i < file_count; i++) {
        const char* path = files[i];
        tree_node_t* current = root;

        // Skip leading slash
        if (path[0] == '/') path++;

        char* path_copy = strdup(path);
        char* token = strtok(path_copy, "/");

        while (token) {
            // Check if child already exists
            tree_node_t* child = NULL;
            for (size_t j = 0; j < current->child_count; j++) {
                if (strcmp(current->children[j]->name, token) == 0) {
                    child = current->children[j];
                    break;
                }
            }

            // Create new child if not found
            if (!child) {
                child = calloc(1, sizeof(tree_node_t));
                child->name = strdup(token);

                // Check if this is the last token (file) or not (directory)
                char* next_token = strtok(NULL, "/");
                child->is_file = (next_token == NULL);

                // Put token back for proper processing
                if (next_token) {
                    // This is a directory, put the token back
                    char* temp = strtok(NULL, ""); // Get rest of string
                    if (temp) {
                        char* full_rest = malloc(strlen(next_token) + strlen(temp) + 2);
                        sprintf(full_rest, "%s/%s", next_token, temp);
                        strtok(full_rest, "/"); // Reset strtok state
                        free(full_rest);
                    }
                }

                // Add to parent's children
                current->children = realloc(current->children, (current->child_count + 1) * sizeof(tree_node_t*));
                current->children[current->child_count] = child;
                current->child_count++;
            }

            current = child;

            // Get next token only if we haven't already processed it
            if (!child->is_file) {
                token = strtok(NULL, "/");
            } else {
                token = NULL;
            }
        }

        free(path_copy);
    }

    return root;
}

// Print tree node with proper indentation
static void print_tree_node(tree_node_t* node, int depth, int is_last, const char* prefix, const char* last_prefix, const char* indent, int max_width, int current_row, int max_row, char*** items, size_t* item_count) {
    if (current_row >= max_row || !items || !item_count || !node || !node->name) {
        return;
    }

    // Print indentation
    char buffer[4096] = {0};
    int buffer_pos = 0;

    for (int i = 0; i < depth; i++) {
        buffer_pos += snprintf(buffer + buffer_pos, sizeof(buffer) - buffer_pos, "%s", indent);
    }

    // Print tree prefix
    if (depth > 0) {
        if (is_last) {
            buffer_pos += snprintf(buffer + buffer_pos, sizeof(buffer) - buffer_pos, "%s", last_prefix);
        } else {
            buffer_pos += snprintf(buffer + buffer_pos, sizeof(buffer) - buffer_pos, "%s", prefix);
        }
    }

    // Calculate available width for filename (accounting for indentation and tree prefix)
    int indent_width = depth * strlen(indent); // Rough estimate for indentation
    int prefix_width = (depth > 0) ? strlen(is_last ? last_prefix : prefix) : 0;
    int used_width = indent_width + prefix_width;
    int available_width = max_width - used_width;

    if (available_width <= 0) available_width = 10; // Minimum fallback

    // Get truncated name using glyph-aware right-priority truncation
    char* display_name = truncate_string_right_priority(node->name, available_width);

    buffer_pos += snprintf(buffer + buffer_pos, sizeof(buffer) - buffer_pos, "%s", display_name);

    free(display_name);

    // Add to items array
    *items = realloc(*items, (*item_count + 1) * sizeof(char*));
    (*items)[*item_count] = strdup(buffer);
    (*item_count)++;
    current_row++;

    // Print children
    for (size_t i = 0; i < node->child_count; i++) {
        int child_is_last = (i == node->child_count - 1);
        print_tree_node(node->children[i], depth + 1, child_is_last, prefix, last_prefix, indent, max_width, current_row, max_row, items, item_count);
    }
}

// Read and parse git-submodules.report for pane 1
int load_git_submodules_data(three_pane_tui_orchestrator_t* orch) {
    json_value_t* report = json_parse_file("git-submodules.report");
    if (!report || report->type != JSON_OBJECT) {
        fprintf(stderr, "Failed to load git-submodules.report\n");
        return -1;
    }

    // Get repositories array
    json_value_t* repos = get_nested_value(report, "repositories");
    if (!repos || repos->type != JSON_ARRAY) {
        fprintf(stderr, "No repositories found in report\n");
        json_free(report);
        return -1;
    }

    // Allocate space for repository data
    orch->data.pane1_count = repos->value.arr_val->count;
    orch->data.pane1_items = calloc(orch->data.pane1_count, sizeof(char*));

    // Parse each repository
    for (size_t i = 0; i < repos->value.arr_val->count; i++) {
        json_value_t* repo = repos->value.arr_val->items[i];
        if (repo->type != JSON_OBJECT) continue;

        // Get repository name and status
        json_value_t* name = get_nested_value(repo, "name");
        json_value_t* status = get_nested_value(repo, "status");
        json_value_t* changes = get_nested_value(repo, "changes");

        // Format repository info
        char buffer[1024];
        if (name && name->type == JSON_STRING &&
            status && status->type == JSON_STRING) {
            if (changes && changes->type == JSON_STRING && strlen(changes->value.str_val) > 0) {
                // Show first line of changes if available
                char* newline_pos = strchr(changes->value.str_val, '\n');
                if (newline_pos) {
                    *newline_pos = '\0';
                }
                snprintf(buffer, sizeof(buffer), "%s [%s]: %s",
                        name->value.str_val, status->value.str_val, changes->value.str_val);
            } else {
                snprintf(buffer, sizeof(buffer), "%s [%s]",
                        name->value.str_val, status->value.str_val);
            }
        } else {
            snprintf(buffer, sizeof(buffer), "Unknown repo");
        }

        orch->data.pane1_items[i] = strdup(buffer);
    }

    json_free(report);
    return 0;
}

// Read and parse committed-not-pushed-report.json for pane 2
int load_committed_not_pushed_data(three_pane_tui_orchestrator_t* orch, view_mode_t view_mode) {
    // Clean up old pane2 data first
    for (size_t i = 0; i < orch->data.pane2_count; i++) {
        if (orch->data.pane2_items[i]) {
            free(orch->data.pane2_items[i]);
        }
    }
    free(orch->data.pane2_items);
    orch->data.pane2_items = NULL;
    orch->data.pane2_count = 0;

    // First, read git-submodules.report to get list of known submodules to filter out
    char** submodules = NULL;
    size_t submodule_count = 0;

    json_value_t* submodules_report = json_parse_file("git-submodules.report");
    if (submodules_report && submodules_report->type == JSON_OBJECT) {
        json_value_t* repos = get_nested_value(submodules_report, "repositories");
        if (repos && repos->type == JSON_ARRAY) {
            // Count non-root repositories (submodules)
            for (size_t i = 0; i < repos->value.arr_val->count; i++) {
                json_value_t* repo = repos->value.arr_val->items[i];
                if (repo->type != JSON_OBJECT) continue;
                json_value_t* name = get_nested_value(repo, "name");
                if (name && name->type == JSON_STRING && strcmp(name->value.str_val, "root") != 0) {
                    submodule_count++;
                }
            }

            // Allocate and populate submodules array
            submodules = calloc(submodule_count, sizeof(char*));
            size_t sub_idx = 0;
            for (size_t i = 0; i < repos->value.arr_val->count; i++) {
                json_value_t* repo = repos->value.arr_val->items[i];
                if (repo->type != JSON_OBJECT) continue;
                json_value_t* name = get_nested_value(repo, "name");
                if (name && name->type == JSON_STRING && strcmp(name->value.str_val, "root") != 0) {
                    submodules[sub_idx++] = strdup(name->value.str_val);
                }
            }
        }
    }

    json_value_t* report = json_parse_file("committed-not-pushed-report.json");
    if (!report || report->type != JSON_OBJECT) {
        fprintf(stderr, "Error: Cannot parse committed-not-pushed-report.json\n");
        if (submodules_report) json_free(submodules_report);
        return 1;
    }

    json_value_t* repos = get_nested_value(report, "repositories");
    if (!repos || repos->type != JSON_ARRAY) {
        fprintf(stderr, "Error: No repositories array in committed-not-pushed-report.json\n");
        json_free(report);
        if (submodules_report) json_free(submodules_report);
        return 1;
    }

    // For tree view, collect all files per repository and build trees
    if (view_mode == VIEW_TREE) {
        // Count repositories with commits
        size_t repo_with_commits_count = 0;
        for (size_t i = 0; i < repos->value.arr_val->count; i++) {
            json_value_t* repo = repos->value.arr_val->items[i];
            if (repo->type != JSON_OBJECT) continue;
            json_value_t* commits = get_nested_value(repo, "unpushed_commits");
            if (commits && commits->type == JSON_ARRAY && commits->value.arr_val->count > 0) {
                repo_with_commits_count++;
            }
        }

        orch->data.pane2_count = 0;
        orch->data.pane2_items = NULL;

        // Process each repository with commits
        for (size_t i = 0; i < repos->value.arr_val->count; i++) {
            json_value_t* repo = repos->value.arr_val->items[i];
            if (repo->type != JSON_OBJECT) continue;

            json_value_t* repo_name = get_nested_value(repo, "name");
            json_value_t* commits = get_nested_value(repo, "unpushed_commits");

            if (!repo_name || repo_name->type != JSON_STRING) continue;
            if (!commits || commits->type != JSON_ARRAY || commits->value.arr_val->count == 0) continue;

            // Get repository name
            json_value_t* repo_path = get_nested_value(repo, "path");
            const char* display_name = repo_name->value.str_val;
            if (repo_path && repo_path->type == JSON_STRING) {
                const char* path = repo_path->value.str_val;
                const char* repo_name_from_path = strrchr(path, '/');
                if (repo_name_from_path) {
                    display_name = repo_name_from_path + 1;
                }
            }

            // Add repository header
            char header_buffer[512];
            snprintf(header_buffer, sizeof(header_buffer), "Repository: %s", display_name);
            orch->data.pane2_items = realloc(orch->data.pane2_items, (orch->data.pane2_count + 1) * sizeof(char*));
            orch->data.pane2_items[orch->data.pane2_count] = strdup(header_buffer);
            orch->data.pane2_count++;

            // Collect all files from all commits in this repository
            char** repo_files = NULL;
            size_t repo_file_count = 0;

            for (size_t j = 0; j < commits->value.arr_val->count; j++) {
                json_value_t* commit = commits->value.arr_val->items[j];
                if (commit->type != JSON_OBJECT) continue;

                json_value_t* files_changed = get_nested_value(commit, "files_changed");
                if (files_changed && files_changed->type == JSON_ARRAY) {
                    for (size_t k = 0; k < files_changed->value.arr_val->count; k++) {
                        json_value_t* file = files_changed->value.arr_val->items[k];
                        if (file->type == JSON_STRING && !is_submodule(file->value.str_val, submodules, submodule_count)) {
                            repo_files = realloc(repo_files, (repo_file_count + 1) * sizeof(char*));
                            repo_files[repo_file_count] = strdup(file->value.str_val);
                            repo_file_count++;
                        }
                    }
                }
            }

            // Build file tree for this repository
            if (repo_file_count > 0) {
                tree_node_t* file_tree = build_file_tree(repo_files, repo_file_count);
                if (file_tree && file_tree->child_count > 0) {
                    // Print tree nodes (with indentation for the repository)
                    for (size_t j = 0; j < file_tree->child_count; j++) {
                        int is_last = (j == file_tree->child_count - 1);
                        print_tree_node(file_tree->children[j], 0, is_last,
                                      "├── ", "└── ", "│   ", 256, 0, 1000,
                                      &orch->data.pane2_items, &orch->data.pane2_count);
                    }
                }
                cleanup_tree_node(file_tree);

                // Cleanup repo files
                for (size_t j = 0; j < repo_file_count; j++) {
                    free(repo_files[j]);
                }
                free(repo_files);
            }
        }
    } else {
        // Original flat view logic
        // Count total items needed (repository headers + commits + non-submodule files)
        size_t total_items = 0;
        for (size_t i = 0; i < repos->value.arr_val->count; i++) {
            json_value_t* repo = repos->value.arr_val->items[i];
            if (repo->type != JSON_OBJECT) continue;

            json_value_t* commits = get_nested_value(repo, "unpushed_commits");
            if (commits && commits->type == JSON_ARRAY) {
                total_items += 1; // Repository header
                for (size_t j = 0; j < commits->value.arr_val->count; j++) {
                    json_value_t* commit = commits->value.arr_val->items[j];
                    if (commit->type != JSON_OBJECT) continue;
                    total_items += 1; // Commit info
                    json_value_t* files = get_nested_value(commit, "files_changed");
                    if (files && files->type == JSON_ARRAY) {
                        // Count only non-submodule files
                        for (size_t k = 0; k < files->value.arr_val->count; k++) {
                            json_value_t* file = files->value.arr_val->items[k];
                            if (file->type == JSON_STRING && !is_submodule(file->value.str_val, submodules, submodule_count)) {
                                total_items++;
                            }
                        }
                    }
                }
            }
        }

        orch->data.pane2_count = total_items;
        orch->data.pane2_items = calloc(total_items, sizeof(char*));

        // Parse committed not pushed data from each repository
        size_t item_index = 0;
        for (size_t i = 0; i < repos->value.arr_val->count; i++) {
            json_value_t* repo = repos->value.arr_val->items[i];
            if (repo->type != JSON_OBJECT) continue;

            json_value_t* repo_name = get_nested_value(repo, "name");
            json_value_t* commits = get_nested_value(repo, "unpushed_commits");

            if (!repo_name || repo_name->type != JSON_STRING) continue;
            if (!commits || commits->type != JSON_ARRAY) continue;

            // Add repository header - use actual repo name from path if available
            json_value_t* repo_path = get_nested_value(repo, "path");
            char header_buffer[512];
            if (repo_path && repo_path->type == JSON_STRING) {
                // Extract repo name from path (last component after '/')
                const char* path = repo_path->value.str_val;
                const char* repo_name_from_path = strrchr(path, '/');
                if (repo_name_from_path) {
                    repo_name_from_path++; // Skip the '/'
                    snprintf(header_buffer, sizeof(header_buffer), "Repository: %s", repo_name_from_path);
                } else {
                    snprintf(header_buffer, sizeof(header_buffer), "Repository: %s", repo_name->value.str_val);
                }
            } else {
                snprintf(header_buffer, sizeof(header_buffer), "Repository: %s", repo_name->value.str_val);
            }
            orch->data.pane2_items[item_index++] = strdup(header_buffer);

            // Add each commit and its files
            for (size_t j = 0; j < commits->value.arr_val->count; j++) {
                json_value_t* commit = commits->value.arr_val->items[j];
                if (commit->type != JSON_OBJECT) continue;

                json_value_t* commit_info = get_nested_value(commit, "commit_info");
                json_value_t* files_changed = get_nested_value(commit, "files_changed");

                // Add commit info
                if (commit_info && commit_info->type == JSON_STRING) {
                    char commit_buffer[1024];
                    // Truncate commit info using glyph-aware approach
                    const char* info = commit_info->value.str_val;
                    char* truncated_commit = truncate_string_right_priority(info, 60 - 4); // 4 for "└── "
                    snprintf(commit_buffer, sizeof(commit_buffer), "└── %s", truncated_commit);
                    free(truncated_commit);
                    orch->data.pane2_items[item_index++] = strdup(commit_buffer);
                }

                // Add files changed (skip submodules)
                if (files_changed && files_changed->type == JSON_ARRAY) {
                    for (size_t k = 0; k < files_changed->value.arr_val->count; k++) {
                        json_value_t* file = files_changed->value.arr_val->items[k];
                        if (file->type == JSON_STRING && !is_submodule(file->value.str_val, submodules, submodule_count)) {
                        // For FLAT view, just show the filename without tree prefixes
                        orch->data.pane2_items[item_index++] = strdup(file->value.str_val);
                        }
                    }
                }
            }
        }
    }

    json_free(report);
    if (submodules_report) json_free(submodules_report);

    // Cleanup submodules array
    if (submodules) {
        for (size_t i = 0; i < submodule_count; i++) {
            free(submodules[i]);
        }
        free(submodules);
    }

    return 0;
}

// Read and parse dirty-files-report.json for pane 1
int load_dirty_files_data(three_pane_tui_orchestrator_t* orch, view_mode_t view_mode) {
    // Clean up old pane1 data first
    for (size_t i = 0; i < orch->data.pane1_count; i++) {
        if (orch->data.pane1_items[i]) {
            free(orch->data.pane1_items[i]);
        }
    }
    free(orch->data.pane1_items);
    orch->data.pane1_items = NULL;
    orch->data.pane1_count = 0;

    json_value_t* report = json_parse_file("dirty-files-report.json");
    if (!report || report->type != JSON_OBJECT) {
        fprintf(stderr, "Failed to load dirty-files-report.json\n");
        return -1;
    }

    // Get repositories array
    json_value_t* repos = get_nested_value(report, "repositories");
    if (!repos || repos->type != JSON_ARRAY) {
        fprintf(stderr, "No repositories found in dirty-files report\n");
        json_free(report);
        return -1;
    }

    // For tree view, collect all files per repository and build trees
    if (view_mode == VIEW_TREE) {
        orch->data.pane1_count = 0;
        orch->data.pane1_items = NULL;

        // Process each repository with dirty files
        for (size_t i = 0; i < repos->value.arr_val->count; i++) {
            json_value_t* repo = repos->value.arr_val->items[i];
            if (repo->type != JSON_OBJECT) continue;

            json_value_t* repo_name = get_nested_value(repo, "name");
            json_value_t* files = get_nested_value(repo, "dirty_files");

            if (!repo_name || repo_name->type != JSON_STRING) continue;
            if (!files || files->type != JSON_ARRAY || files->value.arr_val->count == 0) continue;

            // Get repository name
            json_value_t* repo_path = get_nested_value(repo, "path");
            const char* display_name = repo_name->value.str_val;
            if (repo_path && repo_path->type == JSON_STRING) {
                const char* path = repo_path->value.str_val;
                const char* repo_name_from_path = strrchr(path, '/');
                if (repo_name_from_path) {
                    display_name = repo_name_from_path + 1;
                }
            }

            // Add repository header
            char header_buffer[512];
            snprintf(header_buffer, sizeof(header_buffer), "Repository: %s", display_name);
            orch->data.pane1_items = realloc(orch->data.pane1_items, (orch->data.pane1_count + 1) * sizeof(char*));
            orch->data.pane1_items[orch->data.pane1_count] = strdup(header_buffer);
            orch->data.pane1_count++;

            // Collect all files from this repository
            char** repo_files = NULL;
            size_t repo_file_count = 0;

            for (size_t j = 0; j < files->value.arr_val->count; j++) {
                json_value_t* file = files->value.arr_val->items[j];
                if (file->type == JSON_STRING) {
                    repo_files = realloc(repo_files, (repo_file_count + 1) * sizeof(char*));
                    repo_files[repo_file_count] = strdup(file->value.str_val);
                    repo_file_count++;
                }
            }

            // Build file tree for this repository
            if (repo_file_count > 0) {
                tree_node_t* file_tree = build_file_tree(repo_files, repo_file_count);
                if (file_tree && file_tree->child_count > 0) {
                    // Print tree nodes (with indentation for the repository)
                    for (size_t j = 0; j < file_tree->child_count; j++) {
                        int is_last = (j == file_tree->child_count - 1);
                        print_tree_node(file_tree->children[j], 0, is_last,
                                      "├── ", "└── ", "│   ", 256, 0, 1000,
                                      &orch->data.pane1_items, &orch->data.pane1_count);
                    }
                }
                cleanup_tree_node(file_tree);

                // Cleanup repo files
                for (size_t j = 0; j < repo_file_count; j++) {
                    free(repo_files[j]);
                }
                free(repo_files);
            }
        }
    } else {
        // Flat view mode - group files by repository with headers
        // Count total items needed (repository headers + files per repository)
        size_t total_items = 0;
        for (size_t i = 0; i < repos->value.arr_val->count; i++) {
            json_value_t* repo = repos->value.arr_val->items[i];
            if (repo->type != JSON_OBJECT) continue;

            json_value_t* files = get_nested_value(repo, "dirty_files");
            if (files && files->type == JSON_ARRAY && files->value.arr_val->count > 0) {
                total_items += 1; // Repository header
                total_items += files->value.arr_val->count; // File items
            }
        }

        // Allocate space for all items
        orch->data.pane1_count = total_items;
        orch->data.pane1_items = calloc(total_items, sizeof(char*));

        // Parse dirty files from each repository, grouped by repository
        size_t item_index = 0;
        for (size_t i = 0; i < repos->value.arr_val->count; i++) {
            json_value_t* repo = repos->value.arr_val->items[i];
            if (repo->type != JSON_OBJECT) continue;

            json_value_t* repo_name = get_nested_value(repo, "name");
            json_value_t* files = get_nested_value(repo, "dirty_files");

            if (!files || files->type != JSON_ARRAY || files->value.arr_val->count == 0) continue;

            // Get repository name
            json_value_t* repo_path = get_nested_value(repo, "path");
            const char* display_name = repo_name && repo_name->type == JSON_STRING ? repo_name->value.str_val : "unknown";
            if (repo_path && repo_path->type == JSON_STRING) {
                const char* path = repo_path->value.str_val;
                const char* repo_name_from_path = strrchr(path, '/');
                if (repo_name_from_path) {
                    display_name = repo_name_from_path + 1;
                }
            }

            // Add repository header
            char header_buffer[512];
            snprintf(header_buffer, sizeof(header_buffer), "Repository: %s", display_name);
            orch->data.pane1_items[item_index++] = strdup(header_buffer);

            // Add each dirty file (plain filename, no repo prefix)
            for (size_t j = 0; j < files->value.arr_val->count; j++) {
                json_value_t* file = files->value.arr_val->items[j];
                if (file->type == JSON_STRING && item_index < total_items) {
                    // Store just the filename without repository prefix
                    orch->data.pane1_items[item_index++] = strdup(file->value.str_val);
                }
            }
        }
    }

    json_free(report);
    return 0;
}

// active_file_info_t is defined in three-pane-tui.h

// Load file changes data from file-changes-report.json and return active files info
active_file_info_t* load_file_changes_data(size_t* active_count) {
    json_value_t* report = json_parse_file("file-changes-report.json");
    if (!report || report->type != JSON_OBJECT) {
        fprintf(stderr, "Failed to load file-changes-report.json\n");
        *active_count = 0;
        return NULL;
    }

    json_value_t* files = get_nested_value(report, "files");
    if (!files || files->type != JSON_ARRAY) {
        fprintf(stderr, "No files found in file-changes-report.json\n");
        json_free(report);
        *active_count = 0;
        return NULL;
    }

    time_t now = time(NULL);
    size_t count = 0;

    // Count active files (within last 30 seconds)
    for (size_t i = 0; i < files->value.arr_val->count; i++) {
        json_value_t* file_obj = files->value.arr_val->items[i];
        if (file_obj->type != JSON_OBJECT) continue;

        json_value_t* last_updated = get_nested_value(file_obj, "last_updated");
        if (last_updated && last_updated->type == JSON_NUMBER) {
            time_t updated_time = (time_t)last_updated->value.num_val;
            if (now - updated_time < 30) {  // Active within 30 seconds
                count++;
            }
        }
    }

    active_file_info_t* active_files = NULL;
    if (count > 0) {
        active_files = calloc(count, sizeof(active_file_info_t));
        if (!active_files) {
            json_free(report);
            *active_count = 0;
            return NULL;
        }
    }

    *active_count = 0;

    // Collect active file information
    for (size_t i = 0; i < files->value.arr_val->count; i++) {
        json_value_t* file_obj = files->value.arr_val->items[i];
        if (file_obj->type != JSON_OBJECT) continue;

        json_value_t* path = get_nested_value(file_obj, "path");
        json_value_t* last_updated = get_nested_value(file_obj, "last_updated");

        if (path && path->type == JSON_STRING &&
            last_updated && last_updated->type == JSON_NUMBER) {

            time_t updated_time = (time_t)last_updated->value.num_val;
            if (now - updated_time < 30) {  // Active within 30 seconds
                active_file_info_t* info = &active_files[*active_count];
                info->path = strdup(path->value.str_val);
                info->last_updated = updated_time;
                (*active_count)++;
            }
        }
    }

    json_free(report);
    return active_files;
}

