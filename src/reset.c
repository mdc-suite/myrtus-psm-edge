#include<stdio.h>
#include<string.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include<stdlib.h>
int
xis_dir (const char *d)
{
    DIR *dirptr;

    if (access ( d, F_OK ) != -1 ) {
        // file exists
        if ((dirptr = opendir (d)) != NULL) {
            closedir (dirptr); /* d exists and is a directory */
        } else {
            return -2; /* d exists but is not a directory */
        }
    } else {
        return -1;     /* d does not exist */
    }

    return 1;
}
void main(){


FILE *f;
f=fopen("header.h","wb");
 
fprintf(f,"#ifndef _HEADER_H_\n");
fprintf(f,"#define _HEADER_H_\n");
fprintf(f,"\n\n");
fprintf(f,"///2-00\n");
fprintf(f,"\n\n");
fprintf(f,"///1-00\n");
fprintf(f,"\n\n");
fprintf(f,"///0-00\n");
fprintf(f,"\n\n");
fprintf(f,"//e\n");
fprintf(f,"\n\n");
fprintf(f,"#endif  \n");
fclose(f);
f=fopen("db.yaml","wb");
fclose(f);

if(xis_dir("LIB")==1){system("rm -rf ./LIB/*");}
else system("mkdir LIB");
 
}
