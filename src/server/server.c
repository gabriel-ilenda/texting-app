#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <pthread.h>
#include "database.h"  // your custom DB logic header

#define PORT 4400
#define BUFFER_SIZE 1024

void *handle_client(void *arg) {
    int client_fd = *(int *)arg;
    free(arg);

    char buffer[BUFFER_SIZE];
    int attempts = 0;

    char log_or_sign;
    
    if (recv(client_fd, &log_or_sign, 1, 0) <= 0) {
        close(client_fd);
        pthread_exit(NULL);
    }

    printf("%d", log_or_sign);

    while (attempts < 3) {
        // Read username
        memset(buffer, 0, BUFFER_SIZE);
        if (recv(client_fd, buffer, BUFFER_SIZE, 0) <= 0) break;
        buffer[strcspn(buffer, "\n")] = 0;
        char username[100];
        strncpy(username, buffer, sizeof(username));

        // Receive password
        memset(buffer, 0, BUFFER_SIZE);
        if (recv(client_fd, buffer, BUFFER_SIZE, 0) <= 0) break;
        buffer[strcspn(buffer, "\n")] = 0;
        char password[100];
        strncpy(password, buffer, sizeof(password));

        if (log_or_sign == 1) {
            printf("Login attempt: %s / %s\n", username, password);
            if (db_login(username, password)) {
                send(client_fd, "Login successful\n", 17, 0);
                while (1) {
                    memset(buffer, 0, BUFFER_SIZE);
                    ssize_t n = recv(client_fd, buffer, BUFFER_SIZE, 0);
                    if (n <= 0) break;
                    printf("From client: %s", buffer);
                    send(client_fd, "Echo: ", 6, 0);
                    send(client_fd, buffer, strlen(buffer), 0);
                }
            } else {
                attempts++;
                if (attempts < 3)
                    send(client_fd, "Login failed. Try again.\n", 26, 0);
                else
                    send(client_fd, "Login failed 3 times. Connection closed.\n", 42, 0);
            }
        } else if (log_or_sign == 2) {
            printf("Signup attempt: %s / %s\n", username, password);

            if (db_signup(username, password)) {
                send(client_fd, "Signup successful\n", 18, 0);
                while (1) {
                    memset(buffer, 0, BUFFER_SIZE);
                    ssize_t n = recv(client_fd, buffer, BUFFER_SIZE, 0);
                    if (n <= 0) break;
                    printf("From client: %s", buffer);
                    send(client_fd, "Echo: ", 6, 0);
                    send(client_fd, buffer, strlen(buffer), 0);
                }
            } else {
                attempts++;
                if (attempts < 3)
                    send(client_fd, "Signup failed. Try again.\n", 27, 0);
                else
                    send(client_fd, "Signup failed 3 times. Connection closed.\n", 43, 0);
            }
        } else {
            printf("Passed an invalid command during the login process");
            close(client_fd);
            pthread_exit(NULL);
        }

    }

    printf("Logins exceeded, closing connection");
    close(client_fd);
    pthread_exit(NULL);
}


int main() {
    int server_fd, *client_fd;
    struct sockaddr_in address;
    socklen_t addrlen = sizeof(address);

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == 0) {
        perror("Socket failed");
        exit(EXIT_FAILURE);
    }

    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("Bind failed");
        exit(EXIT_FAILURE);
    }

    if (listen(server_fd, 5) < 0) {
        perror("Listen failed");
        exit(EXIT_FAILURE);
    }

    printf("Server listening on port %d...\n", PORT);

    db_connect();

    while (1) {
        client_fd = malloc(sizeof(int));
        if (!client_fd) continue;
        *client_fd = accept(server_fd, (struct sockaddr *)&address, &addrlen);
        if (*client_fd < 0) {
            perror("Accept failed");
            free(client_fd);
            continue;
        }

        pthread_t tid;
        if (pthread_create(&tid, NULL, handle_client, client_fd) != 0) {
            perror("pthread_create failed");
            close(*client_fd);
            free(client_fd);
        } else {
            pthread_detach(tid);  // Automatically clean up finished threads
        }
    }

    db_close();
    close(server_fd);
    return 0;
}
