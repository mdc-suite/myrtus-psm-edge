CC           = gcc
LD           = ld
LDFLAGS      = -r
ARFLAGS      = rcs
CFLAGS       = -c -fPIC -DUNROLL_TRANSPOSE
src = aes.c bs.c key_schedule.c
 

 
obj    = $(src:.c=.o)

lib: $(src)
	$(CC) $(CFLAGS) $^


aes256.o: $(obj)
	$(LD) $(LDFLAGS) -o $@ $(obj)  

 
clean:
	rm -f $(obj) aes256.o
