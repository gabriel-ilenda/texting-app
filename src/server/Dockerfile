# src/server/Dockerfile

FROM ubuntu:22.04

# install compiler, mysql client dev, ssl dev, bash
RUN apt-get update && apt-get install -y --no-install-recommends \
      build-essential libmysqlclient-dev libssl-dev bash \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# copy in server, db logic, and wait script
COPY server.c database.c database.h wait-for-it.sh ./

# compile
RUN gcc -o server server.c database.c -pthread -lmysqlclient -lcrypto

EXPOSE 4400

# entrypoint: wait up to 30s for mysql:3306, then exec ./server
CMD ["stdbuf", "-o0", "./wait-for-it.sh", "mysql:3306", "--timeout=30", "--strict", "--", "./server"]
