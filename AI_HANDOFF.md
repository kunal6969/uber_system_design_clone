# Uber Ride Platform - AI Handoff

This file is a compact but detailed handoff for any future AI continuing work on this repository.

## 1. Project Goal

Convert a console-based Uber clone written in C++ into a simple web-based demo application.

Primary constraints from the user:
- Keep the original backend ride logic exactly the same.
- Support only three roles: rider, driver, and admin.
- Keep the UI simple and traditional.
- Use SQLite for final auth storage if it does not make the project too complex.
- Keep demo/simulation mode.
- Separate pages for rider, driver, and admin.
- Do not add unnecessary complexity like live tracking.
- Admin panel should be basic management only.

## 2. Original Codebase Shape

The project originally had a single console demo entrypoint in `Main.cpp` that hardcoded one city graph, locations, vehicles, drivers, and riders.

The console flow demonstrated:
- create ride
- compute route
- find nearest drivers
- assign driver
- start ride
- add stop mid-ride
- complete ride
- print receipt
- cancel a second ride

There was no real auth, no database, no frontend, and no role-based separation.

## 3. Current Technical Direction

The app is now being converted into a Crow-based C++ web app.

Chosen stack and architecture:
- Backend language: C++
- Web framework: Crow
- Database: SQLite for auth storage
- Password hashing: salted hash using TinySHA1 from the Crow vendor tree
- Session management: in-memory session map plus cookie named `ubride_session`
- Build target: C++17

Important build/runtime requirements on this machine:
- Crow is vendored in `third_party/Crow`
- Asio is vendored in `third_party/asio`
- Windows build needs `ws2_32` and `mswsock`
- SQLite library is linked for auth

## 4. What Has Been Implemented

### 4.1 Web Server

A Crow web server was added in `src/web/WebServer.cpp`.

It provides:
- `/` - login UI page
- `/rider` - rider booking and ride history page
- `/driver` - driver ride handling page
- `/admin` - basic admin management page
- `/api/health` - health check
- `/api/auth/register` - create account
- `/api/auth/login` - login
- `/api/auth/me` - current session user
- `/api/auth/logout` - end session
- role APIs for rider booking/stops/cancellation, driver accept/start/complete/status, and admin state/reset/basic management

The server currently serves separate role pages and validates auth via cookie sessions.

### 4.2 SQLite Auth

A new auth store was added in `src/auth/AuthStore.cpp`.

Behavior:
- SQLite database file: `uber_ride_auth.db`
- Users are stored with:
  - name
  - email
  - role
  - salt
  - password hash
- Passwords are not stored in plaintext.
- Passwords are hashed as `SHA1(salt + ":" + password)` using TinySHA1.
- Demo users are seeded on startup:
  - rider@demo.local / demo123
  - driver@demo.local / demo123
  - admin@demo.local / demo123
- Sessions are stored in memory, mapped from session token to email.
- Admin reads the user list from SQLite through a small read-only `listAccounts()` method.

### 4.3 SQLite Ride History

Ride history persistence was added in `src/persistence/RideHistoryStore.cpp`.

Behavior:
- Uses the same SQLite database file: `uber_ride_auth.db`
- Stores ride snapshots in `ride_history`
- Stores ordered stops in `ride_stops`
- Saves after each important lifecycle change:
  - ride requested
  - driver assigned/accepted
  - stop added
  - ride started
  - ride completed
  - ride cancelled
- Restores saved rides into live `Ride` objects when web mode starts.
- Advances the `RideService` ride counter after restore so new ride IDs do not collide with saved history.
- Admin "Reset Simulation" clears the saved ride history as well as the in-memory simulation state.

### 4.4 Role Pages and Web Simulation

`src/web/WebServer.cpp` now contains a lightweight in-memory `DemoRideBackend` that seeds the same demo city graph, locations, drivers, and services used by the console demo.

Important details:
- Rider page can request rides, assign the nearest matched driver, add stops, cancel rides, and view history.
- Driver page can view matched ride requests, accept a ride, start a ride, complete a ride, and set availability.
- Admin page can view users, drivers, rides, summary counts, reset the simulation, mark drivers available/offline, and cancel active rides.
- Ride objects are still operated in memory during runtime, but ride history/status snapshots are persisted to SQLite.
- SQLite remains limited to auth/users and ride history so the app stays lightweight.
- Matching, routing, pricing, stops, completion, and cancellation continue to call the existing service methods instead of duplicating business logic.

### 4.5 Entry Point

`Main.cpp` still supports the original console demo flow and also supports web mode.

Current behavior:
- `build/UberRidePlatform.exe` with no args runs console demo.
- `build/UberRidePlatform.exe web` runs the Crow server.
- Web mode reads `PORT` from the environment, defaulting to `18080`.
- Web mode reads `DB_PATH` from the environment, defaulting to `uber_ride_auth.db`.

### 4.6 Build Setup

The build was updated to C++17 and now links the required libraries.

Current build dependencies:
- Crow include path
- Asio include path
- SQLite
- Winsock libraries

`CMakeLists.txt` now links `ws2_32` and `mswsock` only on Windows, so a Linux deployment can link just SQLite.

Current build command used successfully:
- `g++ -std=c++17 -Isrc -Ithird_party/Crow/include -Ithird_party/asio/include -o build/UberRidePlatform.exe Main.cpp -lsqlite3 -lws2_32 -lmswsock`

Typical Linux build command:
- `g++ -std=c++17 -Isrc -Ithird_party/Crow/include -Ithird_party/asio/include -o build/UberRidePlatform Main.cpp -lsqlite3 -pthread`

Docker deployment files:
- `Dockerfile`
- `.dockerignore`

The Dockerfile defaults to:
- `PORT=10000`
- `DB_PATH=/data/uber_ride_auth.db`

For Render, attach a persistent disk at `/data` so SQLite data survives restarts/redeploys.

### 4.7 Browser UI Verification

The login page has been opened in a browser and tested successfully.

Previously verified flow:
- open page at `http://127.0.0.1:18080/`
- login with seeded rider user
- session is reflected in `/api/auth/me`
- logout works

Most recent smoke test after the role split:
- console demo still runs with original flow
- rider login and `/rider` page return 200
- rider can create a CP -> SAKET Sedan ride
- rider assignment chooses nearest matched driver
- driver login and `/driver` page return 200
- driver can start and complete the assigned ride
- admin login and `/admin` page return 200
- `/api/admin/state` reflects the completed ride

Most recent persistence smoke test:
- admin reset clears the simulation and saved ride history
- rider creates `RIDE-1`
- rider assigns nearest driver and adds a stop
- driver starts and completes the ride
- server restarts
- rider history still shows the completed ride with the stop and final fare
- next created ride after restart becomes `RIDE-2`

## 5. Current Scope Decision

The user finalized these constraints:
- roles only: rider, driver, admin
- separate pages
- simple, traditional UI
- SQLite final version if not too complex
- keep demo simulation mode
- keep ride logic exactly the same
- admin panel should be basic management only
- rider panel should keep booking / ride flow / history / cancellation / stops
- driver panel should focus on ride handling only
- do not add extra driver analytics or payments unless necessary

## 6. Important Caveats

### 6.1 Security Caveat

The auth implementation is good for a demo, but not production-grade:
- sessions are in memory, so they do not survive a restart
- TinySHA1 is acceptable for a demo, but not ideal for real security
- password hashing is salted, but not a modern KDF like bcrypt/Argon2

If the project stays a portfolio/demo app, this is acceptable.
If it becomes production-like, auth should be upgraded.

### 6.2 Persistence Caveat

Auth/users and ride history are backed by SQLite.
Driver availability changes are still demo/runtime state unless they are part of a saved ride snapshot.

### 6.3 Scope Caveat

The backend ride logic should not be rewritten unless needed.
The safest path is to keep the core ride services as-is and only wrap them in web pages and HTTP routes.

## 7. Recommended Next Steps

If continuing the work, the best remaining tasks are:
1. Polish the simple role pages if desired.
2. Add focused tests or scripted smoke tests for the role APIs.
3. Optionally add driver availability persistence if the project needs it beyond demo/runtime state.
4. Avoid changing matching, pricing, routing, and ride lifecycle logic unless a bug is discovered.

## 8. Files of Interest

Core logic from the original app:
- `Main.cpp`
- `src/service/RideService.cpp`
- `src/service/DriverMatchingService.cpp`
- `src/service/RouteService.cpp`
- `src/service/PricingService.cpp`
- `src/domain/Ride.cpp`
- `src/domain/Driver.cpp`
- `src/domain/Rider.cpp`
- `src/graph/*`
- `src/pricing/*`

Web/auth layer:
- `src/web/WebServer.cpp`
- `src/auth/AuthStore.cpp`
- `src/persistence/RideHistoryStore.cpp`

Build config:
- `CMakeLists.txt`
- `.vscode/tasks.json`
- `.vscode/launch.json`
- `Dockerfile`
- `.dockerignore`

Vendored dependencies:
- `third_party/Crow`
- `third_party/asio`

## 9. Practical Guidance for a Future AI

If a future AI continues this work, it should:
- preserve the existing ride logic
- keep the codebase small and demo-friendly
- make one role/page at a time
- validate with a build after each meaningful change
- keep the simulation mode intact
- prefer straightforward C++ implementation over introducing more frameworks

## 10. Status Summary

At the moment, the project is in a working transitional state:
- console demo still works
- web server runs
- login UI works
- separate rider, driver, and admin pages work
- role APIs call the existing ride services
- SQLite auth works
- SQLite ride history works
- sessions work via cookie
- live simulation objects are rebuilt from saved ride history on web startup
