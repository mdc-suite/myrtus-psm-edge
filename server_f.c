#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/hmac.h>
#include <openssl/evp.h>
#include <unistd.h>
#include <signal.h>
#include <openssl/bio.h>
#include <openssl/kdf.h>
#include <openssl/core_names.h>
#include <openssl/params.h>
#include <openssl/rand.h>
#include <stdlib.h>
#include "crypto_aead256.h"
#include <time.h>
#include <sys/stat.h>    
#include <stdbool.h>  
#include <sys/types.h>
#include <dirent.h>

#include "cycles.h" # al3monni mod

#define fbits(y)  ((y) & 0xc0)>>6
#define sbits(y)  ((y) & 0x30)>>4
#define ibits(y)  ((y) & 0x0f) 

#define CHUNK_SIZE 1024
#define TAGSIZE 16
volatile int fs=0;
void fshandler(int sig)
{  if (sig == SIGUSR2)
    {
    fs=1-fs;
    }
}

bool file_exists (char *filename) {
  struct stat   buffer;   
  return (stat (filename, &buffer) == 0);
}

unsigned char* read_unknown_stream(SSL *ssl, size_t *total_len) {
    unsigned char chunk[CHUNK_SIZE];
    size_t bytes_read = 0;
    
    unsigned char *full_buffer = NULL;
    *total_len = 0;

    while (1) {
        // This blocks until data arrives or connection closes
        int status = SSL_read_ex(ssl, chunk, CHUNK_SIZE, &bytes_read);

        if (status > 0) {
            // 1. Expand the main buffer to hold new data
            char *new_ptr = realloc(full_buffer, *total_len + bytes_read);
            if (!new_ptr) {
                // Out of memory error handling
                free(full_buffer);
                return NULL;
            }
            full_buffer = new_ptr;

            // 2. Copy chunk into the main buffer
            memcpy(full_buffer + *total_len, chunk, bytes_read);
            *total_len += bytes_read;

            // 3. OPTIONAL: Check application protocol layer
            // if (is_http_body_finished(full_buffer, *total_len)) break;

        } else {
            // status <= 0 means stream paused, ended, or failed
            int err = SSL_get_error(ssl, status);

            if (err == SSL_ERROR_ZERO_RETURN) {
                // Clean close: The peer closed the TLS connection.
                // This is our natural exit point if reading a raw stream.
                break; 
            } 
            else if (err == SSL_ERROR_SYSCALL || err == SSL_ERROR_SSL) {
                // Fatal error: Network dropped, timeout, or TLS protocol error
                free(full_buffer);
                return NULL;
            }
        }
    }

    return full_buffer;
}
 volatile int mode=98;
// al3monni mod
/*  extern inline __attribute__((always_inline)) unsigned long rdtscp()
{
   unsigned long a, d, c;

   __asm__ volatile("rdtscp" : "=a" (a), "=d" (d), "=c" (c));

   return (a | (d << 32));
} */
void handleErrors(void)
{
    unsigned long errCode;

    printf("An error occurred\n");
    while(errCode = ERR_get_error())
    {
        char *err = ERR_error_string(errCode, NULL);
        printf("%s\n", err);
    }
    abort();
}

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
    int sock,decbytes,part,kbyte=0;   unsigned long long ts2,ts1;
    SSL_CTX *ctx;    BIO *b64 = NULL;
    BIO *web = NULL, *fout = NULL;
    unsigned char buffer [CHUNK_SIZE] ;unsigned char *b2;size_t  readbytes;
    unsigned char iv[16],tag[2*TAGSIZE],*tp;    EVP_CIPHER *evp = NULL;
    unsigned char key[32];EVP_CIPHER_CTX *ctxx;
    unsigned char *outmsg, receive[1024];
    char filename[1024];
    int outlen =0,ret,i,nread;
    int tmplen = 0,bytesread,ct=0;
    FILE *fd;
    EDcontext *cx;    
    time_t start_time,curr_time;
    srand(time(NULL));
    ctx = SSL_CTX_new(TLS_server_method());
    if (!ctx) {
        perror("Unable to create SSL context");
        ERR_print_errors_fp(stderr);
        exit(EXIT_FAILURE);
    }
	SSL_CTX_set_min_proto_version(ctx, TLS1_3_VERSION);
	SSL_CTX_set_max_proto_version(ctx, TLS1_3_VERSION);    
	 
        if(port==5544)SSL_CTX_set_cipher_list(ctx, "ECDHE-ECDSA-AES256-GCM-SHA384");
        else         SSL_CTX_set_cipher_list(ctx, "DHE-RSA-AES128-GCM-SHA256");
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
    struct timeval delay ; 
    delay.tv_sec=2;
        delay.tv_usec=2;
    if (setsockopt(sock, SOL_SOCKET, SO_RCVBUFFORCE, &a, sizeof(unsigned int)) == -1) {
    fprintf(stderr, "Error setting socket opts: %s\n", strerror(errno));
}
 //    if(setsockopt(sock, SOL_SOCKET,SO_RCVTIMEO, &delay, sizeof(delay)) == -1) {
// fprintf(stderr, "Error setting socket opts: %s\n", strerror(errno));
 //} 
    if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("Unable to bind");
        exit(EXIT_FAILURE);
    }

    if (listen(sock, 1) < 0) {
        perror("Unable to listen");
        exit(EXIT_FAILURE);
    }
    while((nread = read(sock, receive, sizeof(receive)-1)) > 0) ;
    while(1) {
        struct sockaddr_in addr;
        unsigned int len = sizeof(addr);
        SSL *ssl;

        //printf("Current value of mode at port %d is %d \n", port,mode);
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
                     //printf("%02X ", out[i]);
                     if (i<32) key[i] = out[i];
                     if (i>=32 && i<48) iv[i-32] = out[i]; 
                  }
             printf("Handshake Complete--Key material established\n");
            } else {
                printf("error getting ekm\n");
            }
        //buffer =   read_unknown_stream(ssl,&readbytes); 
        //printf("readbytes=%ld\n",readbytes);
        //for(i=0;i<readbytes;i++) printf("%02x ", *((unsigned char *)buffer+i)  );printf("\n");
        //getchar();
        //SSL_read_ex(ssl, buffer, 1024, &readbytes);
       // memcpy(tag, buffer +readbytes-TAGSIZE,TAGSIZE);  
 
        //b64 = BIO_new(BIO_f_base64());
       /* if ((b64 == NULL)
            || (BIO_push(b64, BIO_new_fp(stdout, BIO_NOCLOSE)) == NULL)
            || (BIO_write(b64, buffer,  readbytes ) <= 0)
            || (BIO_flush(b64) == -1)) {
            fprintf(stderr, "Failed to write base64 data\n");
            goto outg;
        }  */  
 
         if(port==5544) 
         evp = EVP_CIPHER_fetch(NULL, "AES-256-GCM", NULL);
         else
         evp = EVP_CIPHER_fetch(NULL, "AES-128-GCM", NULL);         
         
         outmsg = (unsigned char *) malloc (CHUNK_SIZE);
         tp=tag;
         //memset(outmsg, 0, readbytes );
        
         if(mode!=0){
        printf("From external GCM\nStarting with enc_s%02d_n%02d\n", sbits(mode),ibits(mode));
                start_time=time(NULL);	
             /* ts1    = rdtscp();
              if(crypto_aead_aes256gcmv1_ref_decrypt(outmsg, &decbytes, NULL,buffer , readbytes, NULL, 0, iv, key)==-1)
                 fprintf(stdout, "TAG mismatch\n");
              else
                 fprintf(stdout, "Output message is: %s\n", outmsg);   
                 ts2    = rdtscp();*/
         cx=(EDcontext *) malloc(sizeof(EDcontext));        
         ed_init( cx,  iv , (unsigned char *)key );
          ct=0;part=0;
       while (SSL_read_ex(ssl, buffer, CHUNK_SIZE, &readbytes) > 0  || 0*(kbyte<2097152)) {
            if(readbytes==CHUNK_SIZE){
            if(ct) fwrite(outmsg+CHUNK_SIZE-TAGSIZE, TAGSIZE,1, fd);
            if (dec_update(cx, outmsg, &outlen, buffer, CHUNK_SIZE)) {
            fprintf(stderr, "Failed Decrypt update\n");

            goto outg;
          }               //printf("%d -- %02x \n",ct,buffer[0]); 
             fwrite(outmsg, CHUNK_SIZE-TAGSIZE,1, fd); 
             memcpy(tag, buffer+CHUNK_SIZE-TAGSIZE, TAGSIZE);
             kbyte=(++ct)*CHUNK_SIZE;curr_time = time(NULL);
             printf("\rRecieved %d bytes at rate %f bps",kbyte, (float)kbyte/(curr_time-start_time) );
             fflush(stdout);
             //printf("ct=%d %d\n",ct++, readbytes);
         }    
           else if(readbytes>0){
           part=1;
             if(readbytes>TAGSIZE){if(ct)fwrite(outmsg+CHUNK_SIZE-TAGSIZE, TAGSIZE,1, fd);}
             else 
                  {
                       fwrite(outmsg+CHUNK_SIZE-TAGSIZE, readbytes,1, fd);
                       memcpy(tag+TAGSIZE,buffer ,  readbytes);               
                       tp=tag+readbytes;  
                       cx->mlen -= (TAGSIZE-readbytes);
                      // printf("rb %d \n",readbytes);
                      //  for(i=0;i<16;i++)printf("<%02x> ",cx->paccum[i]);  printf("\n");   
                      // for(i=0;i<readbytes;i++)printf("{%02x} ",outmsg[CHUNK_SIZE-TAGSIZE+i]);  printf("\n");                       
                       addmul(cx->paccum,buffer+CHUNK_SIZE-TAGSIZE, readbytes,cx->H);
                       memcpy(cx->accum, cx->paccum,16);
                      // for(i=0;i<16;i++)printf("<%02x> ",cx->accum[i]);printf("\n"); 
                       
                  }
             if(readbytes>TAGSIZE){dec_update(cx, outmsg, &outlen, buffer, readbytes-TAGSIZE);
 
             fwrite(outmsg,readbytes-TAGSIZE,1, fd);
             memcpy(tag,buffer+readbytes-TAGSIZE, TAGSIZE);
               }
             }
             else if(readbytes==0){
             ;;
             }  
 
        } 
        if(!part) {  fwrite(outmsg+CHUNK_SIZE-TAGSIZE, TAGSIZE,1, fd);
        cx->mlen -= (TAGSIZE);   addmul(cx->paccum,buffer+CHUNK_SIZE-TAGSIZE,TAGSIZE,cx->H);
     memcpy(cx->accum, cx->paccum,16);
 
                  }
           ret = dec_final (cx, tp);
           //for(i=0;i<16;i++) printf("%02x ",tp[i]);printf("\n");
           if(ret==-1) printf("TAG MISMATCH\n");

        } 
        if(mode==0){

        printf("From openssl native GCM\n");ts1=rdtscp();       
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
 ct=0;part=0;
       while (SSL_read_ex(ssl, buffer, CHUNK_SIZE, &readbytes) > 0  ) {
            if(readbytes==CHUNK_SIZE){
            if(ct) fwrite(outmsg+CHUNK_SIZE-TAGSIZE, TAGSIZE,1, fd);
            if (!EVP_DecryptUpdate(ctxx, outmsg, &outlen, buffer, CHUNK_SIZE)) {
            fprintf(stderr, "Failed Decrypt update\n");
 
            goto outg;
          }   
             fwrite(outmsg, CHUNK_SIZE-TAGSIZE,1, fd); //printf("ct=%d %d\n",ct++, readbytes);
         }    
           else if(readbytes>0){
           part=1;
             if(readbytes>TAGSIZE){if(ct)fwrite(outmsg+CHUNK_SIZE-TAGSIZE, TAGSIZE,1, fd);}
             else 
                  { fwrite(outmsg+CHUNK_SIZE-TAGSIZE, readbytes,1, fd);
  
                  }
             if(readbytes>TAGSIZE){EVP_DecryptUpdate(ctxx, outmsg, &outlen, buffer, readbytes);
 
             fwrite(outmsg,readbytes-TAGSIZE,1, fd);
               }
             }
             else if(readbytes==0){
             ;;
             }  
             printf("%d \n",fs);
        } 
        if(!part) {fwrite(outmsg+CHUNK_SIZE-TAGSIZE, TAGSIZE,1, fd);

   }
       ret = EVP_DecryptFinal (ctxx, outmsg , &outlen); //ts2=rdtscp();       
       
     //  for(i=0;i<16;i++) printf("%02x ", outmsg[i]);printf("\n%d %d\n",ret,outlen);     
     //  if (ret <= 0) {
 
                  //  printf("TAG mismatch\n");
   // }
        
     
    //    if(ret>0)             fwrite(outmsg, outlen ,1, fd); 
        //fprintf(stdout, "Output message is: %s\n", outmsg);
    }
        //fprintf(stdout, "number of cycles is:  %llu\n", ts2-ts1);  
        printf("\nFile saved to %s\n",filename); 
        fprintf(stdout, "\n===========================\n");        
        fclose(fd);
        fs=0;
        
 }
 
        SSL_shutdown(ssl);
        SSL_free(ssl);
        close(client);        

    }
    outg:
    close(sock);
    SSL_CTX_free(ctx);


}

int main(int argc, char **argv)
{   signal(SIGPIPE, SIG_IGN);
    signal(SIGUSR2, &fshandler);
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
