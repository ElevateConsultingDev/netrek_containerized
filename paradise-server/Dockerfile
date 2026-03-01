# NetrekII Paradise server container
FROM debian:11

ENV DEBIAN_FRONTEND=noninteractive

EXPOSE 2592 2591

RUN apt-get update && \
    apt-get install -y \
    autoconf \
    build-essential \
    libncurses5-dev \
    procps \
    zlib1g-dev && \
    apt-get clean && \
    rm -rf /var/lib/apt/lists/*

COPY submodules/netrek-server-paradise /usr/local/src/netrek-server-paradise

WORKDIR /usr/local/src/netrek-server-paradise

# -fcommon works around multiple-definition errors in legacy C code
# (GCC 10+ defaults to -fno-common)
RUN autoconf && \
    CFLAGS="-g -O2 -fcommon" ./configure --prefix=/usr/local/netrek && \
    make

RUN make install || true

# Verify the key binary was installed
RUN test -f /usr/local/netrek/bin/listen || \
    (echo "ERROR: listen binary not installed" && exit 1)

ENV NETREKDIR=/usr/local/netrek

RUN mkdir -p "${NETREKDIR}/logs"

COPY paradise-server/entrypoint.sh /usr/local/bin/entrypoint.sh
RUN chmod +x /usr/local/bin/entrypoint.sh

ENTRYPOINT ["/usr/local/bin/entrypoint.sh"]
