#CC           = avr-gcc
#CFLAGS       = -Wall -mmcu=atmega16 -Os -Wl,-Map,test.map
#OBJCOPY      = avr-objcopy
CC           = gcc
LD           = ld
AR           = ar
ARFLAGS      = rcs
CFLAGS       = -c -fPIC
 
 

default: aes256.elf

.SILENT:
.PHONY:  lint clean

 

test : test.c aes256.o
	echo [CC] -o $@  $^
	$(CC) -o $@ $^

aes256.o : aes256.c aes256.h
	echo [CC] $(CFLAGS)  $< -o $@  
	$(CC) $(CFLAGS) $< -o $@


aes.a : aes256.o
	echo [AR] $@
	$(AR) $(ARFLAGS) $@ $^

lib : aes.a

clean:
	rm -f *.OBJ *.LST *.o *.gch *.out *.hex *.map *.elf *.a

testnow:
	make clean && make test && ./test
 

lint:
	$(call SPLINT)
