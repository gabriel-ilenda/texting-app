#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 4400
#define BUFFER_SIZE 1024


int main() {
    int sock = 0;
    struct sockaddr_in serv_addr;
    char buffer[BUFFER_SIZE];

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("Socket creation error");
        return 1;
    }

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
        printf("What would you like to do?\n(Type an integer for your choice)\n1 Login\n2 Signup\nChoice: ");
        if (fgets(buffer, BUFFER_SIZE, stdin) == NULL) {
            printf("Input error.\n");
            continue;
        }
    
        log_or_sign = atoi(buffer); // Convert to integer
    
        if (log_or_sign != 1 && log_or_sign != 2) {
            printf("Invalid input. Please enter 1 for Login or 2 for Signup.\n");
        }
    } while (log_or_sign != 1 && log_or_sign != 2);

    buffer[0] = (char)log_or_sign;
    send(sock, buffer, 1, 0);


    // printf("What would you like to do?\nEnter integer:\n1 Login\n2 Signup)");
    // fgets(buffer, BUFFER_SIZE, stdin);
    // send(sock, buffer, strlen(buffer), 0);
    
    for (int attempt = 0; attempt < 3; attempt++) {
        if (log_or_sign == 1) {
            printf("Username: ");
            fgets(buffer, BUFFER_SIZE, stdin);
            send(sock, buffer, strlen(buffer), 0);

            printf("Password: ");
            fgets(buffer, BUFFER_SIZE, stdin);
            send(sock, buffer, strlen(buffer), 0);
        } else {
            char user[100];
            printf("Username: ");
            fgets(buffer, BUFFER_SIZE, stdin);
            buffer[strcspn(buffer, "\n")] = 0;
            strncpy(user, buffer, sizeof(user));
            user[sizeof(user) - 1] = 0;

            char check_user[100];
            printf("Confirm Username: ");
            fgets(buffer, BUFFER_SIZE, stdin);
            buffer[strcspn(buffer, "\n")] = 0;
            strncpy(check_user, buffer, sizeof(check_user));
            check_user[sizeof(check_user) - 1] = 0;

            if (strcmp(user, check_user) != 0) {
                printf("Error: usernames don't match!\n");
                continue;
            }

            send(sock, buffer, strlen(buffer), 0);

            char pass[100];
            printf("Password: ");
            fgets(buffer, BUFFER_SIZE, stdin);
            buffer[strcspn(buffer, "\n")] = 0;
            strncpy(pass, buffer, sizeof(pass));
            pass[sizeof(pass) - 1] = 0;

            char check_pass[100];
            printf("Confirm Password: ");
            fgets(buffer, BUFFER_SIZE, stdin);
            buffer[strcspn(buffer, "\n")] = 0;
            strncpy(check_pass, buffer, sizeof(check_pass));
            check_pass[sizeof(check_pass) - 1] = 0;

            if (strcmp(pass, check_pass) != 0) {
                printf("Error: passwords don't match!\n");
                continue;
            }

            send(sock, buffer, strlen(buffer), 0);

        }

        memset(buffer, 0, BUFFER_SIZE);
        int valread = recv(sock, buffer, BUFFER_SIZE, 0);
        if (valread <= 0) break;

        printf("%s", buffer);
        if (strstr(buffer, "successful")) {
            
            // Move this to its own method later
            while (1) {
                printf("Message to server: ");
                if (!fgets(buffer, BUFFER_SIZE, stdin)) break;
                send(sock, buffer, strlen(buffer), 0);
        
                memset(buffer, 0, BUFFER_SIZE);
                int valread = recv(sock, buffer, BUFFER_SIZE, 0);
                if (valread <= 0) break;
        
                printf("%s", buffer);
            }

            break;
        };
        if (strstr(buffer, "closed")) {
            break;   
        }
    }



    close(sock);
    return 0;
}
