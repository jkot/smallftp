/** @file security.h
 *  \brief Deklarace funkci a promennych pro praci s TLS.
 *
 */

#ifndef __security_h
#define __security_h


#include <openssl/ssl.h>

extern "C" {
#include <sys/socket.h>
#include <unistd.h>
}

#include <string>

#define PASSWORD "password"

using namespace std;

int TLSInit();
int TLSNeg();
int TLSClean();

int TLSDataInit();
int TLSDataNeg();
int TLSDataShutdown();
int TLSDataClean();


extern int      client_socket;
extern int      client_data_socket;
extern string   key_file;
extern string   ca_list_file;
extern string   dh_file;

#ifndef ALLOW_OLD_VERSIONS
#if (OPENSSL_VERSION_NUMBER < 0x00905100L)
#error "Must use OpenSSL 0.9.6 or later"
#endif
#endif


#endif //__security_h

