
/** @file smallFTPd.cpp 
 *  \brief Implementace FTP serveru smallFTPd - hlavni soubor.
 * 
 * Soubor obsahuje globalni promenne, vychozi nastaveni, inicializacni rutiny,
 * zpracovani prikazoveho radku, funkci main() s hlavnim cyklem serveru a
 * funkci obsluhujici administratorsky prikaz DENYIP.
 *      V hlavni funkci se nejdrive nactou konfiguracni soubory a nastavi
 * handlery signalu (odchytava se napriklad SIGHUP - signal pro znovunacteni
 * konfiguracnich souboru, SIGPIPE pro pripad, ze by klient neocekavane ukoncil
 * spojeni a my jsme zrovna cetli ze socketu a nekolik dalsich). Pote co server
 * zpracuje argumenty, tak se daemonizuje, pokud to ma udelat a az pak zapise
 * svuj PID do souboru pid_file - musi to udelat az pote co se z nej stane
 * daemon, protoze v tu chvili se forkuje a PID se mu zmeni. Soubor s PIDem
 * vyuziva funkce fdenyip() - ktera na administratoruv prikaz denyip x,y,z,w
 * posle rodicovi SIGHUP, aby si nacetl konfiguracni soubory a zakazoval
 * pristup ze zadane IP.
 *      Pote, co se vytvori socket, na kterem server posloucha, ceka na spojeni od
 * klienta, po jehoz prichodu se forkuje. Pote potomek cte od klienta funkci
 * ClientRequest() pozadavky, parsuje je prikazem ParseCommand(), ktery mu take
 * vrati index obsluzneho funkce pro zadany prikaz v tabulce prikazu. Obsluznou
 * funkci pak spusti a zpracuje jeji navratovou hodnotu. Spojeni s klientem
 * udrzuje do doby, nez klient posle prikaz QUIT, pak potomek skonci.
 * Server pro klienta vytvari pomoci tridy VFS virtualni filesystem, ktery take
 * umoznuje kontrolu pristupovych prav klienta. 
 *      Konstruktor tridy VFS muze hodit vyjimky, ty se odchytavaji try a catch
 * blokem v rozsahu cele funkce main(). Aby probehla spravna destrukce objektu,
 * pouzije se, v pripade ze nastala kriticka chyba, skok pomoci goto na konec
 * funkce main.
 *
 */


#include <iostream>
#include "smallFTPd.h"
#include "pomocne.h"
#include "VFS.h"
#include "signaly.h"
#include "ftpcommands.h"
#include "network.h"
#include "security.h"
#include "my_exceptions.h"



#define PRINT(expr) cout << #expr " = " << expr << endl;
//#define DEBUG

user            current_user;
vector<user>    users;
vector<string>  ip_deny_list;

string account_file("account.cfg");
string vfs_config_file("vfs.cfg");
string ip_deny_list_file("deny_list.cfg");
string working_dir(".");
string pid_file("smallFTPd.PID");
string ca_list_file("root.pem");
string key_file("server.pem");
string dh_file("dh1024.pem");

bool   anonymous_allowed = true;

bool   reload_config_file = false;

int  server_listening_port    = 1900; // port na kterem ma server poslouchat
int  server_default_data_port = server_listening_port - 1; 


bool daemonize  = true;  //< mame se detachnout od terminalu nebo ne?
bool use_tls    = false; //< mame inicializovat TLS/SSL a povolit jeho pouziti?
bool tls_up     = false; //< mame pouzivat bezpecne funkce? TLS handshake uz probehl?
bool tls_dc     = false; //< mame pouzivat sifrovany prenos dat?
bool secure_cc  = false; //< secure control connection?
bool secure_dc  = false; //< secure data connection?
bool run        = true;  //< mame dal obsluhovat klienta?
bool parent     = true;
bool finish     = false; //< rekl nam administrator, ze mame skoncit?
bool ftp_abort  = false; //< dostali jsme OOB data a prikaz ABOR?
bool assume_abor= false; //< pokud dostaneme SIGURG, mame predpokladat, ze to je ABOR?

int server_data_socket; //pouziva ho fpasv
int client_data_socket;

int client_socket; //< soket pro control connection
struct sockaddr_in client_data_address;
/*
 * normalne je adresa data connection stejna jako adresa klienta
 * prikazem PORT ji muze zmenit na jakoukoliv jinou
 * 
 */

typedef int handler(list<string> &, VFS &);

handler fuser, fpass, fpasv, fport, ftype, fmode, fstru, fhelp;
handler fquit, fnoop, fpwd,  flist, fcwd , fcdup, fretr, fstor;
handler fsyst, frein, fstou, fappe, fallo, frnfr, frnto, fdele;
handler fmkd , frmd , fsite, fsize, fmdtm, frest, fauth, fpbsz;
handler fprot;

handler fdenyip, ffinish, fsettings;

char user_help[]="USER <username>         :::> login command";
char pass_help[]="PASS <password>         :::> users password";
char pasv_help[]="PASV         :::> makes server enter passive mode.";

char port_help[]="PORT h1,h2,h3,h4,p1,p2         :::> makes server connect to given address and port";
char type_help[]="TYPE A|E|I N|T|C         :::> sets data type";
char mode_help[]="MODE S|B|C         :::> sets data mode";
char stru_help[]="STRU F|R|P         :::> sets data structure";
char help_help[]="HELP <command>         :::> prints help for a given command";
char quit_help[]="QUIT         :::> makes server close control connection.";
char noop_help[]="NOOP         :::> no operation.";
char pwd_help[] ="PWD         :::> prints current working directory.";
char list_help[]="LIST <path>         :::> prints content of given or current directory";
char cwd_help[] ="CWD <path>         :::> changes current directory";
char cdup_help[]="CDUP         :::> changes current directory to its parent directory";
char retr_help[]="RETR <file_name>         :::> downloads a file.";
char stor_help[]="STOR <file_name>         :::> uploads a file.";
char syst_help[]="SYST         :::> prints information about server system type";
char rein_help[]="REIN         :::> logouts current user, leaves control connection open";
char stou_help[]="STOU         :::> stores file to the current directory under a unique file name";
char appe_help[]="APPE <file_name>        :::> appends data to the specified file, creates one if it doesn't exist yet.";
char allo_help[]="ALLO <whatever>         :::> behaves like NOOP in smallFTPd.";
char rnfr_help[]="RNFR <file_name>        :::> sets rename from name.";
char rnto_help[]="RNTO <file_name>        :::> sets the rename to name and completes the renaming sequence.";
char dele_help[]="DELE <file_name>        :::> deletes specified file.";
char rmd_help[] ="RMD <directory_name>          :::> removes specified directory, even if it is empty.";
char mkd_help[] ="MKD <directory_name>          :::> creates specified directory.";
char site_help[]="SITE CHMOD 0xyz <path>          :::> changes mode of <path> to 0xyz";
char size_help[]="SIZE <file_name>              :::> returns size of the specified file";
char mdtm_help[]="MDTM <file_name>              :::> returns modification time of the specified file";
char rest_help[]="REST <number>                 :::> sets byte offset for resume";
char abor_help[]="ABOR                          :::> aborts runnig file transfer";
char auth_help[]="AUTH TLS                      :::> initialize secure connection";
char pbsz_help[]="PBSZ 0                        :::> sets buffer size to zero";
char prot_help[]="PROT C                        :::> insecure data connection";

char denyip_help[]="DENYIP x1,x2,x3,x4		:::> denies access from the given IP";
char finish_help[]="FINISH                      :::> kills the parent FTP process";
char settings_help[]="SETTINGS                  :::> prints daemon settings";


command command_table[]={
  {"user",user_help, fuser, 1},//1
  {"pass",pass_help, fpass, 1},
  {"pasv",pasv_help, fpasv, 0},
  {"port",port_help, fport, 6},
  {"type",type_help, ftype, 2},
  {"mode",mode_help, fmode, 1},
  {"stru",stru_help, fstru, 1},
  {"help",help_help, fhelp, 1},
  {"quit",quit_help, fquit, 0},
  {"noop",noop_help, fnoop, 0},//10
  {"pwd" , pwd_help, fpwd , 0},
  {"list",list_help, flist, 1},
  {"cwd" , cwd_help, fcwd , 1},
  {"cdup",cdup_help, fcdup, 0},
  {"retr",retr_help, fretr, 1},
  {"stor",stor_help, fstor, 1},
  {"syst",syst_help, fsyst, 0},
  {"rein",rein_help, frein, 0},
  {"stou",stou_help, fstou, 0},
  {"appe",appe_help, fappe, 1},//20
  {"allo",allo_help, fallo, 2},
  {"rnfr",rnfr_help, frnfr, 1},
  {"rnto",rnto_help, frnto, 1},
  {"dele",dele_help, fdele, 1},
  {"rmd" , rmd_help, frmd , 1},
  {"mkd" , mkd_help, fmkd , 1},
  {"site",site_help, fsite, 3},
  {"size",size_help, fsize, 1},
  {"mdtm",mdtm_help, fmdtm, 1},
  {"rest",rest_help, frest, 1},//30
  {"abor",abor_help, fnoop, 0},
  {"auth",auth_help, fauth, 1},
  {"pbsz",pbsz_help, fpbsz, 1},
  {"prot",prot_help, fprot, 1},
  {"denyip",denyip_help, fdenyip, 4},
  {"finish",finish_help, ffinish, 0},
  {"settings",settings_help, fsettings, 0},
	0
};

int number_of_commands = 37;

/** Vytiskne na stdout informace o pouziti programu.
 *
 */
void PrintHelp() {
    string s;

    s = gdbm_version;
    if (s.find("This is ") == 0) s.erase(0, strlen("This is "));
    
    cout << MY_NAME " " MY_VERSION << " (" << __DATE__ << ")" << endl;
    //cout << "(" << s << ")" << endl;

    cout << endl << MY_NAME " " << " [PREPINACE]" << endl;
    cout << "   -a <jmeno_souboru>    jmeno souboru s ucty (bez cesty)" << endl;
    cout << "   -v <jmeno_souboru>    konfiguracni soubor virt. filesystemu (bez cesty)" << endl;
    cout << "   -x <jmeno_souboru>    soubor zakazanych IP adres (bez cesty)" << endl;
    cout << "   -w <jmeno_souboru>    cesta k pracovnimu adresari (s konfig.soubory, atp.)" << endl;
    cout << "   -p <cislo>            cislo portu, na kterem ma server poslouchat" << endl;
    cout << "   -d                    server nepobezi na pozadi jako systemova sluzba" << endl;
    cout << "   -g                    zakaze ucet anonymous" << endl;
    cout << "   -s                    povoli pouziti TLS/SSL, pokud je zadanou jednou, tak" << endl;
    cout << "                         povoli jen sifrovne prikazy, pokud je zadano" << endl;
    cout << "                         dvakrat, tak povoli i sifrovane prenosy souboru" << endl;
    cout << "   -u                    alternativni chovani prikazu ABOR" << endl;
    cout << "   -n                    vypise vychozi nastaveni" << endl;
    cout << "   -h                    vypise tento help" << endl;
    //cout << endl;
}


/** Vytiskne na stdout vychozi nastaveni programu.
 *
 */
void PrintDefaultSetting() {
    cout << "Nastaveni " << MY_NAME << ": " << endl << endl;
    cout << "jmeno souboru s ucty   : "   << account_file      << endl;
    cout << "konfiguracni soubor vfs: "   << vfs_config_file   << endl;
    cout << "soubor zakazanych IP   : "   << ip_deny_list_file << endl;
    cout << "pracovni adresar       : "   << working_dir       << endl;
    cout << "vychozi cislo portu    : "   << server_listening_port << endl;
    cout << "ucet anonymous je      : ";
    if (anonymous_allowed) cout << "povolen" << endl; else cout << "zakazan" << endl; 
    //cout << endl;
}

/** Zpracuje argumenty programu.
 *
 * Provadi i zakladni kontrolu chyb zadanych informaci, v pripade ze je
 * argument chybny, vytiskne o tom zpravu na stdout a ukonci program.
 * 
 */
void ZpracujArgumenty(int argc, char ** argv) {
    int zn;
    int n;
    int ret;
    char *x;
    char buf[MAX_PATH_LEN];
    
    opterr = 0;
    while (1) {
        zn = getopt(argc, argv, "a:v:x:w:dp:hsung");
        if (zn == -1) 
            break;

        switch (zn) {
            case 'a':
                account_file = optarg;
                n = account_file.find('/');
                if (n != string::npos) {
                    cout << "Zadejte prosim jen jmeno souboru bez cesty. Jako cesta se pouzije pracovni adresar." << endl;
                    exit(-1);
                }
                break;
            case 'v':
                vfs_config_file = optarg;
                n = vfs_config_file.find('/');
                if (n != string::npos) {
                    cout << "Zadejte prosim jen jmeno souboru bez cesty. Jako cesta se pouzije pracovni adresar." << endl;
                    exit(-1);
                }
                break;
            case 'x':
                ip_deny_list_file = optarg;
                n = ip_deny_list_file.find('/');
                if (n != string::npos) {
                    cout << "Zadejte prosim jen jmeno souboru bez cesty. Jako cesta se pouzije pracovni adresar." << endl;
                    exit(-1);
                }
                break;
            case 'w':
                working_dir = optarg;
                //umazeme pripadne koncove lomitko
                if (working_dir != "/" && working_dir[working_dir.size()-1]=='/') working_dir.erase(working_dir.size()-1,1);
                ret = chdir(working_dir.c_str());
                if (ret == -1) {
                    cout << "Nebylo možné se přepnout do zadaného pracovniho adresáře, zkontrolujte prosím cestu a prava.";
                    cout << endl;
                    exit(-1);
                }
                
                memset(buf, 0, MAX_PATH_LEN);
                x = getcwd(buf, MAX_PATH_LEN); //vrati absolutni cestu, bez lomitka na konci
                if (x == 0) { 
                    cout << "Chyba při ověřování zadané cesty k pracovnimu adresari, zkontrolujte prosím cestu a prava.";
                    cout << endl;
                    exit(-1);
                } else working_dir = buf;
                break;
            case 'd':
                daemonize = false;
                break;
            case 'p':
                char * endptr;
                int    ret;
                ret = strtol(optarg, &endptr, 10); //prevedeme cislo portu z retezce na cislo
                if ((optarg != 0) && (*endptr == 0) && (ret > 1)) { 
                    server_listening_port = ret;
                    server_default_data_port = server_listening_port - 1;
                } else {
                    cout << "Chybne cislo portu." << endl;
                    exit(-1);
                }
                break;
            case 'g':
                anonymous_allowed = false;
                break;
            case 's':
                if (use_tls) tls_dc = true; //dostali jsme opsn -s po druhe
                use_tls = true;
                break;
            case 'u':
                assume_abor = true;
                break;
            case 'n':
                PrintDefaultSetting();
                exit(0);
                break;
            case 'h':
                PrintHelp();
                exit(0);
                break;
            case ':': 
                cout << "Prepinaci chybi parametr." << endl << endl;
                PrintHelp();
                exit(-1);
                break;
            case '?':
                cout << "Neznamy prepinac." << endl << endl;
                PrintHelp();
                exit(-1);
                break;
        }//switch

    }//while
}




void my_unexpected() {
    cout << MY_NAME " (PID " << getpid() << "): Je mi lito, nastala neocekavana chyba." << endl;
    exit(1);
}

void urgent(int signum) {
    cout << "SIGURG received" << endl;
    signal(SIGURG, urgent);
}

void SetWorkingDir() {
    int         ret;
    char        buf[MAX_PATH_LEN];
    char      * x;
    
    ret = chdir(working_dir.c_str());
    if (ret == -1) {
        cout << "Nebylo možné se přepnout do zadaného pracovniho adresáře, zkontrolujte prosím cestu a prava.";
        cout << endl;
        exit(-1);
    }
                
    memset(buf, 0, MAX_PATH_LEN);
    x = getcwd(buf, MAX_PATH_LEN); //vrati absolutni cestu, bez lomitka na konci
    if (x == 0) { 
        cout << "Chyba při ověřování zadané cesty k pracovnimu adresari, zkontrolujte prosím cestu a prava.";
        cout << endl;
        exit(-1);
    } else working_dir = buf;
}

int main(int argc, char *argv[]) try {
    int         ret;
    int         child_pid;
    int         server_len;

    //pro pripad ze nam unikne nejaka vyjimka --> aspon spadneme kulturne
    set_unexpected(my_unexpected);
    
    // Nastavime handlery signalu
    InitSignalHandlers();

    
    // Zpracujeme argumenty prikazove radky
    ZpracujArgumenty(argc, argv);
    
    //pokud nikdo nepouzil prepinac -w, musime zjistit absolutni cestu k
    //aktualnimu adresari
    if (working_dir == ".") SetWorkingDir();

    if (working_dir != "/") account_file = working_dir + "/" + account_file;
    else account_file = "/" + account_file;
    
    // Nahrajeme informace o uzivatelich - identitach (pro prikazy USER, PASS, ...)
    ret = LoadAccountFile(account_file.c_str());
    if (ret < 0)
        switch (ret) {
            case -1: cout << "Chyba pri otvirani souboru " << account_file << endl;
                     exit(-1);
            case -2: cout << "Chyba v souboru " << account_file;
                     cout << " : chybna informace, zda je ucet administratorsky" << endl;
                     exit(-1);
            case -3: cout << "Chyba pri cteni ze souboru " << account_file << endl;
                     exit(-1);
            case -4: cout << "Soubor " << account_file << " obsahuje chybny radek - byl ignorovan" << endl;
                     break;
            default:
                     cout << "Chyba pri praci se souborem " << account_file << endl;
        }//switch
    
    if (working_dir != "/") ip_deny_list_file = working_dir + "/" + ip_deny_list_file;
    else ip_deny_list_file = "/" + ip_deny_list_file;

    // Nahrajeme seznam zakazanych IP - pokud se to nepovede pokracujeme dal
    ret = LoadIPDenyList(ip_deny_list_file.c_str());
    if (ret == 0) {
        cout << "Chyba pri otvirani souboru " << ip_deny_list_file << endl;
    }
    if (ret < 0) {
        cout << "V souboru " << ip_deny_list_file << " je na radce c. " << -ret << " neplatna IP adresa." << endl;
    }

#ifdef DEBUG
    daemonize = false; //pokud debugujeme, tak chceme vystup na terminal
    PrintDeniedIPs();
    PrintAccounts(); 
#endif

    // Inicializujeme virtualni filesystem
    string db_name;
    
    //vytvorime spravne absolutni cesty ke vsem potrebnym souborum
    if (working_dir != "/") {
        db_name         = working_dir + "/" + VFS_DATABASE_NAME;
        vfs_config_file = working_dir + "/" + vfs_config_file;
        pid_file        = working_dir + "/" + pid_file;
        ca_list_file    = working_dir + "/" + ca_list_file;
        key_file        = working_dir + "/" + key_file;
        dh_file         = working_dir + "/" + dh_file;
    } else {
        db_name         = "/";
        db_name         = db_name + VFS_DATABASE_NAME;
        vfs_config_file = "/" + vfs_config_file;
        pid_file        = "/" + pid_file;
        key_file        = "/" + key_file;
        ca_list_file    = "/" + ca_list_file;
        dh_file         = "/" + dh_file;
    }
    
    VFS vfs(vfs_config_file.c_str(), db_name.c_str());
#ifdef DEBUG
    vfs.PrintVirtualTree();
#endif
        
    /* Pripravime socket a struktury na poslouchani */
    // vytvorime socket
    int server_socket = socket(PF_INET, SOCK_STREAM,0);	
    if (server_socket == -1) {
        cout << "Nelze vytvorit socket." << endl;
        //exit(-1);
        goto KONEC;
    }
    
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family      = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY; //nekdo prirazuje htonl(INADDR_ANY), imho je spravne neprevadet
    server_addr.sin_port        = htons(server_listening_port);
    server_len = sizeof(server_addr);
    
    ret = bind(server_socket, (struct sockaddr *)&server_addr, server_len);
    if (ret == -1) {
        perror("bind");
        //exit(-1);
        goto KONEC;
    }
    
    // vytvorime frontu na cekani pro klienty
    ret = listen(server_socket, MAX_WAITING_CLIENTS);
    if (ret == -1) {
        perror("listen");
        //exit(-1);
        goto KONEC;
    }

    /* --- pokud se to chce, udelame ze sebe daemona --- */
    if (daemonize) {
        ret = daemon(1, 1); //deeemonizujeme se, nechceme menit adresar, ani presmerovat I/O do /dev/null
        if (ret == -1) {
            cout << "Nebylo mozne odpojit od terminalu a bezet na pozadi jako systemovy daemon." << endl;
#ifdef DEBUG
            perror("daemon");
#endif
            goto KONEC;
        }
        //pokud jsme se dostali sem, jsme forknuti a je z nas daemon, nas otec
        //zavolal exit(0)
    }
    
    //tady uz jsme pripadne daemon, takze mame finalni PID, ulozime ho
    FILE * fd;
    fd = fopen(pid_file.c_str(), "w");
    if (fd == 0) {
        if (!daemonize) {
            cout << "Nepodarilo se vytvorit soubor s PID cislem ("<<pid_file<<") - nebude fungovat automaticke nahrani ";
            cout << "seznamu zakazanych IP, po zarazeni nove pomoci prikazu \'denyip\'." << endl;
        }
    } else {
        char cislo[100];
        sprintf(cislo, "%d\n", getpid());
        ret = fwrite(cislo, 1, strlen(cislo), fd);
        if (ret != strlen(cislo) && !daemonize) { 
            cout << "Nepodarilo se zapsat cislo PID do " << pid_file; 
            cout << ". Nebude fungovat automaticke nahrani seznamu zakazanych IP, po zarazeni nove pomoci ";
            cout << "prikazu \'denyip\'." << endl;
        }
        fclose(fd);
    }
    
    
    /* *** *** *** Hlavni cyklus *** *** *** */
    while (1) {
        string      request;
        char    *   adresa;
    
        
#ifdef DEBUG
        cout << "server ceka na spojeni" << endl;
#endif
        struct sockaddr_in client_address;
        
	int client_len = sizeof(client_address);
        do {
            if (finish) {
#ifdef DEBUG
                cout << getpid() << ": koncime na prikaz administratora" << endl;
#endif
                goto KONEC;     
            }
	    client_socket = accept(server_socket, (struct sockaddr*)&client_address, (socklen_t *)&client_len);
	} while (client_socket==-1 && errno==EINTR); //dulezite! kvuli preruseni acceptu signalem SIGCHLD

	if (client_socket == -1) {
            if (!daemonize) perror("accept"); 
            //exit(-1); 
            goto KONEC;
        }
        client_data_address = client_address; //defaultne se data connection vytvari na stejnou adresu a port
        
	adresa = inet_ntoa(client_address.sin_addr);
#ifdef DEBUG
	cout << "Prijato spojeni od " << adresa << endl;
#endif

        //zkontrolujeme, jestli nemame znova nahrat konfiguracni soubor - tj.
        //jestli jsme nahodou nekdy nedostali SIGHUP
        if (reload_config_file) {
            ret = vfs.ReloadConfigFile(vfs_config_file.c_str());
            reload_config_file = false;
            if (ret < 0 && !daemonize) {
                cout << "Chyba pri nahravani konfiguracniho souboru " << vfs_config_file << endl;
            }
        }
        
        /* *** Fork *** */
        child_pid = fork();
	if (child_pid == -1) { //chyba asi leda z duvodu nedostatku pameti
            perror("fork");
            //exit(-1);
            goto KONEC;
        }
				
	if (child_pid == 0) { //pokud jsem potomek
            
            parent = false;
            if (CheckIP(adresa) == 0) {
                if (!daemonize) { // pokud jsem daemon, nebudu nic tisknout
                    cout << "Pokus o spojeni ze zakazane IP " << adresa << endl;
                }
                //exit(0); // IP je zakazana, takze koncime
                goto KONEC;
            }

            //inicializujeme generator nahodnych cisel - kvuli prikazu STOU
            time_t      x;
            unsigned int u;
    
                x = time(0);
            u = (unsigned int) x;
            srand(u); 
 
                
            ret = FTPReply(220,"Service ready.");
            if (ret < 0) {
                if (!daemonize) cout << MY_NAME " (PID " << getpid() << "): Klient ukoncil spojeni." << endl;
                //exit(-1);
                goto KONEC;
            }

           //Pokud mame pouzivat sifrovane prenosy, inicializujeme TLS
           if (use_tls) { 
               TLSInit();
               TLSDataInit();
           }

           //Musime zajistit, ze opravdu odchytime SIGURG
           ret = fcntl(client_socket, F_SETOWN, getpid());
#ifdef DEBUG
           if (ret < 0) cout <<"fcntl failed" << endl; else cout << "fcntl Ok." << endl;
#endif

            /****************************************************************
             * ***         Cyklus obsluhujici pozadavky klienta         *** */
	    do {
                list<string> args;
		
                if (secure_cc) ret = ClientSecureRequest(request);
                    else  ret = ClientRequest(request);
                if (ret == 0) {
                    if (!daemonize) cout << getpid() << " - Klient neocekavane ukoncil spojeni." << endl;
                    //exit(-1);
                    goto KONEC;
                }
                if (ret < 0) {
                    if (!daemonize) cout << "Systemova chyba pri cteni pozadavku od klienta." << endl;
                    //exit(-1);
                    goto KONEC;
                }
#ifdef DEBUG
                cout << "--------- server pid " << getpid() << " - pozadavek od klienta: \'" << request << "\'" << endl;
#endif
		if (!args.empty()) args.erase(args.begin(), args.end());
                ret = ParseCommand(request, args);
                if (ret >= 0) {
                   ret = command_table[ret].handler(args, vfs); // zavolame handler, ktery obslouzi pozadavek
		   if (ret < 0)
                   switch (ret) {
                        case -1: if (!daemonize) cout << "---  Chyba pri posilani odpovedi klientovi" << endl;
                            goto KONEC;
                        case -2: if (!daemonize) cout << "---  Spatny deskriptor" <<endl;
                            goto KONEC;
                        case -4: if (!daemonize) cout << "---  Nedostatek pameti" << endl;
                            goto KONEC;
                        case -5: if (!daemonize) cout << "---  Protokol neni podporovan" << endl;
                            goto KONEC;
                        case -6: if (!daemonize) cout << "---  Pocitac nema IP" << endl;
                            goto KONEC;
                        case -7: if (!daemonize) cout << "---  Klient odmitl spojeni" << endl;
                            goto KONEC;
                        case -8: if (!daemonize) cout << "---  Sit neni dosazitelna" << endl;
                            goto KONEC;
                        default: if (!daemonize) cout << "---  Nejaka chyba pri posilani odpovedi klientovi" << endl;
                            goto KONEC;
                   }
                } else
                    FTPReply(500,"Unknown command."); // prikaz je pro nas neznamy
                
            } while (run);    // hodnotu promenne run muze zmenit prikaz QUIT, resp. fce fquit()
            /****************************************************************/
            

            do {
                ret = close(client_socket);
            } while (ret == -1 && errno == EINTR); //dokud nas bude prerusovat signal, budeme se pokouset znova zavrit
#ifdef DEBUG
            if (ret == -1) {
                cout << getpid() << " - Chyba pri zavirani socketu klienta." << endl;
                if (errno == EBADF) cout << "           - pry to neni regulerni deskritpor." << endl;
                if (errno == EIO)   cout << "           - I/O chyba." << endl;
            }
#endif
            //exit(0); // moje prace jako potomka, ktery obsluhoval klienta, skoncila
            goto KONEC;
	} else { //jsem rodic
#ifdef DEBUG
	    cout << "Pozadavek klienta s IP " << adresa << " obsluhuje proces PID " << child_pid << endl;
#endif
            
            do {
	        ret = close(client_socket);
            } while (ret == -1 && errno == EINTR); //tady je zavreni socketu jeste dulezitejsi nez v potomkovi
            if (ret == -1) {
                ret == close(client_socket); //zkusime to jeste jednou, tady je to fakt dulezity
#ifdef DEBUG
                if (ret == -1) { //neda se nic delat ....
                    cout << "Chyba pri zavirani socketu klienta v rodicovi!" << endl;
                    if (errno == EBADF) cout << "           - pry to neni regulerni deskritpor." << endl;
                    if (errno == EIO)   cout << "           - I/O chyba." << endl;
                }
#endif
            } //if ret == -1
	}//else jsem rodic
    }//while(1)    

    /* Sem se nikdy nedostaneme */

    // goto KONEC je tu vsude misto treba exit(-1) kvuli tomu, aby se zavolaly
    // destruktory a skoncilo se ciste.
KONEC:
    if (parent) unlink(pid_file.c_str()); // label musi ukazovat na nejaky konkretni kod
    if (tls_up && use_tls) { 
        TLSClean();
        TLSDataClean();
    }
} catch (VFSError &x) {
    if (!daemonize) cout << MY_NAME " (PID " << getpid() << "): " << x.what() << endl;
    if (x.ErrorNum() == -1) {
        if (daemonize) cout << MY_NAME " (PID " << getpid() << "): " << x.what() << endl;
        cout << "Chyba nastala na radku cislo " << -x.ErrorSubNum() << endl;
    }
    exit(-1);
    
} catch (GdbmError &x) {
    if (!daemonize) cout << MY_NAME " (PID " << getpid() << "): Chyba knihovny Gdbm: " << x.what() << endl;
    exit(-1);
} catch (FileError &x) {
    if (!daemonize) cout << MY_NAME " (PID " << getpid() << "): Chyba pri praci se souborem: " << x.what() << endl;
} catch (...) {
    if (!daemonize) cout << MY_NAME " (PID " << getpid() << "): Je mi lito, nastala neocekavana chyba." << endl;
    exit(-1);
}    





