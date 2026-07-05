#pragma once

#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

#include <sqlite3.h>

using namespace std;

namespace uberride {

struct PersistedRideSnapshot {
    string rideId;
    string riderEmail;
    string riderName;
    string driverId;
    string driverEmail;
    string driverName;
    string vehicleType;
    string sourceId;
    string destinationId;
    string status;
    double distanceKm;
    int timeMinutes;
    double fare;
    double cancellationFee;
    vector<string> stopIds;
};

struct DriverProfileSnapshot {
    string email;
    string name;
    string vehicleType;
    string locationId;
    string availability;
};

struct SavedLocationSnapshot {
    string label;
    string locationId;
};

struct RatingSnapshot {
    string rideId;
    string riderEmail;
    string driverId;
    int rating;
    string comment;
};

struct AuditLogSnapshot {
    string actorEmail;
    string action;
    string target;
    string details;
    string createdAt;
};

class RideHistoryStore {
    sqlite3* db;
    mutable mutex storeMutex;

    static void execute(sqlite3* connection, const string& sql) {
        char* errorMessage = nullptr;
        int status = sqlite3_exec(connection, sql.c_str(), nullptr, nullptr, &errorMessage);
        if (status != SQLITE_OK) {
            string message = errorMessage ? errorMessage : "SQLite execution failed.";
            sqlite3_free(errorMessage);
            throw runtime_error(message);
        }
    }

    void ensureSchema() {
        execute(db, R"SQL(
            CREATE TABLE IF NOT EXISTS ride_history (
                ride_id TEXT PRIMARY KEY,
                rider_email TEXT NOT NULL,
                rider_name TEXT NOT NULL,
                driver_id TEXT,
                driver_email TEXT,
                driver_name TEXT,
                vehicle_type TEXT NOT NULL,
                source_id TEXT NOT NULL,
                destination_id TEXT NOT NULL,
                status TEXT NOT NULL,
                distance_km REAL NOT NULL,
                time_minutes INTEGER NOT NULL,
                fare REAL NOT NULL,
                cancellation_fee REAL NOT NULL DEFAULT 0,
                created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
                updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
            );
        )SQL");

        execute(db, R"SQL(
            CREATE TABLE IF NOT EXISTS ride_stops (
                ride_id TEXT NOT NULL,
                stop_order INTEGER NOT NULL,
                location_id TEXT NOT NULL,
                PRIMARY KEY (ride_id, stop_order),
                FOREIGN KEY (ride_id) REFERENCES ride_history(ride_id) ON DELETE CASCADE
            );
        )SQL");

        execute(db, R"SQL(
            CREATE TABLE IF NOT EXISTS driver_profiles (
                email TEXT PRIMARY KEY,
                name TEXT NOT NULL,
                vehicle_type TEXT NOT NULL DEFAULT 'SEDAN',
                location_id TEXT NOT NULL DEFAULT 'KB',
                availability TEXT NOT NULL DEFAULT 'AVAILABLE',
                updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
            );
        )SQL");

        execute(db, R"SQL(
            CREATE TABLE IF NOT EXISTS saved_locations (
                email TEXT NOT NULL,
                label TEXT NOT NULL,
                location_id TEXT NOT NULL,
                PRIMARY KEY (email, label)
            );
        )SQL");

        execute(db, R"SQL(
            CREATE TABLE IF NOT EXISTS ride_ratings (
                ride_id TEXT PRIMARY KEY,
                rider_email TEXT NOT NULL,
                driver_id TEXT NOT NULL,
                rating INTEGER NOT NULL,
                comment TEXT,
                created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
            );
        )SQL");

        execute(db, R"SQL(
            CREATE TABLE IF NOT EXISTS cancel_reasons (
                ride_id TEXT PRIMARY KEY,
                reason TEXT NOT NULL,
                created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
            );
        )SQL");

        execute(db, R"SQL(
            CREATE TABLE IF NOT EXISTS driver_status_logs (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                driver_id TEXT NOT NULL,
                email TEXT NOT NULL,
                status TEXT NOT NULL,
                created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
            );
        )SQL");

        execute(db, R"SQL(
            CREATE TABLE IF NOT EXISTS admin_audit_logs (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                actor_email TEXT NOT NULL,
                action TEXT NOT NULL,
                target TEXT NOT NULL,
                details TEXT,
                created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
            );
        )SQL");

        execute(db, R"SQL(
            CREATE TABLE IF NOT EXISTS schema_migrations (
                version INTEGER PRIMARY KEY,
                applied_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
            );
        )SQL");

        execute(db, "INSERT OR IGNORE INTO schema_migrations (version) VALUES (1);");
    }

    static string textColumn(sqlite3_stmt* statement, int column) {
        const unsigned char* value = sqlite3_column_text(statement, column);
        return value ? reinterpret_cast<const char*>(value) : "";
    }

    static void bindOptionalText(sqlite3_stmt* statement, int index, const string& value) {
        if (value.empty()) {
            sqlite3_bind_null(statement, index);
            return;
        }
        sqlite3_bind_text(statement, index, value.c_str(), -1, SQLITE_TRANSIENT);
    }

public:
    explicit RideHistoryStore(const string& databasePath = "uber_ride_auth.db") : db(nullptr) {
        if (sqlite3_open(databasePath.c_str(), &db) != SQLITE_OK) {
            string message = db ? sqlite3_errmsg(db) : "Unable to open ride history database.";
            if (db) {
                sqlite3_close(db);
            }
            throw runtime_error(message);
        }

        execute(db, "PRAGMA foreign_keys = ON;");
        ensureSchema();
    }

    ~RideHistoryStore() {
        if (db) {
            sqlite3_close(db);
            db = nullptr;
        }
    }

    void saveRide(const PersistedRideSnapshot& snapshot) {
        lock_guard<mutex> guard(storeMutex);
        execute(db, "BEGIN TRANSACTION;");

        const char* rideSql = R"SQL(
            INSERT INTO ride_history (
                ride_id, rider_email, rider_name, driver_id, driver_email, driver_name,
                vehicle_type, source_id, destination_id, status, distance_km,
                time_minutes, fare, cancellation_fee, updated_at
            )
            VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, CURRENT_TIMESTAMP)
            ON CONFLICT(ride_id) DO UPDATE SET
                rider_email = excluded.rider_email,
                rider_name = excluded.rider_name,
                driver_id = excluded.driver_id,
                driver_email = excluded.driver_email,
                driver_name = excluded.driver_name,
                vehicle_type = excluded.vehicle_type,
                source_id = excluded.source_id,
                destination_id = excluded.destination_id,
                status = excluded.status,
                distance_km = excluded.distance_km,
                time_minutes = excluded.time_minutes,
                fare = excluded.fare,
                cancellation_fee = excluded.cancellation_fee,
                updated_at = CURRENT_TIMESTAMP;
        )SQL";

        sqlite3_stmt* rideStatement = nullptr;
        if (sqlite3_prepare_v2(db, rideSql, -1, &rideStatement, nullptr) != SQLITE_OK) {
            execute(db, "ROLLBACK;");
            return;
        }

        sqlite3_bind_text(rideStatement, 1, snapshot.rideId.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(rideStatement, 2, snapshot.riderEmail.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(rideStatement, 3, snapshot.riderName.c_str(), -1, SQLITE_TRANSIENT);
        bindOptionalText(rideStatement, 4, snapshot.driverId);
        bindOptionalText(rideStatement, 5, snapshot.driverEmail);
        bindOptionalText(rideStatement, 6, snapshot.driverName);
        sqlite3_bind_text(rideStatement, 7, snapshot.vehicleType.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(rideStatement, 8, snapshot.sourceId.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(rideStatement, 9, snapshot.destinationId.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(rideStatement, 10, snapshot.status.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_double(rideStatement, 11, snapshot.distanceKm);
        sqlite3_bind_int(rideStatement, 12, snapshot.timeMinutes);
        sqlite3_bind_double(rideStatement, 13, snapshot.fare);
        sqlite3_bind_double(rideStatement, 14, snapshot.cancellationFee);

        bool saved = sqlite3_step(rideStatement) == SQLITE_DONE;
        sqlite3_finalize(rideStatement);
        if (!saved) {
            execute(db, "ROLLBACK;");
            return;
        }

        sqlite3_stmt* deleteStops = nullptr;
        if (sqlite3_prepare_v2(db, "DELETE FROM ride_stops WHERE ride_id = ?;", -1, &deleteStops, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(deleteStops, 1, snapshot.rideId.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_step(deleteStops);
            sqlite3_finalize(deleteStops);
        }

        const char* stopSql = "INSERT INTO ride_stops (ride_id, stop_order, location_id) VALUES (?, ?, ?);";
        for (int index = 0; index < static_cast<int>(snapshot.stopIds.size()); ++index) {
            sqlite3_stmt* stopStatement = nullptr;
            if (sqlite3_prepare_v2(db, stopSql, -1, &stopStatement, nullptr) != SQLITE_OK) {
                continue;
            }
            sqlite3_bind_text(stopStatement, 1, snapshot.rideId.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(stopStatement, 2, index);
            sqlite3_bind_text(stopStatement, 3, snapshot.stopIds[index].c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_step(stopStatement);
            sqlite3_finalize(stopStatement);
        }

        execute(db, "COMMIT;");
    }

    vector<PersistedRideSnapshot> loadAllRides() const {
        lock_guard<mutex> guard(storeMutex);
        vector<PersistedRideSnapshot> rides;

        const char* sql = R"SQL(
            SELECT ride_id, rider_email, rider_name, driver_id, driver_email, driver_name,
                   vehicle_type, source_id, destination_id, status, distance_km,
                   time_minutes, fare, cancellation_fee
            FROM ride_history
            ORDER BY created_at, ride_id;
        )SQL";

        sqlite3_stmt* statement = nullptr;
        if (sqlite3_prepare_v2(db, sql, -1, &statement, nullptr) != SQLITE_OK) {
            return rides;
        }

        while (sqlite3_step(statement) == SQLITE_ROW) {
            PersistedRideSnapshot snapshot;
            snapshot.rideId = textColumn(statement, 0);
            snapshot.riderEmail = textColumn(statement, 1);
            snapshot.riderName = textColumn(statement, 2);
            snapshot.driverId = textColumn(statement, 3);
            snapshot.driverEmail = textColumn(statement, 4);
            snapshot.driverName = textColumn(statement, 5);
            snapshot.vehicleType = textColumn(statement, 6);
            snapshot.sourceId = textColumn(statement, 7);
            snapshot.destinationId = textColumn(statement, 8);
            snapshot.status = textColumn(statement, 9);
            snapshot.distanceKm = sqlite3_column_double(statement, 10);
            snapshot.timeMinutes = sqlite3_column_int(statement, 11);
            snapshot.fare = sqlite3_column_double(statement, 12);
            snapshot.cancellationFee = sqlite3_column_double(statement, 13);
            rides.push_back(snapshot);
        }

        sqlite3_finalize(statement);

        const char* stopsSql = "SELECT location_id FROM ride_stops WHERE ride_id = ? ORDER BY stop_order;";
        for (PersistedRideSnapshot& snapshot : rides) {
            sqlite3_stmt* stopStatement = nullptr;
            if (sqlite3_prepare_v2(db, stopsSql, -1, &stopStatement, nullptr) != SQLITE_OK) {
                continue;
            }

            sqlite3_bind_text(stopStatement, 1, snapshot.rideId.c_str(), -1, SQLITE_TRANSIENT);
            while (sqlite3_step(stopStatement) == SQLITE_ROW) {
                snapshot.stopIds.push_back(textColumn(stopStatement, 0));
            }
            sqlite3_finalize(stopStatement);
        }

        return rides;
    }

    void clearAll() {
        lock_guard<mutex> guard(storeMutex);
        execute(db, "DELETE FROM ride_stops;");
        execute(db, "DELETE FROM ride_history;");
        execute(db, "DELETE FROM saved_locations;");
        execute(db, "DELETE FROM ride_ratings;");
        execute(db, "DELETE FROM cancel_reasons;");
        execute(db, "DELETE FROM driver_status_logs;");
        execute(db, "DELETE FROM admin_audit_logs;");
    }

    DriverProfileSnapshot getDriverProfile(const string& email, const string& defaultName, const string& defaultVehicleType = "SEDAN", const string& defaultLocationId = "KB") {
        lock_guard<mutex> guard(storeMutex);
        const char* selectSql = "SELECT email, name, vehicle_type, location_id, availability FROM driver_profiles WHERE email = ? LIMIT 1;";
        sqlite3_stmt* statement = nullptr;
        if (sqlite3_prepare_v2(db, selectSql, -1, &statement, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(statement, 1, email.c_str(), -1, SQLITE_TRANSIENT);
            if (sqlite3_step(statement) == SQLITE_ROW) {
                DriverProfileSnapshot profile;
                profile.email = textColumn(statement, 0);
                profile.name = textColumn(statement, 1);
                profile.vehicleType = textColumn(statement, 2);
                profile.locationId = textColumn(statement, 3);
                profile.availability = textColumn(statement, 4);
                sqlite3_finalize(statement);
                return profile;
            }
            sqlite3_finalize(statement);
        }

        DriverProfileSnapshot profile;
        profile.email = email;
        profile.name = defaultName;
        profile.vehicleType = defaultVehicleType;
        profile.locationId = defaultLocationId;
        profile.availability = "AVAILABLE";
        saveDriverProfile(profile);
        return profile;
    }

    void saveDriverProfile(const DriverProfileSnapshot& profile) {
        const char* sql = R"SQL(
            INSERT INTO driver_profiles (email, name, vehicle_type, location_id, availability, updated_at)
            VALUES (?, ?, ?, ?, ?, CURRENT_TIMESTAMP)
            ON CONFLICT(email) DO UPDATE SET
                name = excluded.name,
                vehicle_type = excluded.vehicle_type,
                location_id = excluded.location_id,
                availability = excluded.availability,
                updated_at = CURRENT_TIMESTAMP;
        )SQL";

        sqlite3_stmt* statement = nullptr;
        if (sqlite3_prepare_v2(db, sql, -1, &statement, nullptr) != SQLITE_OK) {
            return;
        }
        sqlite3_bind_text(statement, 1, profile.email.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(statement, 2, profile.name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(statement, 3, profile.vehicleType.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(statement, 4, profile.locationId.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(statement, 5, profile.availability.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(statement);
        sqlite3_finalize(statement);
    }

    void logDriverStatus(const string& driverId, const string& email, const string& status) {
        lock_guard<mutex> guard(storeMutex);
        const char* sql = "INSERT INTO driver_status_logs (driver_id, email, status) VALUES (?, ?, ?);";
        sqlite3_stmt* statement = nullptr;
        if (sqlite3_prepare_v2(db, sql, -1, &statement, nullptr) != SQLITE_OK) {
            return;
        }
        sqlite3_bind_text(statement, 1, driverId.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(statement, 2, email.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(statement, 3, status.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(statement);
        sqlite3_finalize(statement);
    }

    void saveSavedLocation(const string& email, const string& label, const string& locationId) {
        lock_guard<mutex> guard(storeMutex);
        const char* sql = "INSERT INTO saved_locations (email, label, location_id) VALUES (?, ?, ?) ON CONFLICT(email, label) DO UPDATE SET location_id = excluded.location_id;";
        sqlite3_stmt* statement = nullptr;
        if (sqlite3_prepare_v2(db, sql, -1, &statement, nullptr) != SQLITE_OK) {
            return;
        }
        sqlite3_bind_text(statement, 1, email.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(statement, 2, label.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(statement, 3, locationId.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(statement);
        sqlite3_finalize(statement);
    }

    vector<SavedLocationSnapshot> listSavedLocations(const string& email) const {
        lock_guard<mutex> guard(storeMutex);
        vector<SavedLocationSnapshot> locations;
        const char* sql = "SELECT label, location_id FROM saved_locations WHERE email = ? ORDER BY label;";
        sqlite3_stmt* statement = nullptr;
        if (sqlite3_prepare_v2(db, sql, -1, &statement, nullptr) != SQLITE_OK) {
            return locations;
        }
        sqlite3_bind_text(statement, 1, email.c_str(), -1, SQLITE_TRANSIENT);
        while (sqlite3_step(statement) == SQLITE_ROW) {
            SavedLocationSnapshot location;
            location.label = textColumn(statement, 0);
            location.locationId = textColumn(statement, 1);
            locations.push_back(location);
        }
        sqlite3_finalize(statement);
        return locations;
    }

    void saveRating(const RatingSnapshot& rating) {
        lock_guard<mutex> guard(storeMutex);
        const char* sql = R"SQL(
            INSERT INTO ride_ratings (ride_id, rider_email, driver_id, rating, comment)
            VALUES (?, ?, ?, ?, ?)
            ON CONFLICT(ride_id) DO UPDATE SET rating = excluded.rating, comment = excluded.comment;
        )SQL";
        sqlite3_stmt* statement = nullptr;
        if (sqlite3_prepare_v2(db, sql, -1, &statement, nullptr) != SQLITE_OK) {
            return;
        }
        sqlite3_bind_text(statement, 1, rating.rideId.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(statement, 2, rating.riderEmail.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(statement, 3, rating.driverId.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(statement, 4, rating.rating);
        sqlite3_bind_text(statement, 5, rating.comment.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(statement);
        sqlite3_finalize(statement);
    }

    double averageRatingForDriver(const string& driverId) const {
        lock_guard<mutex> guard(storeMutex);
        const char* sql = "SELECT AVG(rating) FROM ride_ratings WHERE driver_id = ?;";
        sqlite3_stmt* statement = nullptr;
        double average = 0.0;
        if (sqlite3_prepare_v2(db, sql, -1, &statement, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(statement, 1, driverId.c_str(), -1, SQLITE_TRANSIENT);
            if (sqlite3_step(statement) == SQLITE_ROW) {
                average = sqlite3_column_double(statement, 0);
            }
            sqlite3_finalize(statement);
        }
        return average;
    }

    int ratingForRide(const string& rideId) const {
        lock_guard<mutex> guard(storeMutex);
        const char* sql = "SELECT rating FROM ride_ratings WHERE ride_id = ? LIMIT 1;";
        sqlite3_stmt* statement = nullptr;
        int rating = 0;
        if (sqlite3_prepare_v2(db, sql, -1, &statement, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(statement, 1, rideId.c_str(), -1, SQLITE_TRANSIENT);
            if (sqlite3_step(statement) == SQLITE_ROW) {
                rating = sqlite3_column_int(statement, 0);
            }
            sqlite3_finalize(statement);
        }
        return rating;
    }

    void saveCancelReason(const string& rideId, const string& reason) {
        lock_guard<mutex> guard(storeMutex);
        const char* sql = "INSERT INTO cancel_reasons (ride_id, reason) VALUES (?, ?) ON CONFLICT(ride_id) DO UPDATE SET reason = excluded.reason;";
        sqlite3_stmt* statement = nullptr;
        if (sqlite3_prepare_v2(db, sql, -1, &statement, nullptr) != SQLITE_OK) {
            return;
        }
        sqlite3_bind_text(statement, 1, rideId.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(statement, 2, reason.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(statement);
        sqlite3_finalize(statement);
    }

    string cancelReasonForRide(const string& rideId) const {
        lock_guard<mutex> guard(storeMutex);
        const char* sql = "SELECT reason FROM cancel_reasons WHERE ride_id = ? LIMIT 1;";
        sqlite3_stmt* statement = nullptr;
        string reason;
        if (sqlite3_prepare_v2(db, sql, -1, &statement, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(statement, 1, rideId.c_str(), -1, SQLITE_TRANSIENT);
            if (sqlite3_step(statement) == SQLITE_ROW) {
                reason = textColumn(statement, 0);
            }
            sqlite3_finalize(statement);
        }
        return reason;
    }

    void logAdminAction(const string& actorEmail, const string& action, const string& target, const string& details) {
        lock_guard<mutex> guard(storeMutex);
        const char* sql = "INSERT INTO admin_audit_logs (actor_email, action, target, details) VALUES (?, ?, ?, ?);";
        sqlite3_stmt* statement = nullptr;
        if (sqlite3_prepare_v2(db, sql, -1, &statement, nullptr) != SQLITE_OK) {
            return;
        }
        sqlite3_bind_text(statement, 1, actorEmail.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(statement, 2, action.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(statement, 3, target.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(statement, 4, details.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(statement);
        sqlite3_finalize(statement);
    }

    vector<AuditLogSnapshot> listAuditLogs(int limit = 50) const {
        lock_guard<mutex> guard(storeMutex);
        vector<AuditLogSnapshot> logs;
        const char* sql = "SELECT actor_email, action, target, details, created_at FROM admin_audit_logs ORDER BY id DESC LIMIT ?;";
        sqlite3_stmt* statement = nullptr;
        if (sqlite3_prepare_v2(db, sql, -1, &statement, nullptr) != SQLITE_OK) {
            return logs;
        }
        sqlite3_bind_int(statement, 1, limit);
        while (sqlite3_step(statement) == SQLITE_ROW) {
            AuditLogSnapshot log;
            log.actorEmail = textColumn(statement, 0);
            log.action = textColumn(statement, 1);
            log.target = textColumn(statement, 2);
            log.details = textColumn(statement, 3);
            log.createdAt = textColumn(statement, 4);
            logs.push_back(log);
        }
        sqlite3_finalize(statement);
        return logs;
    }
};

}  // namespace uberride
