// SERVER CODE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include "database.h"  // your custom DB logic header
#include <sys/select.h>

#define PORT 4400
#define BUFFER_SIZE 1024
#define MAX_USERS 50
#define USERNAME_LEN 100

typedef struct {
    char username[USERNAME_LEN];
    char ip[INET_ADDRSTRLEN];
    int port;
    int fd;
} ActiveUser;

ActiveUser active_users[MAX_USERS];
int active_user_count = 0;
pthread_mutex_t active_users_mutex = PTHREAD_MUTEX_INITIALIZER;

void add_active_user(const char *username, const char *ip, int port, int fd) {
    pthread_mutex_lock(&active_users_mutex);
    for (int i = 0; i < active_user_count; ++i) {
        if (strcmp(active_users[i].username, username) == 0) {
            pthread_mutex_unlock(&active_users_mutex);
            return;
        }
    }
    if (active_user_count < MAX_USERS) {
        strncpy(active_users[active_user_count].username, username, USERNAME_LEN - 1);
        strncpy(active_users[active_user_count].ip, ip, INET_ADDRSTRLEN - 1);
        active_users[active_user_count].port = port;
        active_users[active_user_count].fd = fd;
        active_users[active_user_count].username[USERNAME_LEN - 1] = '\0';
        active_users[active_user_count].ip[INET_ADDRSTRLEN - 1] = '\0';
        active_user_count++;
    }
    pthread_mutex_unlock(&active_users_mutex);
}

void remove_active_user(const char *username) {
    pthread_mutex_lock(&active_users_mutex);
    for (int i = 0; i < active_user_count; ++i) {
        if (strcmp(active_users[i].username, username) == 0) {
            for (int j = i; j < active_user_count - 1; ++j) {
                active_users[j] = active_users[j + 1];
            }
            active_user_count--;
            break;
        }
    }
    pthread_mutex_unlock(&active_users_mutex);
}

void kill_client(char *username, int sock) {
    close(sock);
    pthread_exit(NULL);
    remove_active_user(username);
}

void client_loop(char username[], int client_fd) {
    char buffer[BUFFER_SIZE];
    while (1) {
        memset(buffer, 0, BUFFER_SIZE);
        ssize_t n = recv(client_fd, buffer, BUFFER_SIZE, 0);
        if (n <= 0) break;

        buffer[strcspn(buffer, "\n")] = 0;

        if (strcmp(buffer, "/who") == 0) {
            pthread_mutex_lock(&active_users_mutex);
            char users_msg[BUFFER_SIZE] = "Active users:\n";
            for (int i = 0; i < active_user_count; i++) {
                char line[200];
                snprintf(line, sizeof(line), " - %s (%s:%d)\n",
                         active_users[i].username,
                         active_users[i].ip,
                         active_users[i].port);
                strncat(users_msg, line, sizeof(users_msg) - strlen(users_msg) - 1);
            }
            pthread_mutex_unlock(&active_users_mutex);
            send(client_fd, users_msg, strlen(users_msg), 0);
        } else if (strncmp(buffer, "/p2p", 4) == 0) {
            char ip[INET_ADDRSTRLEN];
            int port;

            if (sscanf(buffer + 5, "%15s %d", ip, &port) != 2) {
                send(client_fd, "Error: Invalid P2P format. Usage: /p2p <ip> <port>\n", 51, 0);
                kill_client(username, client_fd);
            }

            pthread_mutex_lock(&active_users_mutex);
            int found_index = -1;
            for (int i = 0; i < active_user_count; ++i) {
                if (strcmp(active_users[i].ip, ip) == 0 && active_users[i].port == port) {
                    found_index = i;
                    break;
                }
            }
            pthread_mutex_unlock(&active_users_mutex);

            if (found_index == -1) {
                send(client_fd, "Error: No active user found with that IP and port.\n", 51, 0);
                kill_client(username, client_fd);
            }

            char request_msg[BUFFER_SIZE];
            snprintf(request_msg, sizeof(request_msg), "P2P_REQUEST_FROM:%s\n", username);
            send(active_users[found_index].fd, request_msg, strlen(request_msg), 0);
            send(client_fd, "Target found. Awaiting their response...\n", 41, 0);

            // Wait for target client's response
            fd_set readfds;
            FD_ZERO(&readfds);
            FD_SET(active_users[found_index].fd, &readfds);

            struct timeval timeout;
            timeout.tv_sec = 30;  // wait up to 30 seconds
            timeout.tv_usec = 0;

            int activity = select(active_users[found_index].fd + 1, &readfds, NULL, NULL, &timeout);
            if (activity > 0 && FD_ISSET(active_users[found_index].fd, &readfds)) {
                char response[BUFFER_SIZE];
                memset(response, 0, sizeof(response));
                int bytes = recv(active_users[found_index].fd, response, sizeof(response), 0);
                if (bytes > 0) {
                    if (strncmp(response, "P2P_ACCEPT:", 11) == 0) {
                        char confirm[BUFFER_SIZE];
                        snprintf(confirm, sizeof(confirm), "P2P_ACCEPTED by %s\n", active_users[found_index].username);
                        send(client_fd, confirm, strlen(confirm), 0);
                    } else if (strncmp(response, "P2P_REJECT:", 11) == 0) {
                        char reject[BUFFER_SIZE];
                        snprintf(reject, sizeof(reject), "P2P_REJECTED by %s\n", active_users[found_index].username);
                        send(client_fd, reject, strlen(reject), 0);
                    } else {
                        send(client_fd, "Unexpected response received from target.\n", 42, 0);
                    }
                } else {
                    send(client_fd, "Error reading target's response.\n", 34, 0);
                }
            } else {
                send(client_fd, "No response from target. Timeout or error.\n", 44, 0);
            }

        
        } else {
            char *err = "server unknown command\n";
            send(client_fd, err, strlen(err), 0);
        }
    }

    if (strlen(username) > 0) {
        remove_active_user(username);
        printf("User %s disconnected.\n", username);
    }
}

void *handle_client(void *arg) {
    int client_fd = *((int *)arg);
    free(arg);

    struct sockaddr_in client_addr;
    socklen_t addrlen = sizeof(client_addr);
    getpeername(client_fd, (struct sockaddr *)&client_addr, &addrlen);
    char ip_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client_addr.sin_addr, ip_str, sizeof(ip_str));
    int port = ntohs(client_addr.sin_port);

    char buffer[BUFFER_SIZE];
    char log_or_sign;
    int attempts = 0;

    if (recv(client_fd, &log_or_sign, 1, 0) <= 0) {
        close(client_fd);
        pthread_exit(NULL);
    }

    char username[USERNAME_LEN];
    int authenticated = 0;

    while (!authenticated && attempts < 3) {
        memset(buffer, 0, BUFFER_SIZE);
        if (recv(client_fd, buffer, BUFFER_SIZE, 0) <= 0) break;
        buffer[strcspn(buffer, "\n")] = 0;
        strncpy(username, buffer, USERNAME_LEN - 1);
        username[USERNAME_LEN - 1] = '\0';

        memset(buffer, 0, BUFFER_SIZE);
        if (recv(client_fd, buffer, BUFFER_SIZE, 0) <= 0) break;
        buffer[strcspn(buffer, "\n")] = 0;
        char password[100];
        strncpy(password, buffer, sizeof(password) - 1);
        password[sizeof(password) - 1] = '\0';

        if (log_or_sign == 1) {
            if (db_login(username, password)) {
                authenticated = 1;
                add_active_user(username, ip_str, port, client_fd);
                send(client_fd, "Login successful\n", 17, 0);
                client_loop(username, client_fd);
            } else {
                attempts++;
                if (attempts < 3)
                    send(client_fd, "Login failed. Try again.\n", 26, 0);
                else
                    send(client_fd, "Login failed 3 times. Connection closed.\n", 42, 0);
            }
        } else if (log_or_sign == 2) {
            if (db_signup(username, password)) {
                send(client_fd, "Signup successful. Please login now.\n", 38, 0);
                // Reset login mode
                attempts = 0;
                if (recv(client_fd, &log_or_sign, 1, 0) <= 0) break; // Expect login (1)
            } else {
                attempts++;
                if (attempts < 3)
                    send(client_fd, "Signup failed. Try again.\n", 27, 0);
                else
                    send(client_fd, "Signup failed 3 times. Connection closed.\n", 43, 0);
            }
        } else {
            send(client_fd, "Invalid mode. Connection closed.\n", 33, 0);
            close(client_fd);
            pthread_exit(NULL);
        }
    }


    close(client_fd);
    pthread_exit(NULL);
}

int main() {
    int server_fd, *client_fd;
    struct sockaddr_in address;
    socklen_t addrlen = sizeof(address);

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    bind(server_fd, (struct sockaddr *)&address, sizeof(address));
    listen(server_fd, 5);
    db_connect();

    printf("Server listening on port %d...\n", PORT);

    while (1) {
        client_fd = malloc(sizeof(int));
        *client_fd = accept(server_fd, (struct sockaddr *)&address, &addrlen);
        pthread_t tid;
        pthread_create(&tid, NULL, handle_client, client_fd);
        pthread_detach(tid);
    }

    db_close();
    close(server_fd);
    return 0;
}