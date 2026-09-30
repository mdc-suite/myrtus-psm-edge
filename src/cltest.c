#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/hmac.h>
#include <openssl/evp.h>
 #include "crypto_aead256.h"
#include <openssl/bio.h>
#include <openssl/kdf.h>
#include <openssl/core_names.h>
#include <openssl/params.h>
#include <openssl/rand.h>
#include <stdlib.h>

#define CHUNK_SIZE 1024
#define TAG_SIZE 16
int mode=98;
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

int main(int argc, char **argv)
{
    SSL_CTX *ctx; 
     int opt,slevel,i,ed;
    unsigned char df[500], kf[500],of[500],command[900],seed[200],seek[200]; 
    BIO *web = NULL, *out = NULL;
    SSL *ssl = NULL;
    unsigned  char message[CHUNK_SIZE];
    long res = 1,ct=0;
    unsigned char iv[16];
    unsigned char key[32];
    EVP_CIPHER *evp = NULL;
    EVP_CIPHER_CTX *ctxx = NULL;
    FILE *fd;
    size_t size=0;
    unsigned char outmsg[CHUNK_SIZE+16],outmsg1[CHUNK_SIZE+16], tag1[16];
    int outlen = 1024;
    unsigned long long outlen1,sentb=0;
    int tmplen = 0;
    BIO *b64 = NULL;
    BIO *mem = NULL;
    unsigned char binmsg[1024];
    int operation = 0,bytes_read ;
    EDcontext *cx;
    while((opt = getopt(argc, argv, "i:s:f:")) != -1)  
    {  
 
        switch(opt)  
        {  
              
            case 's':
                slevel = atoi(optarg);
                printf("security level: %d\n", slevel );  
                break;
            case 'i':  
                strcpy(df,optarg);
                printf("Connect to: %s\n", df);  
                break;  
            case 'f':
                /*message = (unsigned char*) malloc(sizeof(unsigned char)* (strlen(optarg)+4)); 
                outmsg  = (unsigned char*) malloc(sizeof(unsigned char)* (strlen(optarg)+4+TAG_SIZE)); 
                strcpy(message,optarg);
                printf("message: ");puts(message);
                printf("fin\n"); */
                puts(optarg);
                fd=fopen(optarg,"rb");
                //printf("%x\n",fd);
                if(!fd)  {fprintf(stderr,"could not open file %s\n" ,optarg);
                exit(EXIT_FAILURE);}
                break;  
             default: /* '?' */
                   fprintf(stderr, "Usage: %s [-s 0/1/2 for low/medium/high security level] [-i input stream]  \n",
                           argv[0]);
                   exit(EXIT_FAILURE);
        }  
    }  
 
    for(; optind < argc; optind++){      
        printf("extra arguments: %s\n", argv[optind]);  
    } 

    printf("%s (Library: %s)\n",
               OPENSSL_VERSION_TEXT, OpenSSL_version(OPENSSL_VERSION));
    printf("%s\n", OpenSSL_version(OPENSSL_BUILT_ON));
    printf("%s\n", OpenSSL_version(OPENSSL_PLATFORM));
    
    printf("options: ");
    printf(" %s", BN_options());
    printf("\n");
    
    ctx = SSL_CTX_new(TLS_client_method());
    if (!ctx) {
        perror("Unable to create SSL context");
        ERR_print_errors_fp(stderr);
        exit(EXIT_FAILURE);
    }
	SSL_CTX_set_min_proto_version(ctx, TLS1_3_VERSION);
	SSL_CTX_set_max_proto_version(ctx, TLS1_3_VERSION);   
        if(slevel==1) SSL_CTX_set_cipher_list(ctx, "ECDHE-ECDSA-AES256-GCM-SHA384");
        if(slevel==0) SSL_CTX_set_cipher_list(ctx, "DHE-RSA-AES128-GCM-SHA256"); 
	 
    SSL_CTX_set_max_send_fragment( ctx, 262144000);
    web = BIO_new_ssl_connect(ctx);
    BIO_set_write_buf_size(web, 262144000);
    if(!(web != NULL)) handleErrors();

    res = BIO_set_conn_hostname(web, df);//"127.0.0.1:443"
    if(!(1 == res)) handleErrors();

    BIO_get_ssl(web, &ssl);
    if(!(ssl != NULL)) handleErrors();

    out = BIO_new_fp(stdout, BIO_NOCLOSE);
    if(!(NULL != out)) handleErrors();

    res = BIO_do_connect(web);
    if(!(1 == res)) handleErrors();

    res = BIO_do_handshake(web);
    if(!(1 == res)) handleErrors();

    const char *klabel = "EXPORTER-my_ekm";
    const char *context = "mycontext";

    unsigned char *outc = (unsigned char*)malloc(64 * sizeof(unsigned char));
    printf("Starting handshake with %s\n",df);
    int rs = SSL_export_keying_material(ssl,outc, 64 , klabel, strlen(klabel),context, strlen(context), 1);
    if (rs == 1) {
        for (size_t i = 0; i < 64; ++i) {
            //printf("%02X ", outc[i]);
            if (i<32) key[i] = outc[i];
            if (i>=32 && i<48) iv[i-32] = outc[i]; 
          }
        printf("Handshake Complete--Key material established\n");
    } else {
        printf("error getting ekm\n");
    }

/*
    int len = 0;
    do
    {
    char buff[1536] = {};
    len = BIO_read(web, buff, sizeof(buff));
                
    if(len > 0)
        BIO_write(out, buff, len);

    } while (len > 0 || BIO_should_retry(web));
*/
///////////////////////////////////////////////////////////////////////////////////
 

if(slevel==1) evp = EVP_CIPHER_fetch(NULL, "AES-256-GCM", NULL);
if(slevel==0) evp = EVP_CIPHER_fetch(NULL, "AES-128-GCM", NULL);
 
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

 

        if (!EVP_EncryptInit(ctxx, evp,
                             (unsigned char *)key, iv)) {
            fprintf(stderr, "Failed EVP_EncryptInit!\n");
            goto outg;
        }
         //cx=(EDcontext *) malloc(sizeof(EDcontext));        
         //ed_init( cx,  iv , (unsigned char *)key );
        while ((bytes_read = fread(message, 1,CHUNK_SIZE, fd))>0) {
 
        if (1 != EVP_EncryptUpdate(ctxx, outmsg, &outlen, message, bytes_read)) {
            fprintf(stderr, "Failed Encrypt update\n");
            goto outg;
        }
        //enc_update(cx, outmsg1, &outlen1, message, bytes_read);
        //printf("ct=%d outlen=%d outlen1=%d\n",ct++,outlen,outlen1);
        if(outlen==CHUNK_SIZE) {BIO_write(web, outmsg, outlen); sentb+=outlen;outlen=0;
        //printf("%d %02x \n",ct++,outmsg[0]); 
        //for(i=0;i<CHUNK_SIZE;i++) if(outmsg[i]^outmsg1[i]) printf("---XXX---\n");  
        
        }
    }
 
        
 
        if (!EVP_EncryptFinal(ctxx, outmsg, &tmplen)) {
            fprintf(stderr, "Failed final encrypt\n");
            goto outg;
        }   
   if(tmplen>0){   BIO_write(web, outmsg, tmplen);//printf("ct=%d outlen=%d\n",ct++,tmplen);
               }
 
            
              /*  for(i=0;i<outlen;i++) printf("%02x ", outmsg[i]  );printf("\n");     
     printf("`outlen =%d slen(outmsg)=%d slen(message)=%d tmplen=%d message=%s\n", outlen,strlen(outmsg), strlen(message) ,tmplen,message);*/
        
  
        if (1 != EVP_CIPHER_CTX_ctrl(ctxx, EVP_CTRL_GCM_GET_TAG, TAG_SIZE ,  outmsg+outlen )) handleErrors();
        //enc_final(cx,tag1);
        if(outlen+TAG_SIZE<CHUNK_SIZE){
        BIO_write(web, outmsg, outlen+TAG_SIZE);sentb+=(outlen+TAG_SIZE);}
        else
        {
        BIO_write(web, outmsg, CHUNK_SIZE);BIO_write(web, outmsg+CHUNK_SIZE, outlen+TAG_SIZE-CHUNK_SIZE);sentb+=(outlen+TAG_SIZE);
        }  
        //printf("ct=%d outlen=%d\n",ct++,outlen+TAG_SIZE); 
        //for(i=outlen;i<outlen+16;i++) printf("%02x ", outmsg[i]  );printf("\n");
        //for(i=0;i<16;i++) printf("%02x ", tag1[i]  );printf("\n");
       // system("lsof -n -ti:5544| xargs kill -SIGUSR2");
    /*    b64 = BIO_new(BIO_f_base64());
        if ((b64 == NULL)
            || (BIO_push(b64, BIO_new_fp(stdout, BIO_NOCLOSE)) == NULL)
            || (BIO_write(b64, outmsg, outlen) <= 0)
            || (BIO_flush(b64) == -1)) {
            fprintf(stderr, "Failed to write base64 data\n");
            goto outg;
        }            

        BIO_free(b64);
        b64 = NULL;*/
 
 /*   } else {
        b64 = BIO_new(BIO_f_base64());
        mem = BIO_new_mem_buf(message, strlen(message));
        if ((b64 == NULL)
            || (mem == NULL)
            || (BIO_push(b64, mem) == NULL)) {
            fprintf(stderr, "Unable to set up base64 decoder\n");
            rc = 1;
            goto out;
        }
        BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
        BIO_set_mem_eof_return(mem, 0);
        memset(binmsg, 0, 1024);

        rc = BIO_read(b64, binmsg, 1024);
        if (rc <= 0) {
            fprintf(stderr, "Failed to read message\n");
            ERR_print_errors_fp(stderr);
            rc = 1;
            goto out;
        }
        memcpy(iv, binmsg, sizeof(iv));

        if (!EVP_DecryptInit(ctx, evp,
                             (unsigned char *)key, iv)) {
            fprintf(stderr, "Failed EVP_DecryptInit!\n");
            rc = 1;
            goto out;
        }

        memset(outmsg, 0, 1024);
 
        if (!EVP_DecryptUpdate(ctx, outmsg, &outlen, &binmsg[16], rc-16)) {
            fprintf(stderr, "Failed Decrypt update\n");
            rc = 1;
            goto out;
        }
        if (!EVP_DecryptFinal(ctx, &outmsg[outlen], &outlen)) {
            fprintf(stderr, "Failed final decrypt\n");
            ERR_print_errors_fp(stderr);
            goto out;
        }
        if (outlen >= 1024)
            outmsg[1023] = '\0';  
        fprintf(stdout, "Output message is: %s\n", outmsg);
        rc = 0;
    }*/
   printf("Entire File Sent %ld bytes\n",sentb);
outg:
    BIO_free(b64);
    EVP_CIPHER_free(evp);
    EVP_CIPHER_CTX_free(ctxx);
 


    if(out)
    BIO_free(out);

    if(web != NULL)
    BIO_free_all(web);


    SSL_CTX_free(ctx);
}

