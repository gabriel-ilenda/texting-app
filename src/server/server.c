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

/*
Users have a username, ip, tcp->server port, tcp->p2p port, and udp->server port. 
Users can see each other's username, ip, and p2p port.
*/
typedef struct {
    char username[USERNAME_LEN];
    char ip[INET_ADDRSTRLEN];
    int port;
    int fd;
    int p2p_port;
    int udp_port;
} ActiveUser;

/*
Uses threading to check the users to avoid conflicts/delays when there are subsequent resquests
from clients.
*/
ActiveUser active_users[MAX_USERS];
int active_user_count = 0;
pthread_mutex_t active_users_mutex = PTHREAD_MUTEX_INITIALIZER;

/*
Add a user if there are less than 50 users connected to server.
*/
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

/*
Remove users from list when they disconnect or are force-closed by server.
*/
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

/*
Ends a client connection and removes the user from the list.
*/
void kill_client(char *username, int sock) {
    close(sock);
    pthread_exit(NULL);
    remove_active_user(username);
}

/*
After a client is authenticated, they enter this loop. Server handles /who,
/p2p, and /broadcast commands respectively.
*/
void client_loop(char username[], int client_fd) {
    char buffer[BUFFER_SIZE];
    while (1) {
        memset(buffer, 0, BUFFER_SIZE);
        ssize_t n = recv(client_fd, buffer, BUFFER_SIZE, 0);
        if (n <= 0) break;
        buffer[strcspn(buffer, "\n")] = 0; // receive command from user in buffer

        if (strcmp(buffer, "/exit") == 0) {
            kill_client(username, client_fd); // /exit = force kill client

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
            send(client_fd, users_msg, strlen(users_msg), 0); // /who = list of active user, ip, p2p port 

        } else if (strncmp(buffer, "/p2p", 4) == 0) { // /p2p = request p2p connection
            char ip[INET_ADDRSTRLEN];
            int port;

            if (sscanf(buffer + 5, "%15s %d", ip, &port) != 2) {
                send(client_fd, "FAIL\n", 7, 0);
            } else {
                printf("\nip = %s, port = %d\n", ip, port); // expect from user: "/p2p <ip> <port>"
                pthread_mutex_lock(&active_users_mutex);
                int found_index = -1;
                printf("inside the mutex teehe\n");
                for (int i = 0; i < active_user_count; i++) {
                    printf("Checking user[%d]: ip=%s, port=%d\n", i, active_users[i].ip, active_users[i].p2p_port);
                    if (strcmp(active_users[i].ip, ip) == 0 && active_users[i].p2p_port == port) {
                        found_index = i; // found matching ip and p2p port in users list (request was valid)
                        printf("i have found the index lol = %d\n", found_index);
                        break;
                    }
                }
                pthread_mutex_unlock(&active_users_mutex);
                printf("\nfound index = %d\n", found_index);
                if (found_index == -1) {
                    send(client_fd, "FAIL\n", 7, 0); // did not find ip and p2p (request was invalid)
                } else {
                    char msg[BUFFER_SIZE];
                    // P2P_REQUEST = scan in client to see if someone is trying to connect
                    // end of request = username, saved in target client as peer username
                    snprintf(msg, sizeof(msg), "P2P_REQUEST: Connection query from user: %s\n", username);
                    send(active_users[found_index].fd, msg, strlen(msg), 0);
                    printf("Sent P2P request to %s (fd=%d)\n", active_users[found_index].username, active_users[found_index].fd);

                    char response[BUFFER_SIZE];
                    memset(response, 0, sizeof(response)); // immediately await response (1 = yes, anything else = no)
                    int n = recv(active_users[found_index].fd, response, sizeof(response), 0);
                    printf("n = %d\n", n);

                    if (n <= 0) {
                        send(client_fd, "FAIL\n", 7, 0); // bad request or no response
                    } else {
                        if (response[0] == '1') {
                            printf("received an accept from client \n");
                            char msg[BUFFER_SIZE];
                            snprintf(msg, sizeof(msg), "ACCEPT:%s\n", active_users[found_index].username);
                            // ACCEPT = tells initiator they want to open p2p
                            // end of message = username, saved in initiator client as peer username
                            send(client_fd, msg, strlen(msg), 0); 
                            printf("sent accept to client\n");
                        } else {
                            send(client_fd, "REJECT\n", 7, 0); // client rejected
                        }
                    }
                } 
            }

        } else if (strncmp(buffer, "/broadcast", 10) == 0) {
            char *msg = buffer + 11; // expect from client "/broadcast <message>"
            time_t now = time(NULL);
            struct tm tm_info;
            localtime_r(&now, &tm_info);
            char timestr[64];
            strftime(timestr, sizeof timestr, "%Y-%m-%d %H:%M:%S", &tm_info); // date + time

            char fullmsg[BUFFER_SIZE];
            // message = "[date+time] username: <message>"
            snprintf(fullmsg, sizeof fullmsg, "[%s] %s: %s\n", timestr, username, msg);
            
            int udpsock = socket(AF_INET, SOCK_DGRAM, 0); // open server udp socket
            if (udpsock < 0) {
                perror("socket(UDP)");
            } else {
                for (int i = 0; i < active_user_count; ++i) {
                    if (active_users[i].fd == client_fd) {
                        continue; // skip initiator of broadcast
                    }
                    struct sockaddr_in peer_addr;
                    memset(&peer_addr, 0, sizeof peer_addr);
                    peer_addr.sin_family = AF_INET;
                    // specify the ip and udp port for each client to recieve message
                    inet_pton(AF_INET,
                            active_users[i].ip,
                            &peer_addr.sin_addr);
                    peer_addr.sin_port = htons(active_users[i].udp_port);
                    sendto(udpsock, fullmsg, strlen(fullmsg), 0, (struct sockaddr*)&peer_addr, sizeof peer_addr);
                }
                close(udpsock);
            }
        } else {
            char *err = "server unknown command\n";
            send(client_fd, err, strlen(err), 0); // should never happen, but if an unrecognized command gets thru
        }
    }

    if (strlen(username) > 0) {
        remove_active_user(username); // if user no longer is registered as connceted with server
        printf("User %s disconnected.\n", username);
    }
}

/*
Handles initial startup of client. Expects 0+ signup attempts and 1+ login attempts. Adds
new users to the active user lists and accesses the mysql database for authentication.
*/
void *handle_client(void *arg) {
    int client_fd = *((int *)arg);
    free(arg);

    // read IP and port of the connection between the server and client
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
        // only expect 2 messages from user: a username and a password
        memset(buffer, 0, BUFFER_SIZE);
        if (recv(client_fd, buffer, BUFFER_SIZE, 0) <= 0) break;
        buffer[strcspn(buffer, "\n")] = 0;
        strncpy(username, buffer, USERNAME_LEN - 1);
        username[USERNAME_LEN - 1] = '\0'; // grab username

        memset(buffer, 0, BUFFER_SIZE);
        if (recv(client_fd, buffer, BUFFER_SIZE, 0) <= 0) break;
        buffer[strcspn(buffer, "\n")] = 0;
        char password[100];
        strncpy(password, buffer, sizeof(password) - 1);
        password[sizeof(password) - 1] = '\0'; // grab password

        if (log_or_sign == 1) { // login attempt
            if (db_login(username, password)) {
                authenticated = 1;
                send(client_fd, "Login successful\n", 17, 0);

                // when a user is authenticated, they set up a p2p connection socket
                // expect this socket to be sent over immediately for storage
                memset(buffer, 0, BUFFER_SIZE);
                if (recv(client_fd, buffer, BUFFER_SIZE, 0) <= 0) break;
                buffer[strcspn(buffer, "\n")] = 0;
                int p2p_port = atoi(buffer);
                
                // then they set up a udp port, expect this to be sent next
                memset(buffer, 0, BUFFER_SIZE);
                if (recv(client_fd, buffer, BUFFER_SIZE, 0) <= 0) break;
                buffer[strcspn(buffer, "\n")] = 0;
                int udp_port = atoi(buffer);
                
                // add username, ip, server port, client-server socket, p2p socket, udp socket to list
                add_active_user(username, ip_str, port, client_fd, p2p_port, udp_port);
                client_loop(username, client_fd); // enter client loop indefinitely
            } else {
                attempts++;
                if (attempts < 3)
                    send(client_fd, "Login failed. Try again.\n", 26, 0);
                else
                    // client handles closing when the user fails 3 times
                    send(client_fd, "Login failed 3 times. Connection closed.\n", 42, 0);
            }

        } else if (log_or_sign == 2) { // signup attempt
            if (db_signup(username, password)) { 
                send(client_fd, "Signup successful. Please login now.\n", 38, 0);
                attempts = 0;
                if (recv(client_fd, &log_or_sign, 1, 0) <= 0) break; // Expect login
            } else {
                attempts++;
                if (attempts < 3)
                    send(client_fd, "Signup failed. Try again.\n", 27, 0);
                else
                    // client handles closing when the user fails 3 times
                    send(client_fd, "Signup failed 3 times. Connection closed.\n", 43, 0);
            }

        } else { // user broke the commands, close connection out of safety
            send(client_fd, "Invalid mode. Connection closed.\n", 33, 0);
            close(client_fd);
            pthread_exit(NULL);
        }
    }

    // client has reached end of loop and now needs to be disconnected
    close(client_fd);
    pthread_exit(NULL);
}

int main() {

    int server_fd, *client_fd;
    struct sockaddr_in address;
    socklen_t addrlen = sizeof(address);

    // socket for server 
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    // bind the server to the socket
    bind(server_fd, (struct sockaddr *)&address, sizeof(address));
    listen(server_fd, 5);
    db_connect(); // connect to database

    printf("Server listening on port %d...\n", PORT);
    fflush(stdout); // helps with buffering 

    while (1) {
        // create a client thread, go to handle_client method
        client_fd = malloc(sizeof(int));
        *client_fd = accept(server_fd, (struct sockaddr *)&address, &addrlen);
        pthread_t tid;
        pthread_create(&tid, NULL, handle_client, client_fd);
        pthread_detach(tid);
    }
    db_close(); // close mysql connection
    close(server_fd); // close server
    return 0;
}