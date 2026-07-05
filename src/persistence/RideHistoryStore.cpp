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
    }
};

}  // namespace uberride
