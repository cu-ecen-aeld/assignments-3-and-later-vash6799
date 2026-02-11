#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <syslog.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>

#define PORT "9000"
#define DATA_FILE "/var/tmp/aesdsocketdata"
#define BUFFER_SIZE 1024

int server_fd = -1;
int caught_signal = 0;

void signal_handler(int sig) {
    if (sig == SIGINT || sig == SIGTERM) {
        syslog(LOG_INFO, "Caught signal, exiting");
        caught_signal = 1;
    }
}

void cleanup() {
    if (server_fd != -1) close(server_fd);
    unlink(DATA_FILE);
    closelog();
}

int main(int argc, char *argv[]) {
    int daemon_mode = 0;
    if (argc > 1 && strcmp(argv[1], "-d") == 0) {
        daemon_mode = 1;
    }

    openlog("aesdsocket", LOG_PID, LOG_USER);

    // Set up signal handling
    struct sigaction sa;
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    // 1. Create Socket
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) return -1;

    // Allow port reuse
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(9000);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    // 2. Bind
    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        close(server_fd);
        return -1;
    }

    // 3. Daemonize if requested
    if (daemon_mode) {
        pid_t pid = fork();
        if (pid < 0) exit(EXIT_FAILURE);
        if (pid > 0) exit(EXIT_SUCCESS); // Parent exits
        setsid();
        chdir("/");
        close(STDIN_FILENO);
        close(STDOUT_FILENO);
        close(STDERR_FILENO);
    }

    // 4. Listen
    if (listen(server_fd, 10) < 0) {
        cleanup();
        return -1;
    }

    while (!caught_signal) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &addr_len);
        
        if (caught_signal) break;
        if (client_fd == -1) continue;

        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
        syslog(LOG_INFO, "Accepted connection from %s", client_ip);

        // 1. Open file for appending AND reading
        FILE *fp = fopen(DATA_FILE, "a+");
        if (!fp) {
            close(client_fd);
            continue;
        }

        char buffer[BUFFER_SIZE];
        ssize_t bytes_recv;
        
        // 2. Receive and write until a newline is found
        while ((bytes_recv = recv(client_fd, buffer, BUFFER_SIZE, 0)) > 0) {
            fwrite(buffer, 1, bytes_recv, fp);
            // Search for newline in the received chunk
            if (memchr(buffer, '\n', bytes_recv)) {
                break;
            }
        }
        
        // 3. IMPORTANT: Flush stream to disk before reading
        fflush(fp);
        fseek(fp, 0, SEEK_SET);

        // 4. Send the ENTIRE file content back to the client
        size_t bytes_read;
        while ((bytes_read = fread(buffer, 1, BUFFER_SIZE, fp)) > 0) {
            send(client_fd, buffer, bytes_read, 0);
        }
        
        fclose(fp);
        close(client_fd);
        syslog(LOG_INFO, "Closed connection from %s", client_ip);
    }

    cleanup();
    return 0;
}