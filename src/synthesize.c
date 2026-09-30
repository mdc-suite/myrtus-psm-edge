#include<stdio.h>
#include<string.h>
#include<time.h>
#include<stdlib.h>
#include<unistd.h>
#include <sys/stat.h>   // stat
#include <stdbool.h> 
#include <math.h> 
#define fbits(y)  ((y) & 0xc0)>>6
#define sbits(y)  ((y) & 0x30)>>4
#define ibits(y)  ((y) & 0x0f) 
#define fetch(mode)      slevel= sbits(mode);   num=ibits(mode); \
                         sprintf(buf,"enc_s%02d_n%02d",slevel,num);    \
                         function op = (function) dlsym(cx->handle, buf);  \
                         if (op == NULL) {                             \
                                           fprintf(stderr, "%s\n", dlerror());\
                                          return -1;\
                         }

enum Type {
  SOFTWARE,
  HARDWARE
}; 

enum Function {
  ENCRYPTION,
  HASHING
}; 
#define u8 unsigned char
#define u32 unsigned int

typedef struct{
char name[1024];
u8 slevel;
u32 index;
float time;
float energy;
enum Function ifunc;
enum Type itype;
} entry;

typedef struct{
float time;
float energy;
} base;

char* trim(char *in){
while (*in==' ' || *in == '-') in++;
return in;
}

char* extract(char *in)
{
  while (*in != ':')in++;
  in++;
  return in;
}
enum Type tex(char *in)
{
      if (strncmp(in,"hardware",8)==0) return HARDWARE;
      if (strncmp(in,"software",8)==0) return SOFTWARE;
}

enum Function fex(char *in)
{      
      if (strncmp(in,"encryption",10)==0)  return ENCRYPTION; 
      if (strncmp(in,"hashing",7)==0)return HASHING;
}

enum Type argex(char *in)
{
      if (*in=='e') return ENCRYPTION;
      if (*in=='h') return HASHING;
}


int main(int argc, char **argv)
{  
   int h  ;
   int opt ;
   entry *Dict;
   base bp;
   float *dist;
   char string[1024],scode[1024];
   char df[1024],*tmp,*tmp1;
   float maxt,mint,maxe,mine,mind;
   int i=-1,j,k,count=0,id,imp,code;
   u8 tp,en,slevel,plot; 
   FILE *g=fopen("db.yaml","rb"); 
   FILE *f=fopen("db.yaml","rb");

   enum Type fn;
   char com[1024],com1[1024],com2[1024];
   
   if(g!=NULL){
   while (fgets(string, sizeof(string), g)) {
 
            if(strncmp((string),"name",4)==0)count++;
 
        }
   fclose(g);
   }
 

   //printf("total entry =%d\n",count);
   Dict = (entry *) malloc(count*sizeof(entry));
   dist = (float *) malloc(count*sizeof(float));  
   if(f!=NULL){
   while (fgets(string, sizeof(string), f)) {   
 
      tmp=trim(string);
 
      tmp1=trim(extract(tmp)); 
 
       
      if(strncmp((tmp),"name",4)==0)      {i++;
                                           strcpy(Dict[i].name,tmp1);}
      if(strncmp((tmp),"slevel",6)==0)   Dict[i].slevel=atoi(tmp1);   
      if(strncmp((tmp),"index",5)==0)    Dict[i].index=atoi(tmp1);        
      if(strncmp((tmp),"unhalted time",6)==0)   Dict[i].time=atof(tmp1);  
      if(strncmp((tmp),"energy",6)==0)   Dict[i].energy=atof(tmp1);    
      if(strncmp((tmp),"function",8)==0) Dict[i].ifunc=  fex(tmp1) ;              
      if(strncmp((tmp),"type",4)==0)     Dict[i].itype=  tex(tmp1) ;                 
      
   }
     fclose(f); 
}
 
 
   plot=0;
    while((opt = getopt(argc, argv, "s:e:t:f:p")) != -1)  
    {  
 
        switch(opt)  
        {  
              
            case 's':
                slevel = atoi(optarg);
                //printf("security level: %d\n", slevel );  
                break;
            case 'f':  
                strcpy(df,optarg);
                fn =argex(df);
                //printf("Function %d\n", fn);  
                break;  
                
            case 't':  
                tp = atoi(optarg);
                //printf("Throughput %d\n", tp);  
                break;      
                
            case 'e':  
                en = atoi(optarg);
                //printf("Energy %d\n", en);  
                break; 
                
             case 'p':  
                plot=1;
                //printf("Plot %d\n", plot);  
                break; 
               
       
             default: /* '?' */
               fprintf(stderr, "Usage: %s [-s 0/1/2 for low/medium/high security level] [-f Function] [-t 0/1/2 for Throughput]  [-e 0/1/2 for Energy]\n",
                           argv[0]);
                   exit(EXIT_FAILURE);
        }  
    }  
   
  
  //synthesize
 
  maxe=0;mine =10; maxt=0; mint=10000;
  for(j=0;j<count;j++)if(Dict[j].ifunc == fn && Dict[j].slevel == slevel )
  {
     if(maxe< Dict[j].energy)maxe= Dict[j].energy;
     if(mine> Dict[j].energy)mine= Dict[j].energy;  
     if(maxt< Dict[j].time)maxt= Dict[j].time;
     if(mint> Dict[j].time)mint= Dict[j].time;  
  }
  bp.time =  (tp==0) * mint + (tp==1) * ((mint+maxt)/2.0 ) + (tp==2)* maxt;
  bp.energy =  (en==0) * mine + (en==1) * ((mine+maxe)/2.0 ) + (en==2)* maxe;

    //printf("maxe=%f mine=%f maxt=%f mint=%f\n",  maxe,mine,maxt,mint);  
  for(j=0;j<count;j++)
  {
     if(Dict[j].ifunc == fn && Dict[j].slevel == slevel )
        
        dist[j] = pow((bp.time - Dict[j].time)/ (maxt-mint), 2)+pow((bp.energy - Dict[j].energy)/ (maxe-mine), 2) ;
        
     else
          dist[j]=10000;
  }
  mind=10000; id=0;

  for(j=0;j<count;j++)
     if(mind> dist[j]) {mind= dist[j];id=j;} 
  //printf("Solution = %d\n",id);
  puts(Dict[id].name); 

  imp=atoi(Dict[id].name+9);
  code = (fn==ENCRYPTION)*64 + (fn==HASHING)*128+  slevel*16 +imp;
  sprintf(scode,"./send %d %d", 5544+2-slevel , code);
  puts(scode);
  system(scode);
    if(plot){
  
      sprintf( com, "set object circle at first %f,%f size scr 0.01 fillcolor rgb 'green’ fillstyle solid",bp.energy,bp.time);
      sprintf( com1,"set object circle at first %f,%f size scr 0.01 fillcolor rgb 'blue’ fillstyle solid",Dict[id].energy,Dict[id].time);
      sprintf( com2,"plot 'data.temp' using (%f):(%f):($1-%f):($2-%f) with vectors nohead, \
                                           'data.temp' with points pt \"\\U+2299\"",bp.energy,bp.time,bp.energy,bp.time);
      FILE * temp = fopen("data.temp", "w");
      char * commandsForGnuplot[] = {"set title \"SYNTHESIS PLOT\"", 
                                     "set xlabel \" Energy (Joules)\"",
  				     "set ylabel \" CPU time\"",
  				     "set autoscale xfixmin",
  				     "set autoscale xfixmax", 
  				     "set autoscale yfixmin",
  				     "set autoscale yfixmax",  
  				     com,com1,com2
                                     };

    FILE *gnuplot = popen("gnuplot -persistent", "w");
    for (i=0; i < count; i++)
    {
    fprintf(temp, "%f %f \n", Dict[i].energy, Dict[i].time); //Write the data to a temporary file
    }
 
    for (i=0; i < 10; i++)
    {
    fprintf(gnuplot, "%s \n", commandsForGnuplot[i]); //Send commands to gnuplot one by one.
    }

 
 
fflush(gnuplot);
  }
 }  
   


