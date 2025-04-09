#include "database.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <openssl/sha.h>

MYSQL *conn;

void db_connect() {
    conn = mysql_init(NULL);
    if (!mysql_real_connect(conn, "localhost", "root", "S@ntosRed20", "letschat", 0, NULL, 0)) {
        fprintf(stderr, "MySQL connection failed: %s\n", mysql_error(conn));
        exit(EXIT_FAILURE);
    }
    printf("Connected to MySQL database.\n");
}

void db_close() {
    mysql_close(conn);
}

void sha256_hash(const char *str, char outputBuffer[65]) {
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256((unsigned char*)str, strlen(str), hash);

    for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
        sprintf(outputBuffer + (i * 2), "%02x", hash[i]);
    }
    outputBuffer[64] = 0;
}

int db_signup(const char *username, const char *password) {
    char hash[65];
    sha256_hash(password, hash);

    char query[512];
    snprintf(query, sizeof(query),
        "INSERT INTO users (username, password_hash) VALUES ('%s', '%s')",
        username, hash);

    if (mysql_query(conn, query) == 0) {
        return 1;  // success
    } else {
        fprintf(stderr, "Signup error: %s\n", mysql_error(conn));
        return 0;  // failure
    }
}

int db_login(const char *username, const char *password) {
    char hash[65];
    sha256_hash(password, hash);

    char query[512];
    snprintf(query, sizeof(query),
        "SELECT * FROM users WHERE username='%s' AND password_hash='%s'",
        username, hash);

    if (mysql_query(conn, query) != 0) {
        fprintf(stderr, "Login query error: %s\n", mysql_error(conn));
        return 0;
    }

    MYSQL_RES *result = mysql_store_result(conn);
    int num_rows = mysql_num_rows(result);
    mysql_free_result(result);

    return num_rows == 1;
}
