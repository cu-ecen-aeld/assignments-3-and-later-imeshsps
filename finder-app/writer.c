#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <errno.h>

int main(int argc, char *argv[]) {
    // Open syslog for the application
    openlog("writer", LOG_PID | LOG_CONS, LOG_USER);

    // Check if the arguments are sufficient
    if (argc < 3) {
        syslog(LOG_ERR, "Not enough arguments. Usage: %s <file> <string>", argv[0]);
        fprintf(stderr, "Usage: %s <file> <string>\n", argv[0]);
        closelog();
        return 1;
    }

    // Assign file path and content from arguments
    const char *file_path = argv[1];
    const char *content = argv[2];

    // Open the file for writing
    FILE *file = fopen(file_path, "w");
    if (file == NULL) {
        syslog(LOG_ERR, "Error opening file %s: %s", file_path, strerror(errno));
        perror("Error opening file");
        closelog();
        return 1;
    }

    // Write content to the file
    if (fprintf(file, "%s", content) < 0) {
        syslog(LOG_ERR, "Error writing to file %s: %s", file_path, strerror(errno));
        perror("Error writing to file");
        fclose(file);
        closelog();
        return 1;
    }

    // Log successful writing operation
    syslog(LOG_DEBUG, "Writing '%s' to '%s'", content, file_path);

    // Close the file and syslog
    fclose(file);
    closelog();

    return 0;
}