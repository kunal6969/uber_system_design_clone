#pragma once

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#include "crow.h"
#include "auth/AuthStore.cpp"
#include "persistence/RideHistoryStore.cpp"
#include "service/RideService.cpp"

using namespace std;

namespace uberride {

static crow::response makeJsonResponse(crow::json::wvalue payload, int code = 200) {
    crow::response response(code);
    response.set_header("Content-Type", "application/json");
    response.write(payload.dump());
    return response;
}

static crow::response redirectTo(const string& path) {
    crow::response response(302);
    response.add_header("Location", path);
    return response;
}

static crow::json::wvalue errorPayload(const string& message) {
    crow::json::wvalue payload;
    payload["ok"] = false;
    payload["error"] = message;
    return payload;
}

static bool requireStringField(const crow::json::rvalue& body, const char* key, string& out) {
    if (!body || !body.has(key)) {
        return false;
    }
    out = body[key].s();
    return true;
}

static bool hasRole(const UserAccount& account, const string& role) {
    return account.role == role;
}

static bool isSupportedRole(const string& role) {
    return role == "rider" || role == "driver" || role == "admin";
}

static string driverStatusName(DriverStatus status) {
    if (status == AVAILABLE) {
        return "AVAILABLE";
    }
    if (status == ON_TRIP) {
        return "ON_TRIP";
    }
    return "OFFLINE";
}

static bool parseDriverStatus(const string& statusText, DriverStatus& status) {
    if (statusText == "AVAILABLE" || statusText == "available") {
        status = AVAILABLE;
        return true;
    }
    if (statusText == "OFFLINE" || statusText == "offline") {
        status = OFFLINE;
        return true;
    }
    return false;
}

static string pageTitleForRole(const string& role) {
    if (role == "rider") {
        return "Rider";
    }
    if (role == "driver") {
        return "Driver";
    }
    return "Admin";
}

static void replaceAll(string& text, const string& needle, const string& value) {
    size_t position = 0;
    while ((position = text.find(needle, position)) != string::npos) {
        text.replace(position, needle.size(), value);
        position += value.size();
    }
}

static string escapeHtml(const string& value) {
    string escaped;
    for (char ch : value) {
        if (ch == '&') escaped += "&amp;";
        else if (ch == '<') escaped += "&lt;";
        else if (ch == '>') escaped += "&gt;";
        else if (ch == '"') escaped += "&quot;";
        else if (ch == '\'') escaped += "&#39;";
        else escaped += ch;
    }
    return escaped;
}

static string csvCell(const string& value) {
    string escaped = "\"";
    for (char ch : value) {
        if (ch == '"') {
            escaped += "\"\"";
        } else {
            escaped += ch;
        }
    }
    escaped += "\"";
    return escaped;
}

static crow::response makeTextResponse(const string& body, const string& contentType, const string& fileName = "") {
    crow::response response(200);
    response.set_header("Content-Type", contentType);
    if (!fileName.empty()) {
        response.set_header("Content-Disposition", "attachment; filename=\"" + fileName + "\"");
    }
    response.write(body);
    return response;
}

static bool requireRole(AuthStore& authStore, const crow::request& req, const string& role, UserAccount& account, crow::response& failure) {
    if (!authStore.getSessionUser(req, account)) {
        failure = makeJsonResponse(errorPayload("Not authenticated."), 401);
        return false;
    }
    if (!hasRole(account, role)) {
        failure = makeJsonResponse(errorPayload(pageTitleForRole(role) + " access required."), 403);
        return false;
    }
    return true;
}

static int configuredPort() {
    const char* portFromEnvironment = getenv("PORT");
    if (!portFromEnvironment || string(portFromEnvironment).empty()) {
        return 18080;
    }

    try {
        return stoi(portFromEnvironment);
    } catch (...) {
        return 18080;
    }
}

static string configuredDatabasePath() {
    const char* pathFromEnvironment = getenv("DB_PATH");
    if (!pathFromEnvironment || string(pathFromEnvironment).empty()) {
        return "uber_ride_auth.db";
    }
    return pathFromEnvironment;
}

static bool envFlag(const string& name, bool defaultValue = false) {
    const char* value = getenv(name.c_str());
    if (!value) {
        return defaultValue;
    }

    string text = value;
    transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) { return static_cast<char>(tolower(ch)); });
    return text == "1" || text == "true" || text == "yes" || text == "on";
}

static bool parseRideStatusText(const string& statusText, RideStatus& status) {
    if (statusText == "REQUESTED") {
        status = REQUESTED;
        return true;
    }
    if (statusText == "DRIVER_ASSIGNED") {
        status = DRIVER_ASSIGNED;
        return true;
    }
    if (statusText == "IN_PROGRESS") {
        status = IN_PROGRESS;
        return true;
    }
    if (statusText == "COMPLETED") {
        status = COMPLETED;
        return true;
    }
    if (statusText == "CANCELLED") {
        status = CANCELLED;
        return true;
    }
    return false;
}

static string vehicleCodeFromTypeName(const string& typeName) {
    if (typeName == "Sedan" || typeName == "SEDAN") {
        return "SEDAN";
    }
    if (typeName == "SUV") {
        return "SUV";
    }
    if (typeName == "Bike" || typeName == "BIKE") {
        return "BIKE";
    }
    if (typeName == "Auto" || typeName == "AUTO") {
        return "AUTO";
    }
    return "SEDAN";
}

static int numberAfterPrefix(const string& value, const string& prefix) {
    if (value.rfind(prefix, 0) != 0) {
        return 0;
    }

    string numberText = value.substr(prefix.size());
    if (numberText.empty()) {
        return 0;
    }

    for (char ch : numberText) {
        if (ch < '0' || ch > '9') {
            return 0;
        }
    }

    return stoi(numberText);
}

class DemoRideBackend {
    mutable mutex backendMutex;
    RideHistoryStore historyStore;
    CityGraph* city;
    VehicleFactory* vehicleFactory;
    PricingStrategy* normalPricing;
    PricingService* pricingService;
    RouteService* routeService;
    NotificationService* notificationService;
    DriverMatchingService* matchingService;
    RideService* rideService;
    vector<Location*> locations;
    map<string, Location*> locationById;
    vector<Driver*> drivers;
    map<string, Driver*> driverById;
    map<string, Driver*> driverByEmail;
    map<string, Rider*> riderByEmail;
    vector<Ride*> rides;
    map<string, Ride*> rideById;
    map<string, double> cancellationFeeByRideId;
    int nextRiderId;
    int nextDriverId;

    Location* addLocation(const string& id, const string& name, double latitude, double longitude) {
        Location* location = new Location(id, name, latitude, longitude);
        city->addLocation(location);
        locations.push_back(location);
        locationById[id] = location;
        return location;
    }

    Driver* addDriver(const string& id, const string& name, const string& locationId, const string& vehicleType, DriverStatus status = AVAILABLE) {
        Driver* driver = new Driver(id, name, locationById[locationId], vehicleFactory->createVehicle(vehicleType));
        driver->setStatus(status);
        drivers.push_back(driver);
        driverById[id] = driver;
        return driver;
    }

    void initializeDemoState() {
        city = new CityGraph();
        vehicleFactory = new VehicleFactory();
        normalPricing = new NormalPricing();
        pricingService = new PricingService(normalPricing);
        routeService = new RouteService(city);
        notificationService = new NotificationService();
        matchingService = new DriverMatchingService(routeService);
        rideService = new RideService(routeService, matchingService, notificationService, pricingService, vehicleFactory);

        Location* cp = addLocation("CP", "Connaught Place", 28.6315, 77.2167);
        Location* kb = addLocation("KB", "Karol Bagh", 28.6519, 77.1907);
        Location* hk = addLocation("HK", "Hauz Khas", 28.5494, 77.2001);
        Location* saket = addLocation("SAKET", "Saket", 28.5245, 77.2066);
        Location* dwarka = addLocation("DWARKA", "Dwarka", 28.5921, 77.0460);
        Location* noida = addLocation("NOIDA", "Noida Sector 18", 28.5708, 77.3261);

        city->addRoad(cp, kb, 24.0);
        city->addRoad(cp, hk, 30.0);
        city->addRoad(kb, hk, 28.0);
        city->addRoad(hk, saket, 22.0);
        city->addRoad(cp, noida, 32.0);
        city->addRoad(hk, noida, 35.0);
        city->addRoad(kb, dwarka, 34.0);
        city->addRoad(hk, dwarka, 36.0);
        city->addRoad(saket, noida, 38.0);

        addDriver("D1", "Ramesh", "KB", "SEDAN");
        addDriver("D2", "Suresh", "HK", "SEDAN");
        addDriver("D3", "Ganesh", "NOIDA", "SEDAN");
        addDriver("D4", "Mahesh", "DWARKA", "SUV");
        addDriver("D5", "Vikram", "KB", "BIKE");
        addDriver("D6", "Dinesh", "HK", "SEDAN", ON_TRIP);

        riderByEmail["rider@demo.local"] = new Rider("R1", "Asha Rider");
        driverByEmail["driver@demo.local"] = driverById["D1"];
        nextRiderId = 1;
        nextDriverId = 6;
    }

    Rider* ensureRiderForAccount(const UserAccount& account) {
        if (riderByEmail.count(account.email)) {
            return riderByEmail[account.email];
        }

        Rider* rider = new Rider("RWEB-" + to_string(++nextRiderId), account.name);
        riderByEmail[account.email] = rider;
        return rider;
    }

    Driver* ensureDriverForAccount(const UserAccount& account) {
        if (driverByEmail.count(account.email)) {
            Driver* driver = driverByEmail[account.email];
            DriverProfileSnapshot profile = historyStore.getDriverProfile(account.email, account.name, vehicleCodeFromTypeName(driver->getVehicle()->getTypeName()), driver->getCurrentLocation()->getId());
            Location* profileLocation = findLocation(profile.locationId);
            if (profileLocation) {
                driver->setCurrentLocation(profileLocation);
            }
            DriverStatus status;
            if (parseDriverStatus(profile.availability, status) && !hasActiveRideForDriver(driver)) {
                driver->setStatus(status);
            }
            return driver;
        }

        DriverProfileSnapshot profile = historyStore.getDriverProfile(account.email, account.name);
        Location* defaultLocation = findLocation(profile.locationId);
        if (!defaultLocation) {
            defaultLocation = locationById.count("KB") ? locationById["KB"] : locations.front();
        }
        Driver* driver = new Driver("DWEB-" + to_string(++nextDriverId), profile.name, defaultLocation, vehicleFactory->createVehicle(profile.vehicleType));
        DriverStatus status;
        if (parseDriverStatus(profile.availability, status)) {
            driver->setStatus(status);
        }
        drivers.push_back(driver);
        driverById[driver->getId()] = driver;
        driverByEmail[account.email] = driver;
        return driver;
    }

    Rider* ensureRiderFromSnapshot(const string& email, const string& name) {
        if (riderByEmail.count(email)) {
            return riderByEmail[email];
        }

        Rider* rider = new Rider("RWEB-" + to_string(++nextRiderId), name.empty() ? "Restored Rider" : name);
        riderByEmail[email] = rider;
        return rider;
    }

    Driver* ensureDriverFromSnapshot(const string& driverId, const string& driverEmail, const string& driverName, const string& vehicleType) {
        if (!driverId.empty() && driverById.count(driverId)) {
            Driver* driver = driverById[driverId];
            if (!driverEmail.empty()) {
                driverByEmail[driverEmail] = driver;
            }
            return driver;
        }

        if (!driverEmail.empty() && driverByEmail.count(driverEmail)) {
            return driverByEmail[driverEmail];
        }

        Location* defaultLocation = locationById.count("KB") ? locationById["KB"] : locations.front();
        string id = driverId.empty() ? "DWEB-" + to_string(++nextDriverId) : driverId;
        Driver* driver = new Driver(id, driverName.empty() ? "Restored Driver" : driverName, defaultLocation, vehicleFactory->createVehicle(vehicleCodeFromTypeName(vehicleType)));
        drivers.push_back(driver);
        driverById[driver->getId()] = driver;
        if (!driverEmail.empty()) {
            driverByEmail[driverEmail] = driver;
        }
        nextDriverId = max(nextDriverId, numberAfterPrefix(driver->getId(), "DWEB-"));
        return driver;
    }

    string emailForRider(Rider* rider) const {
        for (const pair<const string, Rider*>& entry : riderByEmail) {
            if (entry.second == rider) {
                return entry.first;
            }
        }
        return rider->getId() + "@ride.local";
    }

    string emailForDriver(Driver* driver) const {
        if (!driver) {
            return "";
        }
        for (const pair<const string, Driver*>& entry : driverByEmail) {
            if (entry.second == driver) {
                return entry.first;
            }
        }
        return "";
    }

    PersistedRideSnapshot snapshotFromRide(Ride* ride) const {
        PersistedRideSnapshot snapshot;
        snapshot.rideId = ride->getId();
        snapshot.riderEmail = emailForRider(ride->getRider());
        snapshot.riderName = ride->getRider()->getName();
        snapshot.vehicleType = vehicleCodeFromTypeName(ride->getVehicle()->getTypeName());
        snapshot.sourceId = ride->getSource()->getId();
        snapshot.destinationId = ride->getDestination()->getId();
        snapshot.status = rideStatusName(ride->getStatus());
        snapshot.distanceKm = ride->getDistanceKm();
        snapshot.timeMinutes = ride->getTimeMinutes();
        snapshot.fare = ride->getFare();
        snapshot.cancellationFee = cancellationFeeByRideId.count(ride->getId()) ? cancellationFeeByRideId.at(ride->getId()) : 0.0;

        if (ride->getDriver()) {
            snapshot.driverId = ride->getDriver()->getId();
            snapshot.driverEmail = emailForDriver(ride->getDriver());
            snapshot.driverName = ride->getDriver()->getName();
        }

        for (Location* stop : ride->getStops()) {
            snapshot.stopIds.push_back(stop->getId());
        }

        return snapshot;
    }

    void persistRide(Ride* ride) {
        try {
            historyStore.saveRide(snapshotFromRide(ride));
        } catch (const exception& ex) {
            cerr << "[RideHistory] Unable to save " << ride->getId() << ": " << ex.what() << endl;
        }
    }

    crow::json::wvalue makeEstimate(Location* source, Location* destination, const string& vehicleType) {
        crow::json::wvalue value;
        vector<Location*> waypoints;
        waypoints.push_back(source);
        waypoints.push_back(destination);

        RouteResult* route = routeService->findShortestRoute(source, destination);
        Vehicle* vehicle = vehicleFactory->createVehicle(vehicleType);
        double fare = pricingService->estimateFare(vehicle, route->getTotalDistanceKm(), route->getTotalTimeMinutes());

        value["sourceName"] = source->getName();
        value["destinationName"] = destination->getName();
        value["vehicleType"] = vehicle->getTypeName();
        value["distanceKm"] = route->getTotalDistanceKm();
        value["timeMinutes"] = route->getTotalTimeMinutes();
        value["fare"] = fare;
        return value;
    }

    void restorePersistedRides() {
        vector<PersistedRideSnapshot> snapshots = historyStore.loadAllRides();
        int maxRideNumber = 0;

        for (const PersistedRideSnapshot& snapshot : snapshots) {
            Location* source = findLocation(snapshot.sourceId);
            Location* destination = findLocation(snapshot.destinationId);
            if (!source || !destination || rideById.count(snapshot.rideId)) {
                continue;
            }

            Rider* rider = ensureRiderFromSnapshot(snapshot.riderEmail, snapshot.riderName);
            Ride* ride = new Ride(snapshot.rideId, rider, vehicleFactory->createVehicle(vehicleCodeFromTypeName(snapshot.vehicleType)), source, destination);

            for (const string& stopId : snapshot.stopIds) {
                Location* stop = findLocation(stopId);
                if (stop) {
                    ride->addStop(stop);
                }
            }

            RideStatus status = REQUESTED;
            parseRideStatusText(snapshot.status, status);
            ride->setStatus(status);
            ride->setDistanceKm(snapshot.distanceKm);
            ride->setTimeMinutes(snapshot.timeMinutes);
            ride->setFare(snapshot.fare);

            if (!snapshot.driverId.empty()) {
                Driver* driver = ensureDriverFromSnapshot(snapshot.driverId, snapshot.driverEmail, snapshot.driverName, snapshot.vehicleType);
                ride->setDriver(driver);
                if (status == DRIVER_ASSIGNED || status == IN_PROGRESS) {
                    driver->setStatus(ON_TRIP);
                }
            }

            rides.push_back(ride);
            rideById[ride->getId()] = ride;
            cancellationFeeByRideId[ride->getId()] = snapshot.cancellationFee;
            maxRideNumber = max(maxRideNumber, numberAfterPrefix(ride->getId(), "RIDE-"));
        }

        rideService->setRideCounter(maxRideNumber);
    }

    Location* findLocation(const string& id) const {
        map<string, Location*>::const_iterator it = locationById.find(id);
        return it == locationById.end() ? nullptr : it->second;
    }

    Ride* findRide(const string& id) const {
        map<string, Ride*>::const_iterator it = rideById.find(id);
        return it == rideById.end() ? nullptr : it->second;
    }

    Driver* findDriver(const string& id) const {
        map<string, Driver*>::const_iterator it = driverById.find(id);
        return it == driverById.end() ? nullptr : it->second;
    }

    bool isKnownVehicleType(const string& vehicleType) const {
        return vehicleType == "SEDAN" || vehicleType == "SUV" || vehicleType == "BIKE" || vehicleType == "AUTO";
    }

    vector<DriverMatch*> findMatchesForRide(Ride* ride, int maxDrivers) {
        return matchingService->findNearestDrivers(
            ride->getSource(),
            drivers,
            ride->getVehicle()->getTypeName(),
            maxDrivers
        );
    }

    bool driverCanSeeRequest(Driver* driver, Ride* ride) {
        if (ride->getStatus() != REQUESTED || driver->getStatus() != AVAILABLE) {
            return false;
        }
        vector<DriverMatch*> matches = findMatchesForRide(ride, 3);
        for (DriverMatch* match : matches) {
            if (match->getDriver()->getId() == driver->getId()) {
                return true;
            }
        }
        return false;
    }

    bool hasActiveRideForDriver(Driver* driver) const {
        for (Ride* ride : rides) {
            if (!ride->getDriver()) {
                continue;
            }
            if (ride->getDriver()->getId() == driver->getId() &&
                (ride->getStatus() == DRIVER_ASSIGNED || ride->getStatus() == IN_PROGRESS)) {
                return true;
            }
        }
        return false;
    }

    crow::json::wvalue locationJson(Location* location) const {
        crow::json::wvalue value;
        value["id"] = location->getId();
        value["name"] = location->getName();
        value["latitude"] = location->getLatitude();
        value["longitude"] = location->getLongitude();
        return value;
    }

    crow::json::wvalue driverJson(Driver* driver) const {
        crow::json::wvalue value;
        value["id"] = driver->getId();
        value["name"] = driver->getName();
        value["status"] = driverStatusName(driver->getStatus());
        value["vehicleType"] = driver->getVehicle()->getTypeName();
        value["locationId"] = driver->getCurrentLocation()->getId();
        value["locationName"] = driver->getCurrentLocation()->getName();
        value["rating"] = historyStore.averageRatingForDriver(driver->getId());
        return value;
    }

    crow::json::wvalue matchJson(DriverMatch* match) const {
        crow::json::wvalue value;
        value["etaMinutes"] = match->getEtaMinutes();
        value["driver"] = driverJson(match->getDriver());
        return value;
    }

    crow::json::wvalue matchesJson(const vector<DriverMatch*>& matches) const {
        crow::json::wvalue value = crow::json::wvalue::list();
        for (unsigned index = 0; index < matches.size(); ++index) {
            value[index] = matchJson(matches[index]);
        }
        return value;
    }

    crow::json::wvalue rideJson(Ride* ride, bool includeMatches = false) {
        crow::json::wvalue value;
        value["id"] = ride->getId();
        value["riderName"] = ride->getRider()->getName();
        value["vehicleType"] = ride->getVehicle()->getTypeName();
        value["sourceId"] = ride->getSource()->getId();
        value["sourceName"] = ride->getSource()->getName();
        value["destinationId"] = ride->getDestination()->getId();
        value["destinationName"] = ride->getDestination()->getName();
        value["status"] = rideStatusName(ride->getStatus());
        value["distanceKm"] = ride->getDistanceKm();
        value["timeMinutes"] = ride->getTimeMinutes();
        value["fare"] = ride->getFare();
        value["cancellationFee"] = cancellationFeeByRideId.count(ride->getId()) ? cancellationFeeByRideId[ride->getId()] : 0.0;
        value["rating"] = historyStore.ratingForRide(ride->getId());
        value["cancelReason"] = historyStore.cancelReasonForRide(ride->getId());

        if (ride->getDriver()) {
            value["driver"] = driverJson(ride->getDriver());
        } else {
            value["driver"] = nullptr;
        }

        value["stops"] = crow::json::wvalue::list();
        vector<Location*> stops = ride->getStops();
        for (unsigned index = 0; index < stops.size(); ++index) {
            value["stops"][index] = locationJson(stops[index]);
        }

        if (includeMatches && ride->getStatus() == REQUESTED) {
            value["matches"] = matchesJson(findMatchesForRide(ride, 3));
        } else {
            value["matches"] = crow::json::wvalue::list();
        }

        return value;
    }

    void appendLocations(crow::json::wvalue& payload) const {
        payload["locations"] = crow::json::wvalue::list();
        for (unsigned index = 0; index < locations.size(); ++index) {
            payload["locations"][index] = locationJson(locations[index]);
        }
    }

    void appendVehicleTypes(crow::json::wvalue& payload) const {
        payload["vehicleTypes"] = crow::json::wvalue::list();
        payload["vehicleTypes"][0] = "SEDAN";
        payload["vehicleTypes"][1] = "SUV";
        payload["vehicleTypes"][2] = "BIKE";
        payload["vehicleTypes"][3] = "AUTO";
    }

    void appendSavedLocations(crow::json::wvalue& payload, const string& email) const {
        vector<SavedLocationSnapshot> savedLocations = historyStore.listSavedLocations(email);
        payload["savedLocations"] = crow::json::wvalue::list();
        for (unsigned index = 0; index < savedLocations.size(); ++index) {
            payload["savedLocations"][index]["label"] = savedLocations[index].label;
            payload["savedLocations"][index]["locationId"] = savedLocations[index].locationId;
            payload["savedLocations"][index]["locationName"] = locationById.count(savedLocations[index].locationId) ? locationById.at(savedLocations[index].locationId)->getName() : savedLocations[index].locationId;
        }
    }

public:
    explicit DemoRideBackend(const string& databasePath = "uber_ride_auth.db") : historyStore(databasePath),
                        city(nullptr),
                        vehicleFactory(nullptr),
                        normalPricing(nullptr),
                        pricingService(nullptr),
                        routeService(nullptr),
                        notificationService(nullptr),
                        matchingService(nullptr),
                        rideService(nullptr),
                        nextRiderId(0),
                        nextDriverId(0) {
        reset();
    }

    void reset(bool clearPersistentHistory = false) {
        lock_guard<mutex> guard(backendMutex);
        locations.clear();
        locationById.clear();
        drivers.clear();
        driverById.clear();
        driverByEmail.clear();
        riderByEmail.clear();
        rides.clear();
        rideById.clear();
        cancellationFeeByRideId.clear();
        nextRiderId = 0;
        nextDriverId = 0;
        initializeDemoState();

        if (clearPersistentHistory) {
            historyStore.clearAll();
        } else {
            restorePersistedRides();
        }
    }

    crow::json::wvalue riderState(const UserAccount& account) {
        lock_guard<mutex> guard(backendMutex);
        Rider* rider = ensureRiderForAccount(account);

        crow::json::wvalue payload;
        payload["ok"] = true;
        payload["profile"]["name"] = account.name;
        payload["profile"]["email"] = account.email;
        appendLocations(payload);
        appendVehicleTypes(payload);
        appendSavedLocations(payload, account.email);
        payload["rides"] = crow::json::wvalue::list();

        unsigned rideIndex = 0;
        for (Ride* ride : rides) {
            if (ride->getRider()->getId() == rider->getId()) {
                payload["rides"][rideIndex++] = rideJson(ride, true);
            }
        }

        return payload;
    }

    bool estimateRideForRider(const UserAccount& account, const string& sourceId, const string& destinationId, const string& vehicleType, crow::json::wvalue& payload, string& error) {
        lock_guard<mutex> guard(backendMutex);
        Location* source = findLocation(sourceId);
        Location* destination = findLocation(destinationId);
        if (!source || !destination || source->getId() == destination->getId()) {
            error = "Choose valid, different pickup and dropoff locations.";
            return false;
        }
        if (!isKnownVehicleType(vehicleType)) {
            error = "Choose a supported vehicle type.";
            return false;
        }

        payload["ok"] = true;
        payload["estimate"] = makeEstimate(source, destination, vehicleType);
        return true;
    }

    bool createRideForRider(const UserAccount& account, const string& sourceId, const string& destinationId, const string& vehicleType, crow::json::wvalue& payload, string& error) {
        lock_guard<mutex> guard(backendMutex);
        Location* source = findLocation(sourceId);
        Location* destination = findLocation(destinationId);
        if (!source || !destination) {
            error = "Choose valid pickup and dropoff locations.";
            return false;
        }
        if (source->getId() == destination->getId()) {
            error = "Pickup and dropoff must be different.";
            return false;
        }
        if (!isKnownVehicleType(vehicleType)) {
            error = "Choose a supported vehicle type.";
            return false;
        }

        Rider* rider = ensureRiderForAccount(account);
        Ride* ride = rideService->createRide(rider, vehicleType, source, destination);
        rides.push_back(ride);
        rideById[ride->getId()] = ride;

        vector<DriverMatch*> matches = rideService->notifyNearbyDrivers(ride, drivers, 3);
        persistRide(ride);

        payload["ok"] = true;
        payload["message"] = "Ride requested.";
        payload["ride"] = rideJson(ride);
        payload["matches"] = matchesJson(matches);
        return true;
    }

    bool rebookRideForRider(const UserAccount& account, const string& rideId, crow::json::wvalue& payload, string& error) {
        lock_guard<mutex> guard(backendMutex);
        Rider* rider = ensureRiderForAccount(account);
        Ride* originalRide = findRide(rideId);
        if (!originalRide || originalRide->getRider()->getId() != rider->getId()) {
            error = "Ride not found.";
            return false;
        }

        Ride* ride = rideService->createRide(
            rider,
            vehicleCodeFromTypeName(originalRide->getVehicle()->getTypeName()),
            originalRide->getSource(),
            originalRide->getDestination()
        );
        rides.push_back(ride);
        rideById[ride->getId()] = ride;
        vector<DriverMatch*> matches = rideService->notifyNearbyDrivers(ride, drivers, 3);
        persistRide(ride);

        payload["ok"] = true;
        payload["message"] = "Ride rebooked.";
        payload["ride"] = rideJson(ride);
        payload["matches"] = matchesJson(matches);
        return true;
    }

    bool assignNearestForRider(const UserAccount& account, const string& rideId, crow::json::wvalue& payload, string& error) {
        lock_guard<mutex> guard(backendMutex);
        Rider* rider = ensureRiderForAccount(account);
        Ride* ride = findRide(rideId);
        if (!ride || ride->getRider()->getId() != rider->getId()) {
            error = "Ride not found.";
            return false;
        }
        if (ride->getStatus() != REQUESTED) {
            error = "Only requested rides can be assigned.";
            return false;
        }

        vector<DriverMatch*> matches = findMatchesForRide(ride, 3);
        if (matches.empty()) {
            error = "No available matching drivers right now.";
            return false;
        }

        rideService->assignDriver(ride, matches.front()->getDriver());
        persistRide(ride);
        payload["ok"] = true;
        payload["message"] = "Driver assigned.";
        payload["ride"] = rideJson(ride);
        return true;
    }

    bool addStopForRider(const UserAccount& account, const string& rideId, const string& stopId, crow::json::wvalue& payload, string& error) {
        lock_guard<mutex> guard(backendMutex);
        Rider* rider = ensureRiderForAccount(account);
        Ride* ride = findRide(rideId);
        Location* stop = findLocation(stopId);
        if (!ride || ride->getRider()->getId() != rider->getId()) {
            error = "Ride not found.";
            return false;
        }
        if (!stop) {
            error = "Stop location not found.";
            return false;
        }
        if (ride->getStatus() == COMPLETED || ride->getStatus() == CANCELLED) {
            error = "Cannot add a stop to a finished ride.";
            return false;
        }

        rideService->addStop(ride, stop);
        persistRide(ride);
        payload["ok"] = true;
        payload["message"] = "Stop added.";
        payload["ride"] = rideJson(ride, true);
        return true;
    }

    bool cancelRideForRider(const UserAccount& account, const string& rideId, const string& reason, crow::json::wvalue& payload, string& error) {
        lock_guard<mutex> guard(backendMutex);
        Rider* rider = ensureRiderForAccount(account);
        Ride* ride = findRide(rideId);
        if (!ride || ride->getRider()->getId() != rider->getId()) {
            error = "Ride not found.";
            return false;
        }
        if (ride->getStatus() == COMPLETED || ride->getStatus() == CANCELLED) {
            error = "This ride is already finished.";
            return false;
        }

        double fee = rideService->cancelRide(ride);
        cancellationFeeByRideId[ride->getId()] = fee;
        if (!reason.empty()) {
            historyStore.saveCancelReason(ride->getId(), reason);
        }
        persistRide(ride);
        payload["ok"] = true;
        payload["message"] = "Ride cancelled.";
        payload["ride"] = rideJson(ride);
        return true;
    }

    bool saveLocationForRider(const UserAccount& account, const string& label, const string& locationId, crow::json::wvalue& payload, string& error) {
        lock_guard<mutex> guard(backendMutex);
        if (label.empty()) {
            error = "Location label is required.";
            return false;
        }
        if (!findLocation(locationId)) {
            error = "Location not found.";
            return false;
        }
        historyStore.saveSavedLocation(account.email, label, locationId);
        payload["ok"] = true;
        payload["message"] = "Saved location updated.";
        return true;
    }

    bool rateRideForRider(const UserAccount& account, const string& rideId, int rating, const string& comment, crow::json::wvalue& payload, string& error) {
        lock_guard<mutex> guard(backendMutex);
        Rider* rider = ensureRiderForAccount(account);
        Ride* ride = findRide(rideId);
        if (!ride || ride->getRider()->getId() != rider->getId()) {
            error = "Ride not found.";
            return false;
        }
        if (ride->getStatus() != COMPLETED || !ride->getDriver()) {
            error = "Only completed rides with drivers can be rated.";
            return false;
        }
        if (rating < 1 || rating > 5) {
            error = "Rating must be between 1 and 5.";
            return false;
        }

        RatingSnapshot snapshot;
        snapshot.rideId = ride->getId();
        snapshot.riderEmail = account.email;
        snapshot.driverId = ride->getDriver()->getId();
        snapshot.rating = rating;
        snapshot.comment = comment;
        historyStore.saveRating(snapshot);

        payload["ok"] = true;
        payload["message"] = "Rating saved.";
        payload["ride"] = rideJson(ride);
        return true;
    }

    crow::json::wvalue driverState(const UserAccount& account) {
        lock_guard<mutex> guard(backendMutex);
        Driver* driver = ensureDriverForAccount(account);

        crow::json::wvalue payload;
        payload["ok"] = true;
        payload["driver"] = driverJson(driver);
        DriverProfileSnapshot profile = historyStore.getDriverProfile(account.email, account.name, vehicleCodeFromTypeName(driver->getVehicle()->getTypeName()), driver->getCurrentLocation()->getId());
        payload["profile"]["name"] = profile.name;
        payload["profile"]["email"] = profile.email;
        payload["profile"]["vehicleType"] = profile.vehicleType;
        payload["profile"]["locationId"] = profile.locationId;
        payload["profile"]["availability"] = driverStatusName(driver->getStatus());
        payload["profile"]["rating"] = historyStore.averageRatingForDriver(driver->getId());
        appendLocations(payload);
        appendVehicleTypes(payload);
        payload["requests"] = crow::json::wvalue::list();
        payload["rides"] = crow::json::wvalue::list();

        unsigned requestIndex = 0;
        unsigned rideIndex = 0;
        for (Ride* ride : rides) {
            if (driverCanSeeRequest(driver, ride)) {
                payload["requests"][requestIndex++] = rideJson(ride);
            }

            if (ride->getDriver() && ride->getDriver()->getId() == driver->getId()) {
                payload["rides"][rideIndex++] = rideJson(ride);
            }
        }

        return payload;
    }

    bool updateDriverProfileForAccount(const UserAccount& account, const string& name, const string& vehicleType, const string& locationId, crow::json::wvalue& payload, string& error) {
        lock_guard<mutex> guard(backendMutex);
        if (!isKnownVehicleType(vehicleType)) {
            error = "Choose a supported vehicle type.";
            return false;
        }
        Location* location = findLocation(locationId);
        if (!location) {
            error = "Location not found.";
            return false;
        }
        Driver* driver = ensureDriverForAccount(account);
        if (hasActiveRideForDriver(driver)) {
            error = "Finish active rides before changing profile.";
            return false;
        }

        DriverProfileSnapshot profile;
        profile.email = account.email;
        profile.name = name.empty() ? account.name : name;
        profile.vehicleType = vehicleType;
        profile.locationId = locationId;
        profile.availability = driverStatusName(driver->getStatus());
        historyStore.saveDriverProfile(profile);

        driver->setCurrentLocation(location);
        payload["ok"] = true;
        payload["message"] = "Driver profile saved. Vehicle changes apply after the next session refresh.";
        payload["driver"] = driverJson(driver);
        return true;
    }

    bool setDriverStatusForAccount(const UserAccount& account, const string& statusText, crow::json::wvalue& payload, string& error) {
        lock_guard<mutex> guard(backendMutex);
        DriverStatus status;
        if (!parseDriverStatus(statusText, status)) {
            error = "Drivers can be set to AVAILABLE or OFFLINE.";
            return false;
        }

        Driver* driver = ensureDriverForAccount(account);
        if (hasActiveRideForDriver(driver)) {
            error = "Finish the active ride before changing availability.";
            return false;
        }

        driver->setStatus(status);
        DriverProfileSnapshot profile = historyStore.getDriverProfile(account.email, account.name, vehicleCodeFromTypeName(driver->getVehicle()->getTypeName()), driver->getCurrentLocation()->getId());
        profile.availability = driverStatusName(status);
        historyStore.saveDriverProfile(profile);
        historyStore.logDriverStatus(driver->getId(), account.email, driverStatusName(status));
        payload["ok"] = true;
        payload["message"] = "Driver status updated.";
        payload["driver"] = driverJson(driver);
        return true;
    }

    bool rejectRideForDriver(const UserAccount& account, const string& rideId, const string& reason, crow::json::wvalue& payload, string& error) {
        lock_guard<mutex> guard(backendMutex);
        Driver* driver = ensureDriverForAccount(account);
        Ride* ride = findRide(rideId);
        if (!ride) {
            error = "Ride not found.";
            return false;
        }
        if (!driverCanSeeRequest(driver, ride)) {
            error = "This ride is not currently matched to this driver.";
            return false;
        }
        historyStore.logDriverStatus(driver->getId(), account.email, "REJECTED " + rideId + (reason.empty() ? "" : ": " + reason));
        payload["ok"] = true;
        payload["message"] = "Ride rejected for this driver session.";
        return true;
    }

    bool acceptRideForDriver(const UserAccount& account, const string& rideId, crow::json::wvalue& payload, string& error) {
        lock_guard<mutex> guard(backendMutex);
        Driver* driver = ensureDriverForAccount(account);
        Ride* ride = findRide(rideId);
        if (!ride) {
            error = "Ride not found.";
            return false;
        }
        if (ride->getStatus() != REQUESTED) {
            error = "This ride is no longer waiting for a driver.";
            return false;
        }
        if (driver->getStatus() != AVAILABLE) {
            error = "Set yourself available before accepting a ride.";
            return false;
        }
        if (!driverCanSeeRequest(driver, ride)) {
            error = "This ride is not currently matched to this driver.";
            return false;
        }

        rideService->assignDriver(ride, driver);
        persistRide(ride);
        payload["ok"] = true;
        payload["message"] = "Ride accepted.";
        payload["ride"] = rideJson(ride);
        payload["driver"] = driverJson(driver);
        return true;
    }

    bool startRideForDriver(const UserAccount& account, const string& rideId, crow::json::wvalue& payload, string& error) {
        lock_guard<mutex> guard(backendMutex);
        Driver* driver = ensureDriverForAccount(account);
        Ride* ride = findRide(rideId);
        if (!ride || !ride->getDriver() || ride->getDriver()->getId() != driver->getId()) {
            error = "Ride not found for this driver.";
            return false;
        }
        if (ride->getStatus() != DRIVER_ASSIGNED) {
            error = "Only assigned rides can be started.";
            return false;
        }

        rideService->startRide(ride);
        persistRide(ride);
        payload["ok"] = true;
        payload["message"] = "Ride started.";
        payload["ride"] = rideJson(ride);
        return true;
    }

    bool completeRideForDriver(const UserAccount& account, const string& rideId, crow::json::wvalue& payload, string& error) {
        lock_guard<mutex> guard(backendMutex);
        Driver* driver = ensureDriverForAccount(account);
        Ride* ride = findRide(rideId);
        if (!ride || !ride->getDriver() || ride->getDriver()->getId() != driver->getId()) {
            error = "Ride not found for this driver.";
            return false;
        }
        if (ride->getStatus() != IN_PROGRESS) {
            error = "Only in-progress rides can be completed.";
            return false;
        }

        rideService->completeRide(ride);
        persistRide(ride);
        payload["ok"] = true;
        payload["message"] = "Ride completed.";
        payload["ride"] = rideJson(ride);
        payload["driver"] = driverJson(driver);
        return true;
    }

    crow::json::wvalue adminState(const vector<UserAccount>& accounts) {
        lock_guard<mutex> guard(backendMutex);
        crow::json::wvalue payload;
        payload["ok"] = true;
        appendLocations(payload);
        appendVehicleTypes(payload);

        payload["summary"]["users"] = static_cast<int>(accounts.size());
        payload["summary"]["drivers"] = static_cast<int>(drivers.size());
        payload["summary"]["rides"] = static_cast<int>(rides.size());

        int activeRides = 0;
        int completedRides = 0;
        int cancelledRides = 0;
        for (Ride* ride : rides) {
            if (ride->getStatus() == DRIVER_ASSIGNED || ride->getStatus() == IN_PROGRESS) {
                ++activeRides;
            } else if (ride->getStatus() == COMPLETED) {
                ++completedRides;
            } else if (ride->getStatus() == CANCELLED) {
                ++cancelledRides;
            }
        }
        payload["summary"]["activeRides"] = activeRides;
        payload["summary"]["completedRides"] = completedRides;
        payload["summary"]["cancelledRides"] = cancelledRides;

        payload["users"] = crow::json::wvalue::list();
        for (unsigned index = 0; index < accounts.size(); ++index) {
            payload["users"][index]["id"] = accounts[index].id;
            payload["users"][index]["name"] = accounts[index].name;
            payload["users"][index]["email"] = accounts[index].email;
            payload["users"][index]["role"] = accounts[index].role;
            payload["users"][index]["active"] = accounts[index].active;
        }

        payload["drivers"] = crow::json::wvalue::list();
        for (unsigned index = 0; index < drivers.size(); ++index) {
            payload["drivers"][index] = driverJson(drivers[index]);
        }

        payload["rides"] = crow::json::wvalue::list();
        for (unsigned index = 0; index < rides.size(); ++index) {
            payload["rides"][index] = rideJson(rides[index]);
        }

        vector<AuditLogSnapshot> auditLogs = historyStore.listAuditLogs(50);
        payload["auditLogs"] = crow::json::wvalue::list();
        for (unsigned index = 0; index < auditLogs.size(); ++index) {
            payload["auditLogs"][index]["actorEmail"] = auditLogs[index].actorEmail;
            payload["auditLogs"][index]["action"] = auditLogs[index].action;
            payload["auditLogs"][index]["target"] = auditLogs[index].target;
            payload["auditLogs"][index]["details"] = auditLogs[index].details;
            payload["auditLogs"][index]["createdAt"] = auditLogs[index].createdAt;
        }

        return payload;
    }

    bool setDriverStatusAsAdmin(const string& driverId, const string& statusText, crow::json::wvalue& payload, string& error) {
        lock_guard<mutex> guard(backendMutex);
        Driver* driver = findDriver(driverId);
        if (!driver) {
            error = "Driver not found.";
            return false;
        }

        DriverStatus status;
        if (!parseDriverStatus(statusText, status)) {
            error = "Drivers can be set to AVAILABLE or OFFLINE.";
            return false;
        }

        if (hasActiveRideForDriver(driver)) {
            error = "Finish the driver's active ride before changing availability.";
            return false;
        }

        driver->setStatus(status);
        historyStore.logDriverStatus(driver->getId(), "admin", driverStatusName(status));
        payload["ok"] = true;
        payload["message"] = "Driver status updated.";
        payload["driver"] = driverJson(driver);
        return true;
    }

    bool cancelRideAsAdmin(const string& rideId, crow::json::wvalue& payload, string& error) {
        lock_guard<mutex> guard(backendMutex);
        Ride* ride = findRide(rideId);
        if (!ride) {
            error = "Ride not found.";
            return false;
        }
        if (ride->getStatus() == COMPLETED || ride->getStatus() == CANCELLED) {
            error = "This ride is already finished.";
            return false;
        }

        double fee = rideService->cancelRide(ride);
        cancellationFeeByRideId[ride->getId()] = fee;
        persistRide(ride);
        payload["ok"] = true;
        payload["message"] = "Ride cancelled.";
        payload["ride"] = rideJson(ride);
        return true;
    }

    bool receiptHtml(const UserAccount& account, const string& rideId, string& html, string& error) {
        lock_guard<mutex> guard(backendMutex);
        Ride* ride = findRide(rideId);
        if (!ride) {
            error = "Ride not found.";
            return false;
        }

        string riderEmail = emailForRider(ride->getRider());
        bool canView = account.role == "admin" || account.email == riderEmail;
        if (!canView && ride->getDriver() && emailForDriver(ride->getDriver()) == account.email) {
            canView = true;
        }
        if (!canView) {
            error = "Receipt access denied.";
            return false;
        }

        ostringstream out;
        out << "<!doctype html><html><head><meta charset='utf-8'><title>Receipt "
            << escapeHtml(ride->getId())
            << "</title><style>body{font-family:Arial,sans-serif;max-width:760px;margin:30px auto;color:#17212f}"
            << ".box{border:1px solid #cbd5e1;border-radius:8px;padding:20px}table{width:100%;border-collapse:collapse}"
            << "td{padding:8px;border-bottom:1px solid #e2e8f0}.right{text-align:right}.muted{color:#64748b}"
            << "button{padding:10px 14px;border:0;border-radius:6px;background:#111827;color:white;font-weight:700}"
            << "@media print{button{display:none}}</style></head><body><div class='box'>"
            << "<button onclick='window.print()'>Download / Save as PDF</button>"
            << "<h1>Ride Receipt</h1><p class='muted'>" << escapeHtml(ride->getId()) << "</p><table>"
            << "<tr><td>Rider</td><td class='right'>" << escapeHtml(ride->getRider()->getName()) << "</td></tr>"
            << "<tr><td>Driver</td><td class='right'>" << (ride->getDriver() ? escapeHtml(ride->getDriver()->getName()) : string("Unassigned")) << "</td></tr>"
            << "<tr><td>Status</td><td class='right'>" << escapeHtml(rideStatusName(ride->getStatus())) << "</td></tr>"
            << "<tr><td>Route</td><td class='right'>" << escapeHtml(ride->getSource()->getName()) << " to " << escapeHtml(ride->getDestination()->getName()) << "</td></tr>";
        vector<Location*> stops = ride->getStops();
        for (Location* stop : stops) {
            out << "<tr><td>Stop</td><td class='right'>" << escapeHtml(stop->getName()) << "</td></tr>";
        }
        out << fixed << setprecision(2)
            << "<tr><td>Distance</td><td class='right'>" << ride->getDistanceKm() << " km</td></tr>"
            << "<tr><td>Time</td><td class='right'>" << ride->getTimeMinutes() << " min</td></tr>"
            << "<tr><td>Fare</td><td class='right'>Rs " << ride->getFare() << "</td></tr>"
            << "<tr><td>Cancellation Fee</td><td class='right'>Rs "
            << (cancellationFeeByRideId.count(ride->getId()) ? cancellationFeeByRideId[ride->getId()] : 0.0)
            << "</td></tr></table></div></body></html>";

        html = out.str();
        return true;
    }

    string exportUsersCsv(const vector<UserAccount>& accounts) {
        ostringstream out;
        out << "id,name,email,role,active\n";
        for (const UserAccount& account : accounts) {
            out << csvCell(account.id) << ","
                << csvCell(account.name) << ","
                << csvCell(account.email) << ","
                << csvCell(account.role) << ","
                << (account.active ? "true" : "false") << "\n";
        }
        return out.str();
    }

    string exportDriversCsv() {
        lock_guard<mutex> guard(backendMutex);
        ostringstream out;
        out << "id,name,vehicle,status,location,rating\n";
        for (Driver* driver : drivers) {
            out << csvCell(driver->getId()) << ","
                << csvCell(driver->getName()) << ","
                << csvCell(driver->getVehicle()->getTypeName()) << ","
                << csvCell(driverStatusName(driver->getStatus())) << ","
                << csvCell(driver->getCurrentLocation()->getName()) << ","
                << fixed << setprecision(2) << historyStore.averageRatingForDriver(driver->getId()) << "\n";
        }
        return out.str();
    }

    string exportRidesCsv() {
        lock_guard<mutex> guard(backendMutex);
        ostringstream out;
        out << "ride_id,rider,driver,vehicle,source,destination,status,distance_km,time_minutes,fare,cancellation_fee,cancel_reason,rating\n";
        for (Ride* ride : rides) {
            out << csvCell(ride->getId()) << ","
                << csvCell(ride->getRider()->getName()) << ","
                << csvCell(ride->getDriver() ? ride->getDriver()->getName() : "") << ","
                << csvCell(ride->getVehicle()->getTypeName()) << ","
                << csvCell(ride->getSource()->getName()) << ","
                << csvCell(ride->getDestination()->getName()) << ","
                << csvCell(rideStatusName(ride->getStatus())) << ","
                << fixed << setprecision(2) << ride->getDistanceKm() << ","
                << ride->getTimeMinutes() << ","
                << fixed << setprecision(2) << ride->getFare() << ","
                << fixed << setprecision(2) << (cancellationFeeByRideId.count(ride->getId()) ? cancellationFeeByRideId[ride->getId()] : 0.0) << ","
                << csvCell(historyStore.cancelReasonForRide(ride->getId())) << ","
                << historyStore.ratingForRide(ride->getId()) << "\n";
        }
        return out.str();
    }

    void logAdminAction(const string& actorEmail, const string& action, const string& target, const string& details) {
        historyStore.logAdminAction(actorEmail, action, target, details);
    }
};

static string renderLoginPage() {
    string html = R"HTML(
<!doctype html>
<html>
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Uber Ride Platform - Login</title>
  <style>
    :root { color-scheme: light; --ink:#15202b; --muted:#64748b; --line:#cbd5e1; --surface:#ffffff; --page:#f3f6fa; --accent:#111827; --blue:#1d4ed8; }
    * { box-sizing: border-box; }
    body { margin: 0; font-family: Arial, sans-serif; background: var(--page); color: var(--ink); }
    header { background: #101820; color: white; padding: 18px 24px; }
    header h1 { margin: 0; font-size: 24px; font-weight: 700; }
    main { max-width: 980px; margin: 0 auto; padding: 24px; }
    .panel { background: var(--surface); border: 1px solid var(--line); border-radius: 8px; padding: 18px; margin-bottom: 16px; }
    .grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(280px, 1fr)); gap: 16px; }
    label { display: block; margin: 12px 0 5px; font-size: 14px; font-weight: 700; }
    input, select, button { width: 100%; min-height: 40px; border: 1px solid var(--line); border-radius: 6px; padding: 8px 10px; font-size: 14px; }
    button { background: var(--accent); border-color: var(--accent); color: white; cursor: pointer; font-weight: 700; }
    button.secondary { background: #334155; border-color: #334155; }
    .status { color: var(--muted); font-size: 14px; margin-top: 8px; }
    .links { display: flex; flex-wrap: wrap; gap: 10px; margin-top: 12px; }
    .links a { color: var(--blue); font-weight: 700; text-decoration: none; }
    .hidden { display: none; }
    pre { margin: 0; min-height: 80px; overflow: auto; background: #172033; color: #e6edf7; border-radius: 6px; padding: 12px; }
  </style>
</head>
<body>
  <header>
    <h1>Uber Ride Platform</h1>
    <div class="status __DEMO_CLASS__">Demo users: rider@demo.local, driver@demo.local, admin@demo.local / demo123</div>
  </header>
  <main>
    <section class="panel">
      <strong id="sessionStatus">Checking session...</strong>
      <div class="links">
        <a href="/rider">Rider page</a>
        <a href="/driver">Driver page</a>
        <a href="/admin">Admin page</a>
      </div>
    </section>

    <div class="grid">
      <section class="panel">
        <h2>Login</h2>
        <label for="loginEmail">Email</label>
        <input id="loginEmail" value="rider@demo.local">
        <label for="loginPassword">Password</label>
        <input id="loginPassword" type="password" value="demo123">
        <button onclick="login()">Login</button>
        <button class="secondary" onclick="logout()">Logout</button>
      </section>

      <section class="panel">
        <h2>Sign Up</h2>
        <label for="signupName">Name</label>
        <input id="signupName" value="New User">
        <label for="signupEmail">Email</label>
        <input id="signupEmail" value="user@example.com">
        <label for="signupPassword">Password</label>
        <input id="signupPassword" type="password" value="password123">
        <label for="signupRole">Role</label>
        <select id="signupRole">
          <option value="rider">rider</option>
          <option value="driver">driver</option>
          <option value="admin">admin</option>
        </select>
        <button onclick="signup()">Create Account</button>
      </section>
    </div>

    <section class="panel">
      <h2>Output</h2>
      <pre id="output">Ready.</pre>
    </section>
  </main>

  <script>
    const output = (value) => {
      document.getElementById('output').textContent = typeof value === 'string' ? value : JSON.stringify(value, null, 2);
    };

    async function requestJson(path, options = {}) {
      const response = await fetch(path, { credentials: 'include', ...options });
      const data = await response.json();
      output(data);
      return data;
    }

    async function refreshSession() {
      const response = await fetch('/api/auth/me', { credentials: 'include' });
      const data = await response.json();
      const status = document.getElementById('sessionStatus');
      if (data.ok && data.user) {
        status.textContent = `Signed in as ${data.user.name} (${data.user.role})`;
      } else {
        status.textContent = 'Not signed in.';
      }
    }

    async function signup() {
      await requestJson('/api/auth/register', {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify({
          name: document.getElementById('signupName').value,
          email: document.getElementById('signupEmail').value,
          password: document.getElementById('signupPassword').value,
          role: document.getElementById('signupRole').value
        })
      });
    }

    async function login() {
      const data = await requestJson('/api/auth/login', {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify({
          email: document.getElementById('loginEmail').value,
          password: document.getElementById('loginPassword').value
        })
      });
      if (data.ok && data.user) {
        window.location.href = '/' + data.user.role;
      } else {
        await refreshSession();
      }
    }

    async function logout() {
      await requestJson('/api/auth/logout', { method: 'POST' });
      await refreshSession();
    }

    refreshSession();
  </script>
</body>
</html>
)HTML";
    replaceAll(html, "__DEMO_CLASS__", envFlag("DEMO_MODE", true) ? "" : "hidden");
    return html;
}

static string renderRolePage(const string& role, const UserAccount& account) {
    string html = R"HTML(
<!doctype html>
<html>
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Uber Ride Platform - __TITLE__</title>
  <style>
    :root { color-scheme: light; --ink:#17212f; --muted:#65758b; --line:#cbd5e1; --surface:#ffffff; --page:#f3f6fa; --accent:#111827; --blue:#1d4ed8; --red:#b91c1c; --green:#047857; }
    * { box-sizing: border-box; }
    body { margin: 0; font-family: Arial, sans-serif; background: var(--page); color: var(--ink); }
    header { background: #101820; color: white; padding: 16px 24px; display: flex; align-items: center; justify-content: space-between; gap: 14px; flex-wrap: wrap; }
    h1 { margin: 0; font-size: 22px; }
    h2 { margin: 0 0 12px; font-size: 18px; }
    main { max-width: 1180px; margin: 0 auto; padding: 20px; }
    nav { display: flex; gap: 10px; flex-wrap: wrap; align-items: center; }
    nav a, nav button { width: auto; min-height: 34px; color: white; border: 1px solid #506071; border-radius: 6px; background: #1f2937; padding: 7px 10px; text-decoration: none; font-weight: 700; }
    .panel { background: var(--surface); border: 1px solid var(--line); border-radius: 8px; padding: 16px; margin-bottom: 16px; }
    .grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(280px, 1fr)); gap: 16px; }
    .wide { grid-column: 1 / -1; }
    label { display: block; margin: 10px 0 5px; font-size: 13px; font-weight: 700; }
    select, input, button { min-height: 38px; border: 1px solid var(--line); border-radius: 6px; padding: 8px 10px; font-size: 14px; }
    select, input { width: 100%; background: white; }
    button { background: var(--accent); color: white; border-color: var(--accent); cursor: pointer; font-weight: 700; }
    button.secondary { background: #334155; border-color: #334155; }
    button.danger { background: var(--red); border-color: var(--red); }
    button.good { background: var(--green); border-color: var(--green); }
    button.inline { width: auto; min-height: 32px; padding: 6px 9px; margin: 2px; }
    table { width: 100%; border-collapse: collapse; }
    th, td { border-bottom: 1px solid var(--line); padding: 9px 8px; text-align: left; vertical-align: top; font-size: 14px; }
    th { background: #eef2f7; font-size: 12px; text-transform: uppercase; color: #415166; }
    .muted { color: var(--muted); font-size: 13px; }
    .notice { min-height: 24px; margin: 0 0 14px; font-weight: 700; color: var(--blue); }
    .pill { display: inline-block; padding: 3px 7px; border-radius: 999px; background: #e8edf5; font-size: 12px; font-weight: 700; }
    .hidden { display: none; }
    .row-actions { min-width: 170px; }
    body.dark { --ink:#e5edf7; --muted:#9fb0c7; --line:#334155; --surface:#101827; --page:#070b12; --accent:#2563eb; }
    body.dark th { background:#172033; color:#cbd5e1; }
    body.dark select, body.dark input { background:#0f172a; color:#e5edf7; border-color:#334155; }
    body.dark .pill { background:#253248; color:#e5edf7; }
    @media (max-width: 760px) {
      main { padding: 14px; }
      header { align-items: flex-start; }
      table { display: block; overflow-x: auto; white-space: nowrap; }
    }
  </style>
</head>
<body>
  <header>
    <div>
      <h1>__TITLE__ Page</h1>
      <div class="muted">Signed in as __NAME__ (__ROLE__)</div>
    </div>
    <nav>
      <a href="/rider">Rider</a>
      <a href="/driver">Driver</a>
      <a href="/admin">Admin</a>
      <button onclick="toggleDark()">Dark</button>
      <button onclick="logout()">Logout</button>
    </nav>
  </header>
  <main>
    <div id="notice" class="notice"></div>

    <section id="riderView" class="hidden">
      <div class="grid">
        <section class="panel">
          <h2>Book Ride</h2>
          <label for="sourceId">Pickup</label>
          <select id="sourceId"></select>
          <label for="destinationId">Dropoff</label>
          <select id="destinationId"></select>
          <label for="vehicleType">Vehicle</label>
          <select id="vehicleType"></select>
          <button class="secondary" onclick="previewFare()">Preview Fare</button>
          <button onclick="bookRide()">Request Ride</button>
          <div id="farePreview" class="muted"></div>
        </section>

        <section class="panel">
          <h2>Add Stop</h2>
          <label for="stopRideId">Ride</label>
          <select id="stopRideId"></select>
          <label for="stopLocationId">Stop</label>
          <select id="stopLocationId"></select>
          <button onclick="addStop()">Add Stop</button>
        </section>

        <section class="panel">
          <h2>Saved Locations</h2>
          <label for="savedLabel">Label</label>
          <input id="savedLabel" value="Home">
          <label for="savedLocationId">Location</label>
          <select id="savedLocationId"></select>
          <button onclick="saveLocation()">Save Location</button>
          <div id="savedLocations" class="muted"></div>
        </section>

        <section class="panel wide">
          <h2>My Rides</h2>
          <div class="grid">
            <div>
              <label for="riderFilter">Filter</label>
              <select id="riderFilter" onchange="renderRider()">
                <option value="all">All</option>
                <option value="active">Active</option>
                <option value="COMPLETED">Completed</option>
                <option value="CANCELLED">Cancelled</option>
              </select>
            </div>
            <div>
              <label for="riderSearch">Search</label>
              <input id="riderSearch" oninput="renderRider()" placeholder="Ride ID, status, route">
            </div>
          </div>
          <div id="riderRides"></div>
        </section>
      </div>
    </section>

    <section id="driverView" class="hidden">
      <div class="grid">
        <section class="panel">
          <h2>Driver Status</h2>
          <div id="driverSummary" class="muted"></div>
          <label for="driverStatus">Availability</label>
          <select id="driverStatus">
            <option value="AVAILABLE">AVAILABLE</option>
            <option value="OFFLINE">OFFLINE</option>
          </select>
          <button onclick="setDriverStatus()">Update Status</button>
        </section>

        <section class="panel">
          <h2>Driver Profile</h2>
          <label for="driverProfileName">Name</label>
          <input id="driverProfileName">
          <label for="driverVehicleType">Vehicle</label>
          <select id="driverVehicleType"></select>
          <label for="driverLocationId">Current Location</label>
          <select id="driverLocationId"></select>
          <button onclick="saveDriverProfile()">Save Profile</button>
        </section>

        <section class="panel">
          <h2>Today</h2>
          <div id="driverStats"></div>
        </section>

        <section class="panel wide">
          <h2>Ride Requests</h2>
          <div id="driverRequests"></div>
        </section>

        <section class="panel wide">
          <h2>Assigned Rides</h2>
          <div id="driverRides"></div>
        </section>
      </div>
    </section>

    <section id="adminView" class="hidden">
      <div class="grid">
        <section class="panel">
          <h2>Summary</h2>
          <div id="adminSummary"></div>
          <button class="secondary" onclick="resetDemo()">Reset Simulation</button>
          <div style="margin-top:10px;">
            <a href="/api/admin/export/users">Export Users CSV</a> |
            <a href="/api/admin/export/drivers">Export Drivers CSV</a> |
            <a href="/api/admin/export/rides">Export Rides CSV</a>
          </div>
        </section>

        <section class="panel">
          <h2>Create User</h2>
          <label for="adminUserName">Name</label>
          <input id="adminUserName" value="New User">
          <label for="adminUserEmail">Email</label>
          <input id="adminUserEmail" value="new@example.com">
          <label for="adminUserPassword">Password</label>
          <input id="adminUserPassword" type="password" value="password123">
          <label for="adminUserRole">Role</label>
          <select id="adminUserRole">
            <option value="rider">rider</option>
            <option value="driver">driver</option>
            <option value="admin">admin</option>
          </select>
          <button onclick="adminCreateUser()">Create User</button>
        </section>

        <section class="panel wide">
          <h2>Users</h2>
          <div id="adminUsers"></div>
        </section>

        <section class="panel wide">
          <h2>Drivers</h2>
          <div id="adminDrivers"></div>
        </section>

        <section class="panel wide">
          <h2>Rides</h2>
          <label for="adminRideSearch">Search Rides</label>
          <input id="adminRideSearch" oninput="renderAdmin()" placeholder="Ride ID, rider, driver, status">
          <div id="adminRides"></div>
        </section>

        <section class="panel wide">
          <h2>Audit Logs</h2>
          <div id="adminAudit"></div>
        </section>
      </div>
    </section>
  </main>

  <script>
    const pageRole = '__ROLE__';
    let state = {};

    const escapeHtml = (value) => String(value ?? '').replace(/[&<>"']/g, char => ({
      '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;'
    }[char]));

    const money = (value) => `Rs ${Number(value || 0).toFixed(2)}`;
    const km = (value) => `${Number(value || 0).toFixed(2)} km`;
    const notice = (text, isError = false) => {
      const node = document.getElementById('notice');
      node.textContent = text || '';
      node.style.color = isError ? '#b91c1c' : '#1d4ed8';
    };
    const isActiveRide = (ride) => !['COMPLETED', 'CANCELLED'].includes(ride.status);

    function toggleDark() {
      document.body.classList.toggle('dark');
      localStorage.setItem('uberDarkMode', document.body.classList.contains('dark') ? '1' : '0');
    }

    if (localStorage.getItem('uberDarkMode') === '1') {
      document.body.classList.add('dark');
    }

    async function api(path, options = {}) {
      const response = await fetch(path, {
        credentials: 'include',
        headers: { 'Content-Type': 'application/json', ...(options.headers || {}) },
        ...options
      });
      const data = await response.json();
      if (!data.ok) {
        notice(data.error || 'Request failed.', true);
        return data;
      }
      if (data.message) {
        notice(data.message);
      }
      return data;
    }

    function optionHtml(items, valueKey = 'id', labelKey = 'name') {
      return (items || []).map(item => `<option value="${escapeHtml(item[valueKey])}">${escapeHtml(item[labelKey])}</option>`).join('');
    }

    function vehicleOptionHtml(items) {
      return (items || []).map(item => `<option value="${escapeHtml(item)}">${escapeHtml(item)}</option>`).join('');
    }

    function rideRoute(ride) {
      const stops = (ride.stops || []).map(stop => ` -> ${escapeHtml(stop.name)}`).join('');
      return `${escapeHtml(ride.sourceName)}${stops} -> ${escapeHtml(ride.destinationName)}`;
    }

    function rideTable(rides, actions) {
      if (!rides || rides.length === 0) {
        return '<div class="muted">No rides to show.</div>';
      }
      return `<table>
        <thead><tr><th>Ride</th><th>Route</th><th>Driver</th><th>Status</th><th>Fare</th><th class="row-actions">Actions</th></tr></thead>
        <tbody>${rides.map(ride => `<tr>
          <td>${escapeHtml(ride.id)}<br><span class="muted">${escapeHtml(ride.vehicleType)} / ${km(ride.distanceKm)} / ${escapeHtml(ride.timeMinutes)} min</span></td>
          <td>${rideRoute(ride)}</td>
          <td>${ride.driver ? `${escapeHtml(ride.driver.name)}<br><span class="muted">${escapeHtml(ride.driver.vehicleType)} / ${Number(ride.driver.rating || 0).toFixed(1)} stars</span>` : '<span class="muted">Unassigned</span>'}</td>
          <td><span class="pill">${escapeHtml(ride.status)}</span>${ride.cancellationFee > 0 ? `<br><span class="muted">Cancel fee ${money(ride.cancellationFee)}</span>` : ''}${ride.cancelReason ? `<br><span class="muted">${escapeHtml(ride.cancelReason)}</span>` : ''}${ride.rating ? `<br><span class="muted">Rated ${escapeHtml(ride.rating)}/5</span>` : ''}</td>
          <td>${money(ride.fare)}</td>
          <td>${actions(ride)}</td>
        </tr>`).join('')}</tbody>
      </table>`;
    }

    function renderRider() {
      document.getElementById('riderView').classList.remove('hidden');
      document.getElementById('sourceId').innerHTML = optionHtml(state.locations);
      document.getElementById('destinationId').innerHTML = optionHtml(state.locations);
      document.getElementById('stopLocationId').innerHTML = optionHtml(state.locations);
      document.getElementById('savedLocationId').innerHTML = optionHtml(state.locations);
      document.getElementById('vehicleType').innerHTML = vehicleOptionHtml(state.vehicleTypes);

      const stopCandidates = (state.rides || []).filter(ride => !['COMPLETED', 'CANCELLED'].includes(ride.status));
      document.getElementById('stopRideId').innerHTML = stopCandidates.length ? optionHtml(stopCandidates, 'id', 'id') : '<option value="">No active rides</option>';
      document.getElementById('savedLocations').innerHTML = (state.savedLocations || []).length
        ? (state.savedLocations || []).map(location => `<span class="pill">${escapeHtml(location.label)}: ${escapeHtml(location.locationName)}</span>`).join(' ')
        : 'No saved locations yet.';

      const filter = document.getElementById('riderFilter').value;
      const query = document.getElementById('riderSearch').value.trim().toLowerCase();
      const rides = (state.rides || []).filter(ride => {
        const filterOk = filter === 'all' || (filter === 'active' ? isActiveRide(ride) : ride.status === filter);
        const haystack = `${ride.id} ${ride.status} ${ride.sourceName} ${ride.destinationName} ${ride.driver ? ride.driver.name : ''}`.toLowerCase();
        return filterOk && (!query || haystack.includes(query));
      });

      document.getElementById('riderRides').innerHTML = rideTable(rides, ride => {
        const parts = [];
        if (ride.status === 'REQUESTED') {
          const matches = ride.matches || [];
          parts.push(`<div class="muted">${matches.length} matched driver${matches.length === 1 ? '' : 's'}</div>`);
          parts.push(`<button class="inline good" onclick="assignNearest('${escapeHtml(ride.id)}')">Assign nearest</button>`);
          parts.push(`<button class="inline danger" onclick="cancelRide('${escapeHtml(ride.id)}')">Cancel</button>`);
        } else if (['DRIVER_ASSIGNED', 'IN_PROGRESS'].includes(ride.status)) {
          parts.push(`<button class="inline danger" onclick="cancelRide('${escapeHtml(ride.id)}')">Cancel</button>`);
        }
        if (ride.status === 'COMPLETED') {
          parts.push(`<a class="pill" href="/receipt/${encodeURIComponent(ride.id)}" target="_blank">Receipt</a>`);
          if (!ride.rating) {
            parts.push(`<button class="inline good" onclick="rateRide('${escapeHtml(ride.id)}')">Rate</button>`);
          }
        }
        parts.push(`<button class="inline secondary" onclick="rebookRide('${escapeHtml(ride.id)}')">Rebook</button>`);
        return parts.join('');
      });
    }

    function renderDriver() {
      document.getElementById('driverView').classList.remove('hidden');
      const driver = state.driver || {};
      const profile = state.profile || {};
      document.getElementById('driverSummary').innerHTML = `${escapeHtml(driver.name)} / ${escapeHtml(driver.vehicleType)} / ${escapeHtml(driver.locationName)}<br>Status: <strong>${escapeHtml(driver.status)}</strong><br>Rating: <strong>${Number(driver.rating || profile.rating || 0).toFixed(1)}</strong>`;
      document.getElementById('driverStatus').value = driver.status === 'OFFLINE' ? 'OFFLINE' : 'AVAILABLE';
      document.getElementById('driverProfileName').value = profile.name || driver.name || '';
      document.getElementById('driverVehicleType').innerHTML = vehicleOptionHtml(state.vehicleTypes);
      document.getElementById('driverVehicleType').value = profile.vehicleType || driver.vehicleType || 'SEDAN';
      document.getElementById('driverLocationId').innerHTML = optionHtml(state.locations);
      document.getElementById('driverLocationId').value = profile.locationId || '';
      const todayCompleted = (state.rides || []).filter(ride => ride.status === 'COMPLETED');
      const todayEarnings = todayCompleted.reduce((sum, ride) => sum + Number(ride.fare || 0), 0);
      const currentRide = (state.rides || []).find(ride => ['DRIVER_ASSIGNED', 'IN_PROGRESS'].includes(ride.status));
      document.getElementById('driverStats').innerHTML = `
        <div>Today's rides: <strong>${todayCompleted.length}</strong></div>
        <div>Earnings: <strong>${money(todayEarnings)}</strong></div>
        <div>Current ride: <strong>${currentRide ? escapeHtml(currentRide.id) : 'None'}</strong></div>`;
      document.getElementById('driverRequests').innerHTML = rideTable(state.requests, ride => `
        <button class="inline good" onclick="acceptRide('${escapeHtml(ride.id)}')">Accept</button>
        <button class="inline danger" onclick="rejectRide('${escapeHtml(ride.id)}')">Reject</button>`);
      document.getElementById('driverRides').innerHTML = rideTable(state.rides, ride => {
        if (ride.status === 'DRIVER_ASSIGNED') {
          return `<button class="inline good" onclick="startRide('${escapeHtml(ride.id)}')">Start</button>`;
        }
        if (ride.status === 'IN_PROGRESS') {
          return `<button class="inline good" onclick="completeRide('${escapeHtml(ride.id)}')">Complete</button>`;
        }
        if (ride.status === 'COMPLETED') {
          return `<a class="pill" href="/receipt/${encodeURIComponent(ride.id)}" target="_blank">Receipt</a>`;
        }
        return '';
      });
    }

    function renderAdmin() {
      document.getElementById('adminView').classList.remove('hidden');
      const summary = state.summary || {};
      document.getElementById('adminSummary').innerHTML = `
        <div>Users: <strong>${escapeHtml(summary.users || 0)}</strong></div>
        <div>Drivers: <strong>${escapeHtml(summary.drivers || 0)}</strong></div>
        <div>Rides: <strong>${escapeHtml(summary.rides || 0)}</strong></div>
        <div>Active: <strong>${escapeHtml(summary.activeRides || 0)}</strong></div>
        <div>Completed: <strong>${escapeHtml(summary.completedRides || 0)}</strong></div>
        <div>Cancelled: <strong>${escapeHtml(summary.cancelledRides || 0)}</strong></div>`;

      document.getElementById('adminUsers').innerHTML = tableOrEmpty(state.users, ['Name', 'Email', 'Role', 'Status', 'Actions'], user => [
        escapeHtml(user.name),
        escapeHtml(user.email),
        `<span class="pill">${escapeHtml(user.role)}</span>`,
        `<span class="pill">${user.active ? 'ACTIVE' : 'INACTIVE'}</span>`,
        `<button class="inline ${user.active ? 'danger' : 'good'}" onclick="adminSetUserActive('${encodeURIComponent(user.email)}', ${user.active ? 'false' : 'true'})">${user.active ? 'Deactivate' : 'Activate'}</button>`
      ]);

      document.getElementById('adminDrivers').innerHTML = tableOrEmpty(state.drivers, ['Driver', 'Vehicle', 'Location', 'Status', 'Actions'], driver => [
        `${escapeHtml(driver.name)}<br><span class="muted">${escapeHtml(driver.id)}</span>`,
        `${escapeHtml(driver.vehicleType)}<br><span class="muted">${Number(driver.rating || 0).toFixed(1)} stars</span>`,
        escapeHtml(driver.locationName),
        `<span class="pill">${escapeHtml(driver.status)}</span>`,
        `<button class="inline good" onclick="adminSetDriver('${escapeHtml(driver.id)}','AVAILABLE')">Available</button>
         <button class="inline secondary" onclick="adminSetDriver('${escapeHtml(driver.id)}','OFFLINE')">Offline</button>`
      ]);

      const rideQuery = document.getElementById('adminRideSearch').value.trim().toLowerCase();
      const rides = (state.rides || []).filter(ride => {
        const haystack = `${ride.id} ${ride.status} ${ride.sourceName} ${ride.destinationName} ${ride.riderName || ''} ${ride.driver ? ride.driver.name : ''}`.toLowerCase();
        return !rideQuery || haystack.includes(rideQuery);
      });
      document.getElementById('adminRides').innerHTML = rideTable(rides, ride => {
        const parts = [];
        if (!['COMPLETED', 'CANCELLED'].includes(ride.status)) {
          parts.push(`<button class="inline danger" onclick="adminCancelRide('${escapeHtml(ride.id)}')">Cancel</button>`);
        }
        parts.push(`<a class="pill" href="/receipt/${encodeURIComponent(ride.id)}" target="_blank">Receipt</a>`);
        return parts.join('');
      });
      document.getElementById('adminAudit').innerHTML = tableOrEmpty(state.auditLogs, ['Time', 'Actor', 'Action', 'Target', 'Details'], log => [
        escapeHtml(log.createdAt), escapeHtml(log.actorEmail), escapeHtml(log.action), escapeHtml(log.target), escapeHtml(log.details)
      ]);
    }

    function tableOrEmpty(items, headers, rowFactory) {
      if (!items || items.length === 0) {
        return '<div class="muted">No records to show.</div>';
      }
      return `<table><thead><tr>${headers.map(header => `<th>${escapeHtml(header)}</th>`).join('')}</tr></thead>
        <tbody>${items.map(item => `<tr>${rowFactory(item).map(cell => `<td>${cell}</td>`).join('')}</tr>`).join('')}</tbody></table>`;
    }

    async function loadState() {
      state = await api(`/api/${pageRole}/state`);
      if (!state.ok) {
        return;
      }
      if (pageRole === 'rider') renderRider();
      if (pageRole === 'driver') renderDriver();
      if (pageRole === 'admin') renderAdmin();
    }

    async function bookRide() {
      notice('Requesting ride...');
      await api('/api/rider/rides', {
        method: 'POST',
        body: JSON.stringify({
          sourceId: document.getElementById('sourceId').value,
          destinationId: document.getElementById('destinationId').value,
          vehicleType: document.getElementById('vehicleType').value
        })
      });
      await loadState();
    }

    async function previewFare() {
      const data = await api('/api/rider/estimate', {
        method: 'POST',
        body: JSON.stringify({
          sourceId: document.getElementById('sourceId').value,
          destinationId: document.getElementById('destinationId').value,
          vehicleType: document.getElementById('vehicleType').value
        })
      });
      if (data.ok && data.estimate) {
        document.getElementById('farePreview').textContent = `${money(data.estimate.fare)} / ${km(data.estimate.distanceKm)} / ${data.estimate.timeMinutes} min`;
      }
    }

    async function assignNearest(rideId) {
      await api(`/api/rider/rides/${rideId}/assign`, { method: 'POST', body: '{}' });
      await loadState();
    }

    async function addStop() {
      const rideId = document.getElementById('stopRideId').value;
      if (!rideId) return notice('No active ride selected.', true);
      await api(`/api/rider/rides/${rideId}/stops`, {
        method: 'POST',
        body: JSON.stringify({ stopId: document.getElementById('stopLocationId').value })
      });
      await loadState();
    }

    async function cancelRide(rideId) {
      const reason = prompt('Cancellation reason (optional):') || '';
      await api(`/api/rider/rides/${rideId}/cancel`, { method: 'POST', body: JSON.stringify({ reason }) });
      await loadState();
    }

    async function rebookRide(rideId) {
      await api(`/api/rider/rides/${rideId}/rebook`, { method: 'POST', body: '{}' });
      await loadState();
    }

    async function saveLocation() {
      await api('/api/rider/saved-locations', {
        method: 'POST',
        body: JSON.stringify({
          label: document.getElementById('savedLabel').value,
          locationId: document.getElementById('savedLocationId').value
        })
      });
      await loadState();
    }

    async function rateRide(rideId) {
      const rating = Number(prompt('Rate this driver from 1 to 5:', '5') || 0);
      const comment = rating ? (prompt('Optional rating comment:', '') || '') : '';
      await api(`/api/rider/rides/${rideId}/rating`, { method: 'POST', body: JSON.stringify({ rating, comment }) });
      await loadState();
    }

    async function setDriverStatus() {
      await api('/api/driver/status', {
        method: 'POST',
        body: JSON.stringify({ status: document.getElementById('driverStatus').value })
      });
      await loadState();
    }

    async function acceptRide(rideId) {
      await api(`/api/driver/rides/${rideId}/accept`, { method: 'POST', body: '{}' });
      await loadState();
    }

    async function rejectRide(rideId) {
      const reason = prompt('Reject reason (optional):') || '';
      await api(`/api/driver/rides/${rideId}/reject`, { method: 'POST', body: JSON.stringify({ reason }) });
      await loadState();
    }

    async function saveDriverProfile() {
      await api('/api/driver/profile', {
        method: 'POST',
        body: JSON.stringify({
          name: document.getElementById('driverProfileName').value,
          vehicleType: document.getElementById('driverVehicleType').value,
          locationId: document.getElementById('driverLocationId').value
        })
      });
      await loadState();
    }

    async function startRide(rideId) {
      await api(`/api/driver/rides/${rideId}/start`, { method: 'POST', body: '{}' });
      await loadState();
    }

    async function completeRide(rideId) {
      await api(`/api/driver/rides/${rideId}/complete`, { method: 'POST', body: '{}' });
      await loadState();
    }

    async function resetDemo() {
      if (!confirm('Reset the simulation and clear persisted ride/demo feature data?')) return;
      await api('/api/admin/demo/reset', { method: 'POST', body: '{}' });
      await loadState();
    }

    async function adminCreateUser() {
      await api('/api/admin/users', {
        method: 'POST',
        body: JSON.stringify({
          name: document.getElementById('adminUserName').value,
          email: document.getElementById('adminUserEmail').value,
          password: document.getElementById('adminUserPassword').value,
          role: document.getElementById('adminUserRole').value
        })
      });
      await loadState();
    }

    async function adminSetUserActive(email, active) {
      if (!confirm(`${active ? 'Activate' : 'Deactivate'} ${decodeURIComponent(email)}?`)) return;
      await api(`/api/admin/users/${email}/active`, { method: 'POST', body: JSON.stringify({ active }) });
      await loadState();
    }

    async function adminSetDriver(driverId, status) {
      await api(`/api/admin/drivers/${driverId}/status`, { method: 'POST', body: JSON.stringify({ status }) });
      await loadState();
    }

    async function adminCancelRide(rideId) {
      await api(`/api/admin/rides/${rideId}/cancel`, { method: 'POST', body: '{}' });
      await loadState();
    }

    async function logout() {
      await fetch('/api/auth/logout', { method: 'POST', credentials: 'include' });
      window.location.href = '/';
    }

    loadState();
  </script>
</body>
</html>
)HTML";

    replaceAll(html, "__TITLE__", pageTitleForRole(role));
    replaceAll(html, "__ROLE__", role);
    replaceAll(html, "__NAME__", account.name);
    return html;
}

static crow::response renderProtectedRolePage(AuthStore& authStore, const crow::request& req, const string& role) {
    UserAccount account;
    if (!authStore.getSessionUser(req, account)) {
        return redirectTo("/");
    }
    if (!hasRole(account, role)) {
        crow::response response(403);
        response.write("<!doctype html><html><body><h1>Access denied</h1><p>This page is for " + role + " users.</p><p><a href=\"/\">Back to login</a></p></body></html>");
        return response;
    }
    return crow::response(renderRolePage(role, account));
}

void runCrowServer() {
    crow::SimpleApp app;
    string databasePath = configuredDatabasePath();
    AuthStore authStore(databasePath);
    DemoRideBackend demoBackend(databasePath);

    CROW_ROUTE(app, "/")([&] {
        return crow::response(renderLoginPage());
    });

    CROW_ROUTE(app, "/rider")([&](const crow::request& req) {
        return renderProtectedRolePage(authStore, req, "rider");
    });

    CROW_ROUTE(app, "/driver")([&](const crow::request& req) {
        return renderProtectedRolePage(authStore, req, "driver");
    });

    CROW_ROUTE(app, "/admin")([&](const crow::request& req) {
        return renderProtectedRolePage(authStore, req, "admin");
    });

    CROW_ROUTE(app, "/api/health")([&] {
        crow::json::wvalue payload;
        payload["ok"] = true;
        payload["status"] = "healthy";
        payload["mode"] = "web";
        return makeJsonResponse(payload);
    });

    CROW_ROUTE(app, "/api/auth/register").methods("POST"_method)([&](const crow::request& req) {
        crow::json::rvalue body = crow::json::load(req.body);
        string name;
        string email;
        string password;
        string role;

        if (!body || !requireStringField(body, "name", name) || !requireStringField(body, "email", email) || !requireStringField(body, "password", password) || !requireStringField(body, "role", role)) {
            return makeJsonResponse(errorPayload("Missing name, email, password, or role."), 400);
        }
        if (!isSupportedRole(role)) {
            return makeJsonResponse(errorPayload("Role must be rider, driver, or admin."), 400);
        }
        if (role == "admin" && !envFlag("ALLOW_PUBLIC_ADMIN_SIGNUP", false)) {
            return makeJsonResponse(errorPayload("Admin accounts must be created from the admin panel."), 403);
        }

        if (!authStore.registerAccount(name, email, password, role)) {
            return makeJsonResponse(errorPayload("Account already exists."), 409);
        }

        crow::json::wvalue payload;
        payload["ok"] = true;
        payload["message"] = "Account created.";
        payload["role"] = role;
        return makeJsonResponse(payload, 201);
    });

    CROW_ROUTE(app, "/api/auth/login").methods("POST"_method)([&](const crow::request& req) {
        crow::json::rvalue body = crow::json::load(req.body);
        string email;
        string password;

        if (!body || !requireStringField(body, "email", email) || !requireStringField(body, "password", password)) {
            return makeJsonResponse(errorPayload("Missing email or password."), 400);
        }

        string sessionToken;
        UserAccount account;
        if (!authStore.authenticate(email, password, sessionToken, account)) {
            return makeJsonResponse(errorPayload("Invalid credentials."), 401);
        }

        crow::response response(200);
        response.set_header("Content-Type", "application/json");
        response.add_header("Set-Cookie", "ubride_session=" + sessionToken + "; Path=/; HttpOnly; SameSite=Lax");

        crow::json::wvalue payload;
        payload["ok"] = true;
        payload["message"] = "Logged in.";
        payload["redirectPath"] = "/" + account.role;
        payload["user"]["id"] = account.id;
        payload["user"]["name"] = account.name;
        payload["user"]["email"] = account.email;
        payload["user"]["role"] = account.role;
        payload["user"]["active"] = account.active;
        response.write(payload.dump());
        return response;
    });

    CROW_ROUTE(app, "/api/auth/me")([&](const crow::request& req) {
        UserAccount account;
        if (!authStore.getSessionUser(req, account)) {
            return makeJsonResponse(errorPayload("Not authenticated."), 401);
        }

        crow::json::wvalue payload;
        payload["ok"] = true;
        payload["user"]["id"] = account.id;
        payload["user"]["name"] = account.name;
        payload["user"]["email"] = account.email;
        payload["user"]["role"] = account.role;
        payload["user"]["active"] = account.active;
        return makeJsonResponse(payload);
    });

    CROW_ROUTE(app, "/api/auth/profile")([&](const crow::request& req) {
        UserAccount account;
        if (!authStore.getSessionUser(req, account)) {
            return makeJsonResponse(errorPayload("Not authenticated."), 401);
        }

        crow::json::wvalue payload;
        payload["ok"] = true;
        payload["isAdmin"] = hasRole(account, "admin");
        payload["isDriver"] = hasRole(account, "driver");
        payload["isRider"] = hasRole(account, "rider");
        payload["user"]["id"] = account.id;
        payload["user"]["name"] = account.name;
        payload["user"]["email"] = account.email;
        payload["user"]["role"] = account.role;
        payload["user"]["active"] = account.active;
        return makeJsonResponse(payload);
    });

    CROW_ROUTE(app, "/api/auth/logout").methods("POST"_method)([&](const crow::request& req) {
        string cookie = req.get_header_value("Cookie");
        string token = AuthStore::extractSessionToken(cookie);
        bool removed = !token.empty() && authStore.logout(token);

        crow::response response(200);
        response.set_header("Content-Type", "application/json");
        response.add_header("Set-Cookie", "ubride_session=; Path=/; Max-Age=0; SameSite=Lax");

        crow::json::wvalue payload;
        payload["ok"] = true;
        payload["message"] = removed ? "Logged out." : "No active session.";
        response.write(payload.dump());
        return response;
    });

    CROW_ROUTE(app, "/api/rider/state")([&](const crow::request& req) {
        UserAccount account;
        crow::response failure;
        if (!requireRole(authStore, req, "rider", account, failure)) {
            return failure;
        }
        return makeJsonResponse(demoBackend.riderState(account));
    });

    CROW_ROUTE(app, "/api/rider/estimate").methods("POST"_method)([&](const crow::request& req) {
        UserAccount account;
        crow::response failure;
        if (!requireRole(authStore, req, "rider", account, failure)) {
            return failure;
        }

        crow::json::rvalue body = crow::json::load(req.body);
        string sourceId;
        string destinationId;
        string vehicleType;
        if (!body || !requireStringField(body, "sourceId", sourceId) || !requireStringField(body, "destinationId", destinationId) || !requireStringField(body, "vehicleType", vehicleType)) {
            return makeJsonResponse(errorPayload("Missing sourceId, destinationId, or vehicleType."), 400);
        }

        crow::json::wvalue payload;
        string error;
        if (!demoBackend.estimateRideForRider(account, sourceId, destinationId, vehicleType, payload, error)) {
            return makeJsonResponse(errorPayload(error), 400);
        }
        return makeJsonResponse(payload);
    });

    CROW_ROUTE(app, "/api/rider/rides").methods("POST"_method)([&](const crow::request& req) {
        UserAccount account;
        crow::response failure;
        if (!requireRole(authStore, req, "rider", account, failure)) {
            return failure;
        }

        crow::json::rvalue body = crow::json::load(req.body);
        string sourceId;
        string destinationId;
        string vehicleType;
        if (!body || !requireStringField(body, "sourceId", sourceId) || !requireStringField(body, "destinationId", destinationId) || !requireStringField(body, "vehicleType", vehicleType)) {
            return makeJsonResponse(errorPayload("Missing sourceId, destinationId, or vehicleType."), 400);
        }

        crow::json::wvalue payload;
        string error;
        if (!demoBackend.createRideForRider(account, sourceId, destinationId, vehicleType, payload, error)) {
            return makeJsonResponse(errorPayload(error), 400);
        }
        return makeJsonResponse(payload, 201);
    });

    CROW_ROUTE(app, "/api/rider/rides/<string>/assign").methods("POST"_method)([&](const crow::request& req, string rideId) {
        UserAccount account;
        crow::response failure;
        if (!requireRole(authStore, req, "rider", account, failure)) {
            return failure;
        }

        crow::json::wvalue payload;
        string error;
        if (!demoBackend.assignNearestForRider(account, rideId, payload, error)) {
            return makeJsonResponse(errorPayload(error), 400);
        }
        return makeJsonResponse(payload);
    });

    CROW_ROUTE(app, "/api/rider/rides/<string>/rebook").methods("POST"_method)([&](const crow::request& req, string rideId) {
        UserAccount account;
        crow::response failure;
        if (!requireRole(authStore, req, "rider", account, failure)) {
            return failure;
        }

        crow::json::wvalue payload;
        string error;
        if (!demoBackend.rebookRideForRider(account, rideId, payload, error)) {
            return makeJsonResponse(errorPayload(error), 400);
        }
        return makeJsonResponse(payload, 201);
    });

    CROW_ROUTE(app, "/api/rider/rides/<string>/stops").methods("POST"_method)([&](const crow::request& req, string rideId) {
        UserAccount account;
        crow::response failure;
        if (!requireRole(authStore, req, "rider", account, failure)) {
            return failure;
        }

        crow::json::rvalue body = crow::json::load(req.body);
        string stopId;
        if (!body || !requireStringField(body, "stopId", stopId)) {
            return makeJsonResponse(errorPayload("Missing stopId."), 400);
        }

        crow::json::wvalue payload;
        string error;
        if (!demoBackend.addStopForRider(account, rideId, stopId, payload, error)) {
            return makeJsonResponse(errorPayload(error), 400);
        }
        return makeJsonResponse(payload);
    });

    CROW_ROUTE(app, "/api/rider/rides/<string>/cancel").methods("POST"_method)([&](const crow::request& req, string rideId) {
        UserAccount account;
        crow::response failure;
        if (!requireRole(authStore, req, "rider", account, failure)) {
            return failure;
        }

        crow::json::rvalue body = crow::json::load(req.body);
        string reason;
        if (body && body.has("reason")) {
            reason = body["reason"].s();
        }

        crow::json::wvalue payload;
        string error;
        if (!demoBackend.cancelRideForRider(account, rideId, reason, payload, error)) {
            return makeJsonResponse(errorPayload(error), 400);
        }
        return makeJsonResponse(payload);
    });

    CROW_ROUTE(app, "/api/rider/saved-locations").methods("POST"_method)([&](const crow::request& req) {
        UserAccount account;
        crow::response failure;
        if (!requireRole(authStore, req, "rider", account, failure)) {
            return failure;
        }

        crow::json::rvalue body = crow::json::load(req.body);
        string label;
        string locationId;
        if (!body || !requireStringField(body, "label", label) || !requireStringField(body, "locationId", locationId)) {
            return makeJsonResponse(errorPayload("Missing label or locationId."), 400);
        }

        crow::json::wvalue payload;
        string error;
        if (!demoBackend.saveLocationForRider(account, label, locationId, payload, error)) {
            return makeJsonResponse(errorPayload(error), 400);
        }
        return makeJsonResponse(payload);
    });

    CROW_ROUTE(app, "/api/rider/rides/<string>/rating").methods("POST"_method)([&](const crow::request& req, string rideId) {
        UserAccount account;
        crow::response failure;
        if (!requireRole(authStore, req, "rider", account, failure)) {
            return failure;
        }

        crow::json::rvalue body = crow::json::load(req.body);
        int rating = body && body.has("rating") ? static_cast<int>(body["rating"].i()) : 0;
        string comment;
        if (body && body.has("comment")) {
            comment = body["comment"].s();
        }

        crow::json::wvalue payload;
        string error;
        if (!demoBackend.rateRideForRider(account, rideId, rating, comment, payload, error)) {
            return makeJsonResponse(errorPayload(error), 400);
        }
        return makeJsonResponse(payload);
    });

    CROW_ROUTE(app, "/api/driver/state")([&](const crow::request& req) {
        UserAccount account;
        crow::response failure;
        if (!requireRole(authStore, req, "driver", account, failure)) {
            return failure;
        }
        return makeJsonResponse(demoBackend.driverState(account));
    });

    CROW_ROUTE(app, "/api/driver/status").methods("POST"_method)([&](const crow::request& req) {
        UserAccount account;
        crow::response failure;
        if (!requireRole(authStore, req, "driver", account, failure)) {
            return failure;
        }

        crow::json::rvalue body = crow::json::load(req.body);
        string status;
        if (!body || !requireStringField(body, "status", status)) {
            return makeJsonResponse(errorPayload("Missing status."), 400);
        }

        crow::json::wvalue payload;
        string error;
        if (!demoBackend.setDriverStatusForAccount(account, status, payload, error)) {
            return makeJsonResponse(errorPayload(error), 400);
        }
        return makeJsonResponse(payload);
    });

    CROW_ROUTE(app, "/api/driver/profile").methods("POST"_method)([&](const crow::request& req) {
        UserAccount account;
        crow::response failure;
        if (!requireRole(authStore, req, "driver", account, failure)) {
            return failure;
        }

        crow::json::rvalue body = crow::json::load(req.body);
        string name;
        string vehicleType;
        string locationId;
        if (!body || !requireStringField(body, "name", name) || !requireStringField(body, "vehicleType", vehicleType) || !requireStringField(body, "locationId", locationId)) {
            return makeJsonResponse(errorPayload("Missing name, vehicleType, or locationId."), 400);
        }

        crow::json::wvalue payload;
        string error;
        if (!demoBackend.updateDriverProfileForAccount(account, name, vehicleType, locationId, payload, error)) {
            return makeJsonResponse(errorPayload(error), 400);
        }
        return makeJsonResponse(payload);
    });

    CROW_ROUTE(app, "/api/driver/rides/<string>/accept").methods("POST"_method)([&](const crow::request& req, string rideId) {
        UserAccount account;
        crow::response failure;
        if (!requireRole(authStore, req, "driver", account, failure)) {
            return failure;
        }

        crow::json::wvalue payload;
        string error;
        if (!demoBackend.acceptRideForDriver(account, rideId, payload, error)) {
            return makeJsonResponse(errorPayload(error), 400);
        }
        return makeJsonResponse(payload);
    });

    CROW_ROUTE(app, "/api/driver/rides/<string>/reject").methods("POST"_method)([&](const crow::request& req, string rideId) {
        UserAccount account;
        crow::response failure;
        if (!requireRole(authStore, req, "driver", account, failure)) {
            return failure;
        }

        crow::json::rvalue body = crow::json::load(req.body);
        string reason;
        if (body && body.has("reason")) {
            reason = body["reason"].s();
        }

        crow::json::wvalue payload;
        string error;
        if (!demoBackend.rejectRideForDriver(account, rideId, reason, payload, error)) {
            return makeJsonResponse(errorPayload(error), 400);
        }
        return makeJsonResponse(payload);
    });

    CROW_ROUTE(app, "/api/driver/rides/<string>/start").methods("POST"_method)([&](const crow::request& req, string rideId) {
        UserAccount account;
        crow::response failure;
        if (!requireRole(authStore, req, "driver", account, failure)) {
            return failure;
        }

        crow::json::wvalue payload;
        string error;
        if (!demoBackend.startRideForDriver(account, rideId, payload, error)) {
            return makeJsonResponse(errorPayload(error), 400);
        }
        return makeJsonResponse(payload);
    });

    CROW_ROUTE(app, "/api/driver/rides/<string>/complete").methods("POST"_method)([&](const crow::request& req, string rideId) {
        UserAccount account;
        crow::response failure;
        if (!requireRole(authStore, req, "driver", account, failure)) {
            return failure;
        }

        crow::json::wvalue payload;
        string error;
        if (!demoBackend.completeRideForDriver(account, rideId, payload, error)) {
            return makeJsonResponse(errorPayload(error), 400);
        }
        return makeJsonResponse(payload);
    });

    CROW_ROUTE(app, "/api/admin/state")([&](const crow::request& req) {
        UserAccount account;
        crow::response failure;
        if (!requireRole(authStore, req, "admin", account, failure)) {
            return failure;
        }
        return makeJsonResponse(demoBackend.adminState(authStore.listAccounts()));
    });

    CROW_ROUTE(app, "/api/admin/users").methods("POST"_method)([&](const crow::request& req) {
        UserAccount account;
        crow::response failure;
        if (!requireRole(authStore, req, "admin", account, failure)) {
            return failure;
        }

        crow::json::rvalue body = crow::json::load(req.body);
        string name;
        string email;
        string password;
        string role;
        if (!body || !requireStringField(body, "name", name) || !requireStringField(body, "email", email) || !requireStringField(body, "password", password) || !requireStringField(body, "role", role)) {
            return makeJsonResponse(errorPayload("Missing name, email, password, or role."), 400);
        }
        if (!isSupportedRole(role)) {
            return makeJsonResponse(errorPayload("Role must be rider, driver, or admin."), 400);
        }
        if (!authStore.registerAccount(name, email, password, role)) {
            return makeJsonResponse(errorPayload("Account already exists."), 409);
        }
        demoBackend.logAdminAction(account.email, "create_user", email, "role=" + role);
        crow::json::wvalue payload;
        payload["ok"] = true;
        payload["message"] = "User created.";
        return makeJsonResponse(payload, 201);
    });

    CROW_ROUTE(app, "/api/admin/users/<string>/active").methods("POST"_method)([&](const crow::request& req, string email) {
        UserAccount account;
        crow::response failure;
        if (!requireRole(authStore, req, "admin", account, failure)) {
            return failure;
        }

        crow::json::rvalue body = crow::json::load(req.body);
        bool active = body && body.has("active") && body["active"].b();
        if (!active && email == account.email) {
            return makeJsonResponse(errorPayload("You cannot deactivate your current admin session."), 400);
        }
        if (!authStore.setAccountActive(email, active)) {
            return makeJsonResponse(errorPayload("User not found."), 404);
        }
        demoBackend.logAdminAction(account.email, active ? "activate_user" : "deactivate_user", email, "");

        crow::json::wvalue payload;
        payload["ok"] = true;
        payload["message"] = active ? "User activated." : "User deactivated.";
        return makeJsonResponse(payload);
    });

    CROW_ROUTE(app, "/receipt/<string>")([&](const crow::request& req, string rideId) {
        UserAccount account;
        if (!authStore.getSessionUser(req, account)) {
            return redirectTo("/");
        }
        string html;
        string error;
        if (!demoBackend.receiptHtml(account, rideId, html, error)) {
            crow::response response(404);
            response.write("<!doctype html><html><body><h1>" + escapeHtml(error) + "</h1></body></html>");
            return response;
        }
        return crow::response(html);
    });

    CROW_ROUTE(app, "/api/admin/export/<string>")([&](const crow::request& req, string kind) {
        UserAccount account;
        crow::response failure;
        if (!requireRole(authStore, req, "admin", account, failure)) {
            return failure;
        }
        if (kind == "users") {
            return makeTextResponse(demoBackend.exportUsersCsv(authStore.listAccounts()), "text/csv", "users.csv");
        }
        if (kind == "drivers") {
            return makeTextResponse(demoBackend.exportDriversCsv(), "text/csv", "drivers.csv");
        }
        if (kind == "rides") {
            return makeTextResponse(demoBackend.exportRidesCsv(), "text/csv", "rides.csv");
        }
        return makeJsonResponse(errorPayload("Unknown export type."), 404);
    });

    CROW_ROUTE(app, "/api/admin/demo/reset").methods("POST"_method)([&](const crow::request& req) {
        UserAccount account;
        crow::response failure;
        if (!requireRole(authStore, req, "admin", account, failure)) {
            return failure;
        }
        if (!envFlag("DEMO_MODE", true)) {
            return makeJsonResponse(errorPayload("Demo reset is disabled in production mode."), 403);
        }
        demoBackend.reset(true);
        demoBackend.logAdminAction(account.email, "reset_simulation", "demo", "cleared rides and feature data");
        crow::json::wvalue payload;
        payload["ok"] = true;
        payload["message"] = "Simulation reset.";
        return makeJsonResponse(payload);
    });

    CROW_ROUTE(app, "/api/admin/drivers/<string>/status").methods("POST"_method)([&](const crow::request& req, string driverId) {
        UserAccount account;
        crow::response failure;
        if (!requireRole(authStore, req, "admin", account, failure)) {
            return failure;
        }

        crow::json::rvalue body = crow::json::load(req.body);
        string status;
        if (!body || !requireStringField(body, "status", status)) {
            return makeJsonResponse(errorPayload("Missing status."), 400);
        }

        crow::json::wvalue payload;
        string error;
        if (!demoBackend.setDriverStatusAsAdmin(driverId, status, payload, error)) {
            return makeJsonResponse(errorPayload(error), 400);
        }
        demoBackend.logAdminAction(account.email, "set_driver_status", driverId, status);
        return makeJsonResponse(payload);
    });

    CROW_ROUTE(app, "/api/admin/rides/<string>/cancel").methods("POST"_method)([&](const crow::request& req, string rideId) {
        UserAccount account;
        crow::response failure;
        if (!requireRole(authStore, req, "admin", account, failure)) {
            return failure;
        }

        crow::json::wvalue payload;
        string error;
        if (!demoBackend.cancelRideAsAdmin(rideId, payload, error)) {
            return makeJsonResponse(errorPayload(error), 400);
        }
        demoBackend.logAdminAction(account.email, "cancel_ride", rideId, "");
        return makeJsonResponse(payload);
    });

    app.port(configuredPort()).multithreaded().run();
}

}  // namespace uberride
