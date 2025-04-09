#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <pthread.h>
#include "database.h"

#define PORT 4400
#define BUFFER_SIZE 1024

void *handle_client(void *arg) {
    int client_fd = *(int *)arg;
    free(arg);

    char username[100], password[100];
    FILE *client_stream = fdopen(client_fd, "r");
    if (!client_stream) {
        perror("fdopen failed");
        close(client_fd);
        return NULL;
    }

    // Read username
    if (fgets(username, sizeof(username), client_stream) == NULL) {
        close(client_fd);
        return NULL;
    }
    username[strcspn(username, "\n")] = 0;

    // Read password
    if (fgets(password, sizeof(password), client_stream) == NULL) {
        close(client_fd);
        return NULL;
    }
    password[strcspn(password, "\n")] = 0;

    printf("Login attempt: %s / %s\n", username, password);

    if (db_login(username, password)) {
        send(client_fd, "SUCCESS\n", 8, 0);
        return NULL;
    } else {
        send(client_fd, "FAIL\n", 5, 0);
    }

    fclose(client_stream);
    return NULL;
}

int get_choice() {
    char input[10];  // buffer large enough for user input
    int choice = 0;

    while (1) {
        printf("Enter 1 for Login or 2 for Signup: ");
        if (fgets(input, sizeof(input), stdin) != NULL) {
            // Try to parse as integer
            if (sscanf(input, "%d", &choice) == 1 && (choice == 1 || choice == 2)) {
                return choice;
            }
        }
        printf("Invalid input. Please enter 1 or 2.\n");
    }
}

void credentials(char* user, char* pass) {
    char buffer[100];

    // Ask for username
    printf("Enter username: ");
    if (fgets(buffer, 100, stdin)) {
        buffer[strcspn(buffer, "\n")] = 0;  // strip newline
        strncpy(user, buffer, 100);
    }

    // Ask for password (masked)
    char *pw = getpass("Enter password: ");
    strncpy(pass, pw, 100);
}

int main() {
    db_connect();
    int server_fd;
    struct sockaddr_in address;
    int addrlen = sizeof(address);



    // while (1) {
    //     if (choice == 1) {
    //         printf("Username: ");
    //         if (fgets(user_input, sizeof(user_input), stdin) != NULL) {
    //             printf("Password: ");
    //             char pass_input[30];
    //             if (fgets(pass_input, sizeof(pass_input), stdin) != NULL) {
    //                 int login = db_login(user_input, pass_input);
    //             } 
    //     } else {
    //         printf("You chose: Signup\n");
    //     }
    // }


    // Create socket
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    // Bind
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }

    // Listen
    if (listen(server_fd, 10) < 0) {
        perror("listen failed");
        exit(EXIT_FAILURE);
    }

    printf("Server listening on port %d...\n", PORT);

    // Accept clients in a loop
    while (1) {
        int *client_fd = malloc(sizeof(int));
        *client_fd = accept(server_fd, (struct sockaddr *)&address, (socklen_t *)&addrlen);
        if (*client_fd < 0) {
            perror("accept failed");
            free(client_fd);
            continue;
        }

        // int choice = get_choice();
        // char user_input[100];
        // char pass_input[100];

        // int attempts = 0;
        // if (choice == 1) {
        //     while (attempts < 3) {
        //         credentials(user_input, pass_input);
        //         if (db_login(user_input, pass_input)) {
        //             printf("login successful");
        //             break;
        //         } else {
        //             printf("invalid credentials, try again");
        //             attempts++;   
        //         }

        //         if (attempts > 2) {
        //             printf("too many attempts, closing connection");
        //             free(client_fd);
        //         }
        //     }
        // } else {
        //     while (attempts < 3) {
        //         credentials(user_input, pass_input);
        //         if (db_signup(user_input, pass_input)) {
        //             printf("signup successful");
        //             break;
        //         } else {
        //             printf("signup failed, try again");
        //             attempts++;   
        //         }

        //         if (attempts > 2) {
        //             printf("too many attempts, closing connection");
        //             free(client_fd);
        //         }
        //     }
        // }

        // if (!client_fd) {
        //     continue;
        // }

        pthread_t tid;
        pthread_create(&tid, NULL, handle_client, client_fd);
        pthread_detach(tid);  // clean up when done
    }

    close(server_fd);

    db_close();
    return 0;
}
