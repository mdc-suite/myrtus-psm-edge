# use alpine as base image
FROM ubuntu:26.04 AS build-env
# install build-base meta package inside build-env container


RUN DEBIAN_FRONTEND=noninteractive \
  apt-get update \
  && apt-get install -y gcc \
  && apt-get install -y make \
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
# copy all files from current directory inside the build-env container
COPY . .
ENV LD_LIBRARY_PATH=:$PWD/LIB
RUN wget http://ftp.fau.de/pub/likwid/likwid-5.5.1.tar.gz
RUN tar -xaf likwid-5.5.1.tar.gz && cd likwid-5.5.1 && make && make install
 
 
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
