#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <regex.h>
#include <time.h>

// Structure for dirty file information
typedef struct {
    char* repo_path;
    char* repo_name;
    char** dirty_files;
    size_t file_count;
    size_t file_capacity;
} dirty_repo_t;

// Collection of dirty repositories
typedef struct {
    dirty_repo_t* repos;
    size_t count;
    size_t capacity;
} dirty_collection_t;

// Initialize dirty collection
dirty_collection_t* dirty_collection_init() {
    dirty_collection_t* collection = calloc(1, sizeof(dirty_collection_t));
    if (!collection) return NULL;

    collection->capacity = 8;
    collection->repos = calloc(collection->capacity, sizeof(dirty_repo_t));
    if (!collection->repos) {
        free(collection);
        return NULL;
    }

    return collection;
}

// Add dirty repository to collection
void add_dirty_repo(dirty_collection_t* collection, const char* path, const char* name) {
    if (collection->count >= collection->capacity) {
        collection->capacity *= 2;
        dirty_repo_t* new_repos = realloc(collection->repos, collection->capacity * sizeof(dirty_repo_t));
        if (!new_repos) return;
        collection->repos = new_repos;
    }

    dirty_repo_t* repo = &collection->repos[collection->count];
    repo->repo_path = strdup(path);
    repo->repo_name = strdup(name);
    repo->file_capacity = 16;
    repo->file_count = 0;
    repo->dirty_files = calloc(repo->file_capacity, sizeof(char*));

    collection->count++;
}

// Add dirty file to repository
void add_dirty_file(dirty_repo_t* repo, const char* filename) {
    if (repo->file_count >= repo->file_capacity) {
        repo->file_capacity *= 2;
        char** new_files = realloc(repo->dirty_files, repo->file_capacity * sizeof(char*));
        if (!new_files) return;
        repo->dirty_files = new_files;
    }

    repo->dirty_files[repo->file_count] = strdup(filename);
    repo->file_count++;
}

// Get git status for dirty files in a specific repository
void get_dirty_files(dirty_repo_t* repo) {
    char cmd[2048];
    FILE* fp;
    char buffer[1024];

    // Run git status --porcelain to get dirty files
    snprintf(cmd, sizeof(cmd), "cd '%s' && git status --porcelain 2>/dev/null", repo->repo_path);

    fp = popen(cmd, "r");
    if (!fp) return;

    // Parse each line of git status output
    while (fgets(buffer, sizeof(buffer), fp) != NULL) {
        // Git status --porcelain format: XY filename
        // Where X = index status, Y = working tree status
        // We want files that are not clean (not "  " at start)

        // Skip empty lines
        if (strlen(buffer) < 4) continue;

        // Check if file has changes (not clean)
        if (buffer[0] != ' ' || buffer[1] != ' ') {
            // Extract filename (skip first 3 chars for status codes)
            char* filename = buffer + 3;

            // Remove trailing newline
            char* newline = strchr(filename, '\n');
            if (newline) *newline = '\0';

            // Add to dirty files list
            add_dirty_file(repo, filename);
        }
    }

    pclose(fp);
}

// Parse git-submodules report to find dirty repositories
void parse_git_submodules_report(dirty_collection_t* collection, const char* report_path) {
    printf("Reading git-submodules report from: %s\n", report_path);

    FILE* fp = fopen(report_path, "r");
    if (!fp) {
        fprintf(stderr, "Could not open git-submodules report: %s\n", report_path);
        return;
    }

    char line[2048];
    char current_repo_name[256] = {0};
    char current_repo_path[2048] = {0};
    int is_current_repo_dirty = 0;

    while (fgets(line, sizeof(line), fp) != NULL) {
        // Look for "Repository:" line
        if (strstr(line, "Repository: ")) {
            char* name = line + strlen("Repository: ");
            char* newline = strchr(name, '\n');
            if (newline) *newline = '\0';
            strcpy(current_repo_name, name);
            is_current_repo_dirty = 0; // Reset dirty flag
        }
        // Look for "Path:" line
        else if (strstr(line, "Path: ")) {
            char* path = line + strlen("Path: ");
            char* newline = strchr(path, '\n');
            if (newline) *newline = '\0';
            strcpy(current_repo_path, path);
        }
        // Look for "Status: DIRTY" line
        else if (strstr(line, "Status: DIRTY")) {
            is_current_repo_dirty = 1;
        }
        // Look for empty line after repository info (end of repository section)
        else if (strlen(line) <= 1 && current_repo_name[0] != '\0' && current_repo_path[0] != '\0') {
            // If this repository was dirty, add it to our collection
            if (is_current_repo_dirty) {
                printf("Found dirty repo: %s at %s\n", current_repo_name, current_repo_path);
                add_dirty_repo(collection, current_repo_path, current_repo_name);
            }

            // Reset for next repository
            current_repo_name[0] = '\0';
            current_repo_path[0] = '\0';
            is_current_repo_dirty = 0;
        }
    }

    fclose(fp);
}

// Run git-submodules and parse its output to find dirty repositories
void run_git_submodules_analysis(dirty_collection_t* collection) {
    printf("Running git-submodules analysis...\n");

    // Execute git-submodules and capture its output
    FILE* fp = popen("../git-submodules/git-submodules 2>/dev/null", "r");
    if (!fp) {
        fprintf(stderr, "Could not execute git-submodules\n");
        return;
    }

    printf("Successfully opened pipe to git-submodules\n");

    char line[2048];
    int found_dirty_header = 0;
    int line_count = 0;

    printf("Reading git-submodules output:\n");
    while (fgets(line, sizeof(line), fp) != NULL) {
        line_count++;
        printf("Line %d: %s", line_count, line);

        // Look for "Dirty repositories:" header
        if (strstr(line, "Dirty repositories:")) {
            found_dirty_header = 1;
            printf("Found dirty repositories header\n");
            continue;
        }

        if (found_dirty_header) {
            // Parse lines like "  - repo_name (path)"
            if (strstr(line, "  - ")) {
                printf("Found dirty repo line: %s", line);
                char* repo_info = line + 4; // Skip "  - "
                char* paren_start = strchr(repo_info, '(');
                char* paren_end = strchr(repo_info, ')');

                if (paren_start && paren_end) {
                    // Extract repo name
                    *paren_start = '\0'; // Null terminate name
                    char repo_name[256];
                    strcpy(repo_name, repo_info);

                    // Extract repo path
                    char repo_path[2048];
                    strcpy(repo_path, paren_start + 1);
                    repo_path[paren_end - paren_start - 1] = '\0'; // Remove closing paren

                    printf("Parsed: name='%s', path='%s'\n", repo_name, repo_path);

                    // Add to collection
                    add_dirty_repo(collection, repo_path, repo_name);
                } else {
                    printf("Could not parse repo info from line\n");
                }
            }
        }
    }

    printf("Finished reading %d lines from git-submodules\n", line_count);

    pclose(fp);
}

// Generate report file
void generate_report(dirty_collection_t* collection) {
    FILE* report_file = fopen(".report", "w");
    if (!report_file) {
        fprintf(stderr, "Failed to create report file\n");
        return;
    }

    fprintf(report_file, "Dirty Files Analysis Report\n");
    fprintf(report_file, "===========================\n");
    fprintf(report_file, "Generated by dirty-files analyzer\n");
    fprintf(report_file, "Timestamp: %ld\n", time(NULL));
    fprintf(report_file, "\n");

    size_t total_dirty_files = 0;

    for (size_t i = 0; i < collection->count; i++) {
        dirty_repo_t* repo = &collection->repos[i];

        fprintf(report_file, "Repository: %s\n", repo->repo_name);
        fprintf(report_file, "Path: %s\n", repo->repo_path);
        fprintf(report_file, "Dirty Files (%zu):\n", repo->file_count);

        if (repo->file_count > 0) {
            for (size_t j = 0; j < repo->file_count; j++) {
                fprintf(report_file, "  - %s\n", repo->dirty_files[j]);
            }
        } else {
            fprintf(report_file, "  (No dirty files found)\n");
        }

        fprintf(report_file, "\n");
        total_dirty_files += repo->file_count;
    }

    fprintf(report_file, "Summary:\n");
    fprintf(report_file, "Total dirty repositories: %zu\n", collection->count);
    fprintf(report_file, "Total dirty files: %zu\n", total_dirty_files);

    fclose(report_file);
}

// Cleanup dirty collection
void dirty_collection_cleanup(dirty_collection_t* collection) {
    if (collection) {
        for (size_t i = 0; i < collection->count; i++) {
            dirty_repo_t* repo = &collection->repos[i];
            free(repo->repo_path);
            free(repo->repo_name);

            for (size_t j = 0; j < repo->file_count; j++) {
                free(repo->dirty_files[j]);
            }
            free(repo->dirty_files);
        }
        free(collection->repos);
        free(collection);
    }
}

int main(int argc, char* argv[]) {
    printf("Dirty Files Analyzer starting...\n");

    // Initialize collection
    dirty_collection_t* collection = dirty_collection_init();
    if (!collection) {
        fprintf(stderr, "Failed to initialize dirty collection\n");
        return 1;
    }

    // Parse git-submodules report to find dirty repositories
    parse_git_submodules_report(collection, "./git-submodules.report");

    printf("Found %zu dirty repositories from git-submodules report\n", collection->count);

    // For each dirty repository, get the specific dirty files
    for (size_t i = 0; i < collection->count; i++) {
        dirty_repo_t* repo = &collection->repos[i];
        printf("Analyzing dirty files in: %s\n", repo->repo_name);
        get_dirty_files(repo);
        printf("  Found %zu dirty files\n", repo->file_count);
    }

    // Generate report
    generate_report(collection);
    printf("Dirty files analysis report generated\n");

    // Print summary to stdout
    size_t total_files = 0;
    for (size_t i = 0; i < collection->count; i++) {
        total_files += collection->repos[i].file_count;
    }

    printf("\nDirty Files Analysis Summary:\n");
    printf("  Total dirty repositories: %zu\n", collection->count);
    printf("  Total dirty files: %zu\n", total_files);

    if (collection->count > 0) {
        printf("\nDetailed breakdown:\n");
        for (size_t i = 0; i < collection->count; i++) {
            dirty_repo_t* repo = &collection->repos[i];
            printf("  %s (%s): %zu dirty files\n", repo->repo_name, repo->repo_path, repo->file_count);

            // Show first few dirty files as examples
            if (repo->file_count > 0) {
                size_t show_count = repo->file_count > 3 ? 3 : repo->file_count;
                for (size_t j = 0; j < show_count; j++) {
                    printf("    - %s\n", repo->dirty_files[j]);
                }
                if (repo->file_count > 3) {
                    printf("    ... and %zu more\n", repo->file_count - 3);
                }
            }
        }
    }

    // Cleanup
    dirty_collection_cleanup(collection);

    printf("Dirty Files Analyzer completed\n");
    return 0;
}
