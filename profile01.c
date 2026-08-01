#define _GNU_SOURCE
#include<stdio.h>
#include<string.h>
#include<time.h>
#include<stdlib.h>
#include <dlfcn.h>
#include <math.h>
#include<sched.h>
#include <ctype.h>
#include"header.h"

#include "cycles.h" // al3monni mod


/*
#define ITER 5000

void calc( unsigned char *key, unsigned char *pt, unsigned char *ct, 
           void (*op)(unsigned char *, unsigned char *, unsigned char *)) 
{
    op(key,pt,ct);
}

extern inline __attribute__((always_inline)) unsigned long rdtscp()
{
   unsigned long a, d, c;

   __asm__ volatile("rdtscp" : "=a" (a), "=d" (d), "=c" (c));

   return (a | (d << 32));
}

long int readreg(char *a)
{
  char str[100]= "rdmsr --decimal ";
  long int t1;
  FILE *fp;
  
  
   strcat(str,a); 
 
   fp = popen(str, "r");
   if (fscanf(fp, "%ld", &t1) == 1) {
 
        pclose(fp);
        return t1;
    } 

}
void get_rand_bytes(unsigned char *x, int n){
int i;

for(i=0;i<n;i++) x[i]=rand()&0xff;
}
*/
double get_double(char *str)
{
double value;
    /* First skip non-digit characters */
    /* Special case to handle negative numbers and the `+` sign */
    while (*str && !(isdigit(*str) || ((*str == '-' || *str == '+') && isdigit(*(str + 1)))))
        str++;

    /* The parse to a double */
    return strtod(str, NULL);
 
}

void extract(char *buff, double *e,double *t)
{

  if(strstr(buff,"Runtime unhalted [s]")!=NULL)
  {
      *t=get_double(buff);
  }
  
   if(strstr(buff,"Energy Core [J]")!=NULL)
  {
     *e=get_double(buff);
  } 
  
}

int main(int argc, char **argv)
{
  int i,j;
  char exec[100],ext[100],name[100];
  char buffer [1024];
  long e1,e2,t1,t2,enunit,s1,s2,eacc,tacc;
  double energy,time;
  unsigned char key[16],pt[16],ct[16];
  FILE *f;
  f=fopen("db.yaml","a");
  int cpu = atoi(argv[3]);
  cpu_set_t cpuset;
 
  CPU_ZERO(&cpuset);
 
  CPU_SET(cpu,&cpuset);
 
  sched_setaffinity(0, sizeof(cpuset), &cpuset);
 
  sprintf(name, "enc_s%02d_n%02d",atoi(argv[1]),atoi(argv[2]));
  sprintf(exec,"likwid-perfctr -f -g ENERGY ./internalprofile %d %d",atoi(argv[1]),atoi(argv[2]) );
  FILE *command = popen(exec,"r");
  while (fgets(buffer, sizeof(buffer), command) != NULL) {
        //puts(buffer);
        extract(buffer, &energy, &time);
  }
  /*
  sprintf(exec,"enc_s%02d_n%02d",atoi(argv[1]),atoi(argv[2]));
        
        
        void *handle;
        handle = dlopen(NULL, RTLD_NOW|RTLD_GLOBAL);
         if (handle == NULL) {
         fprintf(stderr, "%s\n", dlerror());
         return 1;
         }

  typedef void (*function)(unsigned char *, unsigned char *, unsigned char *);
  function func = (function) dlsym(handle, exec);
  if (func == NULL) {
    fprintf(stderr, "%s\n", dlerror());
    return 2;
  }
        
        puts(exec);
  		for(i = 0; i < ITER; i++)
  			{
  			get_rand_bytes(key,16);
  			get_rand_bytes(pt, 16);  			
  			
  			//e1    = readreg("0x611") ;
        		s1    = rdtscp();
  			     
    			calc(key,pt,ct, func);
    			
    			//e2    = readreg("0x611") ;
        		s2    = rdtscp();
        		tacc  += (s2-s1);
        		eacc  += (e2-e1);
  			}
       
*/
        fprintf(f,"name:   %s\n", name);
        fprintf(f,"   -slevel: %d\n", atoi(argv[1]));  
        fprintf(f,"   -index: %d\n",  atoi(argv[2]));              
        fprintf(f,"   -unhalted time: %f\n", time);     
        fprintf(f,"   -energy: %f\n",  energy); 

        fprintf(f,"   -function: encryption\n");   
        fprintf(f,"   -type: software\n\n");               

}
