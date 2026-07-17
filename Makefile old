CC           = gcc
LD           = ld
AR           = ar
ARFLAGS      = rcs
LIBF         =./LIB/
CFLAGS       = -w -Wno-incompatible-pointer-types -lcrypto -lssl  -L$(LIBF) -l_enc -ldl -rdynamic 
 
 
server :  encrypt02.c server_f.c
	$(CC) -o $@ $^ $(CFLAGS)  

 
clean:
	rm -f *.OBJ *.LST *.o *.gch *.out *.hex *.map *.elf *.a

 
