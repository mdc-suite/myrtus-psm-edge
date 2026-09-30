#CC           = avr-gcc
#CFLAGS       = -Wall -mmcu=atmega16 -Os -Wl,-Map,test.map
#OBJCOPY      = avr-objcopy
CC           = gcc
LD           = ld
AR           = ar
ARFLAGS      = rcs
CFLAGS       = -c -fPIC
 
 

default: aes128.elf

.SILENT:
.PHONY:  lint clean

 

test : test.c aes128.o
	echo [CC] -o $@  $^
	$(CC) -o $@ $^

aes128.o : aes128.c aes128.h
	echo [CC] $(CFLAGS)  $< -o $@  
	$(CC) $(CFLAGS) $< -o $@


aes.a : aes128.o
	echo [AR] $@
	$(AR) $(ARFLAGS) $@ $^

lib : aes.a

clean:
	rm -f *.OBJ *.LST *.o *.gch *.out *.hex *.map *.elf *.a

testnow:
	make clean && make test && ./test
 

lint:
	$(call SPLINT)
