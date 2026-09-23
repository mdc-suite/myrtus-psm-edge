# al3monni/kria-ubuntu:22.04.5 is a custom docker image
# based on ubuntu 22.04.5 IotT with kria tools and libraries pre-installed
ARG BASE=al3monni/kria-ubuntu:22.04.5
FROM ${BASE} AS build-env

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

# download and build likwid-5.5.1 from source
RUN wget http://ftp.fau.de/pub/likwid/likwid-5.5.1.tar.gz
RUN tar -xaf likwid-5.5.1.tar.gz \
 && cd likwid-5.5.1 \
 && if [ "$TARGETARCH" = "arm64" ]; then \
      sed -i -e 's|^COMPILER *=.*|COMPILER = GCCARMv8#NO SPACE|' \
             -e 's|^ACCESSMODE *=.*|ACCESSMODE = perf_event#NO SPACE|' \
             -e 's|^BUILDDAEMON *=.*|BUILDDAEMON = false#NO SPACE|' \
             -e 's|^BUILDFREQ *=.*|BUILDFREQ = false#NO SPACE|' \
             config.mk \
      && grep -E '^(COMPILER|ACCESSMODE|BUILDDAEMON|BUILDFREQ)' config.mk ; \
    fi \
 && make && make install

ENV LD_LIBRARY_PATH=:/app/LIB

# copy all files from current directory inside the build-env container
COPY . .

RUN mkdir -p /app/LIB
RUN gcc reset.c -o reset
RUN gcc -o generate gen.c
RUN gcc -o register register.c
RUN gcc -o synthesize synthesize.c -lm
RUN make server
RUN make -f makeclient
RUN gcc send1.c -o send

# set permissions for start.sh script
RUN chmod 777 start.sh

# set the entrypoint to start.sh script
ENTRYPOINT ["/app/start.sh"]

# set the default command to run when the container starts
CMD ["/app/start.sh"] 
