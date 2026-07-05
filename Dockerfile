FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        ca-certificates \
        g++ \
        libsqlite3-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY . .

RUN mkdir -p build \
    && g++ -std=c++17 \
        -Isrc \
        -Ithird_party/Crow/include \
        -Ithird_party/asio/include \
        -o build/UberRidePlatform \
        Main.cpp \
        -lsqlite3 \
        -pthread

RUN mkdir -p /data && chmod 777 /data

ENV PORT=10000
ENV DB_PATH=/data/uber_ride_auth.db

EXPOSE 10000

CMD ["./build/UberRidePlatform", "web"]
