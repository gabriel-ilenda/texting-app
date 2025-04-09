#ifndef DB_H
#define DB_H

#include <mysql/mysql.h>

void db_connect();
void db_close();
int db_signup(const char *username, const char *password);
int db_login(const char *username, const char *password);

#endif