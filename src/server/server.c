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
#include <time.h>

#define PORT 4400
#define BUFFER_SIZE 1024
#define MAX_USERS 50
#define USERNAME_LEN 100

typedef struct {
    char username[USERNAME_LEN];
    char ip[INET_ADDRSTRLEN];
    int port;
    int fd;
    int p2p_port;
    int udp_port;
} ActiveUser;

ActiveUser active_users[MAX_USERS];
int active_user_count = 0;
pthread_mutex_t active_users_mutex = PTHREAD_MUTEX_INITIALIZER;

void add_active_user(const char *username, const char *ip, int port, int fd, int p2p_port, int udp_port) {
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
        active_users[active_user_count].p2p_port = p2p_port;
        active_users[active_user_count].udp_port = udp_port;
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
        if (strcmp(buffer, "/exit") == 0) {
            kill_client(username, client_fd);
        } else if (strcmp(buffer, "/who") == 0) {
            pthread_mutex_lock(&active_users_mutex);
            char users_msg[BUFFER_SIZE] = "Active users:\n";
            for (int i = 0; i < active_user_count; i++) {
                char line[200];
                snprintf(line, sizeof(line), " - %s (%s:%d)\n",
                         active_users[i].username,
                         active_users[i].ip,
                         active_users[i].p2p_port);
                strncat(users_msg, line, sizeof(users_msg) - strlen(users_msg) - 1);
            }
            pthread_mutex_unlock(&active_users_mutex);
            send(client_fd, users_msg, strlen(users_msg), 0);        
        } else if (strncmp(buffer, "/p2p", 4) == 0) {
            char ip[INET_ADDRSTRLEN];
            int port;

            if (sscanf(buffer + 5, "%15s %d", ip, &port) != 2) {
                send(client_fd, "FAIL\n", 7, 0);
            } else {
                printf("\nip = %s, port = %d\n", ip, port);
                pthread_mutex_lock(&active_users_mutex);
                int found_index = -1;
                printf("inside the mutex teehe\n");
                for (int i = 0; i < active_user_count; i++) {
                    printf("Checking user[%d]: ip=%s, port=%d\n", i, active_users[i].ip, active_users[i].p2p_port);
                    if (strcmp(active_users[i].ip, ip) == 0 && active_users[i].p2p_port == port) {
                        found_index = i;
                        printf("i have found the index lol = %d\n", found_index);
                        break;
                    }
                }
                pthread_mutex_unlock(&active_users_mutex);
                printf("\nfound index = %d\n", found_index);
                if (found_index == -1) {
                    send(client_fd, "FAIL\n", 7, 0);
                } else {
                    // Successfully found a target
                    char msg[BUFFER_SIZE];
                    snprintf(msg, sizeof(msg), "P2P_REQUEST: Connection query from user: %s\n", username);
                    send(active_users[found_index].fd, msg, strlen(msg), 0);
                    printf("Sent P2P request to %s (fd=%d)\n", active_users[found_index].username, active_users[found_index].fd);
                    char response[BUFFER_SIZE];
                    memset(response, 0, sizeof(response));

                    int n = recv(active_users[found_index].fd, response, sizeof(response), 0);
                    printf("n = %d\n", n);
                    if (n <= 0) {
                        send(client_fd, "FAIL\n", 7, 0);
                    } else {
                        if (response[0] == '1') {
                            printf("received an accept from client \n");
                            char msg[BUFFER_SIZE];
                            snprintf(msg, sizeof(msg), "ACCEPT:%s\n", active_users[found_index].username);
                            send(client_fd, msg, strlen(msg), 0);
                            printf("sent accept to client\n");
                        } else {
                            send(client_fd, "REJECT\n", 7, 0);
                        }
                    }
                } 
            }
        } else if (strncmp(buffer, "/broadcast", 10) == 0) {
            char *msg = buffer + 11;
            time_t now = time(NULL);
            struct tm tm_info;
            localtime_r(&now, &tm_info);
            char timestr[64];
            strftime(timestr, sizeof timestr, "%Y-%m-%d %H:%M:%S", &tm_info);

            char fullmsg[BUFFER_SIZE];
            snprintf(fullmsg, sizeof fullmsg, "[%s] %s: %s\n", timestr, username, msg);

            int udpsock = socket(AF_INET, SOCK_DGRAM, 0);
            if (udpsock < 0) {
                perror("socket(UDP)");
            } else {
                // 5) Send to every active user’s UDP port
                for (int i = 0; i < active_user_count; ++i) {
                    if (active_users[i].fd == client_fd) {
                        continue;
                    }
                    struct sockaddr_in peer_addr;
                    memset(&peer_addr, 0, sizeof peer_addr);
                    peer_addr.sin_family = AF_INET;
                    inet_pton(AF_INET,
                            active_users[i].ip,
                            &peer_addr.sin_addr);
                    peer_addr.sin_port = htons(active_users[i].udp_port);

                    sendto(udpsock,
                        fullmsg,
                        strlen(fullmsg),
                        0,
                        (struct sockaddr*)&peer_addr,
                        sizeof peer_addr);
                }
                close(udpsock);
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
                send(client_fd, "Login successful\n", 17, 0);
                memset(buffer, 0, BUFFER_SIZE);
                if (recv(client_fd, buffer, BUFFER_SIZE, 0) <= 0) break;
                buffer[strcspn(buffer, "\n")] = 0;
                int p2p_port = atoi(buffer);
                //printf("received p2p_port = %d\n", p2p_port);
                memset(buffer, 0, BUFFER_SIZE);
                if (recv(client_fd, buffer, BUFFER_SIZE, 0) <= 0) break;
                buffer[strcspn(buffer, "\n")] = 0;
                int udp_port = atoi(buffer);
                

                add_active_user(username, ip_str, port, client_fd, p2p_port, udp_port);
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
    fflush(stdout);

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