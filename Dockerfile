FROM alpine:latest

# Install runtime dependencies
RUN apk add --no-cache gcc make musl-dev linux-headers

COPY . /opt/etherdfs
WORKDIR /opt/etherdfs

# Build and install
RUN make && cp ethersrv /usr/local/bin/ && chmod +x /usr/local/bin/ethersrv

# Cleanup
RUN apk del gcc make musl-dev linux-headers && rm -rf /opt/etherdfs

RUN mkdir -p /data
VOLUME /data

# Logging and startup sequence
CMD ["sh", "-c", "\
    echo '[INIT] Starting EtherDFS Docker container...'; \
    echo '[INIT] Cleaning up old lock files...'; \
    rm -f /var/run/ethersrv.lock; \
    if [ -z \"$0\" ] || [ -z \"$1\" ]; then \
        echo '[ERROR] Missing arguments! Usage: <interface> <path>'; \
        exit 1; \
  fi; \
    echo \"[INIT] Interface selected: $0\"; \
    echo \"[INIT] Data path selected: $1\"; \
    echo '[EXEC] Launching ethersrv...'; \
    exec /usr/local/bin/ethersrv -f \"$0\" \"$1\" \
"]