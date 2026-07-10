FROM debian:bookworm AS builder

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    ca-certificates \
    git \
    libbrotli-dev \
    libsqlite3-dev \
    libssl-dev \
    ninja-build \
    pkg-config \
    python3-pip \
    uuid-dev \
    zlib1g-dev \
 && rm -rf /var/lib/apt/lists/*

RUN python3 -m pip install --break-system-packages --no-cache-dir cmake==3.30.9

WORKDIR /src
COPY . .

RUN cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
RUN cmake --build build --target slim_guestbook_api ring_buffer_cli

FROM debian:bookworm-slim AS runtime

RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates \
    libbrotli1 \
    libsqlite3-0 \
    libssl3 \
    libstdc++6 \
    libuuid1 \
    zlib1g \
 && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY --from=builder /src/build/slim_guestbook_api /app/slim_guestbook_api
COPY --from=builder /src/build/ring_buffer_cli /app/ring_buffer_cli
COPY --from=builder /src/build/_deps/drogon-build/libdrogon.so.1.9.13 /usr/local/lib/
COPY --from=builder /src/build/_deps/drogon-build/trantor/libtrantor.so.1.5.28 /usr/local/lib/
COPY config.json /config/config.json

RUN mkdir -p /data \
 && ln -s /usr/local/lib/libdrogon.so.1.9.13 /usr/local/lib/libdrogon.so.1 \
 && ln -s /usr/local/lib/libdrogon.so.1 /usr/local/lib/libdrogon.so \
 && ln -s /usr/local/lib/libtrantor.so.1.5.28 /usr/local/lib/libtrantor.so.1 \
 && ln -s /usr/local/lib/libtrantor.so.1 /usr/local/lib/libtrantor.so \
 && ln -s /config/config.json /app/config.json \
 && ldconfig

VOLUME ["/config", "/data"]

EXPOSE 8098

CMD ["/app/slim_guestbook_api"]
