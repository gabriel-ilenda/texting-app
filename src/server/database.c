#include "database.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <openssl/sha.h>

MYSQL *conn;

/*
Connects to the mysql database. No security here since it's a local instance.
*/
void db_connect() {
    const char *host = getenv("DB_HOST");
    unsigned int port = atoi(getenv("DB_PORT"));
    const char *user = getenv("DB_USER");
    const char *pass = getenv("DB_PASS");
    const char *name = getenv("DB_NAME");
    conn = mysql_init(NULL);
    if (!mysql_real_connect(conn, host, user, pass, name, port, 0, NULL)) {
        fprintf(stderr, "MySQL connection failed: %s\n", mysql_error(conn));
        exit(EXIT_FAILURE);
    }
    printf("Connected to MySQL database.\n");
}

/*
Closes db connection.
*/
void db_close() {
    mysql_close(conn);
}

/*
Hashes the password a user enters using SHA256 protocol. The hashed
password is stored in the db. Each time a user logs in, the hashed version
is compared.
*/
void sha256_hash(const char *str, char outputBuffer[65]) {
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256((unsigned char*)str, strlen(str), hash);

    for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
        sprintf(outputBuffer + (i * 2), "%02x", hash[i]);
    }
    outputBuffer[64] = 0;
}

/*
Check if the user already exists. If not, lets the new user into
database.
*/
int db_signup(const char *username, const char *password) {
    char hash[65];
    sha256_hash(password, hash);

    char query[512];
    snprintf(query, sizeof(query),
        "INSERT INTO users (username, password_hash) VALUES ('%s', '%s')",
        username, hash);

    if (mysql_query(conn, query) == 0) {
        return 1;  // Signup successful
    } else {
        unsigned int err_code = mysql_errno(conn);
        if (err_code == 1062) {
            // Duplicate entry (username already exists)
            fprintf(stderr, "Signup failed: username already exists.\n");
        } else {
            fprintf(stderr, "Signup error [%d]: %s\n", err_code, mysql_error(conn));
        }
        return 0;  // General failure
    }
}

/*
Hash the password a user entered, see if user and password matches a 
tuple in the database.
*/
int db_login(const char *username, const char *password) {
    char hash[65];
    sha256_hash(password, hash); // hash for comparison

    char query[512];
    snprintf(query, sizeof(query),
        "SELECT * FROM users WHERE username='%s' AND password_hash='%s'",
        username, hash);

    if (mysql_query(conn, query) != 0) {
        fprintf(stderr, "Login query error: %s\n", mysql_error(conn));
        return 0; // bad connection
    }

    MYSQL_RES *result = mysql_store_result(conn); 
    int num_rows = mysql_num_rows(result);
    mysql_free_result(result);

    return num_rows == 1; // success
}
