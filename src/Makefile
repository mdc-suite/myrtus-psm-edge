CC           = gcc
CFLAGS       = -w -Wno-incompatible-pointer-types -lcrypto -lssl -ldl -rdynamic

server :  encrypt02.c server_f.c
	$(CC) -o $@ $^ $(CFLAGS)
