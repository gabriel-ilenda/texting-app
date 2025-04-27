
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <bits/pthreadtypes.h>
#include <pthread.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>



#ifndef SERVER_HOST
#define SERVER_HOST "127.0.0.1"
#endif

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

pthread_mutex_t prompt_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t prompt_cond = PTHREAD_COND_INITIALIZER;
int awaiting_p2p_decision = 0;
char p2p_request_msg[BUFFER_SIZE];

char client_username[USERNAME_LEN] = {0};
char peer_username  [USERNAME_LEN] = {0};


void *request_listener(void *arg) {
    int sock = *(int *)arg;
    char buffer[BUFFER_SIZE];

    while (1) {
        memset(buffer, 0, sizeof(buffer));
        int n = recv(sock, buffer, BUFFER_SIZE, 0);
        if (n <= 0) {
            printf("\n[SERVER] Connection lost.\n");
            exit(1);
        }

        // Handle a P2P request
        if (strncmp(buffer, "P2P_REQUEST:", strlen("P2P_REQUEST:")) == 0) {

            // write(STDOUT_FILENO, "\n", 1); 
            // fflush(stdout);
            pthread_mutex_lock(&prompt_mutex);
            awaiting_p2p_decision = 1;
            strncpy(p2p_request_msg, buffer + strlen("P2P_REQUEST:"), sizeof(p2p_request_msg));
            pthread_cond_signal(&prompt_cond);
            pthread_mutex_unlock(&prompt_mutex);   
        }
    }

    return NULL;
}


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

// call this in both sides (initiator after connect, target after accept)
// call this in both sides (initiator after connect, target after accept)
void p2p_chat(int conn_fd) {
    fd_set fds;
    int   maxfd = conn_fd > STDIN_FILENO ? conn_fd : STDIN_FILENO;
    char  buf[BUFFER_SIZE];

    printf("\n📡 P2P chat with %s. Type /exit to leave.\nYou: ", peer_username);
    fflush(stdout);

    while (1) {
        FD_ZERO(&fds);
        FD_SET(STDIN_FILENO, &fds);
        FD_SET(conn_fd,      &fds);

        if (select(maxfd+1, &fds, NULL, NULL, NULL) < 0) {
            perror("p2p select");
            break;
        }

        // Peer → us
        if (FD_ISSET(conn_fd, &fds)) {
            int n = recv(conn_fd, buf, sizeof(buf)-1, 0);
            if (n <= 0) {
                printf("\n[%s] Disconnected.\n", peer_username);
                break;
            }
            buf[n] = '\0';
            printf("\n%s: %s\nYou: ", peer_username, buf);
            fflush(stdout);
        }

        // us → peer
        if (FD_ISSET(STDIN_FILENO, &fds)) {
            if (!fgets(buf, sizeof(buf), stdin)) break;
            buf[strcspn(buf, "\n")] = '\0';
            if (strcmp(buf, "/exit") == 0) {
                printf("Closing P2P connection.\n");
                break;
            }
            send(conn_fd, buf, strlen(buf), 0);
            printf("You: "); fflush(stdout);
        }
    }

    close(conn_fd);
    printf(">> Exited P2P chat.\n");
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
        close(server_fd);
        printf("\n[P2P] Connection established!\n");
        printf("[P2P] Server connection closed.\n");
        p2p_flag = 1;

        p2p_chat(conn_fd);
        exit(0);
    }

    return NULL;
}



int login(int sock) {
    char buffer[BUFFER_SIZE];
    for (int attempt = 0; attempt < 3; attempt++) {
        printf("Username: ");
        if (!fgets(buffer, BUFFER_SIZE, stdin)) return 0;
        buffer[strcspn(buffer, "\n")] = '\0';

        // store globally
        strncpy(client_username, buffer, USERNAME_LEN-1);

        // then send
        strcat(buffer, "\n");
        send(sock, buffer, strlen(buffer), 0);

        printf("Password: ");
        fgets(buffer, BUFFER_SIZE, stdin);
        send(sock, buffer, strlen(buffer), 0);

        memset(buffer, 0, BUFFER_SIZE);
        int valread = recv(sock, buffer, BUFFER_SIZE, 0);
        if (valread <= 0) break;

        printf("%s", buffer);
        if (strstr(buffer, "successful")) {
            return 1;
        }
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

int send_p2p_request(int server_sock) {
    char ip[BUFFER_SIZE];
    char port[BUFFER_SIZE];

    printf("WARNING: ENTERING AN INCORRECT IP/PORT WILL END YOUR SESSION\n");

    printf("Enter the user's IP address: ");
    if (!fgets(ip, BUFFER_SIZE, stdin)) return 0;
    ip[strcspn(ip, "\n")] = 0;

    printf("Enter the user's port number: ");
    if (!fgets(port, BUFFER_SIZE, stdin)) return 0;
    port[strcspn(port, "\n")] = 0;

    int port_num = atoi(port);
    if (port_num <= 0 || port_num > 65535) {
        printf("Invalid port number.\n");
        return 0;
    }

    char message[BUFFER_SIZE];
    snprintf(message, sizeof(message), "/p2p %s %s\n", ip, port);
    send(server_sock, message, strlen(message), 0);

    char response[BUFFER_SIZE];
    memset(response, 0, BUFFER_SIZE);
    recv(server_sock, response, BUFFER_SIZE, 0);

    response[strcspn(response, "\n")] = 0;

    if (strcmp(response, "FAIL") == 0) {
        printf("\nError! Failed find the client or get a response. Closing connection...\n");
        close(server_sock);
        return 0;
    } else if (strcmp(response, "REJECT") == 0) {
        printf("\nThe client rejected the request.\n");
        return 0;
    } else if (strncmp(response, "ACCEPT:", 7) == 0) {
        // Create socket
        char *their_name = response + 7;     // points just past the colon
        // copy into your peer_username buffer
        strncpy(peer_username, their_name, USERNAME_LEN-1);
        peer_username[USERNAME_LEN-1] = '\0';
        int p2p_sock = socket(AF_INET, SOCK_STREAM, 0);
        if (p2p_sock < 0) {
            perror("System Error: Failed to create P2P socket\n");
            return 0;
        }

        struct sockaddr_in peer_addr;
        memset(&peer_addr, 0, sizeof(peer_addr));
        peer_addr.sin_family = AF_INET;
        peer_addr.sin_port = htons(port_num);

        if (inet_pton(AF_INET, ip, &peer_addr.sin_addr) <= 0) {
            perror("System Error: Invalid IP address\n");
            close(p2p_sock);
            return 0;
        }

        printf("Connecting to peer %s:%d...\n", ip, port_num);
        if (connect(p2p_sock, (struct sockaddr *)&peer_addr, sizeof(peer_addr)) < 0) {
            perror("Systen ErrorL P2P connection failed\n");
            close(p2p_sock);
            return 0;
        }

        printf("✅ Connected to peer! Closing server connection...\n");
        return p2p_sock;
    } else {
        printf("\nUnknown response from server\n");
        return 0;
    }

    return 0;
}


int main() {
    setvbuf(stdout, NULL, _IONBF, 0);
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) { perror("socket"); return 1; }

    const char *host = getenv("SERVER_HOST") ? getenv("SERVER_HOST") : SERVER_HOST;
    char portstr[6];
    snprintf(portstr, sizeof portstr, "%d", SERVER_PORT);

    struct addrinfo hints = {0}, *res, *p;
    hints.ai_family   = AF_UNSPEC;      // either IPv4 or IPv6
    hints.ai_socktype = SOCK_STREAM;

    int rv = getaddrinfo(host, portstr, &hints, &res);
    if (rv) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return 1;
    }

    int connected = 0;
    for (p = res; p; p = p->ai_next) {
        if ((sock = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) < 0) {
            continue;  // try next
        }
        if (connect(sock, p->ai_addr, p->ai_addrlen) == 0) {
            connected = 1;
            break;
        }
        close(sock);
    }
    freeaddrinfo(res);

    if (!connected) {
        fprintf(stderr, "Failed to connect to %s:%s\n", host, portstr);
        return 1;
    }

    // ----- Auth (login/signup) -----
    // (Assume you have login(sock) and signup(sock) implemented)
    int mode, done = 0;
    char buffer[BUFFER_SIZE];
    while (!done) {
        printf("1 Login\n2 Signup\nChoice: ");
        if (!fgets(buffer, sizeof(buffer), stdin)) return 0;
        mode = atoi(buffer);

        buffer[0] = (char)mode;
        send(sock, buffer, 1, 0);

        if (mode == 1 && login(sock))   done = 1;
        else if (mode == 2 && signup(sock)) {
            printf("\nSignup successful. Please login now.\n");
        }
    }

    // ----- Setup P2P listener -----
    int p2p_sock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in p2p_addr = {
        .sin_family = AF_INET,
        .sin_addr.s_addr = INADDR_ANY,
        .sin_port = 0  // let OS pick port
    };
    bind(p2p_sock, (struct sockaddr *)&p2p_addr, sizeof(p2p_addr));
    listen(p2p_sock, 5);

    socklen_t alen = sizeof(p2p_addr);
    getsockname(p2p_sock, (struct sockaddr *)&p2p_addr, &alen);
    int p2p_port = ntohs(p2p_addr.sin_port);
    sprintf(buffer, "%d\n", p2p_port);
    send(sock, buffer, strlen(buffer), 0);

    // ----- Setup UDP listener --------
    int udp_sock = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in udp_addr = {
        .sin_family = AF_INET,
        .sin_addr.s_addr = INADDR_ANY,
        .sin_port = 0  // let OS pick port
    };
    bind(udp_sock, (struct sockaddr *)&udp_addr, sizeof(udp_addr));
    listen(udp_sock, 5);

    socklen_t alen2 = sizeof(udp_addr);
    getsockname(udp_sock, (struct sockaddr *)&udp_addr, &alen2);
    int udp_port = ntohs(udp_addr.sin_port);
    sprintf(buffer, "%d\n", udp_port);
    send(sock, buffer, strlen(buffer), 0);

    
    printf("Command List:\n"
        "  /who       List active users\n"
        "  /p2p       Peer-to-peer chat\n"
        "  /broadcast Broadcast to everyone\n"
        "  /exit      Quit\n\n");

    

    // ----- Unified select() loop -----
    fd_set read_fds;
    int maxfd = sock;
    if (p2p_sock > maxfd) maxfd = p2p_sock;
    if (udp_sock  > maxfd) maxfd = udp_sock;
    if (STDIN_FILENO > maxfd) maxfd = STDIN_FILENO;

    while (1) {

        printf("You: ");
        fflush(stdout);

        FD_ZERO(&read_fds);
        FD_SET(STDIN_FILENO, &read_fds);
        FD_SET(sock,         &read_fds);
        FD_SET(p2p_sock,     &read_fds);
        FD_SET(udp_sock,     &read_fds);

        if (select(maxfd+1, &read_fds, NULL, NULL, NULL) < 0) {
            perror("select");
            break;
        }

        // — Incoming server message or P2P reply —
        if (FD_ISSET(sock, &read_fds)) {
            char srv[BUFFER_SIZE] = {0};
            int n = recv(sock, srv, sizeof(srv)-1, 0);
            if (n <= 0) {
                printf("\n[SERVER] Disconnected.\n");
                break;
            }
            srv[n] = '\0';

            // P2P request from another client
            if (strncmp(srv, "P2P_REQUEST:", 12) == 0) {
                printf("\n[SERVER] %s", srv + 12);
                char *colon = strrchr(srv, ':');
                if (colon) {
                    // skip the colon and the space
                    char *name = colon + 2;
                    // strip trailing newline
                    char *nl = strchr(name, '\n');
                    if (nl) *nl = '\0';
                    // copy into our global
                    strncpy(peer_username, name, USERNAME_LEN-1);
                    peer_username[USERNAME_LEN-1] = '\0';
                }
                // Prompt user right here:
                int ans = -1;
                while (ans<0) {
                    printf("[P2P] Accept? (1=yes/0=no): ");
                    if (!fgets(buffer, sizeof(buffer), stdin)) break;
                    if (buffer[0]=='1'||buffer[0]=='0')
                        ans = buffer[0]-'0';
                }
                buffer[0] = (char)(ans+'0');
                send(sock, buffer, 1, 0);
            }
            else {
                printf("\n[SERVER] %s\n", srv);
            }
        }

        // — Incoming P2P connection to *us* —
        if (FD_ISSET(p2p_sock, &read_fds)) {
            int peer = accept(p2p_sock, NULL, NULL);
            if (peer >= 0) {
                close(sock);          // tear down server link
                p2p_chat(peer);       // unified chat
                return 0;             // done
            }
        }

        if (FD_ISSET(udp_sock, &read_fds)) {
            char msg[BUFFER_SIZE];
            struct sockaddr_in src;
            socklen_t src_len = sizeof(src);
    
            ssize_t n = recvfrom(
                udp_sock,
                msg, sizeof(msg)-1,
                0,
                (struct sockaddr*)&src, &src_len
            );
            if (n > 0) {
                msg[n] = '\0';
                // print as soon as it arrives
                printf("\n[BROADCAST] %s\n",msg);
                // reprint your prompt if waiting for user
                fflush(stdout);
            }
        }

        // — User typed a command —
        if (FD_ISSET(STDIN_FILENO, &read_fds)) {
            if (!fgets(buffer, sizeof(buffer), stdin)) break;
            buffer[strcspn(buffer,"\n")] = 0;

            if (strcmp(buffer, "/exit")==0) break;
            else if (strcmp(buffer, "/who")==0) active_list(sock);
            else if (strcmp(buffer, "/p2p")==0) {
                int peer_fd = send_p2p_request(sock);
                if (peer_fd > 0) {
                    close(sock);
                    p2p_chat(peer_fd);
                    return 0;
                }
            } else if (strcmp(buffer, "/broadcast")==0) {
                char msg[BUFFER_SIZE];
                printf("Message to All: ");
                if (!fgets(msg, BUFFER_SIZE, stdin)) return 0;
                msg[strcspn(msg, "\n")] = 0;
                char broadcast[BUFFER_SIZE];
                snprintf(broadcast, sizeof(broadcast), "/broadcast %s\n", msg);
                send(sock, broadcast, strlen(broadcast), 0);
            }
            else {
                printf("Error: Unknown command.\n");
            }
        }
    }

    close(sock);
    return 0;
}