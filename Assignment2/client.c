#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>

#define BUFFER_SIZE 1024
#define PORT 8080

void *client_task(void *arg) {
    int sock = 0;
    struct sockaddr_in serv_addr;
    char buffer[BUFFER_SIZE] = {0};
    int client_num = *(int*)arg;

    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        printf("Client %d: Socket creation error\n", client_num);
        return NULL;
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(PORT);

    if (inet_pton(AF_INET, "127.0.0.1", &serv_addr.sin_addr) <= 0) {
        printf("Client %d: Invalid address/ Address not supported\n", client_num);
        return NULL;
    }

    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        printf("Client %d: Connection Failed\n", client_num);
        return NULL;
    }

    char *message = "Request for top CPU processes";
    send(sock, message, strlen(message), 0);

    int valread = read(sock, buffer, BUFFER_SIZE);
    printf("Client Message received:\n%s\n\n", buffer);

    close(sock);
    return NULL;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <number_of_connections>\n", argv[0]);
        exit(1);
    }

    int n = atoi(argv[1]);
    pthread_t threads[n];
    int thread_args[n];

    for (int i = 0; i < n; i++) {
        thread_args[i] = i + 1;
        if (pthread_create(&threads[i], NULL, client_task, &thread_args[i]) != 0) {
            perror("Failed to create thread");
            return 1;
        }
    }

    for (int i = 0; i < n; i++) {
        pthread_join(threads[i], NULL);
    }

    return 0;
}