# slim_guestbook_api

A small guestbook HTTP API written in C++20 with [Drogon](https://github.com/drogonframework/drogon). Entries are stored in a fixed-size on-disk ring buffer, so the service has no external database dependency and keeps only the most recent entries.

## Features

- `GET /read` returns guestbook entries in newest-first order.
- `POST /write` appends a new guestbook entry.
- Persistent file-backed storage in `ring_buffer.bin`.
- Bounded storage: the default buffer keeps 10,000 entries and overwrites the oldest entry after capacity is reached.
- Simple per-host write rate limiting: one `POST /write` attempt per second.
- Configurable CORS allowlist.
- Optional `X-Forwarded-For` client IP extraction when the immediate peer is trusted.
- Small `ring_buffer_cli` utility for inspecting and mutating the backing file.

## Build

Requirements:

- CMake 3.30 or newer
- C++20 compiler
- Git
- Ninja or another CMake generator
- System libraries needed by Drogon, including OpenSSL, zlib, Brotli, SQLite, and UUID development packages

Dependencies for JsonCpp and Drogon are fetched by CMake through `FetchContent`.

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

Build outputs include:

- `build/slim_guestbook_api`: HTTP API server
- `build/ring_buffer_cli`: utility for direct ring buffer access
- `build/*_tests`: test executables registered with CTest

## Run Locally

The server expects `config.json` in the current working directory. CMake copies the repository `config.json` into the build directory.

```bash
cd build
./slim_guestbook_api
```

The default config listens on `0.0.0.0:8098`.

```bash
curl 'http://localhost:8098/read?pageNumber=0'
```

## Run With Docker

```bash
docker build -t slim-guestbook-api .
docker run --rm -p 8098:8098 slim-guestbook-api
```

The Docker image declares `/config` and `/data` as volumes. The server stores `ring_buffer.bin` under Drogon's configured `app.document_root`. If you want Docker persistence, mount a config that sets `document_root` to `/data`.

Example persistent config:

```json
{
  "listeners": [
    {
      "address": "0.0.0.0",
      "port": 8098
    }
  ],
  "app": {
    "document_root": "/data"
  },
  "custom_config": {
    "trusted_hosts": ["127.0.0.1"],
    "cors_allowed_hosts": ["http://localhost:8080"]
  }
}
```

Example Docker run:

```bash
docker run --rm \
  -p 8098:8098 \
  -v "$PWD/config.json:/config/config.json:ro" \
  -v "$PWD/data:/data" \
  slim-guestbook-api
```

## Configuration

Configuration is read from `config.json` using Drogon's config format.

Relevant settings:

- `listeners[].address`: bind address, for example `0.0.0.0`.
- `listeners[].port`: bind port, for example `8098`.
- `app.document_root`: directory containing `ring_buffer.bin`; the file is created on startup if it does not exist.
- `custom_config.trusted_hosts`: immediate peer IPs trusted to supply `X-Forwarded-For`.
- `custom_config.cors_allowed_host`: optional single CORS origin or host.
- `custom_config.cors_allowed_hosts`: optional array of CORS origins or hosts.

CORS allowlist values may be:

- Full origins, such as `https://guestbook.example`.
- Bare hosts, such as `guestbook.example`; any scheme and port from that host are accepted.
- `*`, which returns `Access-Control-Allow-Origin: *`.

`X-Forwarded-For` is only used for rate limiting when the TCP peer IP is in `trusted_hosts`. The first comma-separated address in `X-Forwarded-For` is used as the poster host.

## Storage Model

The API stores entries in a binary file named `ring_buffer.bin` below `app.document_root`.

Defaults compiled into the ring buffer:

- `max_entries`: 10,000
- `page_size`: 3 entries
- `handle` field storage: 32 bytes including null terminator
- `message` field storage: 128 bytes including null terminator

Read order is newest-to-oldest. `pageNumber=0` returns the newest page. When the buffer is full, new writes overwrite the oldest retained entries.

The HTTP write handler is stricter than the raw ring buffer and accepts:

- `handle`: string, 31 bytes or fewer
- `message`: string, 127 bytes or fewer

## API Specification

Base URL for local development:

```text
http://localhost:8098
```

### GET /read

Returns one page of guestbook entries.

Query parameters:

| Name | Type | Required | Description |
| --- | --- | --- | --- |
| `pageNumber` | unsigned integer | yes | Zero-based page index. `0` is the newest page. |

Successful response: `200 OK`

```json
{
  "pageNumber": 0,
  "entries": [
    {
      "handle": "alice",
      "message": "hello"
    }
  ]
}
```

Notes:

- `entries` contains at most 3 entries.
- Entries are ordered newest-to-oldest.
- `pageNumber=0` on an empty guestbook returns an empty `entries` array.

Error responses:

| Status | Body | Cause |
| --- | --- | --- |
| `400 Bad Request` | `invalid page number.` | Missing, non-numeric, or out-of-range `pageNumber`. |
| `500 Internal Server Error` | `internal server error.` | Unexpected storage or server failure. |

Example:

```bash
curl 'http://localhost:8098/read?pageNumber=0'
```

### POST /write

Appends a guestbook entry.

Request headers:

| Name | Required | Description |
| --- | --- | --- |
| `Content-Type: application/json` | yes | Required for JSON request parsing. |
| `X-Forwarded-For` | no | Used for rate limiting only when the immediate peer is trusted. |

Request body:

```json
{
  "handle": "alice",
  "message": "hello"
}
```

Request schema:

| Field | Type | Required | Limit |
| --- | --- | --- | --- |
| `handle` | string | yes | 31 bytes or fewer |
| `message` | string | yes | 127 bytes or fewer |

Successful response: `200 OK` with an empty body.

Error responses:

| Status | Body | Cause |
| --- | --- | --- |
| `400 Bad Request` | `invalid json.` | Malformed JSON, non-object JSON, missing fields, or non-string fields. |
| `400 Bad Request` | `handle or message too large.` | `handle` or `message` exceeds the HTTP API limit. |
| `429 Too Many Requests` | `too many requests.` | Same poster host made another `POST /write` inside the one-second rate-limit window. |
| `500 Internal Server Error` | `internal server error.` | Unexpected storage or server failure. |

Rate-limit note: the rate-limit check runs before JSON validation, so malformed `POST /write` attempts also consume the host's one-second write slot.

Example:

```bash
curl -X POST 'http://localhost:8098/write' \
  -H 'Content-Type: application/json' \
  -d '{"handle":"alice","message":"hello"}'
```

### OPTIONS /read and OPTIONS /write

Handles CORS preflight requests.

Successful response: `204 No Content`.

When the request `Origin` is allowed, the response includes:

| Header | Value |
| --- | --- |
| `Access-Control-Allow-Origin` | Request origin, or `*` when wildcard CORS is configured |
| `Access-Control-Allow-Methods` | `GET, POST, OPTIONS` |
| `Access-Control-Allow-Headers` | `Content-Type` |
| `Access-Control-Max-Age` | `86400` |
| `Vary` | `Origin` |

For disallowed or missing origins, no CORS headers are added.

## OpenAPI 3.1

```yaml
openapi: 3.1.0
info:
  title: slim_guestbook_api
  version: 1.0.0
  description: Minimal file-backed guestbook API.
servers:
  - url: http://localhost:8098
paths:
  /read:
    get:
      summary: Read a page of guestbook entries
      parameters:
        - name: pageNumber
          in: query
          required: true
          schema:
            type: integer
            minimum: 0
          description: Zero-based page index. Page 0 contains the newest entries.
      responses:
        '200':
          description: Page returned successfully.
          content:
            application/json:
              schema:
                $ref: '#/components/schemas/Page'
              examples:
                page:
                  value:
                    pageNumber: 0
                    entries:
                      - handle: alice
                        message: hello
        '400':
          description: Invalid or out-of-range page number.
          content:
            text/plain:
              schema:
                type: string
                const: invalid page number.
        '500':
          $ref: '#/components/responses/InternalServerError'
    options:
      summary: CORS preflight for read requests
      responses:
        '204':
          $ref: '#/components/responses/CorsPreflight'
  /write:
    post:
      summary: Append a guestbook entry
      requestBody:
        required: true
        content:
          application/json:
            schema:
              $ref: '#/components/schemas/WriteRequest'
            examples:
              entry:
                value:
                  handle: alice
                  message: hello
      responses:
        '200':
          description: Entry written successfully. Response body is empty.
        '400':
          description: Invalid JSON payload or oversized fields.
          content:
            text/plain:
              schema:
                type: string
                enum:
                  - invalid json.
                  - handle or message too large.
        '429':
          description: Same poster host exceeded the write rate limit.
          content:
            text/plain:
              schema:
                type: string
                const: too many requests.
        '500':
          $ref: '#/components/responses/InternalServerError'
    options:
      summary: CORS preflight for write requests
      responses:
        '204':
          $ref: '#/components/responses/CorsPreflight'
components:
  schemas:
    Entry:
      type: object
      required:
        - handle
        - message
      properties:
        handle:
          type: string
          maxLength: 31
          description: Guest handle. The server enforces this limit as UTF-8 bytes.
        message:
          type: string
          maxLength: 127
          description: Guest message. The server enforces this limit as UTF-8 bytes.
      additionalProperties: false
    Page:
      type: object
      required:
        - pageNumber
        - entries
      properties:
        pageNumber:
          type: integer
          minimum: 0
        entries:
          type: array
          maxItems: 3
          items:
            $ref: '#/components/schemas/Entry'
      additionalProperties: false
    WriteRequest:
      type: object
      required:
        - handle
        - message
      properties:
        handle:
          type: string
          maxLength: 31
          description: Guest handle. The server enforces this limit as UTF-8 bytes.
        message:
          type: string
          maxLength: 127
          description: Guest message. The server enforces this limit as UTF-8 bytes.
      additionalProperties: true
  responses:
    CorsPreflight:
      description: CORS preflight response. CORS headers are present only for allowed origins.
      headers:
        Access-Control-Allow-Origin:
          schema:
            type: string
        Access-Control-Allow-Methods:
          schema:
            type: string
            const: GET, POST, OPTIONS
        Access-Control-Allow-Headers:
          schema:
            type: string
            const: Content-Type
        Access-Control-Max-Age:
          schema:
            type: string
            const: '86400'
        Vary:
          schema:
            type: string
            const: Origin
    InternalServerError:
      description: Unexpected server failure.
      content:
        text/plain:
          schema:
            type: string
            const: internal server error.
```

## Ring Buffer CLI

The CLI opens the same binary file used by the server.

```bash
./ring_buffer_cli <ring-buffer-file> read <page-number>
./ring_buffer_cli <ring-buffer-file> write <handle> <message>
./ring_buffer_cli <ring-buffer-file> popLast
```

Examples:

```bash
./ring_buffer_cli ./ring_buffer.bin write alice hello
./ring_buffer_cli ./ring_buffer.bin read 0
./ring_buffer_cli ./ring_buffer.bin popLast
```

## Tests

```bash
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
```

The current test suite covers ring buffer behavior, post validation, page reads, CORS behavior, and rate limiting.
