#CC           = avr-gcc
#CFLAGS       = -Wall -mmcu=atmega16 -Os -Wl,-Map,test.map
#OBJCOPY      = avr-objcopy
CC           = gcc
LD           = ld
AR           = ar
ARFLAGS      = rcs
# al3monni mod: pick the AES instruction set of the build machine, so the
# backend builds both on the Kria (ARMv8 Crypto Extensions) and on x86-64
# (AES-NI). aes256.c selects the matching implementation with #if.
ARCH         := $(shell uname -m)
ifeq ($(ARCH),aarch64)
CFLAGS       = -c -fPIC -march=armv8-a+crypto
else
CFLAGS       = -c -fPIC -maes -msse4.1
endif
 
 
 
aes256.o : aes256.c 
	echo [CC] $(CFLAGS)  $< -o $@  
	$(CC) $(CFLAGS) $< -o $@

 
clean:
	rm -f *.OBJ *.LST *.o *.gch *.out *.hex *.map *.elf *.a
