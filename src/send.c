#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
void sig_send_val ( pid_t id, int signo, int val )
{
union sigval *sigdata;

sigdata = malloc ( sizeof ( union sigval ) );
sigdata -> sival_int = val;
printf("sending %d to %d\n", val, id);
sigqueue ( id, signo, *sigdata );

free ( sigdata );
}

int main(int argc, char **argv)
{
char line[1024],str[1024];

sprintf(str,"lsof -n -ti :%d", atoi(argv[1]));
puts(str);
FILE * command = popen(str,"r");
fgets(line,1024,command);
puts(line);
pid_t pid = strtoul(line,NULL,10);
sig_send_val ( pid, SIGUSR1, atoi(argv[2]) );
}
