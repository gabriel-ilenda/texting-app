-- schema.sql
-- Creates a single `users` table in the `chatdb` database
-- with a primary‐key username and a fixed‐length SHA256 hash column

CREATE DATABASE IF NOT EXISTS chatdb
  CHARACTER SET = utf8mb4
  COLLATE = utf8mb4_unicode_ci;
USE chatdb;

CREATE TABLE IF NOT EXISTS users (
  username       VARCHAR(100)   NOT NULL PRIMARY KEY,
  password_hash  CHAR(64)       NOT NULL
) ENGINE=InnoDB
  DEFAULT CHARSET=utf8mb4
  COLLATE=utf8mb4_unicode_ci;
