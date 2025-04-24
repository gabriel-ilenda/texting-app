
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <bits/pthreadtypes.h>
#include <pthread.h>

#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 4400
#define BUFFER_SIZE 1024
#define USERNAME_LEN 100

typedef struct {
    int listener_fd;
    int server_fd;
    char username[100]; // optional if you need it
} P2PArgs;



int p2p_sock_fd = -1;  // P2P listening socket
int p2p_conn_fd = -1;  // Active connection (if accepted)
int p2p_flag = 0;

void *receive_messages(void *arg) {
    int conn = *((int *)arg);
    char buffer[1024];

    while (1) {
        memset(buffer, 0, sizeof(buffer));
        int n = recv(conn, buffer, sizeof(buffer), 0);
        if (n <= 0) {
            printf("\n[P2P] Disconnected by peer.\n");
            exit(1);
        }
        printf("\n[P2P] Peer: %s\nYou: ", buffer);
        fflush(stdout);
    }

    close(conn);
    pthread_exit(NULL);
}

void p2p_loop(int conn) {
    pthread_t recv_thread;
    pthread_create(&recv_thread, NULL, receive_messages, &conn);

    char buffer[1024];
    while (1) {
        printf("You: ");
        if (!fgets(buffer, sizeof(buffer), stdin)) break;
        buffer[strcspn(buffer, "\n")] = 0;

        if (strcmp(buffer, "/exit") == 0) {
            printf("Closing P2P connection.\n");
            break;
        }

        send(conn, buffer, strlen(buffer), 0);
    }

    // Shutdown and cleanup
    shutdown(conn, SHUT_RDWR);
    close(conn);
    pthread_cancel(recv_thread);  // Stop receiver thread
    pthread_join(recv_thread, NULL);
}


void *p2p_listener_thread(void *arg) {
    P2PArgs *args = (P2PArgs *)arg;
    int listener_fd = args->listener_fd;
    int server_fd = args->server_fd;

    free(args);

    struct sockaddr_in client_addr;
    socklen_t addrlen = sizeof(client_addr);

    int conn_fd = accept(listener_fd, (struct sockaddr *)&client_addr, &addrlen);
    if (conn_fd >= 0) {
        printf("\n[P2P] Connection established!\n");

        // Close connection to server
        close(server_fd);
        printf("[P2P] Server connection closed.\n");
        p2p_flag = 1;

        p2p_loop(conn_fd);
    }

    return NULL;
}



int login(int sock) {
    char buffer[BUFFER_SIZE];
    for (int attempt = 0; attempt < 3; attempt++) {
        printf("Username: ");
        fgets(buffer, BUFFER_SIZE, stdin);
        send(sock, buffer, strlen(buffer), 0);

        printf("Password: ");
        fgets(buffer, BUFFER_SIZE, stdin);
        send(sock, buffer, strlen(buffer), 0);

        memset(buffer, 0, BUFFER_SIZE);
        int valread = recv(sock, buffer, BUFFER_SIZE, 0);
        if (valread <= 0) break;

        printf("%s", buffer);
        if (strstr(buffer, "successful")) return 1;
    }
    return 0;
}

int signup(int sock) {
    char buffer[BUFFER_SIZE];
    for (int attempt = 0; attempt < 3; attempt++) {
        char user[100], check_user[100];
        char pass[100], check_pass[100];

        printf("Username: ");
        fgets(buffer, BUFFER_SIZE, stdin);
        buffer[strcspn(buffer, "\n")] = 0;
        strncpy(user, buffer, sizeof(user));
        user[sizeof(user) - 1] = 0;

        printf("Confirm Username: ");
        fgets(buffer, BUFFER_SIZE, stdin);
        buffer[strcspn(buffer, "\n")] = 0;
        strncpy(check_user, buffer, sizeof(check_user));
        check_user[sizeof(check_user) - 1] = 0;

        if (strcmp(user, check_user) != 0) {
            printf("Error: usernames don't match!\n");
            continue;
        }

        strcat(user, "\n");
        send(sock, user, strlen(user), 0);

        printf("Password: ");
        fgets(buffer, BUFFER_SIZE, stdin);
        buffer[strcspn(buffer, "\n")] = 0;
        strncpy(pass, buffer, sizeof(pass));
        pass[sizeof(pass) - 1] = 0;

        printf("Confirm Password: ");
        fgets(buffer, BUFFER_SIZE, stdin);
        buffer[strcspn(buffer, "\n")] = 0;
        strncpy(check_pass, buffer, sizeof(check_pass));
        check_pass[sizeof(check_pass) - 1] = 0;

        if (strcmp(pass, check_pass) != 0) {
            printf("Error: passwords don't match!\n");
            continue;
        }

        strcat(pass, "\n");
        send(sock, pass, strlen(pass), 0);

        memset(buffer, 0, BUFFER_SIZE);
        int valread = recv(sock, buffer, BUFFER_SIZE, 0);
        if (valread <= 0) break;

        printf("%s", buffer);
        if (strstr(buffer, "successful")) return 1;
    }
    return 0;
}

void active_list(int sock) {
    char buffer[BUFFER_SIZE];
    send(sock, "/who\n", strlen("/who\n"), 0);
    memset(buffer, 0, BUFFER_SIZE);
    recv(sock, buffer, BUFFER_SIZE, 0);
    printf("\n%s", buffer);
}

void send_p2p_request(int server_sock) {
    char ip[BUFFER_SIZE];
    char port[BUFFER_SIZE];

    printf("WARNING: ENTERING AN INCORRECT IP/PORT WILL END YOUR SESSION\n");

    printf("Enter the user's IP address: ");
    if (!fgets(ip, BUFFER_SIZE, stdin)) return;
    ip[strcspn(ip, "\n")] = 0;

    printf("Enter the user's port number: ");
    if (!fgets(port, BUFFER_SIZE, stdin)) return;
    port[strcspn(port, "\n")] = 0;

    int port_num = atoi(port);
    if (port_num <= 0 || port_num > 65535) {
        printf("Invalid port number.\n");
        return;
    }

    // Create socket
    int p2p_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (p2p_sock < 0) {
        perror("Failed to create P2P socket");
        return;
    }

    struct sockaddr_in peer_addr;
    memset(&peer_addr, 0, sizeof(peer_addr));
    peer_addr.sin_family = AF_INET;
    peer_addr.sin_port = htons(port_num);

    if (inet_pton(AF_INET, ip, &peer_addr.sin_addr) <= 0) {
        perror("Invalid IP address");
        close(p2p_sock);
        return;
    }

    printf("Connecting to peer %s:%d...\n", ip, port_num);
    if (connect(p2p_sock, (struct sockaddr *)&peer_addr, sizeof(peer_addr)) < 0) {
        perror("P2P connection failed");
        close(p2p_sock);
        return;
    }

    printf("✅ Connected to peer! Closing server connection...\n");
    close(server_sock);  // Close server socket

    // Chat loop with peer
    p2p_loop(p2p_sock);

}


int main() {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in serv_addr;
    char buffer[BUFFER_SIZE];

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(SERVER_PORT);
    inet_pton(AF_INET, SERVER_IP, &serv_addr.sin_addr);

    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("Connection failed");
        return 1;
    }

    printf("Connected to server.\n");

    int log_or_sign = 0;
    int login_check = 0;

    while (!login_check) {
        printf("1 Login\n2 Signup\nChoice: ");
        if (fgets(buffer, BUFFER_SIZE, stdin) == NULL) continue;
        log_or_sign = atoi(buffer);

        if (log_or_sign == 1) {
            buffer[0] = (char)log_or_sign;
            send(sock, buffer, 1, 0);
            if (login(sock)) {
                login_check = 1;  // success
            } else {
                printf("\nExceeded max amount of attempts.\n");
                close(sock);
                return 0;
            }
        } else if (log_or_sign == 2) {
            buffer[0] = (char)log_or_sign;
            send(sock, buffer, 1, 0);
            if (!signup(sock)) {
                printf("\nExceeded max amount of attempts.\n");
                close(sock);
                return 0;
            }
            // Prompt to log in after successful signup
            printf("\nSignup successful. Please log in now.\n");
        } else {
            printf("\nError: Invalid choice (must select 1 or 2)\n");
        }
    }

    // Setup P2P listener socket
    p2p_sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in p2p_addr;
    p2p_addr.sin_family = AF_INET;
    p2p_addr.sin_addr.s_addr = INADDR_ANY;
    p2p_addr.sin_port = 0;  // let OS pick port

    bind(p2p_sock_fd, (struct sockaddr *)&p2p_addr, sizeof(p2p_addr));
    listen(p2p_sock_fd, 5);

    // Send port to server
    socklen_t addr_len = sizeof(p2p_addr);
    getsockname(p2p_sock_fd, (struct sockaddr *)&p2p_addr, &addr_len);
    int p2p_port = ntohs(p2p_addr.sin_port);
    // printf("p2p port = %d\n", p2p_port);
    sprintf(buffer, "%d\n", p2p_port);
    send(sock, buffer, strlen(buffer), 0);

    P2PArgs *args = malloc(sizeof(P2PArgs));
    args->listener_fd = p2p_sock_fd;
    args->server_fd = sock; // your existing connection to the server

    pthread_t tid;
    pthread_create(&tid, NULL, p2p_listener_thread, args);


    while (1) {
        printf("\nType '/who' to see active users.");
        printf("\nType '/p2p' to start a p2p connection.");
        printf("\nType '/broadcast' to broadcast a message.");
        printf("\nCommand: ");
        // if (!p2p_flag) {
        //     pthread_exit(NULL);
        // }
        if (!fgets(buffer, BUFFER_SIZE, stdin)) break;
    
        buffer[strcspn(buffer, "\n")] = 0;
    
        if (strcmp(buffer, "/exit") == 0) break;
        else if (strcmp(buffer, "/who") == 0) active_list(sock);
        else if (strcmp(buffer, "/p2p") == 0) send_p2p_request(sock);
        else if (p2p_flag) pthread_exit(NULL);
        else printf("Error: Invalid command\n");
    }
    
    close(sock);
    return 0;
}