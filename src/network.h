/** @file network.h
 *  \brief Deklarace sitovych funkci.
 *
 */

#ifndef __network_h
#define __network_h

//#define DEBUG

extern "C" {
#include <unistd.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
}


#include <openssl/err.h>
#include <openssl/ssl.h>

#include <string>

#include "security.h"

using namespace std;


extern int server_data_socket; //pouziva ho fpasv
extern int client_data_socket;
extern int client_socket; //< soket pro control connection
extern struct sockaddr_in client_data_address;
extern bool passive;
extern char transfer_type;  //< pro type command, implicitne ASCII
extern char transfer_typep; //< parametr transfer type, implicitne Non-print
extern char transfer_mode;  //< pro mode command, implicitne Stream
extern char file_structure; //< pro stru command, implicitne File
extern bool secure_cc; //< pouzivat sifrovane control connection?
extern bool secure_dc; //< pouzivat sifrovane data connection?
extern int server_default_data_port;


extern BIO             * io;
extern BIO             * ssl_bio;
extern SSL             * ssl;

extern BIO             * data_io;
extern BIO             * data_ssl_bio;
extern SSL             * data_ssl;

int ClientRequest(string &req);
int ClientSecureRequest(string &req);
int FTPReply(int code, const char * msg);
int FTPMultiReply(int code, const char * msg);
int CreateDataConnection();
int SendDataLine(const char * data);
int SendData(const char * data, int size);
int ReceiveData(char * data, int size);



#define MAX_CLIENT_REPLY_LEN 1024 //musi byt velke c. (delka cesty k souboru + jmena souboru ...)

#endif //__network_h


