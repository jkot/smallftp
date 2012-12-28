#ifndef __ftpcommands_h
#define __ftpcommands_h

extern "C" {
#include <unistd.h>
#include <stdio.h>
#include <time.h>
#include <ctype.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <dirent.h>
#include <netdb.h>
#include <signal.h>
#include <errno.h>
}

#include <string>

#include "security.h"
#include "pomocne.h"
#include "network.h"

extern bool run;
extern bool use_tls;
extern int  server_listening_port;
extern int  server_default_data_port;
extern bool anonymous_allowed;
extern user current_user;
extern string pid_file;
extern string account_file;
extern string vfs_config_file;
extern string ip_deny_list_file;
extern string working_dir;
extern vector<string>  ip_deny_list;
extern string ip_deny_list_file;
extern command command_table[];
extern int number_of_commands;
extern int server_data_socket; //pouziva ho fpasv
extern int client_data_socket;
extern int client_socket; //< soket pro control connection
extern struct sockaddr_in client_data_address;
extern bool ftp_abort;
extern bool tls_up;
extern bool tls_dc;
extern bool secure_cc;


extern BIO             * io;
extern BIO             * ssl_bio;
extern SSL             * ssl;


#define TYPE_ASCII  'A'
#define TYPE_EBCDIC 'E'
#define TYPE_IMAGE  'I'
#define TYPE_LOCAL  'L'

#define STRU_FILE   'F'
#define STRU_RECORD 'R'
#define STRU_PAGE   'P'

#define MODE_STREAM 'S'
#define MODE_BLOCK  'B'
#define MODE_COMPRESSED 'C'

#define HOST_NAME_MAX 100
#define ADDR_LENGTH_MAX 100

#endif //__ftpcommands_h

