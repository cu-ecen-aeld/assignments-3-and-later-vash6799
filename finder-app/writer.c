#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <syslog.h>
#include <errno.h>

int main(int argc, char *argv[]) {
    // 1. Setup syslog logging with LOG_USER facility
    openlog("writer", LOG_PID, LOG_USER);

    // 2. Check for the correct number of arguments
    if (argc != 3) {
        syslog(LOG_ERR, "Invalid number of arguments: Expected 2, got %d", argc - 1);
        fprintf(stderr, "Usage: %s <writefile> <writestr>\n", argv[0]);
        closelog();
        return 1;
    }

    const char *writefile = argv[1];
    const char *writestr = argv[2];

    // 3. Log the attempt to write (LOG_DEBUG)
    syslog(LOG_DEBUG, "Writing %s to %s", writestr, writefile);

    // 4. Open the file
    // O_WRONLY: Write only
    // O_CREAT: Create if it doesn't exist
    // O_TRUNC: Truncate to zero length if it exists (overwrite)
    // 0644: Permissions (rw-r--r--)
    int fd = open(writefile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    
    if (fd == -1) {
        syslog(LOG_ERR, "Error opening file %s: %s", writefile, strerror(errno));
        perror("File open failed");
        closelog();
        return 1;
    }

    // 5. Write the string to the file
    ssize_t nr = write(fd, writestr, strlen(writestr));
    if (nr == -1) {
        syslog(LOG_ERR, "Error writing to file %s: %s", writefile, strerror(errno));
        perror("File write failed");
        close(fd);
        closelog();
        return 1;
    } else if (nr != strlen(writestr)) {
        syslog(LOG_ERR, "Partial write occurred to %s", writefile);
    }

    // 6. Cleanup
    close(fd);
    closelog();

    return 0;
}