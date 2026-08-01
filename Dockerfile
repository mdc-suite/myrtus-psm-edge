# use alpine as base image
FROM ubuntu:26.04 AS build-env
# install build-base meta package inside build-env container


RUN DEBIAN_FRONTEND=noninteractive \
  apt-get update \
  #al3monni edit
  && apt-get install -y build-essential \
  #&& apt-get install -y gcc \ #old lines
  #&& apt-get install -y make \ #old lines
  && apt-get install -y openssl \
  && apt-get install -y libssl-dev \
  && apt-get install -y lsof \
  && apt-get install -y iputils-ping \
  && apt-get install -y wget \
  && apt-get install -y perl \
  && rm -rf /var/lib/apt/lists/*

  
#RUN gcc --version
# change directory to /app
WORKDIR /app

# --- al3monni mod to arm integration ---
ARG TARGETARCH
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
 # --- al3monni mod finish ---

# copy all files from current directory inside the build-env container
COPY . .

#ENV LD_LIBRARY_PATH=:$PWD/LIB                                                #old subhadeep line
#RUN wget http://ftp.fau.de/pub/likwid/likwid-5.5.1.tar.gz                    #old subhadeep line
#RUN tar -xaf likwid-5.5.1.tar.gz && cd likwid-5.5.1 && make && make install  #old subhadeep line
 
RUN gcc reset.c -o reset
RUN gcc -o generate gen.c
RUN gcc -o register register.c
RUN gcc -o synthesize synthesize.c -lm
RUN make server
RUN make -f makeclient
RUN gcc send1.c -o send

RUN chmod 777 start.sh
# use another container to run the program


 


ENTRYPOINT ["/app/start.sh"]
# at last run the program
CMD ["/app/start.sh"] 
