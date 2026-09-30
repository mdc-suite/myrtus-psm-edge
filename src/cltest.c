#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/bio.h>
#include <stdlib.h>

#define CHUNK_SIZE 1024
#define TAG_SIZE 16
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
     int opt,slevel;
    unsigned char df[500];
    BIO *web = NULL;
    SSL *ssl = NULL;
    unsigned  char message[CHUNK_SIZE];
    long res = 1;
    unsigned char iv[16];
    unsigned char key[32];
    EVP_CIPHER *evp = NULL;
    EVP_CIPHER_CTX *ctxx = NULL;
    FILE *fd;
    unsigned char outmsg[CHUNK_SIZE+16];
    int outlen = 0;
    unsigned long long sentb=0;
    int tmplen = 0;
    int bytes_read ;
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
                puts(optarg);
                fd=fopen(optarg,"rb");
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
	 
    SSL_CTX_set_max_send_fragment( ctx, 262144000);
    web = BIO_new_ssl_connect(ctx);
    BIO_set_write_buf_size(web, 262144000);
    if(!(web != NULL)) handleErrors();

    res = BIO_set_conn_hostname(web, df);
    if(!(1 == res)) handleErrors();

    BIO_get_ssl(web, &ssl);
    if(!(ssl != NULL)) handleErrors();


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
            if (i<32) key[i] = outc[i];
            if (i>=32 && i<48) iv[i-32] = outc[i]; 
          }
        printf("Handshake Complete--Key material established\n");
    } else {
        printf("error getting ekm\n");
    }


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
        while ((bytes_read = fread(message, 1,CHUNK_SIZE, fd))>0) {
 
        if (1 != EVP_EncryptUpdate(ctxx, outmsg, &outlen, message, bytes_read)) {
            fprintf(stderr, "Failed Encrypt update\n");
            goto outg;
        }
        if(outlen==CHUNK_SIZE) {BIO_write(web, outmsg, outlen); sentb+=outlen;outlen=0;
        
        }
    }
 
        
 
        if (!EVP_EncryptFinal(ctxx, outmsg, &tmplen)) {
            fprintf(stderr, "Failed final encrypt\n");
            goto outg;
        }   
   if(tmplen>0){   BIO_write(web, outmsg, tmplen);
               }
 
            
        
  
        if (1 != EVP_CIPHER_CTX_ctrl(ctxx, EVP_CTRL_GCM_GET_TAG, TAG_SIZE ,  outmsg+outlen )) handleErrors();
        if(outlen+TAG_SIZE<CHUNK_SIZE){
        BIO_write(web, outmsg, outlen+TAG_SIZE);sentb+=(outlen+TAG_SIZE);}
        else
        {
        BIO_write(web, outmsg, CHUNK_SIZE);BIO_write(web, outmsg+CHUNK_SIZE, outlen+TAG_SIZE-CHUNK_SIZE);sentb+=(outlen+TAG_SIZE);
        }  
   printf("Entire File Sent %ld bytes\n",sentb);
outg:
    EVP_CIPHER_free(evp);
    EVP_CIPHER_CTX_free(ctxx);
 



    if(web != NULL)
    BIO_free_all(web);


    SSL_CTX_free(ctx);
}

