#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <signal.h>
#include <stdlib.h>
#include "crypto_aead256.h"
#include <time.h>
#include <sys/stat.h>    
#include <stdbool.h>  


#define sbits(y)  ((y) & 0x30)>>4
#define ibits(y)  ((y) & 0x0f) 

#define CHUNK_SIZE 1024
#define TAGSIZE 16

bool file_exists (char *filename) {
  struct stat   buffer;   
  return (stat (filename, &buffer) == 0);
}

 volatile int mode=98;

void signal_handler(int signo, siginfo_t *info, void *context) {
 
 
    int received_val = info->si_value.sival_int;
    mode = received_val;    
    printf("\nRecieved signal %d\n",mode);
    printf("Switching to enc_s%02d_n%02d\n", sbits(mode),ibits(mode));  
}
void sig_set_handler ( int signo, void *handler )
{
struct sigaction *act;
act = malloc ( sizeof ( struct sigaction ) );
act -> sa_sigaction = handler;
act -> sa_flags = SA_SIGINFO|SA_RESTART;

sigaction ( signo, act, NULL );
}


void configure_context(SSL_CTX *ctx)
{
    /* Set the key and cert */
    if (SSL_CTX_use_certificate_file(ctx, "certfile.crt", SSL_FILETYPE_PEM) <= 0) {
        ERR_print_errors_fp(stderr);
        exit(EXIT_FAILURE);
    }

    if (SSL_CTX_use_PrivateKey_file(ctx, "keyfile.key", SSL_FILETYPE_PEM) <= 0 ) {
        ERR_print_errors_fp(stderr);
        exit(EXIT_FAILURE);
    }
}

void createserver(int port)
{
    int sock,part,kbyte=0;
    SSL_CTX *ctx;
    unsigned char buffer [CHUNK_SIZE] ;size_t  readbytes;
    unsigned char iv[16],tag[2*TAGSIZE],*tp;    EVP_CIPHER *evp = NULL;
    unsigned char key[32];EVP_CIPHER_CTX *ctxx;
    unsigned char *outmsg;
    char filename[1024];
    int outlen =0,ret;
    int ct=0;
    FILE *fd;
    EDcontext *cx;    
    time_t start_time,curr_time;
    srand(time(NULL));
    /* al3monni mod: start each port on a backend of its own security level. The client
     * encrypts with AES-256-GCM on 5544 and AES-128-GCM on 5545 (cltest.c); upstream
     * started both processes on 98 = enc_s02_n02, so 5545 decrypted with AES-256. */
    mode = (port == 5544) ? 0x62 : 0x52;   /* enc_s02_n02 / enc_s01_n02 */
    ctx = SSL_CTX_new(TLS_server_method());
    if (!ctx) {
        perror("Unable to create SSL context");
        ERR_print_errors_fp(stderr);
        exit(EXIT_FAILURE);
    }
	SSL_CTX_set_min_proto_version(ctx, TLS1_3_VERSION);
	SSL_CTX_set_max_proto_version(ctx, TLS1_3_VERSION);    
	 
    configure_context(ctx);

    struct sockaddr_in addr;

    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("Unable to create socket");
        exit(EXIT_FAILURE);
    }
    unsigned int a = 655350000; 
    if (setsockopt(sock, SOL_SOCKET, SO_RCVBUFFORCE, &a, sizeof(unsigned int)) == -1) {
    fprintf(stderr, "Error setting socket opts: %s\n", strerror(errno));
}
    if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("Unable to bind");
        exit(EXIT_FAILURE);
    }

    if (listen(sock, 1) < 0) {
        perror("Unable to listen");
        exit(EXIT_FAILURE);
    }
    while(1) {
        struct sockaddr_in addr;
        unsigned int len = sizeof(addr);
        SSL *ssl;

        int client = accept(sock, (struct sockaddr*)&addr, &len);
        if (client < 0) {
            perror("Unable to accept");
            exit(EXIT_FAILURE);
        }

        ssl = SSL_new(ctx);
        SSL_set_fd(ssl, client);

        SSL_set_default_read_buffer_len( ssl, 262144000);
        if (SSL_accept(ssl) <= 0) {
            ERR_print_errors_fp(stderr);
        } else {            

            const char *klabel = "EXPORTER-my_ekm";
            unsigned char *out = (unsigned char*)malloc(64 * sizeof(unsigned char));
            do{
            sprintf(filename,"./Downloads/filename-ekm%d",rand()&0xffff);
            }while(file_exists(filename));
            fd=fopen(filename,"wb");
            const char *context = "mycontext";
	    if(port==5544) printf("Security Level high\n"); else printf("Security level low\n");
            printf("Started handshake\n" );
            int rs = SSL_export_keying_material(ssl,out, 64 , klabel, strlen(klabel),context, strlen(context), 1);
            if (rs == 1) {
                for (size_t i = 0; i < 64; ++i) {
                     if (i<32) key[i] = out[i];
                     if (i>=32 && i<48) iv[i-32] = out[i]; 
                  }
             printf("Handshake Complete--Key material established\n");
            } else {
                printf("error getting ekm\n");
            }
 
         if(port==5544) 
         evp = EVP_CIPHER_fetch(NULL, "AES-256-GCM", NULL);
         else
         evp = EVP_CIPHER_fetch(NULL, "AES-128-GCM", NULL);         
         
         outmsg = (unsigned char *) malloc (CHUNK_SIZE);
         tp=tag;
        
         if(mode!=0){
        printf("From external GCM\nStarting with enc_s%02d_n%02d\n", sbits(mode),ibits(mode));
                start_time=time(NULL);	
         cx=(EDcontext *) malloc(sizeof(EDcontext));        
         ed_init( cx,  iv , (unsigned char *)key );
          ct=0;part=0;
       while (SSL_read_ex(ssl, buffer, CHUNK_SIZE, &readbytes) > 0) {
            if(readbytes==CHUNK_SIZE){
            if(ct) fwrite(outmsg+CHUNK_SIZE-TAGSIZE, TAGSIZE,1, fd);
            if (dec_update(cx, outmsg, buffer, CHUNK_SIZE)) {
            fprintf(stderr, "Failed Decrypt update\n");

            goto outg;
          }
             fwrite(outmsg, CHUNK_SIZE-TAGSIZE,1, fd); 
             memcpy(tag, buffer+CHUNK_SIZE-TAGSIZE, TAGSIZE);
             kbyte=(++ct)*CHUNK_SIZE;curr_time = time(NULL);
             printf("\rRecieved %d bytes at rate %f bps",kbyte, (float)kbyte/(curr_time-start_time) );
             fflush(stdout);
         }    
           else if(readbytes>0){
           part=1;
             if(readbytes>TAGSIZE){if(ct)fwrite(outmsg+CHUNK_SIZE-TAGSIZE, TAGSIZE,1, fd);}
             else 
                  {
                       memcpy(tag+TAGSIZE,buffer ,  readbytes);               
                       tp=tag+readbytes;  
                       if(ct){   /* the previous chunk ended with readbytes data bytes and the start of the tag */
                       fwrite(outmsg+CHUNK_SIZE-TAGSIZE, readbytes,1, fd);
                       cx->mlen -= (TAGSIZE-readbytes);
                       addmul(cx->paccum,buffer+CHUNK_SIZE-TAGSIZE, readbytes,cx->H);
                       memcpy(cx->accum, cx->paccum,16);
                       }
                  }
             if(readbytes>TAGSIZE){dec_update(cx, outmsg, buffer, readbytes-TAGSIZE);
 
             fwrite(outmsg,readbytes-TAGSIZE,1, fd);
             memcpy(tag,buffer+readbytes-TAGSIZE, TAGSIZE);
               }
             }
 
        } 
        if(!part) {   /* the last full chunk ended with the whole tag: drop its block from the message */
        cx->mlen -= (TAGSIZE);
        memcpy(cx->accum, cx->paccum,16);
                  }
           ret = dec_final (cx, tp);
           if(ret==-1) printf("TAG MISMATCH\n");

        } 
        if(mode==0){

        printf("From openssl native GCM\n");       
        if (evp == NULL) {
        	fprintf(stderr, "No evp!\n");
        	ERR_print_errors_fp(stderr);
        	goto outg;
    	}
 
    	ctxx = EVP_CIPHER_CTX_new();
    	if (ctxx == NULL) {
        	fprintf(stderr, "No context\n");
        	goto outg;
    		}
 
        if (!EVP_DecryptInit(ctxx, evp,
                             (unsigned char *)key, iv)) {
            fprintf(stderr, "Failed EVP_DecryptInit!\n");
 
            goto outg;
        }
        /* The last TAGSIZE bytes of each record may be (part of) the tag, so they are held
         * back in tag[] and decrypted only when the next record shows they are data. */
        ct=0;
       while (SSL_read_ex(ssl, buffer, CHUNK_SIZE, &readbytes) > 0  ) {
            if(readbytes>TAGSIZE){
            if(ct) { EVP_DecryptUpdate(ctxx, outmsg, &outlen, tag, TAGSIZE); fwrite(outmsg, TAGSIZE,1, fd); }
            if (!EVP_DecryptUpdate(ctxx, outmsg, &outlen, buffer, readbytes-TAGSIZE)) {
            fprintf(stderr, "Failed Decrypt update\n");
 
            goto outg;
          }   
             fwrite(outmsg, readbytes-TAGSIZE,1, fd);
             memcpy(tag, buffer+readbytes-TAGSIZE, TAGSIZE);
             tp=tag; ct++;
         }    
           else if(readbytes>0){   /* tag split: readbytes held-back bytes are data, the rest is the tag */
             if(ct) { EVP_DecryptUpdate(ctxx, outmsg, &outlen, tag, readbytes); fwrite(outmsg, readbytes,1, fd); }
             memcpy(tag+TAGSIZE, buffer, readbytes);
             tp=tag+readbytes;
             }
        } 
       EVP_CIPHER_CTX_ctrl(ctxx, EVP_CTRL_GCM_SET_TAG, TAGSIZE, tp);
       ret = EVP_DecryptFinal (ctxx, outmsg , &outlen);
       if(ret<=0) printf("TAG MISMATCH\n");
    }
        printf("\nFile saved to %s\n",filename); 
        fprintf(stdout, "\n===========================\n");        
        fclose(fd);
        
 }
 
        SSL_shutdown(ssl);
        SSL_free(ssl);
        close(client);        

    }
    outg:
    close(sock);
    SSL_CTX_free(ctx);


}

int main(void)
{   signal(SIGPIPE, SIG_IGN);
    sig_set_handler(SIGUSR1,&signal_handler);
    printf("%s (Library: %s)\n",
               OPENSSL_VERSION_TEXT, OpenSSL_version(OPENSSL_VERSION));
    printf("%s\n", OpenSSL_version(OPENSSL_BUILT_ON));
    printf("%s\n", OpenSSL_version(OPENSSL_PLATFORM));
    
    printf("options: ");
    printf(" %s", BN_options());
    printf("\n");
  
    pid_t c=fork();
    if(c == 0)   
    { 
      
        createserver(5545);
 
        exit(0); 
    }
    else
    {
        createserver(5544);
 
        exit(0); 
    } 
 
    
    
  
}
