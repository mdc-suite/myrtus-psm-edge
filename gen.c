#include<stdio.h>
#include<string.h>
#include<time.h>
#include<stdlib.h>
#include<unistd.h>
#include <sys/stat.h>   // stat
#include <stdbool.h>    // bool type

bool file_exists (char *filename) {
  struct stat   buffer;   
  return (stat (filename, &buffer) == 0);
}
int main(int argc, char **argv)
{
    char str[1024],tmp[1024],df[1204],tf[1024],pf[1024],fn[1024],com1[4096] ,app[1024],string[4096],obj[1024] ;
    char libf[]= "./LIB";
    FILE *f = fopen("header.h","r"); 
    FILE *f1 = fopen("header1.h","w");
    FILE *f2 = fopen("wrapper.c","w"); 
    FILE *f3 = fopen("header.h","r");        
    int h,slevel;
    int opt, i ,cpu=0 ,rt;
    while((opt = getopt(argc, argv, "s:p:t:f:n:b:")) != -1)  
    {  
 
        switch(opt)  
        {  
              
            case 's':
                slevel = atoi(optarg);
                printf("security level: %d\n", slevel );  
                break;
            case 'p':  
                strcpy(df,optarg);
                printf("Makefile PATH %s\n", df);  
                break;  
                
            case 't':  
                strcpy(pf,optarg);
                printf("Toplevel file PATH %s\n", pf);  
                break;      
                
            case 'f':  
                strcpy(tf,optarg);
                printf("Toplevel file %s\n", tf);  
                break; 
                
            case 'n':
                strcpy(fn,optarg);
                printf("Function name %s\n", fn);  
                break; 
            case 'b':
                strcpy(obj,optarg);
                printf("Obj name %s\n", obj);  
                break;     
       
             default: /* '?' */
               fprintf(stderr, "Usage: %s [-s 0/1/2 for low/medium/high security level] [-p MAKEFILE PATH] [-t toplevel file PATH]  [-f toplevel filename] [-n function name] [-b object name]\n",
                           argv[0]);
                   exit(EXIT_FAILURE);
        }  
    }  
    
   strcpy(com1, df);
   strcat(com1, "/Makefile");
   printf("1) ");
   puts(com1);
   if(!file_exists(com1)){ fprintf(stderr, "Makefile does not exist!\n");return 2;}
 
 
   strcpy(com1, pf);
   strcat(com1, "/"); 
   strcat(com1, tf);
   printf("2) ");
   puts(com1);
   //if(system(com1))
   if(!file_exists(com1)) { fprintf(stderr, "Toplevel file does not exist!\n");return 2;}   
   
   strcpy(com1,"grep ");
   strcat(com1, fn);
   strcat(com1," ");

   strcat(com1, pf);
   strcat(com1, "/"); 
   strcat(com1, tf); 
   strcat(com1, "> /dev/null");   
   printf("3) ");
   puts(com1);
   if(system(com1)) { fprintf(stderr, "Function does not exist in toplevel file !\n");return 3;} 
 


     if (f != NULL) {
        // Read each line from the file and store it in the
        // 'line' buffer.
        while (fgets(str, sizeof(str), f)) {
            // Print each line to the standard output.
            
            if (!strncmp(str,"///",3) && str[3]-'0'==slevel) {
                h=atoi(str+5);
                h++;
                //sprintf(tmp,"%d",++h);
                //str[6]=0;
                //strcat(str,tmp);
                //fprintf(f1,"%s", str);
                
            }
            else if(!strncmp(str,"//e",3)){
            //fprintf(f1,"//e\nvoid enc_s%02d_n%02d (unsigned char *, unsigned char *, unsigned char *); \n//\n", slevel,h); 
            sprintf(app,"enc_s%02d_n%02d", slevel,h);
            
            //preprocess file to source
            
            sprintf(string, "#define %s enc_s%02d_n%02d ", fn,slevel,h);
            //strcat(string, fn);
 
            fprintf(f2,"%s\n",string);
            sprintf(string, "#include \"%s/%s\"", pf,tf);
 
            fprintf(f2,"%s\n",string); 
            fclose(f2);    
            system("gcc -E -P wrapper.c > source.c");
            sprintf(com1, "cp source.c %s/source.c",pf);
            system(com1);
            printf("wrapper to source done\n");                      
            // update makefile
            strcpy(com1,"sed  \'s/");
            strcat(com1, tf);strcat(com1,"/");
            strcat(com1, "source.c"); strcat(com1,"/g\' "); //strcat(com1,df);
            strcat(com1, df); strcat(com1, "/Makefile");
            strcat(com1, "| tee ");
            strcat(com1, df); strcat(com1, "/Makefile_new > /dev/null");
            puts(com1);
            system(com1);
             }
            //else 
            //fprintf(f1,"%s", str);
        }
 
        // Close the file stream once all lines have been
        // read.
        fclose(f);
       // fclose(f1);  
    }
    else {
        // Print an error message to the standard error
        // stream if the file cannot be opened.
        fprintf(stderr, "Unable to open file!\n");
    }
   
    
   //make objfile 
   
   sprintf(com1, "make %s -C %s -f Makefile_new",obj,df); 
   system(com1);
   
   //move objfile to libfolder under a different name
   sprintf(com1, "cp %s/%s %s/%s.o", df,obj,libf,app);
   puts(com1);
   system (com1);
   
   //compile profiler with objectfile
   
   sprintf(com1,"gcc -o internalprofile internalprofile.c %s/%s.o -ldl -rdynamic",libf,app);
   printf("profile command : ");
   puts(com1);
   system(com1);
   
   sprintf(com1,"gcc -o profile profile01.c");
   printf("profile command : ");
   puts(com1);
   system(com1);
   
   //run profiler
   sprintf(com1, "./profile %d %d %d",slevel,h,cpu);

   
   //rt=system(com1); //old command

   //al3monni mod start
    #if defined(__aarch64__)
        rt = 0;   // al3monni: skip likwid profiling on aarch64 (hangs under QEMU, MSR/energy unavailable on Kria too). Force success so header.h manifest below still updates.
    #else
        rt=system(com1);
    #endif
    //al3monni mod end
 
   if(!rt){
   
       if (f3 != NULL) {
        // Read each line from the file and store it in the
        // 'line' buffer.
        while (fgets(str, sizeof(str), f3)) {
            // Print each line to the standard output.
            
            if (!strncmp(str,"///",3) && str[3]-'0'==slevel) {
                h=atoi(str+5);
                sprintf(tmp,"%d",++h);
                str[6]=0;
                strcat(str,tmp);
                fprintf(f1,"%s\n", str);
                
            }
            else if(!strncmp(str,"//e",3)){
            fprintf(f1,"//e\nvoid enc_s%02d_n%02d (unsigned char *, unsigned char *, unsigned char *); \n//\n", slevel,h); 
            
             }
            else 
              fprintf(f1,"%s", str);
        }
         
        // Close the file stream once all lines have been
        // read.
        fclose(f3);
        fclose(f1);  
    }
   
   }
   system("rm header.h");
   system("mv header1.h header.h");  
   
   
  return 0;
}

 
