# Base image: stock Ubuntu 22.04 on both architectures (arm64 and amd64).
# The component needs nothing from the Kria userspace: the INA260 is read from
# /sys/class/hwmon, which the privileged container sees. See BASEIMAGE.md.
FROM ubuntu:22.04 AS build-env

# install build-essential, openssl, libssl-dev, lsof, iputils-ping, wget, perl
# buld-essential meta package includes gcc, g++, make, libc-dev, etc.
RUN DEBIAN_FRONTEND=noninteractive \
  apt-get update \
  && apt-get install -y build-essential \
  && apt-get install -y openssl \
  && apt-get install -y libssl-dev \
  && apt-get install -y lsof \
  && apt-get install -y iputils-ping \
  && apt-get install -y wget \
  && apt-get install -y perl \
  && rm -rf /var/lib/apt/lists/*

# set the working directory inside the build-env container
WORKDIR /app

# set the TARGETARCH build argument to the architecture of the target platform
ARG TARGETARCH

# download and build likwid-5.5.1 from source, on x86-64 only: likwid measures energy
# through the RAPL registers (profile01.c); on aarch64 the INA260 does, and likwid is unused
RUN if [ "$TARGETARCH" != "arm64" ]; then \
      wget http://ftp.fau.de/pub/likwid/likwid-5.5.1.tar.gz \
      && tar -xaf likwid-5.5.1.tar.gz \
      && cd likwid-5.5.1 \
      && make && make install ; \
    fi

ENV LD_LIBRARY_PATH=:/app/LIB

# lay the sources out flat in /app, as the pipeline expects them:
# start.sh, register, gen and the backends' config.txt all use paths relative to /app
COPY src/ ./
COPY backends/ ./
COPY certs/ ./
COPY test/rfile ./
COPY tools/ina260_test.c ./

RUN mkdir -p /app/LIB
RUN gcc reset.c -o reset
RUN gcc -o generate gen.c
RUN gcc -o register register.c
RUN gcc -o synthesize synthesize.c -lm
RUN make server
RUN make -f makeclient
RUN gcc send.c -o send

# set permissions for start.sh script
RUN chmod 777 start.sh

# set the entrypoint to start.sh script
ENTRYPOINT ["/app/start.sh"]

# set the default command to run when the container starts
CMD ["/app/start.sh"] 
