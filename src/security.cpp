/** @file security.cpp
 *  \brief Implementace funkci pro podporu TLS/SSL.
 *
 *  V security.cpp jsou implementovany funkce, ktere inicializuji TLS/SSL,
 *  navazuji spojeni a ukoncuji ho. V pripade control connection to jsou funkce
 *  TLSInit(), TLSNeg() a TLSClean(). V pripade data connection je to
 *  TLSDataInit(), TLSDataNeg(), a jako ekvivalent TLSClean() jsou zde dve
 *  funkce TLSDataShutdown() a TLSDataClean(). U data connection jsou tyto dve
 *  rozliseny, protoze je potreba navazovat a ukoncovat spojeni pri kazdem
 *  prenosu dat po data connection, nejen pri startu a ukonceni serveru jako je
 *  tomu v pripade control connection.
 *
 */

#include "security.h"
#include <openssl/err.h>

int     client_auth     = 0;
char *  ciphers         = 0;
int     s_server_session_id_context      = 1;
int     s_server_auth_session_id_context = 2;


BIO             * bio_err = 0;
const char      * pass;

SSL_CTX         * ctx; //< ssl kontext pro control connection
BIO             * io;  //< bio rozhrani pro zapis a cteni po control connection
BIO             * ssl_bio;
SSL             * ssl;

SSL_CTX         * data_ctx; //< ssl kontext pro data connection
BIO             * data_io;  //< bio rozhrani pro zapis a cteni po data connection
BIO             * data_ssl_bio;
SSL             * data_ssl;

int password_cb(char *buf,int num, int rwflag,void *userdata);



/* A simple error and exit routine*/
int err_exit(char *string) {
    fprintf(stderr,"%s\n",string);
    exit(0);
}

/* Print SSL errors and exit*/
int berr_exit(char *string) {
    BIO_printf(bio_err,"%s\n",string);
    ERR_print_errors(bio_err);
    exit(0);
}


int password_cb(char *buf,int num, int rwflag,void *userdata) {
    if(num<strlen(pass)+1)
      return(0);

    strcpy(buf,pass);
    return(strlen(pass));
}


SSL_CTX *initialize_ctx(const char * keyfile, const char * password) {
    SSL_METHOD *meth;
    SSL_CTX *ctx;
    
    if(!bio_err){
      /* Global system initialization*/
      SSL_library_init();
      SSL_load_error_strings();
      
      /* An error write context */
      bio_err = BIO_new_fp(stderr,BIO_NOCLOSE);
    }
    
    /* Create our context*/
    meth = SSLv23_method();
    ctx  = SSL_CTX_new(meth);

    /* Load our keys and certificates*/
    if (!(SSL_CTX_use_certificate_chain_file(ctx,keyfile)))
          berr_exit("Can't read certificate file");

    pass = password;
    SSL_CTX_set_default_passwd_cb(ctx,password_cb);
    if (!(SSL_CTX_use_PrivateKey_file(ctx,keyfile,SSL_FILETYPE_PEM)))
          berr_exit("Can't read key file");

    /* Load the CAs we trust*/
    if  (!(SSL_CTX_load_verify_locations(ctx,ca_list_file.c_str(),0)))
          berr_exit("Can't read CA list");
    
#if (OPENSSL_VERSION_NUMBER < 0x00905100L)
    SSL_CTX_set_verify_depth(ctx,1);
#endif
    
    return ctx;
}


void destroy_ctx(SSL_CTX *ctx) {
    SSL_CTX_free(ctx);
}


void load_dh_params(SSL_CTX * ctx, const char * file) {
    DH  *       ret = 0;
    BIO *       bio;

    if ((bio = BIO_new_file(file,"r")) == 0)
      berr_exit("Couldn't open DH file");

    ret = PEM_read_bio_DHparams(bio, 0, 0, 0);
    BIO_free(bio);
    if(SSL_CTX_set_tmp_dh(ctx,ret) < 0)
      berr_exit("Couldn't set DH parameters");
}


void generate_eph_rsa_key(SSL_CTX *ctx) {
    RSA *rsa;

    rsa = RSA_generate_key(512,RSA_F4,0,0);
    
    if (!SSL_CTX_set_tmp_rsa(ctx,rsa))
      berr_exit("Couldn't set RSA key");

    RSA_free(rsa);
}



/* ------------------- FUNKCE PRO CONTROL CONNECTION ----------------------- */

/** Inicializuje TLS pro control connection.
 *
 */
int TLSInit() {
    ctx = initialize_ctx(key_file.c_str(),PASSWORD);
    load_dh_params(ctx,dh_file.c_str());
}

/** Provadi TLS handshake pro control connection.
 *
 */
int TLSNeg() {
    BIO *       sbio;
    int         r;

    //s je to co vrati accept(sock)

    sbio = BIO_new_socket(client_socket,BIO_NOCLOSE);
    ssl  = SSL_new(ctx);
    SSL_set_bio(ssl,sbio,sbio);
        
    //ted udelame SSL handshake
    if ((r=SSL_accept(ssl) <= 0))
        return -1; //SSL accept error
    
    //vytvorime buffrovane BIO pro pohodlnejsi praci
    io      = BIO_new(BIO_f_buffer());
    ssl_bio = BIO_new(BIO_f_ssl());
    BIO_set_ssl(ssl_bio,ssl,BIO_CLOSE);
    BIO_push(io,ssl_bio);

}//TLSNeg()

/** Ukoncuje bezpecne control connection.
 *
 * Uvolnuje prostredky alokovane pro TLS a ukoncuje spojeni.
 * 
 */
int TLSClean() {
    int         ret;
    
    ret = SSL_shutdown(ssl);
    if(!ret){
      /* If we called SSL_shutdown() first then
         we always get return value of '0'. In
         this case, try again, but first send a
         TCP FIN to trigger the other side's
         close_notify */
      shutdown(client_socket, 1);
      ret = SSL_shutdown(ssl);
    }
      
    switch(ret){  
      case 1:
        break; /* Success */
      case 0:
      case -1:
      default:
        return -1; //shutdown failed
    }

    SSL_free(ssl);
    close(client_socket);
    destroy_ctx(ctx);
    
    return 1;
}//TSLClean()




/* ------------------ FUNKCE PRO DATA CONNECTION ---------------------- */

/** Inicializuje TLS pro data connection.
 *
 */
int TLSDataInit() {
    data_ctx = initialize_ctx(key_file.c_str(),PASSWORD);
    load_dh_params(data_ctx,dh_file.c_str());
}

/** Provadi TLS handshake pro data connection.
 *
 */
int TLSDataNeg() {
    BIO *       sbio;
    int         r;

    //s je to co vrati accept(sock)

    sbio      = BIO_new_socket(client_data_socket,BIO_NOCLOSE);
    data_ssl  = SSL_new(data_ctx);
    SSL_set_bio(data_ssl,sbio,sbio);
        
    //ted udelame SSL handshake
    if ((r=SSL_accept(data_ssl) <= 0))
        return -1; //SSL accept error
    
    //vytvorime buffrovane BIO pro pohodlnejsi praci
    data_io      = BIO_new(BIO_f_buffer());
    data_ssl_bio = BIO_new(BIO_f_ssl());
    BIO_set_ssl(data_ssl_bio,data_ssl,BIO_CLOSE);
    BIO_push(data_io,data_ssl_bio);

}//TLSNeg()

/** Uzavira bezpecne data connection.
 *
 * Zavre TLS data connection, ale neuvolni prostredky pouzivane TLS pro data
 * connection.
 *
 */
int TLSDataShutdown() {
    int         ret;
    
    ret = SSL_shutdown(data_ssl);
    if(!ret){
      /* If we called SSL_shutdown() first then
         we always get return value of '0'. In
         this case, try again, but first send a
         TCP FIN to trigger the other side's
         close_notify */
      shutdown(client_data_socket, 1);
      ret = SSL_shutdown(data_ssl);
    }
    close(client_data_socket);
      
    switch(ret){  
      case 1:
        break; /* Success */
      case 0:
      case -1:
      default:
        return -1; //shutdown failed
    }
}

/** Uvolni prostredky alokovane pro TLS data connection.
 *
 */
int TLSDataClean() {
    int         ret;
    
    SSL_free(data_ssl);
    destroy_ctx(data_ctx);
    
    return 1;
}//TSLClean()


