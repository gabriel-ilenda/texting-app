#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 4400
#define BUFFER_SIZE 1024


void ask_credentials(char *username, char *password) {
    char buffer[100];

    printf("Enter username: ");
    fgets(buffer, sizeof(buffer), stdin);
    buffer[strcspn(buffer, "\n")] = 0;
    strcpy(username, buffer);

    char *pw = getpass("Enter password: ");
    strcpy(password, pw);
}

int send_credentials(int sockfd) {
    int attempts = 0;
    while (attempts < 3) {
        char username[100], password[100];
        ask_credentials(username, password);

        // Send username
        send(sockfd, username, strlen(username), 0);
        send(sockfd, "\n", 1, 0); // separate fields

        // Send password
        send(sockfd, password, strlen(password), 0);
        send(sockfd, "\n", 1, 0); // separate fields

        // Wait for response
        char response[100];
        int len = read(sockfd, response, sizeof(response)-1);
        if (len > 0) {
            response[len] = 0;
            if (strcmp(response, "SUCCESS\n") == 0) {
                printf("Logged in successfully!\n");
                return 1;
            } else {
                printf("Login failed.\n");
                attempts++;
                if (attempts == 3) {
                    printf("Too many attempts, closing connection");
                    return 0;
                }
            }
        }
    }
    return 0;
} 


int main() {
    int sock = 0;
    struct sockaddr_in serv_addr;
    char buffer[BUFFER_SIZE] = {0};

    // Create TCP socket
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("socket creation error");
        return -1;
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(SERVER_PORT);

    // Convert IP string to binary
    if (inet_pton(AF_INET, SERVER_IP, &serv_addr.sin_addr) <= 0) {
        perror("invalid address");
        return -1;
    }

    // Connect to server
    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("connection failed");
        return -1;
    }

    if (!send_credentials(sock)) {
        close(sock);
        return 0;
    }

    while (1) {
        printf("You: ");
        fgets(buffer, BUFFER_SIZE, stdin);

        // Send message
        send(sock, buffer, strlen(buffer), 0);

        // Receive echo
        memset(buffer, 0, BUFFER_SIZE);
        int bytes_read = read(sock, buffer, BUFFER_SIZE);
        if (bytes_read <= 0) break;

        printf("Echo: %s", buffer);
    }

    printf("Disconnected from server.\n");
    close(sock);
    return 0;
}
