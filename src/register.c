#include<stdio.h>
#include<string.h>
#include<time.h>
#include<stdlib.h>
#include<unistd.h>
#include <sys/stat.h>    
#include <stdbool.h>  
#include <sys/types.h>
#include <dirent.h>

char* t2s(char *in){
while (*in!=' ') in++;
in++;
while (*in!=' ') in++;
in++;
return in;
}
char* lf(char *in){
char *t;
t=in;
while (*t != 10) t++;
  *t=0;
  return in;
 
}
char* trim(char *in){
while (*in==' ' || *in == '-') in++;
return in;
}
char* extract(char *in)
{
  char *t;
  while (*in != ':')in++;
  in++;
  t=in;
  while (*t != 10) t++;
  *t=0;
  return in;
}
bool file_exists (char *filename) {
  struct stat   buffer;   
  return (stat (filename, &buffer) == 0);
}
int isDirectoryEmpty(char *dirname) {
  int n = 0;
  struct dirent *d;
  DIR *dir = opendir(dirname);
  if (dir == NULL) //Not a directory or doesn't exist
    return 1;
  while ((d = readdir(dir)) != NULL) {
    if(++n > 2)
      break;
  }
  closedir(dir);
  if (n <= 2) //Directory Empty
    return 1;
  else
    return 0;
}
bool collide (void) {
   char *tmp,*tmp1;
   int r;
   bool rv=0;
   char com[1024],string[1024];
   srand(time(NULL));
   system("nm --defined-only ./LIB/*.o > stabpresent.txt");
   FILE *fl=fopen("stab.txt","rb");
   
     if(fl!=NULL){
       while (fgets(string, sizeof(string), fl)) {   
 
      tmp=t2s(string);
 
      tmp1=lf(tmp); 
      sprintf(com,"grep -E '(^|\\s)%s($|\\s)' stabpresent.txt",tmp1);
      if(!system(com)){
         rv=1;r=rand();
         printf("Can not proceed with registration!! Symbol %s already exists !!!\n",tmp1);
         sprintf(com,"grep %s%x stabpresent.txt",tmp1,r);
         if(system(com))printf("Suggest changing to %s%x \n",tmp1,r);
      }
       
      
 
     }
     fclose(fl); 
  }
 return rv;
}
int main(int argc, char **argv){
    char str[1024], df[1204],tf[1024],pf[1024],fn[1024],com1[8192],com2[8192] ,app[1024],app1[1024],string[1024],obj[1024],te[2048] ;
    int  slevel,rt;
    char *tmp,*tmp1;
    int opt, i  ;
     bool rval;
 while((opt = getopt(argc, argv, "c:")) != -1)  
    {  
 
        switch(opt)  
        {  
              
   
            case 'c':  
                strcpy(str,optarg);
                printf("Toplevel file %s\n", str);  
                break; 
                
          
       
             default: /* '?' */
               fprintf(stderr, "Usage: %s [-c config file]  \n",
                           argv[0]);
                   exit(EXIT_FAILURE);
        }  
    }  
    
    FILE *f=fopen(str,"rb"); 

    
       if(f!=NULL){
       while (fgets(string, sizeof(string), f)) {   
 
      tmp=trim(string);
 
      tmp1=trim(extract(tmp)); 
 
       
      if(strncmp((tmp),"Security_level",14)==0) {slevel=atoi(tmp1); }//printf("slevel=%d\n",slevel);}
      if(strncmp((tmp),"Makefile_Path",13)==0)  {strcpy(df,tmp1); printf("%s\n",df);}
      if(strncmp((tmp),"Toplevel_file_PATH",18)==0) {strcpy(tf,tmp1);}// printf("%s\n",tf); }  
      if(strncmp((tmp),"Toplevel_file",13)==0)      {strcpy(pf,tmp1);}// printf("%s\n",pf);}
      if(strncmp((tmp),"Function_name",13)==0)      {strcpy(fn,tmp1);}// printf("%s\n",fn);}
      if(strncmp((tmp),"Object_name",11)==0)        {strcpy(obj,tmp1);}//printf("%s\n",obj); }
 
   }
     fclose(f); 
}
 
  sprintf(app,"check%d.c",slevel);

  FILE *g=fopen(app,"rb");
  sprintf(app1,"test%d.c",slevel);  
  FILE *h=fopen(app1,"wb");  
  sprintf(com1,"./generate -s %d -p %s -t %s -f %s -n %s -b %s",slevel, df,tf,pf,fn,obj);  
  //puts(com1);
  
  // check if makefile exists
   strcpy(com2, df);
   strcat(com2, "/Makefile");
   printf("1) ");
   puts(com2);
   if(!file_exists(com2)){ fprintf(stderr, "Makefile does not exist!\n");return 2;}
 
    // check if toplevel file exists
   strcpy(com2, tf);
   strcat(com2, "/"); 
   strcat(com2, pf);
   printf("2) ");
   puts(com2);
 
   if(!file_exists(com2)) { fprintf(stderr, "Toplevel file does not exist!\n");return 2;}   
   
   
   // check if function exists in toplevel file 
   strcpy(com2,"grep ");
   strcat(com2, fn);
   strcat(com2," ");

   strcat(com2, tf);
   strcat(com2, "/"); 
   strcat(com2, pf); 
   strcat(com2, "> /dev/null");   
   printf("3) ");
   puts(com2);
   if(system(com2)) { fprintf(stderr, "Function does not exist in toplevel file !\n");return 3;}   
   
   
  //test if make command runs 
   sprintf(com2,"rm -rf %s/*.o",df);
   system(com2);
   sprintf(com2, "make %s -C %s",obj,df); 
   rt=system(com2);
   if(rt) { fprintf(stderr, "Make command failed!! objfile could not be created ! make sure make %s creates %s\n",obj,obj );return 4;}   
 
     //test if implementation is correct
   
    if(g!=NULL){
       while (fgets(string, sizeof(string), g)) {   
 
      if(strncmp(string,"    // insert_func here",23)==0)    
      {
          sprintf(te,"%s(PT, Key, CT);", fn);
          fprintf(h,"%s", te);
      }
       else 
      fprintf(h,"%s", string);
 
   }
     fclose(g);      fclose(h); 
 }
 
   
   sprintf(com2,"gcc %s %s/%s -Wno-implicit-function-declaration -o test",app1,df,obj);
   puts(com2);
   system(com2);
   rt=system("./test");
   
   if(rt)  { fprintf(stderr, "TEST FAILED !!!!\n");return 5;}
   
   sprintf(com2,"nm --defined-only %s/%s > stab.txt",df,obj);
   system(com2);
   
   if(!isDirectoryEmpty("./LIB"))
   {
        
        rval = collide();
        
        
   }
   
      
   sprintf(com2,"rm -rf %s/*.o",df);
   system(com2);
   
   ///

   if(rval==0)
   {  system("gcc -o generate gen.c");
      system(com1);
   }
   
}
