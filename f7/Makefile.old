#CC           = avr-gcc
#CFLAGS       = -Wall -mmcu=atmega16 -Os -Wl,-Map,test.map
#OBJCOPY      = avr-objcopy
CC           = gcc
LD           = ld
AR           = ar
ARFLAGS      = rcs
CFLAGS       = -c -fPIC -maes -msse4.1
 
 
 
aes128.o : aes128.c 
	echo [CC] $(CFLAGS)  $< -o $@  
	$(CC) $(CFLAGS) $< -o $@

 
clean:
	rm -f *.OBJ *.LST *.o *.gch *.out *.hex *.map *.elf *.a

 
