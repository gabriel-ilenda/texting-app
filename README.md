RUN PROJECT:

1. Ensure you are in the root of the project (~/LetsChat)
2. $ docker-compose down -v
    - This kills any stray docker processes and resets the database (if for some reason
      you have used my database before).
    - Note that this command resets the database, so the users
      you signup with will not persist across mutliple docker builds (but will persist across
      running a client multiple times for 1 build).
3. $ docker-compose up --build -d
    - Wait for containers: chat_myserver, chat_client1, chat_client2, chat_client3 to be "Started"
    - Wait for container: chat_mysql to be "Healthy"
4. $ docker ps
    - Ensure that all 5 containers are running (chat_client1, chat_client2, chat_client3, chat_server, chat_mysql)
    - If not all 5 are running (sometimes the clients get stuck because of a lengthy database connection process),
      repeat step 3.
5. Open 3 new terminals (do not split terminals, open 3 fresh ones). Current terminal = Terminal 0 = where we manage
   and kill the processes.
6. Terminal 1: $ docker-compose exec client1 ./client
7. Terminal 2: $ docker-compose exec client2 ./client
8. Terminal 2: $ docker-compose exec client3 ./client
9. From here, you can interact with the app as described in the shell. Note that if your client connection is terminated
   and the shell is killed (ex: you start a p2p connection, you type '/exit', you fail a login 3 times, you fail a p2p request), 
   you can rerun steps 6, 7, or 8 (depending on the terminal you're in) to reopen the connection.
10. When you're done with he app, you can ctrl+c or /exit to kill the clients if they're still running. In terminal 0, use
   ($ docker-compose down -v) to terminate all containers and cleanup. 

USE PROJECT

1. When prompted in the clients for a login or signup, enter 1 to login, or 2 to signup.
2. If you signup, you'll be prompted for a user, confirm user, password, and confirm password. If you fail to 
   create a unique user with matching name/pass 3 times, the client will be closed.
3. Now you can login. You'll be prompted for a user and password. If you fail to enter a correct combo 3 times, the client
   will be closed.
4. Once logged in you have access to 4 commands:
    - /who : queries the server a list of active users and displays their username, IP, and P2P port
    - /p2p : prompts the user for the IP address and P2P port of the user they want to connect with
    - /broadcast : prompts the user to enter a message that they want to be sent to all active clients
    - /exit (ctrl+c) : closes the client's connection
5. At any point, a client can receive a request to start a P2P connection with a user. If they want to 
   accept, they enter 1, anything else will reject the connection. If the users connect with each
   other, their connections to the server are closed. They can message back and forth indefinitely 
   until someone enter /exit or ctrl+c.
6. At any point, all active clients can receive a broadcasted message. The message shows the user it was from,
   the date and time it was sent, and the message contents. 

LIBRARIES

- mysql/mysql
- arpa/inet
- netinet/in
- openssl/sha

