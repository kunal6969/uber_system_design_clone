# Uber Ride Platform - Interview Q&A

This document answers the theoretical questions an interviewer may ask after reading the resume bullets for this project.

Project summary:

- Language: C++
- Web framework: Crow
- Database: SQLite
- Architecture: modular monolith with layered LLD
- Core concepts: OOP, SOLID, Factory Pattern, Strategy Pattern, A* routing, Haversine heuristic, K-nearest driver matching, sessions, persistence

---

## 1. Architecture And LLD

### Why did you choose a layered architecture for this project?

I chose a layered architecture because the project has multiple responsibilities: HTTP request handling, authentication, ride lifecycle management, route computation, pricing, matching, and persistence. If all of this logic were placed in one file or one class, the system would become hard to test, extend, and explain.

The layered architecture separates the project into clear responsibilities:

- Web/API layer: handles routes, sessions, request parsing, and responses.
- Service layer: handles business workflows such as ride creation, driver assignment, start, completion, cancellation, pricing, routing, and matching.
- Domain layer: contains core entities such as `Ride`, `Driver`, `Rider`, `Vehicle`, and `Location`.
- Persistence layer: stores users and ride history in SQLite.
- Deployment layer: contains Docker and environment variable configuration.

This improves maintainability because changes in one layer do not necessarily require changes in other layers.

### What are the main layers in your system?

The main layers are:

1. Entry point
   - `Main.cpp`
   - Decides whether the app runs in console demo mode or web mode.

2. Web/API layer
   - `src/web/WebServer.cpp`
   - Defines Crow routes such as `/rider`, `/driver`, `/admin`, `/api/auth/login`, `/api/rider/rides`, `/api/driver/rides/<id>/start`, and `/api/admin/state`.

3. Service layer
   - `RideService`
   - `RouteService`
   - `DriverMatchingService`
   - `PricingService`
   - `NotificationService`

4. Domain layer
   - `Ride`
   - `Driver`
   - `Rider`
   - `Vehicle`
   - `Location`
   - `CityGraph`
   - `RouteResult`

5. Persistence layer
   - `AuthStore`
   - `RideHistoryStore`

6. Infrastructure/deployment layer
   - `Dockerfile`
   - `CMakeLists.txt`
   - environment variables such as `PORT` and `DB_PATH`

### How does your web layer interact with the business logic layer?

The web layer does not directly implement ride logic. Instead, it receives HTTP requests, validates authentication/roles, extracts input fields, and calls the existing service methods.

For example:

- Rider books a ride through an API route.
- The route handler validates the user is a rider.
- It extracts `sourceId`, `destinationId`, and `vehicleType`.
- It calls `RideService::createRide`.
- It calls matching logic through `RideService::notifyNearbyDrivers` or matching helpers.
- It persists the ride snapshot using `RideHistoryStore`.
- It returns JSON to the frontend.

This keeps HTTP concerns separate from ride logic.

### How did you make sure ride logic is not tightly coupled with UI or database code?

The original ride logic lives in services like `RideService`, `DriverMatchingService`, `RouteService`, and `PricingService`. These services do not know about HTML pages, JSON responses, cookies, or SQLite tables.

The web adapter in `WebServer.cpp` calls those services and then converts the results into JSON or HTML. Persistence is also handled outside the core ride logic through `RideHistoryStore`, which saves snapshots after ride state changes.

This means the ride lifecycle can still run in console mode without any web or database-specific changes.

### What does modular service design mean in your project?

Modular service design means each major capability is placed into a focused service:

- `RideService`: ride lifecycle
- `RouteService`: route calculation
- `DriverMatchingService`: driver matching
- `PricingService`: fare estimation
- `NotificationService`: demo driver notifications
- `AuthStore`: user authentication and session lookup
- `RideHistoryStore`: ride history persistence

Each service has a specific job, which makes the system easier to reason about and extend.

### If you had to add payments tomorrow, where would that logic go?

I would not put payment logic directly into `RideService`. I would add a separate payment module, for example:

- `PaymentService`
- `PaymentStrategy`
- `CardPayment`
- `WalletPayment`
- `CashPayment`

`RideService` could request payment authorization or mark payment status, but actual payment processing should remain separate. This follows separation of concerns and keeps the ride lifecycle independent from payment provider details.

### Is your project a monolith or microservice? Why?

It is a modular monolith.

It is a monolith because rider, driver, admin, auth, matching, routing, pricing, and persistence are deployed as one C++ application. It is modular because the code is separated internally into services and layers.

For a portfolio/demo project, this is a good tradeoff. It avoids unnecessary microservice complexity while still demonstrating clean low-level design.

In production, this could later be split into services such as:

- Auth service
- Ride service
- Matching service
- Pricing service
- Notification service
- Payment service

---

## 2. OOP And SOLID

### Which SOLID principles did you apply?

The project applies several SOLID principles:

- Single Responsibility Principle: each service has one main responsibility.
- Open/Closed Principle: pricing behavior can be extended through new pricing strategies without modifying `RideService`.
- Liskov Substitution Principle: derived vehicle and pricing classes can be used where their base abstractions are expected.
- Interface Segregation style: classes expose focused APIs instead of one huge interface.
- Dependency Inversion style: higher-level ride logic depends on service abstractions/concepts rather than hardcoding every implementation detail.

### Give an example of Single Responsibility Principle from your code.

`RouteService` is responsible only for route calculation. It does not manage users, pricing, authentication, or persistence.

`PricingService` is responsible only for fare estimation. It delegates actual fare formula behavior to a `PricingStrategy`.

`AuthStore` is responsible for user registration, password verification, and session lookup. It does not handle ride matching or route calculation.

This separation is an example of the Single Responsibility Principle.

### How does your pricing design follow the Open/Closed Principle?

Pricing follows the Open/Closed Principle because new pricing behavior can be added by creating a new class derived from `PricingStrategy`.

Existing examples include:

- `NormalPricing`
- `SurgePricing`
- `PoolPricing`

If I want to add `PeakHourPricing`, I can create a new strategy class without modifying `RideService`. The ride workflow stays closed for modification but open for extension.

### Why did you use classes like `Ride`, `Driver`, `Rider`, `Vehicle`, and `Location`?

These classes represent real-world domain entities in a ride-sharing system:

- `Ride`: trip request and lifecycle state
- `Driver`: driver identity, vehicle, location, and status
- `Rider`: customer booking rides
- `Vehicle`: common vehicle abstraction
- `Location`: graph node with coordinates

Using domain classes makes the code easier to map to the real-world problem. It also supports encapsulation because data and behavior related to an entity stay together.

### What is the difference between domain models and services in your project?

Domain models represent core objects and state:

- `Ride`
- `Driver`
- `Rider`
- `Vehicle`
- `Location`

Services implement operations and workflows:

- `RideService` changes ride status and coordinates ride lifecycle.
- `RouteService` computes shortest routes.
- `DriverMatchingService` finds nearby drivers.
- `PricingService` calculates fare.

In simple terms, models hold business state, while services perform business actions.

### Where did you use abstraction or polymorphism?

The clearest abstraction is in pricing and vehicles.

For pricing:

- `PricingStrategy` acts as a base class.
- `NormalPricing`, `SurgePricing`, and `PoolPricing` implement different fare formulas.
- `PricingService` uses a strategy without needing to know the specific formula.

For vehicles:

- `Vehicle` is the base class.
- `Bike`, `Auto`, `Sedan`, and `SUV` are derived classes.
- `VehicleFactory` creates the correct vehicle object based on the requested type.

### If a new vehicle type is added, what code changes are required?

To add a new vehicle type, for example `LuxurySedan`, I would:

1. Create a new class derived from `Vehicle`.
2. Define its base fare, per-km rate, per-minute rate, and capacity.
3. Add creation logic in `VehicleFactory`.
4. Optionally expose it in the web UI vehicle list.

The ride lifecycle logic does not need to change.

---

## 3. Design Patterns

### Why did you use the Factory Pattern?

I used the Factory Pattern because vehicle creation should not be scattered across the codebase.

Without a factory, `RideService` might contain conditionals like:

```cpp
if (type == "SEDAN") new Sedan();
if (type == "SUV") new SUV();
```

That would make `RideService` responsible for object creation details. Instead, `VehicleFactory` centralizes vehicle creation, so `RideService` only asks for a vehicle by type.

### How does `VehicleFactory` help in your design?

`VehicleFactory` improves the design by:

- centralizing object creation
- hiding constructor details
- making new vehicle types easier to add
- reducing duplication
- keeping ride workflow code cleaner

It supports the Open/Closed Principle because vehicle additions mostly happen in the factory and vehicle classes, not in the ride lifecycle.

### Why did you use the Strategy Pattern for pricing?

Pricing can vary based on business rules:

- normal fare
- surge fare
- pool discount
- future coupon pricing
- future peak-hour pricing

If all formulas were inside `RideService`, then every pricing change would require modifying ride workflow code. The Strategy Pattern allows different pricing algorithms to be plugged into `PricingService`.

### How are normal pricing, surge pricing, and pool pricing different strategies?

They share the same conceptual interface: calculate fare based on vehicle, distance, and time.

- `NormalPricing`: standard fare formula.
- `SurgePricing`: applies a multiplier to the base fare.
- `PoolPricing`: applies a discount factor.

Each strategy changes the fare calculation without changing the ride lifecycle.

### What problem would happen if pricing logic was written directly inside `RideService`?

`RideService` would become too large and would violate Single Responsibility Principle.

It would handle:

- ride creation
- driver assignment
- ride start/completion
- cancellation
- stop addition
- fare formulas
- surge rules
- discount rules

This would make it harder to maintain and extend. Every new pricing rule would risk breaking ride lifecycle code.

### Did you use Singleton? If not, why should you not claim it?

The current code does not implement a formal Singleton Pattern.

It creates long-lived objects such as `AuthStore`, `DemoRideBackend`, and services in web mode, but that is not the same as Singleton. A Singleton means a class enforces only one instance globally, usually through a private constructor and a static accessor.

I should not claim Singleton on the resume unless the code actually implements it. It is better to accurately claim Factory, Strategy, OOP, SOLID, and layered architecture.

---

## 4. Routing And Maps Logic

### How did you model the city map?

The city is modeled as a graph:

- `Location` is a node.
- Road connection is an edge.
- `CityGraph` stores locations and adjacency lists.

Each edge stores:

- destination location
- road distance
- estimated travel time

This graph allows route algorithms to search from source to destination.

### What are nodes and edges in your graph?

Nodes are city locations, such as:

- Connaught Place
- Karol Bagh
- Hauz Khas
- Saket
- Dwarka
- Noida Sector 18

Edges are roads connecting these locations. Each edge has a distance and travel time.

### Why did you use A* instead of plain Dijkstra?

Dijkstra explores nodes based only on the shortest known cost from the source. A* improves this by adding a heuristic estimate of remaining cost to the destination.

In this project, the heuristic is based on straight-line distance calculated using the Haversine formula. This helps A* prioritize nodes that are more likely to lead toward the destination.

For small graphs, both Dijkstra and A* are fast. But A* better demonstrates map-routing logic because it uses both actual cost so far and estimated remaining cost.

### What is the role of the Haversine formula?

The Haversine formula calculates the straight-line distance between two latitude/longitude points on Earth.

In this project, it is used for:

- estimating road distance between connected locations
- computing the A* heuristic
- estimating remaining travel time

It is useful because latitude and longitude are spherical coordinates, not flat Cartesian coordinates.

### What is a heuristic in A*?

A heuristic is an estimated cost from the current node to the destination.

A* uses:

```text
f(n) = g(n) + h(n)
```

Where:

- `g(n)` = actual cost from source to current node
- `h(n)` = estimated cost from current node to destination
- `f(n)` = estimated total cost through current node

In this project, `h(n)` is based on Haversine straight-line distance converted into optimistic travel time.

### What makes a heuristic admissible?

A heuristic is admissible if it never overestimates the true cost to reach the destination.

For example, straight-line distance is usually a lower bound for real road distance, because roads are rarely shorter than the direct line between two points.

An admissible heuristic helps A* find the optimal shortest path.

### What is the time complexity of A* in your implementation?

The worst-case complexity is similar to Dijkstra when using a priority queue:

```text
O((V + E) log V)
```

Where:

- `V` = number of locations
- `E` = number of roads

In practice, A* can explore fewer nodes if the heuristic is good.

### How do you calculate ETA and distance?

Distance is calculated from graph edges. Each road has a distance based on Haversine straight-line distance multiplied by a road-distance multiplier.

ETA is calculated using:

```text
time = distance / averageSpeed
```

The route result accumulates total distance and total time across all edges in the path.

### How is your routing logic similar to Google Maps at a small scale?

Google Maps also models roads as a graph and computes optimal routes based on cost. The real system uses far more data, including traffic, turn restrictions, road classes, live speed, and historical data.

This project is a simplified version:

- city locations are graph nodes
- roads are graph edges
- A* finds the shortest/fastest path
- Haversine estimates remaining distance
- ETA and fare are derived from route distance and time

So it is "Google-Maps-like" at a small demo scale, not a replacement for a real maps platform.

---

## 5. Driver Matching

### How does your K-nearest driver matching work?

The matching service:

1. Iterates over available drivers.
2. Filters out drivers who are not available.
3. Filters out drivers whose vehicle type does not match the requested ride.
4. Computes route ETA from driver location to pickup location.
5. Maintains a priority queue of the top K nearest drivers.
6. Returns the best K drivers ranked by ETA.

This gives the rider or driver page a list of nearest eligible drivers.

### Why do you filter by vehicle type first?

Vehicle type filtering avoids unnecessary route computation.

If a rider requests a Sedan, Bike or SUV drivers should not be considered. Filtering early reduces the number of route calculations, which improves efficiency.

### Why do you check driver availability?

A driver should only be matched if they can accept a ride.

Drivers can have statuses such as:

- `AVAILABLE`
- `ON_TRIP`
- `OFFLINE`

Only `AVAILABLE` drivers should be included in matching.

### Why did you use a priority queue?

A priority queue is useful for keeping only the best K candidates.

Instead of sorting every eligible driver, the algorithm maintains a heap of size K. If a better driver is found, it replaces the farthest driver in the heap.

This is efficient when K is small compared to the total number of drivers.

### What is the time complexity of your matching algorithm?

Let:

- `D` = number of drivers
- `K` = number of nearest drivers needed
- `R` = cost of route calculation

The matching complexity is approximately:

```text
O(D * R + D log K)
```

The route calculation dominates because for each eligible driver, the service may compute an ETA to the pickup location.

### What happens if no drivers are available?

The matching service returns an empty result.

The web/API layer then returns a message like:

```text
No available matching drivers right now.
```

This prevents invalid assignment.

### How would you scale matching for 1 million drivers?

I would not scan all drivers.

For production scale, I would use:

- geospatial indexing
- grid-based partitioning
- geohash
- Redis GEO
- PostGIS
- location shards
- streaming location updates

The matching flow would first find nearby drivers geographically, then run more detailed ETA ranking only on a small candidate set.

### Would you still scan all drivers in production?

No.

Scanning all drivers is acceptable for a demo-scale simulation, but production systems need spatial indexing. The system should narrow candidates by geography first, then run route/ETA calculations.

### What data structure would you use for real geospatial driver lookup?

Good options include:

- Geohash grid
- Quad tree
- KD-tree
- R-tree
- Redis GEO
- PostgreSQL PostGIS

For a real backend, Redis GEO or PostGIS would be practical choices.

---

## 6. Pricing

### How is fare calculated?

Fare is calculated using vehicle pricing attributes and route details.

Inputs include:

- base fare
- per-km rate
- per-minute rate
- trip distance
- trip time

The general formula is:

```text
fare = baseFare + distanceKm * perKmRate + timeMinutes * perMinuteRate
```

Strategy classes can modify this base fare, for example by applying a surge multiplier or pool discount.

### What inputs affect pricing?

The current inputs are:

- vehicle type
- distance
- time
- pricing strategy

Vehicle type matters because Sedan, SUV, Auto, and Bike have different rates.

### How does Strategy Pattern make pricing extensible?

The Strategy Pattern allows the system to add new pricing algorithms without changing ride workflow code.

For example, I can add:

- `PeakHourPricing`
- `CouponPricing`
- `SubscriptionPricing`
- `AirportPricing`

Each class can implement the same pricing interface/concept and plug into `PricingService`.

### How would you add peak-hour pricing?

I would create a new pricing strategy:

```text
PeakHourPricing
```

It could apply a multiplier based on time of day. The rest of the ride creation flow would remain unchanged.

### How would you add coupon or wallet logic?

I would separate coupon and payment logic from pricing:

- `PricingService`: calculates base ride fare.
- `CouponService`: applies discounts.
- `PaymentService`: handles payment method and transaction state.

This avoids mixing fare calculation with payment processing.

### How would you prevent pricing changes from breaking ride creation?

I would:

- keep pricing behind `PricingService`
- use strategies for formulas
- add unit tests for fare calculations
- add integration tests for ride creation
- avoid hardcoding pricing rules inside `RideService`

---

## 7. Authentication And Sessions

### How does your login flow work?

The login flow is:

1. User sends email and password to `/api/auth/login`.
2. `AuthStore` looks up the user by email in SQLite.
3. It hashes the submitted password with the stored salt.
4. It compares the computed hash with the stored password hash.
5. If valid, it creates a session token.
6. The server sends the token as a cookie.
7. Future requests use the cookie to identify the user.

### What data is stored in SQLite for users?

The `users` table stores:

- id
- name
- email
- role
- salt
- password hash

The plain password is not stored.

### Are passwords stored directly?

No.

The password is combined with a random salt and hashed. Only the salt and password hash are stored.

### What is salting and hashing?

Hashing converts a password into a fixed-length value that is hard to reverse.

Salting means adding random data to the password before hashing. This prevents two users with the same password from having the same stored hash and reduces the effectiveness of precomputed rainbow-table attacks.

### Why is SHA1 not production-grade?

SHA1 is fast and old. For password storage, fast hashing is a weakness because attackers can brute-force many guesses quickly.

Production systems should use slow password hashing algorithms such as:

- bcrypt
- Argon2
- scrypt
- PBKDF2

In this project, SHA1 is acceptable only as a demo simplification.

### How does session management work?

After login, the server creates a session token and maps it to a user email in memory.

The browser stores the token in a cookie named:

```text
ubride_session
```

On each protected request, the backend extracts the cookie, finds the session, and loads the corresponding user.

### Where are sessions stored?

Sessions are stored in memory using a map from session token to email.

### What happens to sessions after server restart?

Sessions are lost after server restart because they are stored in memory.

Users and ride history remain saved in SQLite, but the user must log in again.

### How would you make sessions production-ready?

I would:

- store sessions in Redis or a database
- add session expiration
- generate cryptographically secure tokens
- set secure cookie flags
- use HTTPS
- add logout-all-sessions support
- rotate session tokens after login

### How would you prevent public admin signup?

I would remove `admin` from the public signup form.

Better options:

- seed the first admin through an environment variable
- allow only existing admins to create new admins
- add invite-based admin creation
- enforce role restrictions in backend registration routes

---

## 8. Persistence And SQLite

### Why did you choose SQLite?

SQLite is lightweight and easy to deploy. It does not require a separate database server, which makes it suitable for a demo or portfolio project.

It is good for:

- authentication storage
- ride history snapshots
- small-scale persistent data
- simple deployment

### What data is persisted?

The app persists:

- users
- password salts and hashes
- ride history
- ride status
- assigned driver
- source and destination
- stops
- fare
- cancellation fee

### How is ride history saved?

`RideHistoryStore` saves a snapshot after each important lifecycle event:

- ride requested
- driver assigned/accepted
- stop added
- ride started
- ride completed
- ride cancelled

The main tables are:

- `ride_history`
- `ride_stops`

### What is a ride snapshot?

A ride snapshot is a persisted representation of the current ride state at a point in time.

It stores fields such as:

- ride ID
- rider
- driver
- vehicle type
- route
- status
- distance
- time
- fare
- stops

Instead of trying to serialize the entire C++ object graph, the app saves the important business state.

### How do you restore ride history after restart?

On web server startup:

1. The demo city graph and drivers are seeded.
2. `RideHistoryStore` loads saved ride snapshots.
3. The web backend recreates `Ride` objects from those snapshots.
4. It restores status, stops, driver assignment, fare, distance, and time.
5. It updates the ride counter so new ride IDs do not collide.

### Why not store every object directly?

Directly storing C++ objects is not portable or clean. Objects contain pointers and runtime-specific memory addresses.

It is better to store normalized business data:

- IDs
- names
- statuses
- route fields
- fare values

Then reconstruct runtime objects when needed.

### What are the limitations of SQLite?

SQLite limitations include:

- limited concurrent writes
- not ideal for high-traffic multi-instance deployments
- database is a single file
- no built-in distributed replication
- not ideal for large-scale real-time systems

For this project, SQLite is a reasonable lightweight choice.

### When would you move from SQLite to PostgreSQL?

I would move to PostgreSQL when:

- concurrent traffic increases
- multiple backend instances are needed
- complex queries become common
- stricter transactions are needed
- production-grade reliability is required
- geospatial queries are needed through PostGIS

### How would you design the database schema for production?

A production schema could include:

- users
- riders
- drivers
- vehicles
- locations
- rides
- ride_stops
- payments
- ratings
- driver_location_events
- sessions
- audit_logs

I would also add indexes on:

- rider_id
- driver_id
- ride_status
- created_at
- geospatial location fields

---

## 9. Ride Lifecycle

### What are the possible ride statuses?

The ride statuses are:

- `REQUESTED`
- `DRIVER_ASSIGNED`
- `IN_PROGRESS`
- `COMPLETED`
- `CANCELLED`

These represent the ride lifecycle from booking to finish or cancellation.

### What happens when a rider books a ride?

The flow is:

1. Rider selects pickup, dropoff, and vehicle type.
2. `RideService::createRide` creates a ride.
3. `RouteService` computes route distance and ETA.
4. `PricingService` calculates fare.
5. Matching finds nearby eligible drivers.
6. Ride state is saved to SQLite.
7. The API returns ride details and matching results.

### What happens when a driver accepts a ride?

The flow is:

1. Driver selects a ride request.
2. Backend verifies driver role and availability.
3. Backend verifies the ride is still requested.
4. `RideService::assignDriver` assigns the driver.
5. Driver status becomes `ON_TRIP`.
6. Ride status becomes `DRIVER_ASSIGNED`.
7. Ride history is updated in SQLite.

### What happens when a ride is cancelled?

The flow is:

1. Backend checks the ride is not already completed or cancelled.
2. If a driver was assigned or ride was in progress, a cancellation fee is applied.
3. Driver status is set back to `AVAILABLE`.
4. Ride status becomes `CANCELLED`.
5. Cancellation fee and status are persisted.

### How do stops affect fare and distance?

When a stop is added:

1. The stop is added to the ride.
2. The route is recomputed using source, stops, and destination.
3. Distance and time are recalculated.
4. Fare is recalculated using the pricing strategy.
5. Updated ride state is saved.

### Why should completed rides not be cancellable?

A completed ride is final. Cancelling it would create inconsistent business state because:

- the trip already ended
- fare is already final
- driver availability has already been restored
- receipt/history should remain stable

So cancellation is blocked after completion.

### How do you ensure driver status changes after ride completion?

`RideService::completeRide` sets the ride status to `COMPLETED` and changes the assigned driver's status back to `AVAILABLE`.

This makes the driver eligible for future matches.

---

## 10. Performance

### What does p95 <10ms mean?

p95 means the 95th percentile latency.

If 100 requests are sorted from fastest to slowest, the p95 value is around the 95th request. It means 95% of requests completed at or below that latency.

In this project, local benchmark results showed p95 below 10ms for demo-scale ride creation and nearest-driver assignment APIs.

### How did you measure it?

I measured it by sending repeated local HTTP requests to the running Crow server and timing API calls using a stopwatch.

Measured endpoints included:

- ride creation
- nearest-driver assignment
- rider state fetch

The measured local results were approximately:

- create ride: p95 around 7.93ms
- assign nearest: p95 around 6.71ms
- rider state: p95 around 1.90ms

### Why is p95 better than average latency?

Average latency can hide slow requests.

p95 shows tail latency, which is closer to what real users may experience. A low average with high p95 means some users still see slow responses.

### Does local p95 mean production p95 will also be the same?

No.

Local p95 measures backend processing on the local machine. Production latency can include:

- network latency
- cold starts
- cloud CPU limits
- database disk speed
- request routing overhead
- TLS overhead

So the resume claim should say "local" or "demo-scale".

### What operations are included in your latency measurement?

The measured ride creation and assignment APIs include:

- HTTP request handling
- session validation
- route calculation
- fare calculation
- driver matching
- SQLite persistence
- JSON response generation

### Why is C++ efficient for routing and matching?

C++ is compiled and has low runtime overhead. It gives strong performance for CPU-heavy operations such as:

- graph traversal
- priority queues
- route computation
- repeated distance calculations

However, algorithmic complexity matters more than language alone.

### Is C++ automatically more scalable than Node.js?

No.

C++ can be more CPU-efficient, but scalability depends on architecture:

- stateless services
- load balancing
- database design
- caching
- queues
- horizontal scaling
- geospatial indexing

A well-designed Node.js/MERN system can scale better than a poorly designed C++ monolith.

### What bottlenecks would appear at large scale?

Potential bottlenecks:

- scanning all drivers
- SQLite write concurrency
- in-memory sessions
- single process deployment
- no distributed driver location index
- no queue for notifications
- no horizontal scaling support

---

## 11. Scalability And Production Readiness

### What changes are needed to make this production-ready?

I would add:

- PostgreSQL/PostGIS instead of SQLite
- Redis for sessions and driver locations
- stronger password hashing such as bcrypt/Argon2
- stateless backend instances
- load balancer
- persistent distributed session storage
- proper logging and monitoring
- rate limiting
- role management
- HTTPS
- audit logs
- CI/CD

### How would you scale driver matching?

I would use a two-step process:

1. Candidate generation:
   - Use geospatial index to find nearby drivers quickly.
   - Use Redis GEO, geohash, or PostGIS.

2. Ranking:
   - Run ETA and route calculation only on a smaller candidate set.
   - Rank by ETA, vehicle type, availability, rating, and acceptance probability.

### How would you handle real-time location updates?

I would use:

- WebSockets for driver location streaming
- Redis or Kafka for location event ingestion
- geospatial cache for latest driver positions
- periodic updates to persistent storage

The matching service would query the latest driver location from the cache.

### How would you split this monolith into services?

Possible services:

- Auth service
- Ride service
- Matching service
- Pricing service
- Location service
- Notification service
- Payment service
- Admin service

Each service would own its data and expose APIs/events.

### What would you move to Redis?

I would move:

- sessions
- driver live locations
- nearby-driver geospatial lookup
- temporary ride request state
- rate-limit counters

Redis is good for low-latency ephemeral data.

### What would you move to PostgreSQL/PostGIS?

I would move:

- users
- rides
- ride stops
- drivers
- vehicles
- payments
- ratings
- audit logs
- geospatial location tables

PostGIS would be useful for spatial queries.

### How would you support multiple backend instances?

I would make the backend stateless:

- store sessions in Redis
- store ride state in a shared database
- store live driver locations in Redis/PostGIS
- use a load balancer
- avoid in-memory-only state for shared workflows

### Why are in-memory sessions a problem in horizontal scaling?

If sessions are stored inside one process, another server instance cannot validate that session.

This causes problems when requests are load-balanced across multiple instances. Shared session storage such as Redis solves this.

---

## 12. Role-Based Dashboards

### What are the three user roles?

The roles are:

- rider
- driver
- admin

Each role has separate pages and API permissions.

### How do rider, driver, and admin flows differ?

Rider:

- books rides
- adds stops
- cancels rides
- views ride history

Driver:

- views matched requests
- accepts rides
- starts rides
- completes rides
- updates availability

Admin:

- views users, drivers, rides
- changes driver status
- cancels active rides
- resets simulation

### How do you enforce role-based access?

The backend checks the session user and role before allowing access to protected routes.

For example:

- rider APIs require role `rider`
- driver APIs require role `driver`
- admin APIs require role `admin`

This is enforced server-side, not just hidden in the UI.

### What APIs are available to each role?

Rider APIs:

- `/api/rider/state`
- `/api/rider/rides`
- `/api/rider/rides/<id>/assign`
- `/api/rider/rides/<id>/stops`
- `/api/rider/rides/<id>/cancel`

Driver APIs:

- `/api/driver/state`
- `/api/driver/status`
- `/api/driver/rides/<id>/accept`
- `/api/driver/rides/<id>/start`
- `/api/driver/rides/<id>/complete`

Admin APIs:

- `/api/admin/state`
- `/api/admin/demo/reset`
- `/api/admin/drivers/<id>/status`
- `/api/admin/rides/<id>/cancel`

### Why should admin functionality be protected carefully?

Admin can view and modify system-wide data, including users, drivers, and rides.

If admin access is compromised, an attacker could:

- cancel rides
- change driver status
- view user records
- reset simulation data

So production admin access should require stronger controls.

### How would you audit admin actions?

I would create an `audit_logs` table with:

- admin user ID
- action type
- target entity
- timestamp
- old value
- new value
- IP address

This helps debugging, compliance, and abuse detection.

---

## 13. Tradeoffs

### What was the biggest tradeoff in this project?

The biggest tradeoff was keeping the app lightweight while still showing real backend architecture.

I chose:

- Crow instead of a heavy web framework
- SQLite instead of a full database server
- server-rendered HTML strings instead of a separate React frontend
- modular monolith instead of microservices

This kept the project understandable and deployable while still demonstrating LLD, algorithms, auth, persistence, and web routing.

### Why did you keep the frontend simple?

The main goal was backend/system design, not frontend animation or UI complexity.

The frontend is simple server-rendered HTML/CSS/JS so the focus stays on:

- ride lifecycle
- algorithms
- design patterns
- persistence
- role-based flows

### Why did you avoid live tracking?

Live tracking would add significant complexity:

- WebSockets
- frequent location updates
- map rendering
- geospatial storage
- real-time synchronization

It was intentionally avoided to keep the project focused on core ride booking, routing, matching, and lifecycle design.

### What would you improve next?

Good next improvements:

- disable public admin signup
- move sessions to SQLite or Redis
- add stronger password hashing
- persist driver availability
- add admin audit logs
- add automated tests
- add README and architecture diagram
- add geospatial indexing for driver lookup

### What part of the project best demonstrates system design?

The best system-design part is the separation between:

- web/API layer
- ride lifecycle service
- route service
- matching service
- pricing service
- persistence layer

This shows how a real ride-sharing backend can be decomposed into focused modules.

### What part best demonstrates algorithms?

The strongest algorithmic parts are:

- A* shortest-path routing
- Haversine heuristic
- K-nearest driver selection
- priority queue based top-K matching

### What part best demonstrates backend engineering?

The backend engineering strengths are:

- Crow route handling
- role-based APIs
- SQLite auth
- session-based login
- persistent ride history
- Docker/Render readiness
- clean separation between HTTP layer and business logic

---

## 14. Short Interview Pitch

I built an Uber-style ride booking platform in C++ using Crow. The project follows SOLID OOP and layered LLD, with separate services for ride lifecycle, routing, pricing, matching, authentication, and persistence. I implemented a custom Google-Maps-like routing engine using city graph modeling, A* shortest path, and Haversine distance. For matching, I used vehicle filtering, driver availability, ETA ranking, and priority queues for K-nearest driver selection. The app supports rider, driver, and admin dashboards, SQLite authentication, session management, persistent ride history, cancellations, stops, and local p95 API response times under 10ms for demo-scale ride creation and matching.
