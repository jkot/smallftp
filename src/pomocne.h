/** @file pomocne.h
 *  /brief Deklarace pomocnych funkci.
 *
 */


#ifndef __pomocne_h
#define __pomocne_h

#include <iostream>
#include <string>
#include <deque>
#include <vector>
#include <list>

extern "C" {
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>
#include <dirent.h>
}

#include "VFS.h"

#define MY_NAME "smallFTPd"
#define MAX_NAME_LEN 50
#define MAX_CMD_LENGTH 15
#define MAX_ARG_LENGTH 50
#define ERR_MALLOC 10
#define MY_NAME "smallFTPd"
#define MAX_NAME_LEN 50

using namespace std;

class user {
public:
    void Clear() { name = ""; password = ""; is_admin = false; }
    user &operator=(user &x) { name = x.name; password = x.password; is_admin = x.is_admin; return *this; }
        
    string name;
    string password;
    bool is_admin; ///< ma user admin prava?
};

struct command {
        char *name;
	char *help;
	int (*handler)(list<string> &, VFS &);
        int   max_args;
};

extern vector<user>     users;
extern vector<string>   ip_deny_list;

extern command command_table[];
extern int number_of_commands;

extern char FTP_EOR[2];
extern char FTP_EOF[2];
extern char FTP_EOR_EOF[2];

int cutCRLF(char *a); //umaze <CR><LF> na konci retezce (dva znaky pred nulou), a posune nulu.
int addCRLF(char *a); //pripoji na konec retezce, na ktere ukazuje a <CR><LF>a za ne prida koncovou nulu

int ParseCommand(string command, list<string> &atoms);
int LoadAccountFile(const char * path);  //nacte z fajlu 'name' info o juzrech do globalniho usr_tbl v main.c
int LoadIPDenyList(const char * path);
int TidyUp();
int LF2CRLF(char *kam, char *odkud, int kolik);
int CRLF2LF(char *kam, char *odkud, int kolik);
int EraseEOR(char *data, int velikost);
int IsDelim(char zn);
int PocetArgumentu(char *line);
int GetCommandIndex(string cmd);
int CheckIP(char *IP);
int CutPathIntoParts(string path, deque<string> &x);
int CheckRights(int x);
int CheckDir(const char * path);
void PrintDeniedIPs();
void PrintAccounts();
void ToLower(string &x);

#endif //__pomocne_h

