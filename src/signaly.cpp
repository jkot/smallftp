/** @file signaly.cpp
 *  \brief Implementace handleru signalu.
 *
 * smallFTPd odchytava signaly SIGCHLD, SIGTERM, SIGHUP, SIGPIPE a SIGURG.
 * Signaly se odchytavaji z bezpecnostnich duvodu - napr. SIGPIPE pro
 * pripad, ze klient neocekavane ukonci spojeni, a take jako komunikacni
 * prostredek mezi klientem a serverem a mezi potomky a rodicovskym procesem -
 * to se tyka signalu SIGTERM a SIGHUP.
 * Vsechny signaly az na SIGURG se odchytavaji beznym zpusobem. Handler SIGURGu
 * zaprve znovu registruje sam sebe a za druhe se v hlavnim souboru (smallFTPd)
 * vola po forku v potomkovi funkce fcntl, kterou se urci, ze tento signal ma
 * byt dorucen potomkovi.
 *
 */

extern "C" {
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <fcntl.h>
}

#include <string>
#include "pomocne.h"
#include "signaly.h"
#include "network.h"

using namespace std;

/* *** *** Struktury *** *** */

void ReapChild(int pid); //zabrani zombie procesu
struct sigaction ReapAction = {
    ReapChild, 0, SA_RESTART, 0 //SA_RESTART - prijde-li sig. behem sys.volani, provede se ReapChild a to sys.v. se
					//restartuje, jde-li to
};


void TelnetSYNCHHandler(int socket);
struct sigaction SynchAction = {
  TelnetSYNCHHandler, 0, SA_RESTART, 0
};


void HUPHandler(int arg);
struct sigaction HUPAction = {
  HUPHandler, 0, SA_RESTART, 0
};


void PipeHandler(int arg);
struct sigaction PipeAction = {
    PipeHandler, 0, SA_RESTART, 0
};


void TermHandler(int arg);
struct sigaction TermAction = {
    TermHandler, 0, SA_RESTART, 0
};


/* *** *** Handlery *** *** */

void ReapChild(int pid) {
    int status,val;
    
    val=wait(&status); //hned se vrati, protoze tahle fce je zavolana hned po SIGCHLD
#ifdef DEBUG
    cout << "Proces s PID " << val << " prave skoncil." << endl;
#endif
}

/** Obsluha signalu SIGURG.
 *
 * Podiva se jestli po control connection prisel prikaz ABOR. Viz RFC959
 * Pg.34/Pg.35. Pokud prisel nastavi globalni promennou abort na true - tim se
 * prerusi aktualni prenos dat po data connection.
 * Pokud je nastavena promenna assume_abor na true, bude po prijeti signalu
 * automaticky predpokladat, ze se jedna o prichozi prikaz ABOR.
 * 
 */
void TelnetSYNCHHandler(int signum) {
    int         MAX_SCANNED = 10;
    char        msg[MAX_SCANNED];
    char *      p;
    int         TELNET_IP = 244; //Telneti InterruptProcess signal ASCII #244, viz RFC854 Pg. 14
    string      s;
    int         i;
    
#ifdef DEBUG
    cout << getpid() << " - prijat TCP Urgent packet" << endl;  
#endif
    //SIGURG jsme dostali, pze na nas cekaji OOB data, takze si je precteme
    //v nasem pripade by to mel byt jen ASCII #255, ale klienti to posilaj
    //spatne, takze to radsi nekontrolujeme
    i = recv(client_socket, &msg, MAX_SCANNED, MSG_OOB); 
    // je nutne DM precist, jinak kdyby prisly dalsi urgentni data, tak by se
    // zaradil do normalniho streamu
#ifdef DEBUG
    cout << "prijato " << i << " OOB bytu" << endl;
    cout << "msg[0] = '" << msg[0] << "' = " << (int)msg[0]  << endl;
    cout << "msg[1] = '" << msg[1] << "' = " << (int)msg[1]  << endl;
    cout << "msg[2] = '" << msg[2] << "' = " << (int)msg[2]  << endl;
#endif
   
    //mame predpokladat, ze jsme dostali ABOR? pokud ano, koncime
    //pokud je control connection sifrovane, tak se z nej nebudeme pokouset
    //cist.
    if (assume_abor && secure_cc) {
        ftp_abort = true;
        signal(SIGURG, TelnetSYNCHHandler);
        return;
    }
 
    //jeste by na nas mel cekat na control connection telneti IP signal,
    //precteme ho
    memset(msg, 0, MAX_SCANNED);
    i = read(client_socket, msg, MAX_SCANNED);
    
#ifdef DEBUG
    cout << "normalnich dat prijato " << i << endl;
    cout << "msg[0] = '" << msg[0] << "' = " << (int)msg[0]  << endl;
    cout << "msg[1] = '" << msg[1] << "' = " << (int)msg[1]  << endl;
    cout << "msg[2] = '" << msg[2] << "' = " << (int)msg[2]  << endl;
#endif

    //ted si konecne precteme ten prikaz co nam klient poslal
    memset(msg, 0, MAX_SCANNED);
    i = read(client_socket, msg, MAX_SCANNED);
    
    //mame predpokladat, ze jsme dostali ABOR? pokud ano, koncime
    if (assume_abor) {
        ftp_abort = true;
        signal(SIGURG, TelnetSYNCHHandler);
        return;
    }
    
#ifdef DEBUG
    cout << "normalnich dat2 prijato " << i << endl;
    cout << "msg = '" << msg << "'" << endl;
#endif
    
    s = msg;
    ToLower(s);
    //pokud jsme dostali ABOR, nastavime ftp_abort na true, tim se prerusi
    //probihajici prenos dat
    //pro najiti toho abor radsi pouzijeme find, pze napriklad TotalCommander
    //posila <IP>ABOR ...
    if (s.find("abor") != string::npos) ftp_abort = true;
    if (s.find("quit") != string::npos) { 
        ftp_abort = true; 
        run = false; 
        FTPReply(221, "smallFTPd closing control connection. Bye bye, and come again ;)");
    }
    if (s.find("stat") != string::npos) FTPReply(500, "Unknown command.");
    
    //kvuli IglooFTP si to precteme jeste jednou ... hruza ..
    if (i < 5) { //pokud jsme dostali naposledy neco divnyho, zkusime to znova
        memset(msg, 0, MAX_SCANNED);
        i = read(client_socket, msg, MAX_SCANNED);
    
#ifdef DEBUG
        cout << "normalnich dat3 prijato " << i << endl;
        cout << "msg = '" << msg << "'" << endl;
#endif
   
        s = msg;
        ToLower(s);
        //pokud jsme dostali ABOR, nastavime ftp_abort na true, tim se prerusi
        //probihajici prenos dat
        if (s.find("abor") != string::npos) ftp_abort = true;
        if (s.find("quit") != string::npos) { 
            ftp_abort = true; 
            run = false; 
            FTPReply(221, "smallFTPd closing control connection. Bye bye, and come again ;)");
        }
        if (s.find("stat") != string::npos) FTPReply(500, "Unknown command.");
    }//IglooFTP
    
    signal(SIGURG, TelnetSYNCHHandler);
}


/** Obsluha signalu SIGHUP.
 *
 * Zpusobi znovunacteni souboru s ucty a souboru zakazanych IP adres.
 * 
 */
void HUPHandler(int arg)
{
#ifdef DEBUG
   cout << getpid() << " dostali jsme SIGHUP" << endl;
#endif
   LoadAccountFile(account_file.c_str());   
   LoadIPDenyList(ip_deny_list_file.c_str());
   reload_config_file = true; //signal pro znovunahrani konfiguracniho souboru virtualniho filesystemu
}

/** Obsluha signalu SIGPIPE.
 *
 * SIGPIPE se odchytava z bezpecnostnich duvodu. Kdybychom ho neodchytavali,
 * tak by se nam mohlo stat, ze ho dostaneme behem readu nebo writu a to nas
 * program ukonci, coz urcite nechceme.
 *
 */
void PipeHandler(int arg) {
#ifdef DEBUG
    cout << getpid() << " - Prijato SIGPIPE" << endl;
#endif
}



/** Obsluha signalu SIGTERM.
 *
 * Smaze soubor s cislem PID a zpusobi ukonceni serveru.
 *
 */
void TermHandler(int arg) {
#ifdef DEBUG
    cout << getpid() << " dostali jsme SIGTERM" << endl;
#endif
    if (parent) {
        unlink(pid_file.c_str());
        finish = true;
    } else run = false;
}


/** Nastavi handlery signalu.
 * 
 * Signal SIGURG by se mel odchytavat pouze v pripade, ze se nepouziva
 * SSL/TLS, protoze jeho procedury nepodporuji urgentni pakety.... jenze ve
 * skutecnosti klienti nejakym zpusobem posilaji urgentni pakety i v sifrovanem
 * spojeni, takze ten signal odchytavat budeme.
 * 
 */
void InitSignalHandlers() {
    int ret;

    ret = sigaction(SIGCHLD, &ReapAction, NULL); // nastavime handler, ktery zabrani zombie
#ifdef DEBUG
    ERR(ret,InitSignalHandlers())
#endif
        
    ret = sigaction(SIGHUP, &HUPAction, NULL); // handler pro nacteni konfiguraku na SIGHUP
#ifdef DEBUG
    ERR(ret,InitSignalHandlers())
#endif
        
    if (!use_tls) ret = sigaction(SIGURG, &SynchAction, NULL); // nastavime handler, ktery se vyrovna s Telnetim SYNCH
#ifdef DEBUG
    ERR(ret,InitSignalHandlers())
#endif    

    // v pripade, ze posilame data a klient zavre spojeni, mohli bychom dostat
    // SIGPIPE, takze ho pro jistotu budeme odchytavat
    ret = sigaction(SIGPIPE, &PipeAction, NULL); 
#ifdef DEBUG
    ERR(ret,InitSignalHandlers())
#endif        

    ret = sigaction(SIGTERM, &TermAction, NULL);
#ifdef DEBUG
    ERR(ret, InitSignalHandlers());
#endif
}



