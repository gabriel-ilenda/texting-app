
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

// assume 127.0.0.1 if server can't comm with client
#ifndef SERVER_HOST
#define SERVER_HOST "127.0.0.1"
#endif

#define SERVER_PORT 4400
#define BUFFER_SIZE 1024
#define USERNAME_LEN 100

char client_username[USERNAME_LEN] = {0};
char peer_username  [USERNAME_LEN] = {0};

/*
Assumes we have 2 clients involved in a p2p connection over a tcp socket. Uses select
to constantly scan for stdin or communication over p2p socket. Called in initiator after
connect, in target after they accept.
*/
void p2p_chat(int conn_fd) {
    fd_set fds;
    int   maxfd = conn_fd > STDIN_FILENO ? conn_fd : STDIN_FILENO;
    char  buf[BUFFER_SIZE];

    // startup message for both users
    printf("\nP2P chat with %s. Type /exit to leave.\nYou: ", peer_username);
    fflush(stdout);

    // infinite loop for chat
    while (1) {
        // stdin and p2p in select loop
        FD_ZERO(&fds);
        FD_SET(STDIN_FILENO, &fds);
        FD_SET(conn_fd,      &fds);

        // couldn't select a fd to read
        if (select(maxfd+1, &fds, NULL, NULL, NULL) < 0) {
            perror("p2p select");
            break;
        }

        // reading from p2p input
        if (FD_ISSET(conn_fd, &fds)) {
            int n = recv(conn_fd, buf, sizeof(buf)-1, 0);
            if (n <= 0) {
                printf("\n[%s] Disconnected.\n", peer_username);
                break; // client disconnected, end loop for us
            }
            buf[n] = '\0';
            printf("\n%s: %s\nYou: ", peer_username, buf); // print peer's username with their message
            fflush(stdout);
        }

        // reading from stdin input
        if (FD_ISSET(STDIN_FILENO, &fds)) {
            if (!fgets(buf, sizeof(buf), stdin)) break; // we disconnected
            buf[strcspn(buf, "\n")] = '\0';
            if (strcmp(buf, "/exit") == 0) { // /exit = close connection
                printf("Closing P2P connection.\n");
                break;
            }
            send(conn_fd, buf, strlen(buf), 0); // send our message, each person handles usernames separately
            printf("You: "); 
            fflush(stdout); // kill buffering
        }
    }
    close(conn_fd);
    printf(">> Exited P2P chat.\n");
}

/*
Send username and password to server to see if the client can be authenticated.
*/
int login(int sock) {
    char buffer[BUFFER_SIZE];
    for (int attempt = 0; attempt < 3; attempt++) {
        printf("Username: ");
        if (!fgets(buffer, BUFFER_SIZE, stdin)) return 0;
        buffer[strcspn(buffer, "\n")] = '\0'; // username 
        strncpy(client_username, buffer, USERNAME_LEN-1); // store username as gloabl var
        strcat(buffer, "\n");
        send(sock, buffer, strlen(buffer), 0); // server await user

        printf("Password: ");
        fgets(buffer, BUFFER_SIZE, stdin); // password
        send(sock, buffer, strlen(buffer), 0); // server awaits password

        memset(buffer, 0, BUFFER_SIZE);
        int valread = recv(sock, buffer, BUFFER_SIZE, 0);
        if (valread <= 0) break;

        printf("%s\n", buffer);
        if (strstr(buffer, "successful")) {
            return 1; // success
        } else if (strstr(buffer, "mode")) {
            return -1;
        }
    }
    return 0;
}

/*
Sends username and password for a signup attempt server after the client has confirmed
the user and password.
*/
int signup(int sock) {
    char buffer[BUFFER_SIZE];
    for (int attempt = 0; attempt < 3; attempt++) {
        char user[100], check_user[100]; // compare both
        char pass[100], check_pass[100]; // compare both

        printf("Username: ");
        fgets(buffer, BUFFER_SIZE, stdin);
        buffer[strcspn(buffer, "\n")] = 0;
        strncpy(user, buffer, sizeof(user));
        user[sizeof(user) - 1] = 0; // user

        printf("Confirm Username: ");
        fgets(buffer, BUFFER_SIZE, stdin);
        buffer[strcspn(buffer, "\n")] = 0;
        strncpy(check_user, buffer, sizeof(check_user));
        check_user[sizeof(check_user) - 1] = 0; // check_user

        if (strcmp(user, check_user) != 0) {
            printf("Error: usernames don't match!\n");
            continue; // user = check_user?
        }

        strcat(user, "\n");
        send(sock, user, strlen(user), 0); // server awaits username

        printf("Password: ");
        fgets(buffer, BUFFER_SIZE, stdin);
        buffer[strcspn(buffer, "\n")] = 0;
        strncpy(pass, buffer, sizeof(pass));
        pass[sizeof(pass) - 1] = 0; // password

        printf("Confirm Password: ");
        fgets(buffer, BUFFER_SIZE, stdin);
        buffer[strcspn(buffer, "\n")] = 0;
        strncpy(check_pass, buffer, sizeof(check_pass));
        check_pass[sizeof(check_pass) - 1] = 0; // check password

        if (strcmp(pass, check_pass) != 0) {
            printf("Error: passwords don't match!\n");
            continue; // password = check_password?
        }

        strcat(pass, "\n");
        send(sock, pass, strlen(pass), 0); // server awaits password 

        memset(buffer, 0, BUFFER_SIZE);
        int valread = recv(sock, buffer, BUFFER_SIZE, 0);
        if (valread <= 0) break;

        printf("%s\n", buffer);
        if (strstr(buffer, "successful")) {
            return 1; // success
        } else if (strstr(buffer, "mode")) {
            return -1;
        }
    }
    return 0;
}

/*
Queries server for a list of active users an ddisplays the list.
*/
void active_list(int sock) {
    char buffer[BUFFER_SIZE];
    send(sock, "/who\n", strlen("/who\n"), 0);
    memset(buffer, 0, BUFFER_SIZE);
    recv(sock, buffer, BUFFER_SIZE, 0);
    printf("\n%s", buffer);
}

/*
Send a request to the specified client for a p2p connection. Return the socket number if
the connection is approved.
*/
int send_p2p_request(int server_sock) {
    char ip[BUFFER_SIZE];
    char port[BUFFER_SIZE];

    // will kill client if they enter bad IP and port
    printf("WARNING: ENTERING AN INCORRECT IP/PORT WILL END YOUR SESSION\n");

    printf("Enter the user's IP address: ");
    if (!fgets(ip, BUFFER_SIZE, stdin)) return 0;
    ip[strcspn(ip, "\n")] = 0; // ip = target ip

    printf("Enter the user's port number: ");
    if (!fgets(port, BUFFER_SIZE, stdin)) return 0;
    port[strcspn(port, "\n")] = 0; // port = target port

    int port_num = atoi(port);
    if (port_num <= 0 || port_num > 65535) {
        printf("Invalid port number.\n");
        return 0; // port does not exist on machine
    }

    char message[BUFFER_SIZE];
    snprintf(message, sizeof(message), "/p2p %s %s\n", ip, port);
    send(server_sock, message, strlen(message), 0); // send "/p2p <ip> <port> to server"

    // immediately expect response from server
    // server determines if the user is valid, prompts user, and determines if they want connection
    char response[BUFFER_SIZE];
    memset(response, 0, BUFFER_SIZE);
    recv(server_sock, response, BUFFER_SIZE, 0);
    
    response[strcspn(response, "\n")] = 0;
    if (strcmp(response, "FAIL") == 0) { // bad request (ip or port wasn't right)
        printf("\nError! Failed find the client or get a response. Closing connection...\n");
        return -1;
    } else if (strcmp(response, "REJECT") == 0) { // good request but client rejected p2p connection
        printf("\nThe client rejected the request.\n");
        return 0;
    } else if (strncmp(response, "ACCEPT:", 7) == 0) { // good request and client accepted p2p connection
        char *their_name = response + 7; 
        strncpy(peer_username, their_name, USERNAME_LEN-1);
        peer_username[USERNAME_LEN-1] = '\0'; // peer username = trailing ACCEPT: tag
        int p2p_sock = socket(AF_INET, SOCK_STREAM, 0); // create a new p2p socket for dedicated messaging
        if (p2p_sock < 0) {
            perror("System Error: Failed to create P2P socket\n");
            return 0;
        }

        // setup p2p socket for the intended ip and port
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
            perror("Systen Error: P2P connection failed\n");
            close(p2p_sock);
            return 0;
        }

        printf("Connected to peer! Closing server connection...\n");
        return p2p_sock; // new socket created for p2p messaging
    } else {
        printf("\nUnknown response from server\n");
        return 0;
    }

    return 0;
}

/*
Setup a client's server connection, p2p socket, and udp socket. Prompt for authentication
and enter client loop for commands.
*/
int main() {
    // stop buffering to display the login/signup message immediately
    setvbuf(stdout, NULL, _IONBF, 0);

    // client-server connection
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) { perror("socket"); return 1; }

    // server host is a global variable in docker, assume 127.0.0.1 if can't find it
    const char *host = getenv("SERVER_HOST") ? getenv("SERVER_HOST") : SERVER_HOST;
    char portstr[6];
    snprintf(portstr, sizeof portstr, "%d", SERVER_PORT);

    // struct for determining the correct connection to server
    struct addrinfo hints = {0}, *res, *p;
    hints.ai_family   = AF_UNSPEC;      // either IPv4 or IPv6
    hints.ai_socktype = SOCK_STREAM;

    // couldn't access any address info
    int rv = getaddrinfo(host, portstr, &hints, &res);
    if (rv) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return 1;
    }

    // try connection points until correct ip and port are found
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

    // no connection point found
    if (!connected) {
        fprintf(stderr, "Failed to connect to %s:%s\n", host, portstr);
        return 1;
    }

    int mode, done = 0;
    char buffer[BUFFER_SIZE];
    while (!done) {
        printf("1 Login\n2 Signup\nChoice: ");
        if (!fgets(buffer, sizeof(buffer), stdin)) return 0;
        mode = atoi(buffer);

        buffer[0] = (char)mode;
        send(sock, buffer, 1, 0);
        if (mode == -1) {
            close(sock);
            return 0;
        }
        if (mode == 1 && login(sock))   done = 1;
        else if (mode == 2 && signup(sock)) {
            printf("\nSignup successful. Please login now.\n");
        }
    }    

    // create p2p listener socket
    int p2p_sock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in p2p_addr = {
        .sin_family = AF_INET,
        .sin_addr.s_addr = INADDR_ANY,
        .sin_port = 0  // let OS pick port
    };
    // bind p2p and listen for incoming connection
    bind(p2p_sock, (struct sockaddr *)&p2p_addr, sizeof(p2p_addr));
    listen(p2p_sock, 5);

    // determine p2p port and send to the server for storage
    socklen_t alen = sizeof(p2p_addr);
    getsockname(p2p_sock, (struct sockaddr *)&p2p_addr, &alen);
    int p2p_port = ntohs(p2p_addr.sin_port);
    sprintf(buffer, "%d\n", p2p_port);
    send(sock, buffer, strlen(buffer), 0);

    // bind udp socket and listen for incoming connection
    int udp_sock = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in udp_addr = {
        .sin_family = AF_INET,
        .sin_addr.s_addr = INADDR_ANY,
        .sin_port = 0  // let OS pick port
    };
    bind(udp_sock, (struct sockaddr *)&udp_addr, sizeof(udp_addr));
    listen(udp_sock, 5);

    // determine udp port and send to the server for storage
    socklen_t alen2 = sizeof(udp_addr);
    getsockname(udp_sock, (struct sockaddr *)&udp_addr, &alen2);
    int udp_port = ntohs(udp_addr.sin_port);
    sprintf(buffer, "%d\n", udp_port);
    send(sock, buffer, strlen(buffer), 0);

    // all commands a user can do
    printf("Command List:\n"
        "  /who       List active users\n"
        "  /p2p       Peer-to-peer chat\n"
        "  /broadcast Broadcast to everyone\n"
        "  /exit      Quit\n\n");

    // all fd's the select can read from: stdin, server, p2p, or udp
    fd_set read_fds;
    int maxfd = sock;
    if (p2p_sock > maxfd) maxfd = p2p_sock;
    if (udp_sock  > maxfd) maxfd = udp_sock;
    if (STDIN_FILENO > maxfd) maxfd = STDIN_FILENO;

    while (1) {

        printf("You: ");
        fflush(stdout); // flush buffering to avoid delay of "You;"

        // set of the above fd's
        FD_ZERO(&read_fds);
        FD_SET(STDIN_FILENO, &read_fds);
        FD_SET(sock,         &read_fds);
        FD_SET(p2p_sock,     &read_fds);
        FD_SET(udp_sock,     &read_fds);

        // select failed to listen to all fd's
        if (select(maxfd+1, &read_fds, NULL, NULL, NULL) < 0) {
            perror("select");
            break;
        }

        // incoming message from server
        if (FD_ISSET(sock, &read_fds)) {
            char srv[BUFFER_SIZE] = {0};
            int n = recv(sock, srv, sizeof(srv)-1, 0);
            if (n <= 0) {
                printf("\n[SERVER] Disconnected.\n"); // server connection broke
                break;
            }
            srv[n] = '\0';

            // server tells client about a P2P request from another client
            if (strncmp(srv, "P2P_REQUEST:", 12) == 0) {
                printf("\n[SERVER] %s", srv + 12); // tells client it's from server
                char *colon = strrchr(srv, ':');
                if (colon) {
                    char *name = colon + 2;
                    char *nl = strchr(name, '\n');
                    if (nl) *nl = '\0';
                    strncpy(peer_username, name, USERNAME_LEN-1);
                    peer_username[USERNAME_LEN-1] = '\0'; // username = end of message 
                }

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
                printf("\n[SERVER] %s\n", srv); // if /who, server send the message
            }
        }

        // this client has accepted p2p connection request, p2p sock is triggered
        if (FD_ISSET(p2p_sock, &read_fds)) {
            int peer = accept(p2p_sock, NULL, NULL);
            if (peer >= 0) {
                close(sock); // close connection to server
                p2p_chat(peer); // enter p2p_chat loop
                return 0;
            }
        }

        // incoming udp broadcast
        if (FD_ISSET(udp_sock, &read_fds)) {
            // receive message from udp socket (udp port created when authentication occurred)
            char msg[BUFFER_SIZE];
            struct sockaddr_in src;
            socklen_t src_len = sizeof(src);
            ssize_t n = recvfrom(udp_sock, msg, sizeof(msg)-1, 0, (struct sockaddr*)&src, &src_len);
            if (n > 0) {
                msg[n] = '\0';
                // print as soon as it arrives
                printf("\n[BROADCAST] %s\n",msg);
                // reprint prompt if waiting for user
                fflush(stdout);
            }
        }

        // user has typed to terminal using stdin
        if (FD_ISSET(STDIN_FILENO, &read_fds)) {
            if (!fgets(buffer, sizeof(buffer), stdin)) break;
            buffer[strcspn(buffer,"\n")] = 0;

            if (strcmp(buffer, "/exit")==0) break; // /exit = kill loop and client
            else if (strcmp(buffer, "/who")==0) active_list(sock); // /who = active lsit
            else if (strcmp(buffer, "/p2p")==0) { // /p2p = start p2p request
                int peer_fd = send_p2p_request(sock);
                if (peer_fd < 0) break;
                if (peer_fd > 0) {
                    close(sock); // if we get the p2p socket, close server connection
                    p2p_chat(peer_fd); // enter p2p chat
                    return 0;
                } 
            } else if (strcmp(buffer, "/broadcast")==0) { // /broadcast = send all users a message
                char msg[BUFFER_SIZE];
                printf("Message to All: ");
                if (!fgets(msg, BUFFER_SIZE, stdin)) return 0;
                msg[strcspn(msg, "\n")] = 0;
                char broadcast[BUFFER_SIZE];
                snprintf(broadcast, sizeof(broadcast), "/broadcast %s\n", msg);
                send(sock, broadcast, strlen(broadcast), 0); // send "/broadcast <message>" to server
            }
            else {
                printf("Error: Unknown command.\n");
            }
        }
    }

    close(sock);
    return 0;
}