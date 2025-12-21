#include "../three-pane-tui.h"

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
int load_committed_not_pushed_data(three_pane_tui_orchestrator_t* orch) {
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
    // Right pane data (keeping this hardcoded as requested)
    orch->data.pane3_count = 6;
    orch->data.pane3_items = calloc(6, sizeof(char*));
    orch->data.pane3_items[0] = strdup("Right 1");
    orch->data.pane3_items[1] = strdup("Right 2");
    orch->data.pane3_items[2] = strdup("Right 3");
    orch->data.pane3_items[3] = strdup("Right 4");
    orch->data.pane3_items[4] = strdup("Right 5");
    orch->data.pane3_items[5] = strdup("Right 6");

    return 0;
}
