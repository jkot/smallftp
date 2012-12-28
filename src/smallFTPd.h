/** @file smallFTPd.h
 *  /brief Hlavickovy soubor pro smallFTPd.cpp.
 *
 */

#ifndef __SMALLFTPD_H
#define __SMALLFTPD_H

extern "C" {
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ctype.h>

#include <sys/time.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <dirent.h>

#include <signal.h>
#include <errno.h>
#include <assert.h>
}

#include "VFS.h"

#define MY_VERSION "1.0" //< cislo verze smallFTPd
#define VFS_DATABASE_NAME "vfsdb" //< jmeno pro soubor databaze gdbm, kterou pouzivaji objekty VFS a DirectoryDatabase.


#define MAX_IP_LIST 100
#define MAX_WAITING_CLIENTS 10 //<delka fronty, kterou vytvori listen pro socket, na kterem server posloucha


#define ERR(num,msg) if (num<0) cout << "Nastala chyba: " << #msg << "(" << num << ")" << endl;

using namespace std;


#endif //__SMALLFTPD_H



