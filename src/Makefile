CC           = gcc
LD           = ld
AR           = ar
ARFLAGS      = rcs
LIBF         =./LIB/
# al3monni mod
CFLAGS       = -w -Wno-incompatible-pointer-types -lcrypto -lssl -ldl -rdynamic
#CFLAGS       = -w -Wno-incompatible-pointer-types -lcrypto -lssl  -L$(LIBF) -l_enc -ldl -rdynamic 
 
 
server :  encrypt02.c server_f.c
	$(CC) -o $@ $^ $(CFLAGS)  

 
clean:
	rm -f *.OBJ *.LST *.o *.gch *.out *.hex *.map *.elf *.a

 
