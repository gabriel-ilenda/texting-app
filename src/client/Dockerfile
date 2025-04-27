# src/client/Dockerfile
FROM gcc:latest

WORKDIR /app

# Only copy the client sources
COPY client.c ./

# Compile
RUN gcc -o client client.c

# Default entrypoint is our client binary
CMD ["./client"]
