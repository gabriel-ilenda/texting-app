// CLIENT CODE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/select.h>

#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 4400
#define BUFFER_SIZE 1024
#define USERNAME_LEN 100

int p2p_request_pending = 0;
char p2p_request_from[USERNAME_LEN];

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

void send_p2p_request(int sock) {
    char ip[BUFFER_SIZE];
    char port[BUFFER_SIZE];
    char command[BUFFER_SIZE];

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

    snprintf(command, sizeof(command), "/p2p %s %s\n", ip, port);
    send(sock, command, strlen(command), 0);
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
    do {
        printf("1 Login\n2 Signup\nChoice: ");
        if (fgets(buffer, BUFFER_SIZE, stdin) == NULL) continue;
        log_or_sign = atoi(buffer);
    } while (log_or_sign != 1 && log_or_sign != 2);

    buffer[0] = (char)log_or_sign;
    send(sock, buffer, 1, 0);

    int success = (log_or_sign == 1) ? login(sock) : signup(sock);
    if (!success) {
        printf("Authentication failed.\n");
        close(sock);
        return 0;
    }

    fd_set read_fds;
    int max_fd = sock > STDIN_FILENO ? sock : STDIN_FILENO;

    while (1) {
        FD_ZERO(&read_fds);
        FD_SET(STDIN_FILENO, &read_fds);
        FD_SET(sock, &read_fds);

        if (select(max_fd + 1, &read_fds, NULL, NULL, NULL) < 0) {
            perror("select");
            break;
        }

        if (FD_ISSET(sock, &read_fds)) {
            memset(buffer, 0, BUFFER_SIZE);
            int n = recv(sock, buffer, BUFFER_SIZE, 0);
            if (n <= 0) {
                printf("\nServer disconnected.\n");
                break;
            }

            buffer[strcspn(buffer, "\n")] = 0;

            if (strncmp(buffer, "P2P_REQUEST_FROM:", 17) == 0) {
                strncpy(p2p_request_from, buffer + 17, USERNAME_LEN - 1);
                p2p_request_from[USERNAME_LEN - 1] = '\0';

                printf("\n[P2P] %s wants to connect with you. Accept? (yes/no): ", p2p_request_from);
                fflush(stdout);
                if (fgets(buffer, BUFFER_SIZE, stdin) == NULL) break;
                buffer[strcspn(buffer, "\n")] = 0;

                if (strcmp(buffer, "yes") == 0)
                    snprintf(buffer, BUFFER_SIZE, "P2P_ACCEPT:%s\n", p2p_request_from);
                else
                    snprintf(buffer, BUFFER_SIZE, "P2P_REJECT:%s\n", p2p_request_from);

                send(sock, buffer, strlen(buffer), 0);
                continue;
            } else {
                printf("\n[SERVER] %s\n", buffer);
            }
        }

        if (FD_ISSET(STDIN_FILENO, &read_fds)) {
            printf("Command: ");
            if (!fgets(buffer, BUFFER_SIZE, stdin)) break;
            buffer[strcspn(buffer, "\n")] = 0;

            if (strcmp(buffer, "/exit") == 0) break;
            else if (strcmp(buffer, "/who") == 0) active_list(sock);
            else if (strcmp(buffer, "/p2p") == 0) send_p2p_request(sock);
            else send(sock, buffer, strlen(buffer), 0);
        }
    }

    close(sock);
    return 0;
}