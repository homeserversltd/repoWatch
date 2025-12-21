#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main(int argc, char* argv[]) {
    printf("Test child starting - sleeping for 5 seconds...\n");

    // Sleep for 5 seconds
    sleep(5);

    printf("Test child completed sleep\n");

    // Write report to test/.report file (since we run from repoWatch root)
    FILE* report_file = fopen("test/.report", "w");
    if (report_file) {
        fprintf(report_file, "Test child executed successfully!\n");
        fprintf(report_file, "Slept for 5 seconds as requested.\n");
        fprintf(report_file, "This demonstrates child-to-parent state reporting.\n");
        fclose(report_file);
        printf("Report written to .report file\n");
    } else {
        fprintf(stderr, "Failed to write report file\n");
        return 1;
    }

    return 0;
}
