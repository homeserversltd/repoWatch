#include "three-pane-tui.h"

// Main entry point for the three-pane-tui module
int main(int argc, char* argv[]) {
    // Get the module path - three-pane-tui is executed from repoWatch root,
    // but its files are in the three-pane-tui subdirectory
    char module_path[1024];
    if (!getcwd(module_path, sizeof(module_path))) {
        fprintf(stderr, "Error: Cannot get current working directory\n");
        return 1;
    }

    // Append the three-pane-tui subdirectory to the module path
    char full_module_path[2048];
    snprintf(full_module_path, sizeof(full_module_path), "%s/three-pane-tui", module_path);

    // Initialize three-pane-tui orchestrator
    three_pane_tui_orchestrator_t* orch = three_pane_tui_init(full_module_path);
    if (!orch) {
        fprintf(stderr, "Error: Failed to initialize three-pane-tui orchestrator\n");
        return 1;
    }

    // Execute three-pane-tui module
    int result = three_pane_tui_execute(orch);

    // Cleanup
    three_pane_tui_cleanup(orch);

    return result;
}
