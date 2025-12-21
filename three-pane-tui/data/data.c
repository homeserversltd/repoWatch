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
    fprintf(stderr, "DEBUG: print_tree_node called with node='%s', depth=%d\n", node ? node->name : "NULL", depth);
    if (current_row >= max_row || !items || !item_count || !node || !node->name) {
        fprintf(stderr, "DEBUG: print_tree_node early return - invalid params or node\n");
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

    // Print node name (truncated if necessary)
    char truncated_name[256];
    size_t len = strlen(node->name);
    if (len <= max_width) {
        strcpy(truncated_name, node->name);
    } else {
        int copy_len = max_width - 3;
        if (copy_len < 1) copy_len = 1;
        strncpy(truncated_name, node->name, copy_len);
        truncated_name[copy_len] = '\0';
        strcat(truncated_name, "...");
    }

    buffer_pos += snprintf(buffer + buffer_pos, sizeof(buffer) - buffer_pos, "%s", truncated_name);

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
                                      "├── ", "└── ", "│   ", 60, 0, 1000,
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
                    // Truncate commit info if too long
                    const char* info = commit_info->value.str_val;
                    if (strlen(info) > 60) {
                        snprintf(commit_buffer, sizeof(commit_buffer), "└── %.60s...", info);
                    } else {
                        snprintf(commit_buffer, sizeof(commit_buffer), "└── %s", info);
                    }
                    orch->data.pane2_items[item_index++] = strdup(commit_buffer);
                }

                // Add files changed (skip submodules)
                if (files_changed && files_changed->type == JSON_ARRAY) {
                    for (size_t k = 0; k < files_changed->value.arr_val->count; k++) {
                        json_value_t* file = files_changed->value.arr_val->items[k];
                        if (file->type == JSON_STRING && !is_submodule(file->value.str_val, submodules, submodule_count)) {
                            char file_buffer[1024];
                            snprintf(file_buffer, sizeof(file_buffer), "    ├── %s", file->value.str_val);
                            orch->data.pane2_items[item_index++] = strdup(file_buffer);
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

// Read and parse dirty-files-report.json for pane 2
int load_dirty_files_data(three_pane_tui_orchestrator_t* orch) {
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

    // Count total dirty files across all repositories
    size_t total_files = 0;
    for (size_t i = 0; i < repos->value.arr_val->count; i++) {
        json_value_t* repo = repos->value.arr_val->items[i];
        if (repo->type != JSON_OBJECT) continue;

        json_value_t* files = get_nested_value(repo, "dirty_files");
        if (files && files->type == JSON_ARRAY) {
            total_files += files->value.arr_val->count;
        }
    }

    // Allocate space for all dirty files
    orch->data.pane2_count = total_files;
    orch->data.pane2_items = calloc(total_files, sizeof(char*));

    // Parse dirty files from each repository
    size_t file_index = 0;
    for (size_t i = 0; i < repos->value.arr_val->count; i++) {
        json_value_t* repo = repos->value.arr_val->items[i];
        if (repo->type != JSON_OBJECT) continue;

        json_value_t* repo_name = get_nested_value(repo, "name");
        json_value_t* files = get_nested_value(repo, "dirty_files");

        if (!files || files->type != JSON_ARRAY) continue;

        // Add each dirty file with repository prefix
        for (size_t j = 0; j < files->value.arr_val->count && file_index < total_files; j++) {
            json_value_t* file = files->value.arr_val->items[j];
            if (file->type == JSON_STRING) {
                char buffer[1024];
                if (repo_name && repo_name->type == JSON_STRING) {
                    snprintf(buffer, sizeof(buffer), "%s: %s",
                            repo_name->value.str_val, file->value.str_val);
                } else {
                    snprintf(buffer, sizeof(buffer), "%s", file->value.str_val);
                }
                orch->data.pane2_items[file_index] = strdup(buffer);
                file_index++;
            }
        }
    }

    json_free(report);
    return 0;
}

// Load hardcoded data for the third pane (right pane)
int load_hardcoded_data(three_pane_tui_orchestrator_t* orch) {
    // Right pane data (keeping this hardcoded as requested) - add more items for scrolling test
    orch->data.pane3_count = 20;
    orch->data.pane3_items = calloc(20, sizeof(char*));
    orch->data.pane3_items[0] = strdup("Live Feed of Changes");
    orch->data.pane3_items[1] = strdup("├── Modified files");
    orch->data.pane3_items[2] = strdup("├── New files");
    orch->data.pane3_items[3] = strdup("├── Deleted files");
    orch->data.pane3_items[4] = strdup("├── Renamed files");
    orch->data.pane3_items[5] = strdup("├── Untracked files");
    orch->data.pane3_items[6] = strdup("├── Staged changes");
    orch->data.pane3_items[7] = strdup("├── Commit history");
    orch->data.pane3_items[8] = strdup("├── Branch status");
    orch->data.pane3_items[9] = strdup("├── Remote status");
    orch->data.pane3_items[10] = strdup("├── Merge conflicts");
    orch->data.pane3_items[11] = strdup("├── Stash list");
    orch->data.pane3_items[12] = strdup("├── Tag list");
    orch->data.pane3_items[13] = strdup("├── Submodule status");
    orch->data.pane3_items[14] = strdup("├── Worktree list");
    orch->data.pane3_items[15] = strdup("├── Reflog entries");
    orch->data.pane3_items[16] = strdup("├── Config changes");
    orch->data.pane3_items[17] = strdup("├── Hook status");
    orch->data.pane3_items[18] = strdup("├── LFS objects");
    orch->data.pane3_items[19] = strdup("└── Build artifacts");

    return 0;
}
