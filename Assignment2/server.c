#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <dirent.h>
#include <ctype.h>

#define MAX_CLIENTS 10
#define BUFFER_SIZE 1024
#define PORT 8080

typedef struct {
    char name[256];
    int pid;
    long long utime;
    long long stime;
} ProcessInfo;

void get_top_cpu_processes(ProcessInfo *top_processes, int n) {
    DIR *dir;
    struct dirent *entry;
    char path[512];
    FILE *fp;
    char line[256];
    ProcessInfo *processes = NULL;
    int process_count = 0;

    dir = opendir("/proc");
    if (dir == NULL) {
        perror("Unable to open /proc");
        return;
    }

    while ((entry = readdir(dir)) != NULL) {
        if (isdigit(entry->d_name[0])) {
            snprintf(path, sizeof(path), "/proc/%s/stat", entry->d_name);
            fp = fopen(path, "r");
            if (fp) {
                if (fgets(line, sizeof(line), fp) != NULL) {
                    ProcessInfo info;
                    char *token = strtok(line, " ");
                    int field = 0;
                    while (token != NULL) {
                        if (field == 0) info.pid = atoi(token);
                        else if (field == 1) sscanf(token, "(%[^)])", info.name);
                        else if (field == 13) info.utime = atoll(token);
                        else if (field == 14) info.stime = atoll(token);
                        token = strtok(NULL, " ");
                        field++;
                    }
                    processes = realloc(processes, (process_count + 1) * sizeof(ProcessInfo));
                    processes[process_count++] = info;
                }
                fclose(fp);
            }
        }
    }
    closedir(dir);

    // Sort processes by total CPU time (user + system)
    for (int i = 0; i < process_count - 1; i++) {
        for (int j = 0; j < process_count - i - 1; j++) {
            long long total1 = processes[j].utime + processes[j].stime;
            long long total2 = processes[j+1].utime + processes[j+1].stime;
            if (total1 < total2) {
                ProcessInfo temp = processes[j];
                processes[j] = processes[j+1];
                processes[j+1] = temp;
            }
        }
    }

    // Copy top n processes
    for (int i = 0; i < n && i < process_count; i++) {
        top_processes[i] = processes[i];
    }

    free(processes);
}

void *handle_client(void *socket_desc) {
    int sock = *(int*)socket_desc;
    char buffer[BUFFER_SIZE] = {0};
    ProcessInfo top_processes[2];

    get_top_cpu_processes(top_processes, 2);

    char response[BUFFER_SIZE];
    snprintf(response, sizeof(response), 
             "Top CPU Processes:\n1. %s (PID: %d, User: %lld, Kernel: %lld)\n2. %s (PID: %d, User: %lld, Kernel: %lld)",
             top_processes[0].name, top_processes[0].pid, top_processes[0].utime, top_processes[0].stime,
             top_processes[1].name, top_processes[1].pid, top_processes[1].utime, top_processes[1].stime);

    send(sock, response, strlen(response), 0);
    printf("Message sent \n");
    close(sock);
    free(socket_desc);
    return NULL;
}


int main(int argc, char *argv[]) {
    int server_fd, new_socket;
    struct sockaddr_in address;
    int opt = 1;
    int addrlen = sizeof(address);
    pthread_t thread_id;

    // Create socket file descriptor
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    // Set socket options
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))) {
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    // Bind the socket to the network address and port
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }

    // Start listening for connections
    if (listen(server_fd, MAX_CLIENTS) < 0) {
        perror("listen");
        exit(EXIT_FAILURE);
    }

    printf("Server is running and listening on port %d...\n", PORT);

    while (1) {
        if ((new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0) {
            perror("accept");
            continue;
        }

        int *new_sock = malloc(sizeof(int));
        *new_sock = new_socket;

        if (pthread_create(&thread_id, NULL, handle_client, (void*)new_sock) < 0) {
            perror("could not create thread");
            free(new_sock);
            continue;
        }

        pthread_detach(thread_id);
    }

    close(server_fd);
    return 0;
}