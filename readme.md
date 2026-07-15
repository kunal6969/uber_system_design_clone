<div align="center">

# 🚖 RideFlow

### Scalable Ride-Hailing Platform built in Modern C++

*Real-time ride matching · Graph-based routing · Geospatial search · Role-based dashboards · Persistent ride history*

---

![C++17](https://img.shields.io/badge/C%2B%2B-17-blue?style=for-the-badge&logo=cplusplus)
![Crow](https://img.shields.io/badge/Crow-HTTP-darkblue?style=for-the-badge)
![SQLite](https://img.shields.io/badge/SQLite-Persistence-lightblue?style=for-the-badge&logo=sqlite)
![Docker](https://img.shields.io/badge/Docker-Ready-2496ED?style=for-the-badge&logo=docker)
![Architecture](https://img.shields.io/badge/Architecture-Layered%20%7C%20SOLID-green?style=for-the-badge)
![License](https://img.shields.io/badge/License-MIT-yellow?style=for-the-badge)

---

> ⭐ **Graph Algorithms** &nbsp;|&nbsp; ⭐ **A\* Path Finding** &nbsp;|&nbsp; ⭐ **Haversine Distance** &nbsp;|&nbsp; ⭐ **K-Nearest Driver Selection**
> ⭐ **Crow HTTP Server** &nbsp;|&nbsp; ⭐ **SQLite Persistence** &nbsp;|&nbsp; ⭐ **SOLID Architecture** &nbsp;|&nbsp; ⭐ **Docker Support**

</div>

---

## 📌 Table of Contents

- [Overview](#-overview)
- [System Architecture](#-system-architecture)
- [Core Features](#-core-features)
- [Tech Stack](#-tech-stack)
- [Folder Structure](#-folder-structure)
- [Algorithms & Data Structures](#-algorithms--data-structures)
- [Ride Lifecycle](#-ride-lifecycle)
- [Authentication Flow](#-authentication-flow)
- [Driver Matching Flow](#-driver-matching-flow)
- [Routing Engine](#-routing-engine)
- [Pricing Engine](#-pricing-engine)
- [Database Schema](#-database-schema)
- [API Reference](#-api-reference)
- [Local Setup](#-local-setup)
- [Docker Deployment](#-docker-deployment)
- [Demo Credentials](#-demo-credentials)
- [Future Improvements](#-future-improvements)
- [Resume Description](#-resume-description)
- [Author](#-author)

---

## 🧭 Overview

RideFlow is a **production-inspired ride-hailing backend** written from scratch in **Modern C++17**. It combines rigorous systems design with real algorithmic depth — not a tutorial clone, but an end-to-end engineering exercise covering:

- **Domain modeling** of riders, drivers, vehicles, and rides
- **Graph-based routing** using A\* Search with Haversine heuristics
- **Geospatial driver matching** with a K-Nearest priority queue
- **Strategy-pattern pricing** supporting Normal, Surge, and Pool modes
- **Cookie-authenticated HTTP API** via the Crow web framework
- **SQLite persistence** for users, ride history, stops, ratings, and audit logs
- **Dual-mode operation**: console simulation and live HTTP server

The simulation engine is intentionally preserved intact — the web layer wraps it with HTTP without rewriting a single line of business logic. This is a strong architectural decision that demonstrates the value of clean separation between domain logic and delivery mechanism.

---

## 🏗️ System Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                        CLIENT LAYER                             │
│     Browser (Rider Page │ Driver Page │ Admin Page)             │
│              Vanilla JS + Fetch API                             │
└──────────────────────────┬──────────────────────────────────────┘
                           │ HTTP + Cookie Sessions
┌──────────────────────────▼──────────────────────────────────────┐
│                      WEB LAYER (Crow)                           │
│   WebServer.cpp — routes, session guards, JSON responses        │
│   AuthStore.cpp — login, register, session management           │
└─────────────┬───────────────────────────┬───────────────────────┘
              │                           │
┌─────────────▼──────────┐  ┌────────────▼──────────────────────┐
│    SERVICE LAYER        │  │      PERSISTENCE LAYER            │
│  RideService            │  │  RideHistoryStore (SQLite)        │
│  DriverMatchingService  │  │  AuthStore (SQLite)               │
│  RouteService           │  │  Tables: ride_history, stops,     │
│  PricingService         │  │  driver_profiles, ratings,        │
│  NotificationService    │  │  saved_locations, audit_logs      │
└─────────────┬──────────┘  └───────────────────────────────────┘
              │
┌─────────────▼──────────────────────────────────────────────────┐
│                       DOMAIN LAYER                              │
│   Ride · Driver · Rider · Vehicle (Bike/Auto/Sedan/SUV)        │
│   Location · Edge · CityGraph · RouteResult · DriverMatch      │
└─────────────┬──────────────────────────────────────────────────┘
              │
┌─────────────▼──────────────────────────────────────────────────┐
│                     ALGORITHM LAYER                             │
│   A* Search (RouteService) · Haversine Formula (CityGraph)     │
│   K-Nearest Heap (DriverMatchingService)                        │
│   Strategy Pattern (PricingService)                             │
└────────────────────────────────────────────────────────────────┘
```

---

## ✨ Core Features

### Rider Dashboard
- Request a ride by selecting source, destination, and vehicle type
- View real-time fare estimate before confirming a booking
- Assign the nearest available driver in one click
- Add stops mid-ride with live fare recalculation
- Cancel rides with automatic cancellation fee logic (₹30 if driver assigned)
- Rebook a previously cancelled ride
- Save favourite locations with custom labels
- Rate completed rides with a comment
- View full ride history with status and fare breakdown

### Driver Dashboard
- View matched ride requests assigned to your vehicle type
- Accept or reject a ride with optional reason
- Start and complete rides through a clean state machine
- Update availability status (Available / Offline)
- Update vehicle type and home location in profile

### Admin Dashboard
- Summary counts: total users, drivers, rides, and revenue
- Full user list with role, status, and active/inactive toggle
- Driver list with current status, vehicle type, and location
- Live ride table with status, rider, driver, fare, and admin cancel
- Export Users, Drivers, and Rides as CSV
- Reset simulation (clears in-memory state and SQLite ride history)
- Admin audit log for all admin-initiated actions

### Simulation Engine
- Dual-mode: `./RideFlow` for console demo, `./RideFlow web` for HTTP server
- MNIT Jaipur campus graph with 50+ real landmarks as demo topology
- Full ride lifecycle exercised in the console demo: create → match → assign → start → stop → complete → receipt
- Ride history is restored from SQLite on web server restart, preserving RIDE-N counters

---

## 🛠️ Tech Stack

| Layer | Technology | Purpose |
|---|---|---|
| Language | C++17 | Core implementation |
| HTTP Framework | Crow (vendored) | REST API and HTML serving |
| Async I/O | Asio (vendored) | Crow's async networking backend |
| Database | SQLite 3 | Auth, ride history, ratings, audit logs |
| Password Hashing | TinySHA1 (Crow vendor) | Salted SHA1 password storage |
| Build System | CMake 3.10+ / g++ | Build toolchain |
| Container | Docker (Ubuntu 24.04) | Reproducible deployment |
| Session Management | In-memory cookie map | `ubride_session` cookie |
| Frontend | Vanilla JS + Fetch API | Embedded in WebServer.cpp |

---

## 📁 Folder Structure

```
RideFlow/
│
├── Main.cpp                          # Entry point: console demo + web mode
├── CMakeLists.txt                    # CMake build configuration
├── Dockerfile                        # Docker container definition
├── .dockerignore
│
├── src/
│   ├── auth/
│   │   └── AuthStore.cpp             # SQLite auth: register, login, sessions
│   │
│   ├── domain/
│   │   ├── Ride.cpp                  # Ride entity + RideStatus enum
│   │   ├── Driver.cpp                # Driver entity + DriverStatus enum
│   │   └── Rider.cpp                 # Rider entity
│   │
│   ├── factory/
│   │   └── VehicleFactory.cpp        # Factory pattern: creates vehicle by type
│   │
│   ├── graph/
│   │   ├── CityGraph.cpp             # Adjacency list graph + Haversine distance
│   │   ├── Location.cpp              # Node with lat/lng and name
│   │   ├── Edge.cpp                  # Directed edge with distance and time
│   │   └── RouteResult.cpp           # Path + total distance + total time
│   │
│   ├── matching/
│   │   └── DriverMatch.cpp           # Driver + ETA pair for matching results
│   │
│   ├── persistence/
│   │   └── RideHistoryStore.cpp      # SQLite persistence for all ride data
│   │
│   ├── pricing/
│   │   ├── PricingStrategy.cpp       # Abstract base: computeBaseFare()
│   │   ├── NormalPricing.cpp         # 1.0× multiplier
│   │   ├── SurgePricing.cpp          # Configurable surge multiplier
│   │   └── PoolPricing.cpp           # Discount factor (e.g. 0.75×)
│   │
│   ├── service/
│   │   ├── RideService.cpp           # Core ride lifecycle orchestration
│   │   ├── DriverMatchingService.cpp # K-nearest driver selection
│   │   ├── RouteService.cpp          # A* pathfinding on CityGraph
│   │   ├── PricingService.cpp        # Delegates to active PricingStrategy
│   │   └── NotificationService.cpp   # Driver notification (console / extensible)
│   │
│   ├── vehicle/
│   │   ├── Vehicle.cpp               # Abstract base: baseFare, perKmRate, perMinuteRate
│   │   ├── Bike.cpp                  # Capacity 1, ₹15 base, ₹6/km
│   │   ├── Auto.cpp                  # Capacity 3, ₹25 base, ₹8/km
│   │   ├── Sedan.cpp                 # Capacity 4, ₹50 base, ₹12/km
│   │   └── SUV.cpp                   # Capacity 6, ₹80 base, ₹16/km
│   │
│   └── web/
│       └── WebServer.cpp             # Crow routes, embedded HTML, DemoRideBackend
│
└── third_party/
    ├── Crow/                         # Vendored Crow HTTP framework
    └── asio/                         # Vendored Asio (Crow dependency)
```

---

## 🔬 Algorithms & Data Structures

### 1. A\* Search — Shortest Route

> **File:** `src/service/RouteService.cpp`

RideFlow does not use Dijkstra's algorithm. It implements **A\*** with a geospatial heuristic, which provably explores fewer nodes by prioritising candidates that are both close to source and geometrically close to destination.

```
f(n) = g(n) + h(n)

where:
  g(n) = actual travel time from source to node n (minutes)
  h(n) = optimistic remaining time via straight-line distance at 80 km/h
```

The open set is a `std::priority_queue` ordered by `f(n)`. Visited nodes are skipped using a `bestTime` map. The algorithm terminates as soon as the destination is dequeued.

**Path reconstruction** walks backwards through the `previous` map and reverses the result.

**Complexity:** O((V + E) log V) — same asymptotic as Dijkstra but with a tighter constant in practice.

---

### 2. Haversine Distance — Geospatial Metric

> **File:** `src/graph/CityGraph.cpp`

Edge weights (distance in km, time in minutes) are computed from GPS coordinates using the **Haversine formula**, which accounts for Earth's curvature:

```
a = sin²(Δlat/2) + cos(lat₁) · cos(lat₂) · sin²(Δlon/2)
c = 2 · atan2(√a, √(1−a))
d = R · c      where R = 6371 km
```

Road distance applies a `1.25×` multiplier over straight-line distance to approximate real road paths. Travel time is derived from distance and a configurable speed (km/h), rounded up to the nearest minute.

The same formula powers the A\* heuristic — no separate implementation needed.

---

### 3. K-Nearest Driver Selection — Priority Queue

> **File:** `src/service/DriverMatchingService.cpp`

Given a pickup location and a desired vehicle type, RideFlow finds the top-K nearest **available** drivers using a **max-heap of size K** — the classic streaming top-K algorithm:

```
For each eligible driver d:
  route = A*(d.location → pickup)
  
  if heap.size < K:
    heap.push(d, eta)
  else if eta < heap.top().eta:
    heap.pop()
    heap.push(d, eta)

Result: sorted(heap) by ascending ETA
```

This runs in **O(D · log K)** time where D = number of available drivers, avoiding a full sort of all drivers.

Drivers are filtered by:
- `status == AVAILABLE` (not ON_TRIP or OFFLINE)
- `vehicle.typeName == requestedType`
- A valid A\* route exists from driver to pickup

---

### 4. Graph Modelling — Adjacency List

> **File:** `src/graph/CityGraph.cpp`

The city map is modelled as a **weighted undirected graph**:

```
Nodes:  Location (id, name, lat, lng)
Edges:  Edge (destination, distanceKm, timeMinutes)
Store:  unordered_map<string, vector<Edge*>> — O(1) neighbor lookup
```

`addRoad(from, to, speed)` automatically adds both directed edges, computing distance and time from GPS coordinates.

---

### 5. Strategy Pattern — Pricing

> **Files:** `src/pricing/`

The `PricingService` delegates to a pluggable `PricingStrategy`:

```
PricingStrategy (abstract)
├── NormalPricing   — baseFare + perKmRate × km + perMinuteRate × min
├── SurgePricing    — NormalPricing × multiplier (e.g. 1.5×)
└── PoolPricing     — NormalPricing × discountFactor (e.g. 0.75×)
```

Switching pricing at runtime requires only `pricingService->setStrategy(new SurgePricing(2.0))`.

**Vehicle pricing parameters:**

| Vehicle | Base Fare | Per km | Per min | Capacity |
|---------|-----------|--------|---------|----------|
| Bike    | ₹15       | ₹6     | ₹1.0    | 1        |
| Auto    | ₹25       | ₹8     | ₹1.5    | 3        |
| Sedan   | ₹50       | ₹12    | ₹2.0    | 4        |
| SUV     | ₹80       | ₹16    | ₹2.5    | 6        |

---

## 🔄 Ride Lifecycle

```
                    ┌─────────────┐
                    │  REQUESTED  │  ← createRide()
                    └──────┬──────┘
                           │ assignDriver()
                    ┌──────▼──────┐
                    │DRIVER_ASSIG-│
                    │    NED      │
                    └──────┬──────┘
                           │ startRide()
                    ┌──────▼──────┐
                ┌───│  IN_PROGRESS│───┐
                │   └──────┬──────┘   │
         addStop│          │          │cancelRide()
            (fare          │completeRide()  [fee=₹30]
           updates)        │          │
                │   ┌──────▼──────┐   │  ┌────────────┐
                └──▶│  COMPLETED  │   └─▶│  CANCELLED │
                    └─────────────┘      └────────────┘
```

**Key lifecycle rules:**
- Stops can only be added to rides that are not COMPLETED or CANCELLED
- Adding a stop triggers full route recomputation and fare update
- Cancellation fee (₹30) applies only if driver was already assigned
- Completing or cancelling a ride frees the driver back to AVAILABLE
- All lifecycle transitions are persisted to SQLite immediately

---

## 🔐 Authentication Flow

```
User submits login form
        │
        ▼
POST /api/auth/login
        │
        ├── Lookup email in SQLite users table
        ├── Compute SHA1(stored_salt + ":" + input_password)
        ├── Compare with stored hash
        │
        ▼
Match? ─────────────── No ──▶ 401 Unauthorized
   │
  Yes
   │
   ▼
Generate random session token
Store in memory: sessionToEmail[token] = email
Set cookie: ubride_session=<token>; HttpOnly; Path=/
        │
        ▼
Redirect to role page (/rider | /driver | /admin)

─────────────────────────────────────────────────────────

Subsequent requests:
        │
        ▼
Read ubride_session cookie
        │
        ├── Lookup email in sessionToEmail map
        ├── Load UserAccount from SQLite
        ├── Verify account.role matches route guard
        │
        ▼
Match? ─────────────── No ──▶ 401 / 403
   │
  Yes
   ▼
Proceed to handler
```

**Security notes (demo-grade):**
- Passwords are stored as `SHA1(salt + ":" + password)` — not bcrypt/Argon2
- Sessions live in memory and reset on server restart
- Cookie is `HttpOnly` — not accessible from JavaScript
- Adequate for a portfolio demo; upgrade auth before production use

**Demo seed accounts** (auto-created on first run):

| Email | Password | Role |
|---|---|---|
| rider@demo.local | demo123 | rider |
| driver@demo.local | demo123 | driver |
| admin@demo.local | demo123 | admin |

---

## 🎯 Driver Matching Flow

```
Rider requests ride (vehicleType, pickup location)
        │
        ▼
RideService.notifyNearbyDrivers(ride, allDrivers, maxK=3)
        │
        ▼
DriverMatchingService.findNearestDrivers()
        │
        ├── For each driver:
        │     ├── Skip if status != AVAILABLE
        │     ├── Skip if vehicle.type != requestedType
        │     ├── Run A*(driver.location → pickup)
        │     ├── Skip if no route found
        │     └── Push (driver, eta) into max-heap of size K
        │
        ▼
Heap contains top-K nearest drivers (by ETA)
        │
        ▼
Extract and reverse → sorted ascending by ETA
        │
        ▼
NotificationService prints candidates (extensible to push/SMS)
        │
        ▼
Rider calls /api/rider/rides/{id}/assign
        │
        ▼
RideService.assignDriver(ride, matches[0].driver)
  ride.status = DRIVER_ASSIGNED
  driver.status = ON_TRIP
        │
        ▼
Snapshot saved to SQLite ride_history
```

---

## 🗺️ Routing Engine

```
findShortestRoute(source, destination)
        │
        ▼
Initialize:
  bestTime[source] = 0
  openSet = PriorityQueue { (source, g=0, f=h(source→dest)) }
        │
        ▼
Loop while openSet not empty:
  current = openSet.pop()          // lowest f(n)
        │
        ├── Stale entry? (g > bestTime[current]) → skip
        ├── current == destination? → break
        │
        ▼
  For each Edge from current:
    newTime = bestTime[current] + edge.timeMinutes
    newDist = bestDist[current] + edge.distanceKm
        │
        ├── newTime < bestTime[neighbor]?
        │     ├── Update bestTime, bestDist, previous
        │     └── Push (neighbor, g=newTime, f=newTime + h(neighbor→dest))
        │
        ▼
RouteResult {
  found: bool,
  totalTimeMinutes: int,
  totalDistanceKm: double,
  path: vector<Location*>      // reconstructed via previous[] map
}
```

**Multi-stop recomputation:** When a stop is added mid-ride, `RideService.recomputeRoute()` runs A\* for each consecutive pair of waypoints `[source → stop₁ → stop₂ → ... → destination]` and sums distances and times. Fare is then recomputed using the updated totals.

---

## 🗄️ Database Schema

All data lives in a single SQLite file (`uber_ride_auth.db` by default, `/data/uber_ride_auth.db` in Docker).

```sql
-- User accounts
CREATE TABLE users (
    id    TEXT PRIMARY KEY,
    name  TEXT NOT NULL,
    email TEXT UNIQUE NOT NULL,
    role  TEXT NOT NULL,           -- 'rider' | 'driver' | 'admin'
    salt  TEXT NOT NULL,
    hash  TEXT NOT NULL,
    active INTEGER NOT NULL DEFAULT 1
);

-- Ride history (upserted on every lifecycle change)
CREATE TABLE ride_history (
    ride_id          TEXT PRIMARY KEY,
    rider_email      TEXT NOT NULL,
    rider_name       TEXT NOT NULL,
    driver_id        TEXT,
    driver_email     TEXT,
    driver_name      TEXT,
    vehicle_type     TEXT NOT NULL,
    source_id        TEXT NOT NULL,
    destination_id   TEXT NOT NULL,
    status           TEXT NOT NULL,
    distance_km      REAL NOT NULL,
    time_minutes     INTEGER NOT NULL,
    fare             REAL NOT NULL,
    cancellation_fee REAL NOT NULL DEFAULT 0,
    created_at       TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,
    updated_at       TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);

-- Ordered stops per ride
CREATE TABLE ride_stops (
    ride_id     TEXT NOT NULL,
    stop_order  INTEGER NOT NULL,
    location_id TEXT NOT NULL,
    PRIMARY KEY (ride_id, stop_order),
    FOREIGN KEY (ride_id) REFERENCES ride_history(ride_id) ON DELETE CASCADE
);

-- Driver profile snapshots (vehicle type, home location, availability)
CREATE TABLE driver_profiles (
    email        TEXT PRIMARY KEY,
    name         TEXT NOT NULL,
    vehicle_type TEXT NOT NULL DEFAULT 'SEDAN',
    location_id  TEXT NOT NULL DEFAULT 'KB',
    availability TEXT NOT NULL DEFAULT 'AVAILABLE',
    updated_at   TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);

-- Rider saved locations (e.g. "Home", "Office")
CREATE TABLE saved_locations (
    email       TEXT NOT NULL,
    label       TEXT NOT NULL,
    location_id TEXT NOT NULL,
    PRIMARY KEY (email, label)
);

-- Post-ride ratings from riders
CREATE TABLE ride_ratings (
    ride_id    TEXT PRIMARY KEY,
    rider_email TEXT NOT NULL,
    driver_id   TEXT NOT NULL,
    rating      INTEGER NOT NULL,    -- 1–5
    comment     TEXT,
    created_at  TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);

-- Cancellation reasons
CREATE TABLE cancel_reasons (
    ride_id    TEXT PRIMARY KEY,
    reason     TEXT NOT NULL,
    created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);

-- Driver status change log
CREATE TABLE driver_status_logs (
    id         INTEGER PRIMARY KEY AUTOINCREMENT,
    driver_id  TEXT NOT NULL,
    email      TEXT NOT NULL,
    status     TEXT NOT NULL,
    created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);

-- Admin audit trail
CREATE TABLE admin_audit_logs (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    actor_email TEXT NOT NULL,
    action      TEXT NOT NULL,
    target      TEXT NOT NULL,
    details     TEXT,
    created_at  TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);
```

**Persistence strategy:** Ride objects remain live in memory during runtime. Every lifecycle transition (REQUESTED → DRIVER_ASSIGNED → IN_PROGRESS → COMPLETED / CANCELLED) immediately upserts the corresponding row in `ride_history`. On web server restart, all rides are restored from SQLite and re-inflated into live `Ride` objects, and the ride counter is advanced to avoid ID collisions.

---

## 📡 API Reference

All endpoints return `application/json`. Authentication uses the `ubride_session` cookie set at login.

### Auth

| Method | Endpoint | Role | Description |
|---|---|---|---|
| `POST` | `/api/auth/register` | — | Register a new account |
| `POST` | `/api/auth/login` | — | Login and receive session cookie |
| `GET` | `/api/auth/me` | Any | Return current session user |
| `GET` | `/api/auth/profile` | Any | Full profile details |
| `POST` | `/api/auth/logout` | Any | Clear session cookie |
| `GET` | `/api/health` | — | Health check `{"status":"ok"}` |

### Rider

| Method | Endpoint | Description |
|---|---|---|
| `GET` | `/api/rider/state` | Full rider dashboard state |
| `POST` | `/api/rider/estimate` | Fare estimate before booking |
| `POST` | `/api/rider/rides` | Create a new ride |
| `POST` | `/api/rider/rides/:id/assign` | Assign nearest available driver |
| `POST` | `/api/rider/rides/:id/stops` | Add a stop (recalculates fare) |
| `POST` | `/api/rider/rides/:id/cancel` | Cancel ride with optional reason |
| `POST` | `/api/rider/rides/:id/rebook` | Rebook a cancelled ride |
| `POST` | `/api/rider/rides/:id/rating` | Submit post-ride rating |
| `POST` | `/api/rider/saved-locations` | Save a favourite location |
| `GET` | `/receipt/:id` | HTML ride receipt page |

### Driver

| Method | Endpoint | Description |
|---|---|---|
| `GET` | `/api/driver/state` | Full driver dashboard state |
| `POST` | `/api/driver/status` | Set availability (AVAILABLE/OFFLINE) |
| `POST` | `/api/driver/profile` | Update vehicle type and home location |
| `POST` | `/api/driver/rides/:id/accept` | Accept an assigned ride |
| `POST` | `/api/driver/rides/:id/reject` | Reject a ride with reason |
| `POST` | `/api/driver/rides/:id/start` | Start the ride |
| `POST` | `/api/driver/rides/:id/complete` | Complete the ride |

### Admin

| Method | Endpoint | Description |
|---|---|---|
| `GET` | `/api/admin/state` | Full admin dashboard state |
| `POST` | `/api/admin/users` | Create a user account |
| `POST` | `/api/admin/users/:email/active` | Toggle user active status |
| `POST` | `/api/admin/drivers/:id/status` | Force-set driver status |
| `POST` | `/api/admin/rides/:id/cancel` | Admin cancel a ride |
| `POST` | `/api/admin/demo/reset` | Reset simulation + ride history |
| `GET` | `/api/admin/export/users` | Download users CSV |
| `GET` | `/api/admin/export/drivers` | Download drivers CSV |
| `GET` | `/api/admin/export/rides` | Download rides CSV |

---

## ⚙️ Local Setup

### Prerequisites

- `g++` with C++17 support (GCC 7+ or Clang 5+)
- `libsqlite3-dev`
- `cmake` 3.10+ (optional — g++ direct build also works)

### Linux / macOS

```bash
# Clone the repository
git clone https://github.com/your-username/rideflow.git
cd rideflow

# Build (direct g++ — fastest for local dev)
mkdir -p build
g++ -std=c++17 \
    -Isrc \
    -Ithird_party/Crow/include \
    -Ithird_party/asio/include \
    -o build/RideFlow \
    Main.cpp \
    -lsqlite3 \
    -pthread

# Run console simulation
./build/RideFlow

# Run HTTP server (default port 18080)
./build/RideFlow web

# Custom port and database path
PORT=8080 DB_PATH=./data/app.db ./build/RideFlow web
```

### Windows

```bash
g++ -std=c++17 ^
    -Isrc ^
    -Ithird_party/Crow/include ^
    -Ithird_party/asio/include ^
    -o build/RideFlow.exe ^
    Main.cpp ^
    -lsqlite3 -lws2_32 -lmswsock
```

### CMake Build

```bash
mkdir build && cd build
cmake ..
cmake --build .
```

Open `http://localhost:18080` in your browser once the server is running.

---

## 🐳 Docker Deployment

```bash
# Build the image
docker build -t rideflow .

# Run with persistent storage
docker run -d \
  -p 10000:10000 \
  -v rideflow_data:/data \
  -e PORT=10000 \
  -e DB_PATH=/data/uber_ride_auth.db \
  rideflow

# Open in browser
open http://localhost:10000
```

**Deploying on Render:**
1. Connect your GitHub repository
2. Select "Docker" as the environment
3. Attach a persistent disk mounted at `/data`
4. Set `PORT=10000` in environment variables
5. Deploy — ride history survives restarts and redeploys

**Dockerfile summary:**
- Base image: `ubuntu:24.04`
- Installs: `g++`, `libsqlite3-dev`
- Compiles at image build time — no runtime compiler needed
- Data directory: `/data` (mount a volume here for persistence)
- Default port: `10000`

---

## 🔑 Demo Credentials

The following accounts are seeded automatically on first startup:

| Role | Email | Password |
|---|---|---|
| 🧑 Rider | `rider@demo.local` | `demo123` |
| 🚗 Driver | `driver@demo.local` | `demo123` |
| 🛡️ Admin | `admin@demo.local` | `demo123` |

Additional accounts can be created via `/api/auth/register` or from the admin panel.

---

## 🚀 Future Improvements

| Area | Improvement |
|---|---|
| **Security** | Replace SHA1 with bcrypt or Argon2 for password hashing |
| **Sessions** | Persist sessions to SQLite to survive restarts |
| **Real-time** | Add WebSocket support for live ride status updates |
| **Maps** | Integrate real GPS tile rendering (Leaflet.js + OpenStreetMap) |
| **Auth** | Add JWT support for stateless API access |
| **Testing** | Add unit tests for A\*, Haversine, pricing, and ride lifecycle |
| **Matching** | Upgrade to geohash or KD-tree for sub-millisecond spatial lookup |
| **Surge** | Auto-enable surge pricing based on driver availability thresholds |
| **Payments** | Add a mock payment/wallet system with ride receipts |
| **Notifications** | Replace console notifications with email or SMS hooks |

---

## 📝 Resume Description

> **RideFlow – Scalable Ride-Hailing Platform in Modern C++** *(Personal Project)*
>
> Built a full-stack ride-hailing platform in C++17 featuring graph-based routing (A\* with Haversine heuristics), geospatial K-nearest driver matching via a streaming max-heap, and a Strategy-pattern pricing engine supporting Normal, Surge, and Pool modes. Implemented a Crow HTTP REST API with SQLite persistence for users, ride history, stops, ratings, and audit logs. Designed a layered architecture (domain, service, persistence, web) respecting SOLID principles. Preserved the original simulation engine intact and exposed it through HTTP routes without rewriting business logic. Supports dual-mode operation (console demo and live web server), cookie-based session authentication, and role-based dashboards for Rider, Driver, and Admin. Containerised with Docker for reproducible deployment on Render/Railway.

---

## 👤 Author

**Rajat Khedar**  
B.Tech — Artificial Intelligence and Data Engineering  
Malaviya National Institute of Technology (MNIT), Jaipur

[![GitHub](https://img.shields.io/badge/GitHub-Rajatkhedar12--cmd-black?style=flat&logo=github)](https://github.com/Rajatkhedar12-cmd)
[![Email](https://img.shields.io/badge/Email-rajat55work%40gmail.com-red?style=flat&logo=gmail)](mailto:rajat55work@gmail.com)

---

<div align="center">

*Built with ⚙️ C++17 · Crow · SQLite · Docker*

*If this project helped you, consider leaving a ⭐ on GitHub*

</div>
