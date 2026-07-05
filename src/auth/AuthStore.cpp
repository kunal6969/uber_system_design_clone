#pragma once

#include <cstdlib>
#include <iomanip>
#include <map>
#include <mutex>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <sqlite3.h>

#include "crow.h"
#include "crow/TinySHA1.hpp"

using namespace std;

namespace uberride {

struct UserAccount {
    string id;
    string name;
    string email;
    string role;
    bool active;
};

class AuthStore {
    sqlite3* db;
    mutable mutex storeMutex;
    map<string, string> sessionToEmail;
    int nextSessionId;

    static string toHex(const unsigned char* data, size_t length) {
        ostringstream out;
        out << hex << setfill('0');
        for (size_t index = 0; index < length; ++index) {
            out << setw(2) << static_cast<unsigned>(data[index]);
        }
        return out.str();
    }

    static string randomSalt(size_t bytes = 16) {
        static random_device device;
        static mt19937_64 generator(device());
        uniform_int_distribution<int> distribution(0, 255);

        ostringstream out;
        out << hex << setfill('0');
        for (size_t index = 0; index < bytes; ++index) {
            out << setw(2) << distribution(generator);
        }
        return out.str();
    }

    static string sha1Hex(const string& input) {
        sha1::SHA1 sha1;
        sha1.processBytes(input.data(), input.size());
        sha1::SHA1::digest8_t digest{};
        sha1.getDigestBytes(digest);
        return toHex(digest, sizeof(digest));
    }

    static string hashPassword(const string& salt, const string& password) {
        return sha1Hex(salt + ":" + password);
    }

    static bool execute(sqlite3* connection, const string& sql, bool throwOnError = true) {
        char* errorMessage = nullptr;
        int status = sqlite3_exec(connection, sql.c_str(), nullptr, nullptr, &errorMessage);
        if (status != SQLITE_OK) {
            string message = errorMessage ? errorMessage : "SQLite execution failed.";
            sqlite3_free(errorMessage);
            if (!throwOnError) {
                return false;
            }
            throw runtime_error(message);
        }
        return true;
    }

    void ensureSchema() {
        execute(db, R"SQL(
            CREATE TABLE IF NOT EXISTS users (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                name TEXT NOT NULL,
                email TEXT NOT NULL UNIQUE,
                role TEXT NOT NULL,
                salt TEXT NOT NULL,
                password_hash TEXT NOT NULL,
                active INTEGER NOT NULL DEFAULT 1
            );
        )SQL");

        execute(db, "ALTER TABLE users ADD COLUMN active INTEGER NOT NULL DEFAULT 1;", false);
    }

    bool getUserByEmail(const string& email, string& id, string& name, string& role, string& salt, string& passwordHash, bool& active) const {
        const char* sql = "SELECT id, name, role, salt, password_hash, active FROM users WHERE email = ? LIMIT 1;";
        sqlite3_stmt* statement = nullptr;
        if (sqlite3_prepare_v2(db, sql, -1, &statement, nullptr) != SQLITE_OK) {
            return false;
        }

        sqlite3_bind_text(statement, 1, email.c_str(), -1, SQLITE_TRANSIENT);

        bool found = false;
        if (sqlite3_step(statement) == SQLITE_ROW) {
            id = reinterpret_cast<const char*>(sqlite3_column_text(statement, 0));
            name = reinterpret_cast<const char*>(sqlite3_column_text(statement, 1));
            role = reinterpret_cast<const char*>(sqlite3_column_text(statement, 2));
            salt = reinterpret_cast<const char*>(sqlite3_column_text(statement, 3));
            passwordHash = reinterpret_cast<const char*>(sqlite3_column_text(statement, 4));
            active = sqlite3_column_int(statement, 5) != 0;
            found = true;
        }

        sqlite3_finalize(statement);
        return found;
    }

public:
    explicit AuthStore(const string& databasePath = "uber_ride_auth.db") : db(nullptr), nextSessionId(0) {
        if (sqlite3_open(databasePath.c_str(), &db) != SQLITE_OK) {
            string message = db ? sqlite3_errmsg(db) : "Unable to open auth database.";
            if (db) {
                sqlite3_close(db);
            }
            throw runtime_error(message);
        }

        ensureSchema();
        seedDemoUsers();
    }

    ~AuthStore() {
        if (db) {
            sqlite3_close(db);
            db = nullptr;
        }
    }

    void seedDemoUsers() {
        registerAccount("Asha Rider", "rider@demo.local", "demo123", "rider");
        registerAccount("Dinesh Driver", "driver@demo.local", "demo123", "driver");
        registerAccount("Admin", "admin@demo.local", "demo123", "admin");
    }

    bool registerAccount(const string& name, const string& email, const string& password, const string& role) {
        lock_guard<mutex> guard(storeMutex);

        string existingId;
        string existingName;
        string existingRole;
        string existingSalt;
        string existingHash;
        bool existingActive = false;
        if (getUserByEmail(email, existingId, existingName, existingRole, existingSalt, existingHash, existingActive)) {
            return false;
        }

        string salt = randomSalt();
        string passwordHash = hashPassword(salt, password);

        const char* sql = "INSERT INTO users (name, email, role, salt, password_hash) VALUES (?, ?, ?, ?, ?);";
        sqlite3_stmt* statement = nullptr;
        if (sqlite3_prepare_v2(db, sql, -1, &statement, nullptr) != SQLITE_OK) {
            return false;
        }

        sqlite3_bind_text(statement, 1, name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(statement, 2, email.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(statement, 3, role.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(statement, 4, salt.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(statement, 5, passwordHash.c_str(), -1, SQLITE_TRANSIENT);

        bool inserted = sqlite3_step(statement) == SQLITE_DONE;
        sqlite3_finalize(statement);
        return inserted;
    }

    bool authenticate(const string& email, const string& password, string& sessionToken, UserAccount& account) {
        lock_guard<mutex> guard(storeMutex);

        string id;
        string name;
        string role;
        string salt;
        string storedHash;
        bool active = false;
        if (!getUserByEmail(email, id, name, role, salt, storedHash, active)) {
            return false;
        }

        if (!active) {
            return false;
        }

        if (hashPassword(salt, password) != storedHash) {
            return false;
        }

        sessionToken = "session-" + to_string(++nextSessionId);
        sessionToEmail[sessionToken] = email;

        account.id = id;
        account.name = name;
        account.email = email;
        account.role = role;
        account.active = active;
        return true;
    }

    bool logout(const string& sessionToken) {
        lock_guard<mutex> guard(storeMutex);
        return sessionToEmail.erase(sessionToken) > 0;
    }

    bool getSessionUser(const crow::request& req, UserAccount& account) const {
        lock_guard<mutex> guard(storeMutex);

        string cookie = req.get_header_value("Cookie");
        string token = extractSessionToken(cookie);
        if (token.empty()) {
            return false;
        }

        map<string, string>::const_iterator it = sessionToEmail.find(token);
        if (it == sessionToEmail.end()) {
            return false;
        }

        string id;
        string name;
        string role;
        string salt;
        string passwordHash;
        bool active = false;
        if (!getUserByEmail(it->second, id, name, role, salt, passwordHash, active) || !active) {
            return false;
        }

        account.id = id;
        account.name = name;
        account.email = it->second;
        account.role = role;
        account.active = active;
        return true;
    }

    bool setAccountActive(const string& email, bool active) {
        lock_guard<mutex> guard(storeMutex);
        const char* sql = "UPDATE users SET active = ? WHERE email = ?;";
        sqlite3_stmt* statement = nullptr;
        if (sqlite3_prepare_v2(db, sql, -1, &statement, nullptr) != SQLITE_OK) {
            return false;
        }

        sqlite3_bind_int(statement, 1, active ? 1 : 0);
        sqlite3_bind_text(statement, 2, email.c_str(), -1, SQLITE_TRANSIENT);
        bool updated = sqlite3_step(statement) == SQLITE_DONE && sqlite3_changes(db) > 0;
        sqlite3_finalize(statement);
        return updated;
    }

    vector<UserAccount> listAccounts() const {
        lock_guard<mutex> guard(storeMutex);

        vector<UserAccount> accounts;
        const char* sql = "SELECT id, name, email, role, active FROM users ORDER BY role, name;";
        sqlite3_stmt* statement = nullptr;
        if (sqlite3_prepare_v2(db, sql, -1, &statement, nullptr) != SQLITE_OK) {
            return accounts;
        }

        while (sqlite3_step(statement) == SQLITE_ROW) {
            UserAccount account;
            account.id = reinterpret_cast<const char*>(sqlite3_column_text(statement, 0));
            account.name = reinterpret_cast<const char*>(sqlite3_column_text(statement, 1));
            account.email = reinterpret_cast<const char*>(sqlite3_column_text(statement, 2));
            account.role = reinterpret_cast<const char*>(sqlite3_column_text(statement, 3));
            account.active = sqlite3_column_int(statement, 4) != 0;
            accounts.push_back(account);
        }

        sqlite3_finalize(statement);
        return accounts;
    }

    static string extractSessionToken(const string& cookie) {
        string key = "ubride_session=";
        size_t pos = cookie.find(key);
        if (pos == string::npos) {
            return string();
        }

        pos += key.size();
        size_t end = cookie.find(';', pos);
        return cookie.substr(pos, end == string::npos ? string::npos : end - pos);
    }
};

}  // namespace uberride
