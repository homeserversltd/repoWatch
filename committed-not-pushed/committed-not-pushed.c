#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <regex.h>
#include <time.h>
#include "../json-utils/json-utils.h"

// Structure for committed-not-pushed information
typedef struct {
    char* repo_path;
    char* repo_name;
    char** unpushed_commits;
    size_t commit_count;
    size_t commit_capacity;
} unpushed_repo_t;

// Collection of repositories with unpushed commits
typedef struct {
    unpushed_repo_t* repos;
    size_t count;
    size_t capacity;
    char** submodule_paths;  // List of submodule paths to filter out
    size_t submodule_count;
} unpushed_collection_t;

// Initialize unpushed collection
unpushed_collection_t* unpushed_collection_init() {
    unpushed_collection_t* collection = calloc(1, sizeof(unpushed_collection_t));
    if (!collection) return NULL;

    collection->capacity = 8;
    collection->repos = calloc(collection->capacity, sizeof(unpushed_repo_t));
    if (!collection->repos) {
        free(collection);
        return NULL;
    }

    collection->submodule_paths = NULL;
    collection->submodule_count = 0;

    return collection;
}

// Check if a path is a submodule
int is_submodule_path(unpushed_collection_t* collection, const char* path) {
    for (size_t i = 0; i < collection->submodule_count; i++) {
        if (strcmp(collection->submodule_paths[i], path) == 0) {
            return 1;
        }
    }
    return 0;
}

// Add submodule path to filter list
void add_submodule_path(unpushed_collection_t* collection, const char* path) {
    collection->submodule_paths = realloc(collection->submodule_paths,
                                         (collection->submodule_count + 1) * sizeof(char*));
    collection->submodule_paths[collection->submodule_count] = strdup(path);
    collection->submodule_count++;
}

// Add repository to collection
void add_repo(unpushed_collection_t* collection, const char* repo_path, const char* repo_name) {
    if (collection->count >= collection->capacity) {
        collection->capacity *= 2;
        collection->repos = realloc(collection->repos,
                                   collection->capacity * sizeof(unpushed_repo_t));
    }

    unpushed_repo_t* repo = &collection->repos[collection->count];
    repo->repo_path = strdup(repo_path);
    repo->repo_name = strdup(repo_name);
    repo->unpushed_commits = NULL;
    repo->commit_count = 0;
    repo->commit_capacity = 8;
    repo->unpushed_commits = calloc(repo->commit_capacity, sizeof(char*));

    collection->count++;
}

// Add unpushed commit to repository
void add_unpushed_commit(unpushed_repo_t* repo, const char* commit_info) {
    if (repo->commit_count >= repo->commit_capacity) {
        repo->commit_capacity *= 2;
        repo->unpushed_commits = realloc(repo->unpushed_commits,
                                        repo->commit_capacity * sizeof(char*));
    }

    repo->unpushed_commits[repo->commit_count] = strdup(commit_info);
    repo->commit_count++;
}

// Get unpushed commits for a specific repository
void get_unpushed_commits(unpushed_collection_t* collection, unpushed_repo_t* repo) {
    char cmd[2048];
    FILE* fp;
    char buffer[1024];

    // First, check if repository has a remote
    snprintf(cmd, sizeof(cmd), "cd '%s' && git remote 2>/dev/null", repo->repo_path);
    fp = popen(cmd, "r");
    if (!fp) return;

    char remote_name[256] = "";
    if (fgets(remote_name, sizeof(remote_name), fp) != NULL) {
        // Remove newline
        remote_name[strcspn(remote_name, "\n")] = 0;
    }
    pclose(fp);

    if (strlen(remote_name) == 0) {
        // No remote configured, skip this repo
        return;
    }

    // Get current branch
    snprintf(cmd, sizeof(cmd), "cd '%s' && git branch --show-current 2>/dev/null", repo->repo_path);
    fp = popen(cmd, "r");
    if (!fp) return;

    char branch_name[256] = "";
    if (fgets(branch_name, sizeof(branch_name), fp) != NULL) {
        branch_name[strcspn(branch_name, "\n")] = 0;
    }
    pclose(fp);

    if (strlen(branch_name) == 0) {
        // Not on any branch, skip
        return;
    }

    // Check for unpushed commits: git log --oneline origin/branch..HEAD
    snprintf(cmd, sizeof(cmd), "cd '%s' && git log --oneline %s/%s..HEAD 2>/dev/null",
             repo->repo_path, remote_name, branch_name);

    fp = popen(cmd, "r");
    if (!fp) return;

    // Parse each line of git log output
    while (fgets(buffer, sizeof(buffer), fp) != NULL) {
        // Remove newline
        buffer[strcspn(buffer, "\n")] = 0;

        // Skip empty lines
        if (strlen(buffer) == 0) continue;

        // Add commit info (first 7 chars should be hash, rest is message)
        add_unpushed_commit(repo, buffer);
    }

    pclose(fp);
}

// Parse git-submodules report to find repositories to check
void parse_git_submodules_report(unpushed_collection_t* collection, const char* report_path) {
    printf("Reading git-submodules JSON report from: %s\n", report_path);

    json_value_t* report = json_parse_file(report_path);
    if (!report || report->type != JSON_OBJECT) {
        fprintf(stderr, "Failed to parse git-submodules report\n");
        return;
    }

    // Get repositories array
    json_value_t* repos = get_nested_value(report, "repositories");
    if (!repos || repos->type != JSON_ARRAY) {
        fprintf(stderr, "No repositories found in report\n");
        json_free(report);
        return;
    }

    printf("Found %zu repositories in git-submodules report\n", repos->value.arr_val->count);

    // Process each repository
    for (size_t i = 0; i < repos->value.arr_val->count; i++) {
        json_value_t* repo_obj = repos->value.arr_val->items[i];
        if (repo_obj->type != JSON_OBJECT) continue;

        // Get repository info
        json_value_t* name = get_nested_value(repo_obj, "name");
        json_value_t* path = get_nested_value(repo_obj, "path");

        if (name && name->type == JSON_STRING &&
            path && path->type == JSON_STRING) {

            printf("Found repo: %s at %s\n", name->value.str_val, path->value.str_val);

            // Check if it's a submodule path (should be filtered)
            if (is_submodule_path(collection, path->value.str_val)) {
                printf("  Skipping submodule: %s\n", path->value.str_val);
                continue;
            }

            // Add repository to check
            add_repo(collection, path->value.str_val, name->value.str_val);

            // Get submodule paths for filtering
            json_value_t* submodules = get_nested_value(repo_obj, "submodules");
            if (submodules && submodules->type == JSON_ARRAY) {
                for (size_t j = 0; j < submodules->value.arr_val->count; j++) {
                    json_value_t* submodule = submodules->value.arr_val->items[j];
                    if (submodule->type == JSON_STRING) {
                        add_submodule_path(collection, submodule->value.str_val);
                    }
                }
            }
        }
    }

    json_free(report);
    printf("Collected %zu submodule paths for filtering\n", collection->submodule_count);
}

// Generate JSON report
void generate_report(unpushed_collection_t* collection) {
    // Create JSON report
    json_value_t* report = json_create_object();
    json_object_set(report, "report_type", json_create_string("committed_not_pushed_analysis"));
    json_object_set(report, "generated_by", json_create_string("committed-not-pushed analyzer"));

    // Add timestamp
    time_t now = time(NULL);
    json_object_set(report, "timestamp", json_create_number((double)now));

    // Create repositories array
    json_value_t* repos_array = json_create_array();

    size_t total_unpushed_repos = 0;
    size_t total_unpushed_commits = 0;

    for (size_t i = 0; i < collection->count; i++) {
        unpushed_repo_t* repo = &collection->repos[i];

        // Only include repos with unpushed commits
        if (repo->commit_count > 0) {
            total_unpushed_repos++;
            total_unpushed_commits += repo->commit_count;

            json_value_t* repo_obj = json_create_object();
            json_object_set(repo_obj, "name", json_create_string(repo->repo_name));
            json_object_set(repo_obj, "path", json_create_string(repo->repo_path));
            json_object_set(repo_obj, "unpushed_commit_count", json_create_number((double)repo->commit_count));

            // Create commits array
            json_value_t* commits_array = json_create_array();
            for (size_t j = 0; j < repo->commit_count; j++) {
                json_array_add(commits_array, json_create_string(repo->unpushed_commits[j]));
            }
            json_object_set(repo_obj, "unpushed_commits", commits_array);

            json_array_add(repos_array, repo_obj);
        }
    }

    json_object_set(report, "repositories", repos_array);

    // Create summary
    json_value_t* summary = json_create_object();
    json_object_set(summary, "total_unpushed_repositories", json_create_number((double)total_unpushed_repos));
    json_object_set(summary, "total_unpushed_commits", json_create_number((double)total_unpushed_commits));
    json_object_set(report, "summary", summary);

    // Write report to file
    json_write_file("committed-not-pushed-report.json", report);
    printf("Committed-not-pushed analysis report generated\n");

    printf("\nCommitted Not Pushed Analysis Summary:\n");
    printf("  Total repositories with unpushed commits: %zu\n", total_unpushed_repos);
    printf("  Total unpushed commits: %zu\n", total_unpushed_commits);

    if (total_unpushed_repos > 0) {
        printf("\nDetailed breakdown:\n");
        for (size_t i = 0; i < collection->count; i++) {
            unpushed_repo_t* repo = &collection->repos[i];
            if (repo->commit_count > 0) {
                printf("  %s (%s): %zu unpushed commits\n",
                       repo->repo_name, repo->repo_path, repo->commit_count);
                for (size_t j = 0; j < repo->commit_count && j < 3; j++) {  // Show first 3 commits
                    printf("    - %s\n", repo->unpushed_commits[j]);
                }
                if (repo->commit_count > 3) {
                    printf("    ... and %zu more\n", repo->commit_count - 3);
                }
            }
        }
    }

    json_free(report);
}

// Cleanup collection
void unpushed_collection_cleanup(unpushed_collection_t* collection) {
    if (collection) {
        for (size_t i = 0; i < collection->count; i++) {
            unpushed_repo_t* repo = &collection->repos[i];
            free(repo->repo_path);
            free(repo->repo_name);
            for (size_t j = 0; j < repo->commit_count; j++) {
                free(repo->unpushed_commits[j]);
            }
            free(repo->unpushed_commits);
        }
        free(collection->repos);

        for (size_t i = 0; i < collection->submodule_count; i++) {
            free(collection->submodule_paths[i]);
        }
        free(collection->submodule_paths);

        free(collection);
    }
}

int main(int argc, char* argv[]) {
    printf("Committed Not Pushed Analyzer starting...\n");

    // Initialize collection
    unpushed_collection_t* collection = unpushed_collection_init();
    if (!collection) {
        fprintf(stderr, "Failed to initialize collection\n");
        return 1;
    }

    // Parse git-submodules report
    parse_git_submodules_report(collection, "./git-submodules.report");

    // Analyze each repository for unpushed commits
    for (size_t i = 0; i < collection->count; i++) {
        unpushed_repo_t* repo = &collection->repos[i];
        printf("Analyzing unpushed commits in: %s\n", repo->repo_name);
        get_unpushed_commits(collection, repo);
        printf("  Found %zu unpushed commits\n", repo->commit_count);
    }

    // Generate report
    generate_report(collection);

    printf("Committed Not Pushed Analyzer completed\n");

    // Cleanup
    unpushed_collection_cleanup(collection);

    return 0;
}
