#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <dirent.h>
#include <ctype.h>
#include <errno.h>

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

int main() {
    int master_socket, addrlen, new_socket, client_socket[MAX_CLIENTS],
        max_clients = MAX_CLIENTS, activity, i, valread, sd;
    int max_sd;
    struct sockaddr_in address;
    char buffer[BUFFER_SIZE];
    fd_set readfds;

    for (i = 0; i < max_clients; i++) {
        client_socket[i] = 0;
    }

    if ((master_socket = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    int opt = 1;
    if (setsockopt(master_socket, SOL_SOCKET, SO_REUSEADDR, (char *)&opt, sizeof(opt)) < 0) {
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(master_socket, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }

    if (listen(master_socket, 3) < 0) {
        perror("listen");
        exit(EXIT_FAILURE);
    }

    addrlen = sizeof(address);
    printf("Server is running and listening on port %d...\n", PORT);

    while (1) {
        FD_ZERO(&readfds);
        FD_SET(master_socket, &readfds);
        max_sd = master_socket;

        for (i = 0; i < max_clients; i++) {
            sd = client_socket[i];
            if (sd > 0)
                FD_SET(sd, &readfds);
            if (sd > max_sd)
                max_sd = sd;
        }

        activity = select(max_sd + 1, &readfds, NULL, NULL, NULL);

        if ((activity < 0) && (errno != EINTR)) {
            printf("select error");
        }

        if (FD_ISSET(master_socket, &readfds)) {
            if ((new_socket = accept(master_socket, (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0) {
                perror("accept");
                exit(EXIT_FAILURE);
            }

            for (i = 0; i < max_clients; i++) {
                if (client_socket[i] == 0) {
                    client_socket[i] = new_socket;
                    break;
                }
            }
        }

        for (i = 0; i < max_clients; i++) {
            sd = client_socket[i];

            if (FD_ISSET(sd, &readfds)) {
                if ((valread = read(sd, buffer, BUFFER_SIZE)) == 0) {
                    getpeername(sd, (struct sockaddr*)&address, (socklen_t*)&addrlen);
                    close(sd);
                    client_socket[i] = 0;
                } else {
                    ProcessInfo top_processes[2];
                    get_top_cpu_processes(top_processes, 2);

                    char response[BUFFER_SIZE];
                    snprintf(response, sizeof(response), 
                             "Top CPU Processes:\n1. %s (PID: %d, User: %lld, Kernel: %lld)\n2. %s (PID: %d, User: %lld, Kernel: %lld)",
                             top_processes[0].name, top_processes[0].pid, top_processes[0].utime, top_processes[0].stime,
                             top_processes[1].name, top_processes[1].pid, top_processes[1].utime, top_processes[1].stime);
                    
                    printf("Message Sent \n");
                    send(sd, response, strlen(response), 0);
                }
            }
        }
    }

    return 0;
}