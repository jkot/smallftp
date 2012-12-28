
/** @file ftpcommands.cpp
 *  \brief Implementace funkci obsluhujicich jednotlive FTP prikazy.
 *
 * Obsahuje i nektere pomocne funkce, jako napr. generovani souboru s
 * jedinecnym jmenem k zadanemu adresari pro STOU a posilani dat po data
 * connection.
 * Vetsina funkci jejichz jmeno zacina na "f" ma navratove hodnoty shodne s
 * funkci FTPReply().
 * 
 */


#include "ftpcommands.h"

bool logged_in  = false; //< uz se nekdo uspesne nalogoval?
bool passive    = false; //< prenosy dat v pasivnim modu?
bool restart    = false; //< chce klient obnovit prenos?
bool pbsz       = false; //< pred PROT musi byt PBSZ 0, byl uz?

long restart_offset = 0; //< odkud zacit prenos

char transfer_type      = TYPE_ASCII; //< pro type command, implicitne ASCII
char transfer_typep     = 'N'; //< parametr transfer type, implicitne Non-print, nepouziva se
char transfer_mode      = MODE_STREAM; //< pro mode command, implicitne Stream
char file_structure     = STRU_FILE; //< pro stru command, implicitne File

char FTP_EOR[2]     = {255, 1}; //< EOR kod pro record structure
char FTP_EOF[2]     = {255, 2}; //< EOF kod pro record structure
char FTP_EOR_EOF[2] = {255, 3}; //< kombinace EOR a EOF kodu pro record structure

string rename_from = ""; //<pro prikazy RNFR, RNTO
string rename_to   = ""; //<pro prikazy RNFR, RNTO
VFS_file rename_from_info("","");

union WORD {
  unsigned short w;
  struct { unsigned char l; unsigned char h; } x;
};




/** Funkce obsluhujici FTP prikaz USER.
 *
 * Inicializuje prihlasovaci proceduru. Do current_user.name ulozi zadane
 * jmeno, ktere dal zpracuje prikaz PASS (funkce fpass()). Je-li uz nejaky 
 * uzivatel nalogovan, odloguje ho a nastavi aktualni adresar na "/".
 *
 * Navratove hodnoty:
 *
 * viz. FTPReply()
 *
 */
int fuser(list<string> &args, VFS &vfs) {
    int ret;
    
    if (logged_in) { //zrusime info o aktualnim uzivateli - odlogujeme ho
        current_user.Clear(); 
        logged_in = false;
        vfs.ChangeDir("/");
    }

    if (args.size() == 1) {
        ret = FTPReply(500,"Missing username.");
    } else {
        args.pop_front(); //vyhodime jmeno prikazu
        current_user.name = args.front();
        args.pop_front();
        ret = FTPReply(331,"User name Okay, need password.");
    }

    return ret;
}//fuser()





/** Funkce obsluhujici FTP prikaz PASS.
 *
 * V pripade uspechu naloguje klienta a povoli mu nastavenim globalni promenne
 * logged_in na true provadet dalsi prikazy. Uzivatelovo jmeno a heslo musi byt
 * v globalnim seznamu users nactenem ze souboru account_file.
 * 
 * Navratove hodnoty:
 *
 * viz. FTPReply()
 *
 */
int fpass (list<string> &args, VFS &vfs) {
    int         ret;
    string      password;    
    vector<user>::iterator it;

    if (logged_in) {
        ret = FTPReply(503,"Bad command sequence");
        return ret;
    }
  
    if (args.size() == 1) {
        ret = FTPReply(500,"Missing password.");
    } else {
        args.pop_front();
        password = args.front();
        args.pop_front();
        
        if (current_user.name != "anonymous") {
            it = users.begin();
            while (it != users.end()) { // zjistime, jestli jsou poskytnute udaje v nasem seznamu uctu
                if ((current_user.name == it->name) && (password == it->password)) {
                    current_user = *it;
                    logged_in = true;
                    break;
                }
                it++;
            }//while
        } else if (anonymous_allowed) {
            current_user.password = password;
            current_user.is_admin = false;
            logged_in = true;
        }
        
        if (!logged_in) {
            ret = FTPReply(530,"Login failed.");
            return ret;
        } else { // OK, user se uspesne nalogoval:
            //dame VFS vedet, kdo se nalogoval, aby spravne vracel jen jemu pristupne soubory;
            vfs.FtpUserName(current_user.name); 

            ret = FTPReply(230,"Logged in, proceed.");
            return ret;
        }
    }//else u if argc == 1   
}//fpass()




/** Funkce obsluhujici FTP prikaz PASV.
 *
 * Pripravi server na pasivni roli pri vytvareni data connection. Nastavi
 * globalni promennou passive na true.
 * 
 * Socket nebudeme nepojmenovavat.
 *
 * Prikazem PASV nas klient zada, abychom poslouchali na nedefaultnim
 * dataportu (viz RFC959 str. 19), pokud mozno pokazde jinem - resp. volnem,
 * cimz se vyresi problem s posilanim vic souboru v kratkem case (ve STREAM
 * modu, kde je jako EOF potreba ukoncit spojeni) (aby TCP zajistilo
 * spolehlive spojeni, musi jeste na portu chvili cekat).
 * 
 * Pokud socket pomoci bindu nepojmenujeme, nasledne volani listen nebo
 * accept mu prideli nahodny volny port a lokalni adresu nastavi na
 * INADDR_ANY (viz man 7 ip), coz je presne co potrebujeme. 
 *
 * Navratove hodnoty:
 *
 * jako FTPReply() + :
 *      - -4      nedostatek pameti
 *      - -5      protokol neni podporovan
 *      - -6      pocitac na kterem server bezi nema pridelenou IP
 *
 */
int fpasv(list<string> &, VFS &) {
    int         ret;
    int         delka;
    struct sockaddr_in tmp_addr;
    union  WORD w;
    struct hostent * info;  // info o host, plne jmeno a jeho ip
    char   host_name[HOST_NAME_MAX+1];
    char   ip[ADDR_LENGTH_MAX+1];
    char * p;
    char   sreply[200];
    char * jmeno;

    if (!logged_in) {
        ret = FTPReply(530,"Not logged in.");
        return ret;
    }

    passive = true;

    // vytvorime socket pro data connection, na kterem budeme poslouchat
    server_data_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_data_socket == -1) {
        switch (errno) {
            case ENOBUFS:
            case ENOMEM: //nedostatek pameti
            case ENFILE:
                return -4;
            case EINVAL: //protokol neni podporovan
                return -5;
        }
    }
    

    // vytvorime frontu na cekani - tentokrat nam uplne staci delky 1
    ret = listen(server_data_socket,1);
    if (ret == -1) {
        switch (errno) {
            case EADDRINUSE: return -1; //tohle nenastane - listen si samo vybere volny port
            case EBADF: return -2; //spatny deskriptor
            case ENOTSOCK: return -2; //neni to socket
            default: return -1;
        } 
    }
    
    // zjistime jaky port nam listen priradil
    delka = sizeof(tmp_addr);
    ret = getsockname(server_data_socket,(struct sockaddr*)&tmp_addr,(socklen_t *)&delka);
    if (ret == -1) {
        switch (errno) {
            case EBADF: return -2;
            case ENOTSOCK: return -2;
            case ENOBUFS: return -4; //nedostatek sys. zdroju
            default: return -1;
        }
    }

    w.w = ntohs(tmp_addr.sin_port);    
    jmeno = inet_ntoa(tmp_addr.sin_addr);
#ifdef DEBUG
   // cout << "fpasv(): getsockname tvrdi, ze se jmenujeme \'" << jmeno << "\'" << endl;
#endif

    // zjistime svou IP adresu:
    ret = gethostname(host_name, HOST_NAME_MAX);
    if (ret == -1) return -1;
    
    info = gethostbyname(host_name);
    if (info == NULL)
        switch (errno) {
            case HOST_NOT_FOUND: return -1; // nezname sami sebe??
            case NO_ADDRESS:
#ifdef DEBUG
                cout << "fpasv(): gethostbyname - pry nemame pridelenou zadnou IP." << endl;
#endif
                return -6;
            default: return -1;
        }
    
    strncpy(ip,inet_ntoa(*(struct in_addr*)info->h_addr_list[0]), ADDR_LENGTH_MAX);
    
    //adresu tvaru 192.168.1.1 prevedeme na 192,168,1,1
    p = ip;
    while (*p){
        if (*p=='.') *p=',';
        p++;
    }
    
    ret = snprintf(sreply, 200, "Entering passive mode (%s,%d,%d).", ip, w.x.h, w.x.l);
    if (ret >= 200 || ret < 0) return -1; //nepodarilo se vytvorit zpravu
    
    ret = FTPReply(227, sreply);
    
    return ret;
}




/** Funkce obsluhujici FTP prikaz PORT.
 *
 * V argumentu dostane od klienta adresu na kterou se ma pripojit pro vytvoreni
 * data connection. Nastavi globalni promennou passive na false. V pristim
 * datovem prenosu bude server v aktivnim modu.
 * 
 * Navratove hodnoty:
 *
 * viz. FTPReply()
 *
 */
int fport(list<string> &args, VFS &) {
    int         ret;
    char        adresa[20];
    union WORD  port;
    long        cislo;
    char      * p;
    string      c[7];

    if (!logged_in) {
        ret = FTPReply(530, "Not logged in."); 
        return ret;
    }
  
    if (args.size() < 7) {
        ret = FTPReply(501, "PORT: too few parameters."); 
        return ret;
    }

    args.pop_front();
    
    c[1] = args.front(); args.pop_front();
    c[2] = args.front(); args.pop_front();
    c[3] = args.front(); args.pop_front();
    c[4] = args.front(); args.pop_front();
    c[5] = args.front(); args.pop_front();
    c[6] = args.front(); args.pop_front();
    ret = snprintf(adresa, 20, "%s.%s.%s.%s", c[1].c_str(), c[2].c_str(), c[3].c_str(), c[4].c_str());
    if (ret >= 20 || ret < 0) return -1; //pokud se to stane, tak je to fakt divny

    cislo = strtol(c[5].c_str(), &p, 10);
    if (cislo == LONG_MIN || cislo == LONG_MAX || p == c[5].c_str()) {
#ifdef DEBUG
        perror("* ** ** ** * fport(): strtol1");
#endif
        return -1;
    }
    port.x.h = cislo; 
    
    cislo = strtol(c[6].c_str(), &p, 10);
    if (cislo == LONG_MIN || cislo == LONG_MAX || p == c[6].c_str()) {
#ifdef DEBUG
        perror("* ** ** ** * fport(): strtol2");
#endif
        return -1;
    }
    port.x.l = cislo;
  
    client_data_address.sin_family         = AF_INET;
    client_data_address.sin_addr.s_addr    = inet_addr(adresa);
    client_data_address.sin_port           = htons(port.w);
    
    passive = false;

    ret = FTPReply(200,"PORT command okay.");
    return ret;
}


/** Funkce obsluhujici FTP prikaz TYPE.
 *
 * Nastavi typ prenosu po data connection, podle ktereho se budou ridit
 * napriklad funkce obsluhujici prikaz STOR nebo RETR.
 * 
 * Navratove hodnoty:
 *
 * viz. FTPReply()
 *
 */
int ftype(list<string> &args, VFS &) {
    int         ret;
    string      s;

    
    if (!logged_in) {
        ret = FTPReply(530,"Not logged in.");          
        return ret;
    }

    if (args.size() == 1) {
        ret = FTPReply(200,"Okay, using default parameter A N (ASCII Non-print).");
        return ret;
    } else {
        args.pop_front();
        s = args.front(); args.pop_front();
        if (s.size() != 1) {
            ret = FTPReply(501,"Syntax error."); 
            return ret;
        }
        
        switch (toupper(s[0])) {
            case TYPE_ASCII:
              ret = FTPReply(200,"Command okay.");
              transfer_type = TYPE_ASCII;
              break;
            case TYPE_EBCDIC:
              ret = FTPReply(504,"Type EBCDIC not implemented.");
              break;
            case TYPE_IMAGE:
              ret = FTPReply(200,"Command okay.");
              transfer_type = TYPE_IMAGE;
              break;
            default:
              ret = FTPReply(501,"Syntax error - bad parameter.");
        }//switch
    }//else
    
    return ret;
}//ftype()



/** Funkce obsluhujici FTP prikaz MODE.
 *
 * Navratove hodnoty:
 *
 * viz. FTPReply()
 *
 */
int fmode(list<string> &args, VFS &) {
    int         ret;
    string      s;
    
    if (!logged_in) {
        ret = FTPReply(530,"Not logged in."); 
        return ret;
    }
    
    if (args.size() == 1) {
        ret = FTPReply(200,"Okay, using default parameter S (Stream).");
    } else {
        args.pop_front();
        s = args.front(); args.pop_front();
        if (s.size() != 1) {
            ret = FTPReply(501,"Syntax error in parameter."); 
            return ret;
        }
        
        switch (toupper(s[0])) {
            case MODE_STREAM:
                transfer_mode = MODE_STREAM;
                ret = FTPReply(200,"Mode stream set.");
                break;
            case MODE_BLOCK:
                transfer_mode = MODE_BLOCK;
                ret = FTPReply(200,"Mode block set.");
                break;
            case MODE_COMPRESSED:
                ret = FTPReply(504,"Command not implemented for that parameter.");
                break;
            default:
                ret = FTPReply(501,"Syntax error - unknown parameter.");
        }
    }
    
    return ret;
}


/** Funkce obsluhujici FTP prikaz STRU.
 *
 * Navratove hodnoty:
 *
 * viz. FTPReply()
 *
 */
int fstru(list<string> &args, VFS &) {
    int         ret;
    string      s;
    
    if (!logged_in) {
        ret = FTPReply(530,"Not logged in."); 
        return ret;
    }

    if (args.size() == 1) {
        ret = FTPReply(200,"Okay, using defalut parameter F (file).");
    } else {
        args.pop_front();
        s = args.front(); args.pop_front();
        if (s.size() != 1) {
            ret = FTPReply(501,"Syntax error."); 
            return ret;
        }
        
        switch (toupper(s[0])) {
            case STRU_FILE:
                file_structure = STRU_FILE;
                ret = FTPReply(200,"Structure file set.");
                break;
            case STRU_RECORD:
                file_structure = STRU_RECORD;
                ret = FTPReply(200,"Structure record set.");
                break;
            case STRU_PAGE:
                ret = FTPReply(504,"Command not implemented for that parameter.");
                break;
            default:
                ret = FTPReply(501,"Syntax error - unknown parameter.");
        }
    }

    return ret;
}//fstru()



/** Funkce obsluhujici FTP prikaz HELP.
 *
 * Posle klientovi zpet help k pozadovanemu prikazu.
 * 
 * Navratove hodnoty:
 *
 * viz. FTPReply()
 *
 */
int fhelp(list<string> &args, VFS &) {
    int         ret;
    int         i; 
    string      s;
  
    switch (args.size()) {
        case 1: //jen samotny prikaz HELP
            ret = FTPReply(211,"smallFTPd v. 1.0");
            break;
        case 2:
            args.pop_front();
            s = args.front(); args.pop_front();
            i = GetCommandIndex(s);
            if (i == -1) ret = FTPReply(501,"There is no help available for that command.");
                    else ret = FTPReply(214, command_table[i].help);
            break;
        default:
            ret = FTPReply(501,"Usage: HELP <command>.");
    }
  
    return ret;
}//fhelp()




/** Funkce obsluhujici FTP prikaz QUIT.
 *
 * Nastavi globalni promennou run na false, coz zpusobi ukonceni provadeni
 * cyklu ktery obsahuje obsluhu klienta.
 *
 * Navratove hodnoty:
 *
 * viz. FTPReply()
 *
 */
int fquit(list<string> &, VFS &) {
    int         ret;
    
    ret = FTPReply(221,"smallFTPd closing control connection. Bye bye, and come again ;)");
    run = false; //ukonci hlavni cyklus obsluhujici klienta
    
    return ret;
}



/** Funkce obsluhujici FTP prikaz NOOP.
 *
 * Pouze odesle odpoved s kodem 200, Command Okay.
 * 
 * Navratove hodnoty:
 *
 * viz. FTPReply()
 *
 */
int fnoop(list<string> &, VFS &) {
    int         ret;
    
    ret = FTPReply(200,"Command Okay.");
    return ret;
}


/** Funkce obsluhujici FTP prikaz PWD.
 *
 * Posle klientovi odpoved s aktualnim adresarem. Odpoved posila ve tvaru jaky
 * naznacuje RFC959.
 * 
 * Navratove hodnoty:
 *
 * viz. FTPReply()
 *
 */
int fpwd(list<string> &, VFS &vfs) {
    int         ret;
    char        sreply[MAX_PATH_LEN];
  
    if (!logged_in) {
        ret = FTPReply(530, "Not logged in."); 
        return ret;
    }
    
    ret = snprintf(sreply, MAX_PATH_LEN, "\"%s\" is your current location.", vfs.CurrentDir().c_str());
    if (ret >= MAX_PATH_LEN || ret < 0) return -1;
    
    ret = FTPReply(257, sreply);
    return ret;
}







/** Funkce obsluhujici FTP prikaz LIST.
 *
 * Posila klientovi obsah zadaneho adresare. Informace posila vzdy ve tvaru
 * prikazu ls -l. Pokud klient zada prikaz "LIST -a" nebo "LIST -aL", tak
 * argument jednoduse ignoruje. Pro ziskani informaci o souborech se pouzivaji
 * funkce NextFile(), ResetFiles() a GetLslInfo(), uzivatel musi mit dostatecna
 * prava, aby mohl provest prikaz LIST.
 *
 */
int flist(list<string> &args, VFS &vfs) { 
    // kdyz uz v nejakem adresari jsme, urcite k nemu ma klient prava!
    // a tedy muze vylistovat soubory v nem obsazene
    try{
    int         argc = args.size();
    int         ret;
    string      path;
    string      old_dir;
    string      s;
    
    if (!logged_in) {
        ret = FTPReply(530, "Not logged in."); 
        return ret;
    }
    
    if (argc > 2) {
        ret = FTPReply(501, "Syntax error.");
        return ret;
    }
    
    args.pop_front();
    if (argc == 1) path = "."; else { path = args.front(); args.pop_front(); }
    string tmp;
    tmp = path;
    ToLower(tmp);
    if (tmp == "-a" || tmp == "-al" || tmp == "-l" || tmp == "-la") path = ".";
    old_dir = vfs.CurrentDir();

    //jen pokud je to adresar....!!!
    if (vfs.IsDir(path.c_str())) {
        VFS_file  * file;
        
        ret = vfs.ChangeDir(path.c_str());
        if (ret < 0) 
            switch (ret) {
                case -2: ret = FTPReply(450, "Permission denied.");
                         return ret;
                case -3: ret = FTPReply(450, "Doesn't exist.");
                         return ret;
                case -4: ret = FTPReply(450, "Too long name.");
                         return ret;
                case -5: ret = FTPReply(450, "Can't go further up than root is.");
                         return ret;
                case -6: ret = FTPReply(450, "Permisson denied.");
                         return ret;
                default: ret = FTPReply(450, "Local processing error.");
                         return ret;
            }
    
//        ret = FTPReply(150,"Ok, about to open data connection.");
//        if (ret < 0) return ret;
        
        ret = CreateDataConnection();
                //Data connection nam sice nejde vytvorit, ale treba jeste bude mozna
                //pokracovat a vyjde to priste, proto vracime 1
        if (ret < 0) return 1;
 
        vfs.ResetFiles();
        while ((file = vfs.NextFile()) != 0) {
            ret = file->GetLslInfo(s);
            if (ret < 0) continue; //nelze ziskat info o souboru, jdem na dalsi
           
            //pokud tenhle uzivatel s tim souborem nesmi pracovat, tak ho ani neuvidi 
            if (file->UserName() != current_user.name && 
                file->UserName() != NO_USER && 
                file->UserName() != "anonymous" &&
                file->IsRegularFile()) continue; //adresare nepreskakujeme
            
            if (transfer_type == TYPE_ASCII) s = s + "\r\n"; //pridame na konec CRLF    
                else s = s + "\n";
            
            if (transfer_mode == MODE_STREAM && file_structure == STRU_RECORD) {
                s = s + FTP_EOR;
            }
            
            ret = SendDataLine(s.c_str());
            if (ret < 0) {  //nelze posilat data, koncime
                FTPReply(426, "Data connection lost.");
                
                if (passive) close(server_data_socket);
                passive = false;
                close(client_data_socket);
                
                return 1;
            }
            delete file;    
        }//while
        
        if (transfer_mode == MODE_STREAM && file_structure == STRU_RECORD) {
            //at mame typ ASCII nebo IMAGE musime ted poslat EOF
            SendData(FTP_EOF, sizeof(FTP_EOF));
        }//if

        vfs.ChangeDir(old_dir.c_str()); //prepneme se zpet
    } else { // je-li to soubor
        VFS_file file("","");

        ret = vfs.GetFileInfo(path.c_str(), file);
        if (ret < 0) {
            FTPReply(450, "Bad file name.");
            
            if (passive) close(server_data_socket);
            passive = false;
            close(client_data_socket);
            
            return 1;
        }

        ret = file.GetLslInfo(s);
        if (ret < 0) {
            FTPReply(450, "Bad file name.");
            
            if (passive) close(server_data_socket);
            passive = false;
            close(client_data_socket);
            
            return 1;
        }
        
//        ret = FTPReply(150,"Ok, about to open data connection.");
//        if (ret < 0) return ret;
        
        ret = CreateDataConnection();
                //Data connection nam sice nejde vytvorit, ale treba jeste bude mozna
                //pokracovat a vyjde to priste, proto vracime 1
        if (ret < 0) return 1;

        if (transfer_type == TYPE_ASCII) s = s + "\r\n";
            else s = s + "\n";
            
        ret = SendDataLine(s.c_str());
        if (ret < 0) {  //nelze posilat data, koncime
            FTPReply(426, "Data connection lost.");
            
            if (passive) close(server_data_socket);
            passive = false;
            close(client_data_socket);

            return 1;
        }
        
        if (transfer_mode == MODE_STREAM && file_structure == STRU_RECORD) {
            //at mame typ ASCII nebo IMAGE musime ted poslat EOF
            SendData(FTP_EOR_EOF, sizeof(FTP_EOR_EOF));
        }//if
    }//byl to soubor

    
    ret = FTPReply(226,"Closing data connection. LIST successful.");
    if (passive) close(server_data_socket); 
    passive = false;
    close(client_data_socket);

  return 1;
    } catch (...) { cout << MY_NAME ": je mi lito, nastala neocekavana vyjimka ve funkci flist()" << endl; return -1; }
}// --- flist() ---





/** Funkce obsluhujici FTP prikaz CWD.
 *
 * Prepne uzivatele do zadaneho adresare, pokud k tomu ma dostatecna prava, coz
 * zajistuje funkce VFS::ChangeDir().
 *
 */
int fcwd(list<string> &args, VFS &vfs) {
    int         ret;
    int         argc = args.size();
    string      s;
    
    if (!logged_in) {
        ret = FTPReply(530, "Not logged in.");
        return ret;
    }

    if (argc == 1) {
        ret = FTPReply(501, "CWD needs a parameter.");
        return ret;
    }
    
    if (argc > 2) {
        ret = FTPReply(501, "Syntax error.");
        return ret;
    }
    args.pop_front();
    s = args.front(); args.pop_front();
    
    ret = vfs.ChangeDir(s.c_str());
    if (ret < 0) 
        switch (ret) {
            case -2: ret = FTPReply(550, "Permission denied.");
                    return ret;
            case -3: ret = FTPReply(550, "Doesn't exist.");
                    return ret;
            case -4: ret = FTPReply(550, "Too long name.");
                    return ret;
            case -5: ret = FTPReply(550, "Can't go further up than root is.");
                    return ret;
            case -6: ret = FTPReply(550, "Permisson denied");
                    return ret;
            default: ret = FTPReply(550, "Local processing error.");
                    return ret;
        }
    
    ret = FTPReply(250,"CWD Okay.");
    return ret;
} // --- fcwd() ---



/** Funkce obsluhujici FTP prikaz CDUP.
 *
 * V RFC959 se tvrdi, ze reply kody na CWD a CDUP by mely byt identicke, ale
 * pak v prehledu kodu, je na potvrzeni uspechu prikazu CWD pouzit kod 250,
 * kdezto pro CDUP je to 200 --> budeme pouzivat v obou pripadech prihodnejsi
 * kod 250. Funkce umoznuje pristup pouze do adresaru ke kterym ma uzivatel
 * pravo cteni - to zajistuje funkce VFS::ChangeDir(). 
 *
 */
int fcdup(list<string> &, VFS &vfs) {
    int         ret;
    
    if (!logged_in) {
        ret = FTPReply(530,"Not logged in.");
        return ret;
    }

    ret = vfs.ChangeDir("..");
    if (ret < 0) 
        switch (ret) {
            case -2: ret = FTPReply(550, "Permission denied.");
                    return ret;
            case -3: ret = FTPReply(550, "Doesn't exist.");
                    return ret;
            case -4: ret = FTPReply(550, "Too long name.");
                    return ret;
            case -5: ret = FTPReply(550, "Can't go further up than root is.");
                    return ret;
            case -6: ret = FTPReply(550, "Permisson denied.");
                    return ret;
            default: ret = FTPReply(550, "Local processing error.");
                    return ret;
        }
    
    ret = FTPReply(250,"CDUP ok.");
    return ret;
} // --- fcdup() ---





/** Funkce obsluhujici FTP prikaz RETR.
 *
 * Posila vyzadany soubor klientovi. Kontroluje, zda k tomu ma dostatecna
 * prava. Podle globalni promenne transfer_type zjisti, jestli ma soubor
 * posilat v rezimu ASCII nebo IMAGE.
 *
 */
int fretr(list<string> &args, VFS &vfs) {
    int         ret;
    VFS_file    file("","");
    string      name;
    FILE      * fd;
    bool        cti = true;
    const int   BUF_SIZE = 4096;
    char        buffer[BUF_SIZE];
    char        buffer2[2*BUF_SIZE]; //do nej se prevede buffer s tim, ze misto LF se zapise CRLF
    int         nacteno;
    unsigned long long   transferred = 0;
    
    if (!logged_in) {
        ret = FTPReply(530,"Not logged in."); 
        return ret;
    }

    if (args.size() == 1) {
        ret = FTPReply(501,"RETR command needs a parameter specifying a file to be transfered."); 
        return ret;
    } 

    args.pop_front();
    name = args.front(); args.pop_front();

    ret = vfs.GetFileInfo(name.c_str(), file);
    if ( (ret < 0) 
            || !(   (current_user.name == file.UserName() && (file.UserRights() & R_READ)) ||
                     (file.OthersRights() & R_READ)   ||
                    (file.UserName()=="anonymous" && (file.UserRights() & R_READ))  ) 
            ) {
        FTPReply(550, "File not available.");
#ifdef DEBUG
        cout << "RETR ret = " << ret << " file.UserName = "<< file.UserName() << endl;
        cout << "usr rights = " << file.UserRights() << " oth_r = " << file.OthersRights() << endl;
#endif
        if (passive) close(server_data_socket);
        passive = false;
        close(client_data_socket);
            
        return 1;
    }
 
    if (file.Path()!="/") name = file.Path() + "/" + file.Name(); else name = "/" + file.Name();
    
    fd = fopen(name.c_str(),"r");
    if (fd == 0) { 
        ret = FTPReply(450, "File busy."); 
        return ret;
    }

    if (restart) {
        ret = fseek(fd, restart_offset, SEEK_SET);
        if (ret < 0) {
            ret = FTPReply(450, "Error while resuming file transfer.");
            return ret;
        }
        restart = false;
        restart_offset = 0;
    }
    
//    ret = FTPReply(150,"File status Okay, about to open data connection.");
//    if (ret < 0) return ret;

    ret = CreateDataConnection();
    if (ret < 0) {
        fclose(fd);
        //Data connection nam sice nejde vytvorit, ale treba jeste bude mozna
        //pokracovat a vyjde to priste, proto vracime 1
        return 1;
    }

    while (cti) {
        nacteno = fread(buffer, 1, BUF_SIZE, fd);
        if (feof(fd)) { cti = false; }
        if (ferror(fd)) {
            cti = false;
            FTPReply(451,"Requested action aborted: local error in processing.");
            fclose(fd);
            if (secure_dc) TLSDataShutdown();
            if (passive) close(server_data_socket); 
            passive = false;
            return 1;
        }
        
        //muzeme dostat OOB data upozornujici na ABOR, handler SIGURGu nastavi
        //abort na true
        if (ftp_abort) {
#ifdef DEBUG
            cout << getpid() << "fretr(): aborting" << endl;
#endif
            ret = FTPReply(426,"Transfer aborted.");
            if (ret < 0) { 
                ftp_abort = false; passive = false; 
                close(client_data_socket); if (passive) close(server_data_socket); 
                return ret;
            }
            ret = FTPReply(226,"Closing data connection.");
            if (ret < 0) { 
                ftp_abort = false; passive = false; 
                close(client_data_socket); if (passive) close(server_data_socket); 
                return ret;
            }
            fclose(fd);
            if (secure_dc) TLSDataShutdown();
                else close(client_data_socket);
            if (passive) close(server_data_socket); 
            passive     = false;
            ftp_abort   = false; 
            return 1;            
        }

        if (transfer_type == TYPE_ASCII) nacteno = LF2CRLF(buffer2, buffer, nacteno);
            else memcpy(buffer2, buffer, nacteno); //pro IMAGE type
        ret = SendData(buffer2, nacteno);
        if (ret < 0) {  //nelze posilat data, koncime
            FTPReply(426, "Data connection lost.");
               
            if (secure_dc) TLSDataShutdown();
                else close(client_data_socket);
            if (passive) close(server_data_socket);
            passive = false;
                
            return 1;
        }
        
    }//while
    
    if (transfer_mode == MODE_STREAM && file_structure == STRU_RECORD) {
        //at mame typ ASCII nebo IMAGE musime ted poslat EOF
        SendData(FTP_EOF, sizeof(FTP_EOF));
    }//if

    fclose(fd);
    FTPReply(226,"Closing data connection. RETR successful.");
    if (secure_dc) TLSDataShutdown();
        else close(client_data_socket);

    if (passive) {
        close(server_data_socket);
        passive = false;
    }

    return 1;
}//fretr()



/** Funkce obsluhujici FTP prikaz STOR.
 *
 * Funkce provadi upload zadaneho souboru na server. Podle globalni promenne
 * transfer_type pozna jestli ma prenos probihat v rezimu ASCII nebo IMAGE.
 * Kontroluje, jestli ma uzivatel dostatecna prava pro zapis do zadaneho
 * adresare. Informace o novem souboru ulozi do databaze.
 *
 */
int fstor(list<string> &args, VFS &vfs) {
    int         ret;
    VFS_file    file("","");
    string      argument;
    string      dir;
    int         n;
    FILE      * fd;
    const int   BUF_SIZE = 4096;
    char        buffer[BUF_SIZE];
    char        buffer2[BUF_SIZE]; //do nej se prevede buffer s tim, ze misto LF se zapise CRLF
    int         nacteno;
    bool        CR = false;

    
    if (!logged_in) {
        ret = FTPReply(530,"Not logged in."); 
        return ret;
    }

    if (args.size() == 1) {
        ret = FTPReply(501,"STOR command needs a parameter specifying a file to be transfered."); 
        return ret;
    }
    
    args.pop_front();
    argument = args.front(); args.pop_front();
    
    n = argument.rfind('/');
    if (n == string::npos) { //je to jen jmeno bez cesty
        file.Name(argument);
        
        string path;
        vfs.ConvertToPhysicalPath(".", path);
        file.Path(path);
        dir = ".";
    } else {
        file.Name(argument.substr(n+1, argument.size()-n));
        
        string path;  
        if (n != 0) {
            vfs.ConvertToPhysicalPath(argument.substr(0, n), path);
            dir = argument.substr(0, n);
        } else {
            vfs.ConvertToPhysicalPath("/", path);//argument je tvaru "/jmeno_souboru.txt"
            dir = "/";
        }
        file.Path(path);
    }
    
    if ( !vfs.AllowedToWriteToDir(dir) ) {
        ret = FTPReply(553, "Filename or directory not allowed.");
#ifdef DEBUG
        cout << "fstor: dir = " << dir << endl;
        cout << "fstor: name = " << file.Name() << endl;
        cout << "fstor: path = " << file.Path() << endl;
#endif
        return ret;
    }

    string tmp;
    if (file.Path() != "/") tmp = file.Path() + "/" + file.Name(); else tmp = "/" + file.Name();
/*
    fd = fopen(tmp.c_str(),"r"); //overime jestli soubor uz neexistuje, abychom ho neprepsali
    if (fd != 0) {
        ret = FTPReply(450, "STOR not taken, file already exists.");
        return ret;
    }
*/    
    
    if (restart) {         
        ret = truncate(tmp.c_str(), restart_offset);
        if (ret < 0) {
            //nekdo muze testovat jestli umime REST tak ze zada REST 100 a
            //nasledne REST 0, tj. pri nasledujicim STOR bychom se pokusili
            //restartovat soubor, ktery neexistuje, osetrime to
            if (errno == ENOENT) { //soubor neexistuje, zkusime ho vytvorit
                fd = fopen(tmp.c_str(), "w");
                if (fd != 0) fclose(fd);
                else {
                    ret = FTPReply(550,"Error while trying to resume.");
                    return ret;
                }
            } else {
                ret = FTPReply(550, "Error while trying to resume.");
                return ret;
            }
        }

        fd = fopen(tmp.c_str(), "a");
        if (fd == 0) {
            ret = FTPReply(550,"Unable to open file.");
            return ret;
        }
        
        restart = false;
        restart_offset = 0;
    } else { 
        fd = fopen(tmp.c_str(), "w"); 
        if (fd == 0) {
            ret = FTPReply(450,"STOR not taken, error while creating/accessing the file."); 
            return ret;
        }
    }


    
//    ret = FTPReply(150,"File status Okay, about to open data connection.");
//    if (ret < 0) return ret;

    ret = CreateDataConnection();
    if (ret < 0) {
        fclose(fd);
        //Data connection nam sice nejde vytvorit, ale treba jeste bude mozna
        //pokracovat a vyjde to priste, proto vracime 1
        return 1;
    }
    
    while (1) {
        nacteno = ReceiveData(buffer, BUF_SIZE);
        if (nacteno < 0) {
            ret = FTPReply(451, "STOR aborted: local error in processing");
            if (secure_dc) TLSDataShutdown();
            if (passive) close(server_data_socket);
            passive = false;
            fclose(fd);
            return ret;
        }

        //muzeme dostat OOB data upozornujici na ABOR, handler SIGURGu nastavi
        //abort na true
        if (ftp_abort) {
            ret = FTPReply(426,"Transfer aborted.");
            if (ret < 0) { 
                ftp_abort = false; passive = false; 
                close(client_data_socket); if (passive) close(server_data_socket); 
                return ret;
            }
            ret = FTPReply(226,"Closing data connection.");
            if (ret < 0) { 
                ftp_abort = false; passive = false; 
                close(client_data_socket); if (passive) close(server_data_socket); 
                return ret;
            }
            fclose(fd);
            if (secure_dc) TLSDataShutdown();
                else close(client_data_socket);
            if (passive) close(server_data_socket); 
            passive = false;
            ftp_abort   = false; 
            return 1;            
        }

        if (nacteno == 0) break; //konec souboru
        
        if (transfer_type == TYPE_ASCII)  {
            //osetreni CRLF na konci bufferu (pripad, ze na konci bufferu bude
            //jen CR a priste bude na zacatku bufferu LF)
            if (buffer[0] == '\n' && CR) {
                CR = false;
               // fwrite("\n", 1, 1, fd);
               // memmove(buffer, buffer + 1, nacteno - 1); //posuneme buffer o 1 zpet
               // nacteno--;
            } else if (buffer[0] != '\n' && CR) {
                CR = false;
                fwrite("\r", 1, 1, fd);
            }
        
            if (buffer[nacteno-1] == '\r') { 
                CR = true;
                nacteno--;
            }
        
            nacteno = CRLF2LF(buffer2, buffer, nacteno);
        }
        
        
        if (transfer_type == TYPE_ASCII) {
            if (transfer_mode == MODE_STREAM && file_structure == STRU_RECORD) nacteno = EraseEOR(buffer2, nacteno);
            n = fwrite(buffer2, 1, nacteno, fd);
        }
        else {
            if (transfer_mode == MODE_STREAM && file_structure == STRU_RECORD) nacteno = EraseEOR(buffer, nacteno);
            n = fwrite(buffer, 1, nacteno, fd);
        }
        
        memset(buffer,0, BUF_SIZE);
        memset(buffer2,0, BUF_SIZE);
        if (n != nacteno) {
            ret = FTPReply(451, "STOR aborted: local error in processing.");
            fclose(fd);
            if (secure_dc) TLSDataShutdown();
                else close(client_data_socket);
            if (passive) close(server_data_socket); 
            passive = false;
            return ret;
        }
    }
    fclose(fd);

    //doplnime informace o souboru
    file.UserRights(R_ALL);
    file.OthersRights(R_NONE);
    file.UserName(current_user.name);
    
#ifdef DEBUG
    cout << "fstor: usr_r = " << file.UserRights()<< endl;
    cout << "fstor: oth_r = " << file.OthersRights() << endl;
    cout << "fstor: usr_n = " << file.UserName() << endl;
#endif
    
    //a ulozime je do databaze
    ret = vfs.PutFileInfo(file);
    
#ifdef DEBUG
    if (ret < 0) cout << "*** fstor(): chyba pri praci s databazi ... " << ret << endl;
#endif
    
    if (secure_dc) TLSDataShutdown();
        else close(client_data_socket);
    
    ret = FTPReply(226,"Closing data connection. STOR successful.");
    if (passive) close(server_data_socket); 
    passive = false;
    return ret;
}//fstor()



/** Funkce obsluhujici FTP prikaz SYST.
 *
 * Vrati odpoved s kodem 215 a slovem UNIX - tim dava klientovi napriklad
 * najevo, ze by mohl a mel pouzivat po STOR file, take SITE CHMOD 0xyz file.
 * 
 */
int fsyst(list<string> &args, VFS &) {
    int         ret;
    
    if (args.size() != 1) {
        ret = FTPReply(501, "SYST can't have parameters.");
        return ret;
    }


    ret = FTPReply(215, "UNIX - smallFTPd was created for RedHat Linux 8.0 (Psyche)");
    return ret;
}


/** Funkce obsluhujici FTP prikaz REIN.
 *
 * Odloguje aktualniho uzivatele, zmeni aktualni adresar na "/" a ceka na 
 * login dalsiho uzivatele. Nezavira control connection.
 * 
 */
int frein(list<string> &args, VFS &vfs) {
    int         ret;

    if (logged_in) { //zrusime info o aktualnim uzivateli - odlogujeme ho
        current_user.Clear(); 
        logged_in = false;
        vfs.ChangeDir("/");
    }
    
    ret = FTPReply(220, "smallFTPd ready for new user.");
    return ret;
}


/** Vygeneruje string o length nahodnych znacich.
 *
 * Pouziva rand(), neinicializuje pomoci srand(), takze je to nutne nekde
 * udelat sam. String zkrati nebo prodlouzi na prislusnou delku.
 *
 */
void RandomString(int length, string &s) {
    int i = 0;
    int r = 0;

    s.resize(length); //chceme string velikosti length
    while (i < length) {
        do {
            r = 32+(int)(128.0*rand()/(RAND_MAX + 1.0)); //generujeme cisla v rozsahu 32 az 128
        } while (!isalnum(r));
        s[i] = (char)r;
        i++;
    }
}

/** Pokusi se v adresari dir vytvorit soubor s dosud neexistujicim jmenem.
 *
 * Pokud se nepodari napoprve vygenerovat jedinecne jmeno o length znacich pro
 * dany adresar, zkusi to jeste MAX_TIMES krat (MAX_TIMES je lokalni promenna).
 * 
 * Navratove hodnoty:
 *
 *      - -1            nepovedlo se, v _name vrati ""
 *      - jinak         vrati file descriptor noveho souboru otevreneho pro cteni i
 * zapis, jmeno souboru bez cesty ulozi do _name
 *
 */
int CreateUniqueFile(string dir, int length, string &_name) {
    int         MAX_TIMES = 10;
    int         fd;
    string      name;
    string      file_name;
    int         opakovani = 0;

    if ((dir[dir.size()-1] == '/') && dir != "/") dir.erase(dir.size()-1, 1); //umazeme pripadne lomitko na konci
    
    do {
        opakovani++;
        RandomString(length, name);
        if (dir != "/") file_name = dir + "/" + name; else file_name = "/" + name;
        fd = open(file_name.c_str(), O_CREAT | O_EXCL | O_RDWR, 0600);
    } while (opakovani < MAX_TIMES && fd == -1);

    if (fd == -1) {
        _name = "";
        return -1;
    } else {
        _name = name;
        return fd;
    }
}

/** Funkce obsluhujici FTP prikaz STOU.
 *
 * Narozdil od fstor() pouziva pro zapis do souboru funkci write() - to protoze
 * funkce CreateUniqueFile() pouziva pro jeho vytvoreni funkci open() s
 * priznaky O_CREAT a O_EXCL, takze je zajisteno, ze soubor vytvori jen nas
 * proces a bude k nemu mit exkluzivni pristup. Vytvorene jmeno souboru bude 10
 * nahodnych alfabetickych znaku.
 *
 */
int fstou(list<string> &args, VFS &vfs) {
    int         ret;
    VFS_file    file("","");
    string      argument;
    string      dir;
    string      name;
    int         n;
    int         fd;
    const int   BUF_SIZE = 4096;
    char        buffer[BUF_SIZE];
    char        buffer2[BUF_SIZE]; //do nej se prevede buffer s tim, ze misto LF se zapise CRLF
    int         nacteno;
    bool        CR = false;

    
    if (!logged_in) {
        ret = FTPReply(530,"Not logged in."); 
        return ret;
    }

    if (args.size() != 1) {
        ret = FTPReply(501,"STOU command doesn't have a parameter."); 
        return ret;
    }
    
    
    if ( !vfs.AllowedToWriteToDir(".") ) {
        ret = FTPReply(553, "You are not allowed to write to this directory.");
        return ret;
    }

    vfs.ChangeDir(".");
    dir = vfs.CurrentPhysicalDir();
    
    
    fd = CreateUniqueFile(dir, 10, name); 
    if (fd == -1) {
        ret = FTPReply(450,"STOU not taken, unable to create unique file."); 
        return ret;
    }
    
    file.Path(dir);
    file.Name(name);
    
//    ret = FTPReply(150,"File status Okay, about to open data connection.");
//    if (ret < 0) return ret;

    ret = CreateDataConnection();
    if (ret < 0) {
        close(fd);
        //Data connection nam sice nejde vytvorit, ale treba jeste bude mozna
        //pokracovat a vyjde to priste, proto vracime 1
        return 1;
    }
    
    while (1) {
        nacteno = ReceiveData(buffer, BUF_SIZE);
        if (nacteno < 0) {
            ret = FTPReply(451, "STOR aborted: local error in processing");
            if (passive) close(server_data_socket);
            passive = false;
            close(fd);
            return ret;
        }
        //muzeme dostat OOB data upozornujici na ABOR, handler SIGURGu nastavi
        //abort na true
        if (ftp_abort) {
            ret = FTPReply(426,"Transfer aborted.");
            if (ret < 0) { 
                ftp_abort = false; passive = false; 
                close(client_data_socket); if (passive) close(server_data_socket); 
                return ret;
            }
            ret = FTPReply(226,"Closing data connection.");
            if (ret < 0) { 
                ftp_abort = false; passive = false; 
                close(client_data_socket); if (passive) close(server_data_socket); 
                return ret;
            }
            close(fd);
            close(client_data_socket);
            if (passive) close(server_data_socket); 
            passive = false;
            ftp_abort   = false; 
            return 1;            
        }

        if (nacteno == 0) break; //konec souboru
        
        if (transfer_type == TYPE_ASCII) { //ASCII TYPE
            //osetreni CRLF na konci bufferu (pripad, ze na konci bufferu bude
            //jen CR a priste bude na zacatku bufferu LF)
            if (buffer[0] == '\n' && CR) {
                CR = false;
               // fwrite("\n", 1, 1, fd);
               // memmove(buffer, buffer + 1, nacteno - 1); //posuneme buffer o 1 zpet
               // nacteno--;
            } else if (buffer[0] != '\n' && CR) {
                CR = false;
                write(fd, "\r", 1);
            }
        
            if (buffer[nacteno-1] == '\r') { 
                CR = true;
                nacteno--;
            }
        
            nacteno = CRLF2LF(buffer2, buffer, nacteno);
        }
        
        if (transfer_type == TYPE_ASCII) {
            if (transfer_mode == MODE_STREAM && file_structure == STRU_RECORD) nacteno = EraseEOR(buffer2, nacteno);
            n = write(fd, buffer2, nacteno);
        } else {
            if (transfer_mode == MODE_STREAM && file_structure == STRU_RECORD) nacteno = EraseEOR(buffer, nacteno);
            n = write(fd, buffer, nacteno);
        }
        
        memset(buffer,0, BUF_SIZE);
        memset(buffer2,0, BUF_SIZE);
        if (n != nacteno) {
            ret = FTPReply(451, "STOR aborted: local error in processing.");
            close(fd);
            close(client_data_socket);
            if (passive) close(server_data_socket); 
            passive = false;
            return ret;
        }
    }
  
    close(fd);

    //doplnime informace o souboru
    file.UserRights(R_ALL);
    file.OthersRights(R_NONE);
    file.UserName(current_user.name);

    //a ulozime je do databaze
    ret = vfs.PutFileInfo(file);
    
#ifdef DEBUG
    if (ret < 0) cout << "*** fstou(): chyba pri praci s databazi ... " << ret << endl;
#endif
    
    string s;
    s = file.Name() + " - file transfer successful."; 
    ret = FTPReply(226,s.c_str());
    close(client_data_socket);
    if (passive) close(server_data_socket); 
    passive = false;
    return ret;
}//fstou()



/** Funkce obsluhujici FTP prikaz APPE.
 * 
 * Dela prakticky to same jako fstor(), jen pokud soubor existuje, pripisuje na
 * jeho konec, pokud neexistuje, vytvori ho.
 *
 */
int fappe(list<string> &args, VFS &vfs) {
    int         ret;
    VFS_file    file("","");
    string      argument;
    string      dir;
    int         n;
    FILE      * fd;
    const int   BUF_SIZE = 4096;
    char        buffer[BUF_SIZE];
    char        buffer2[BUF_SIZE]; //do nej se prevede buffer s tim, ze misto LF se zapise CRLF
    int         nacteno;
    bool        CR = false;

    
    if (!logged_in) {
        ret = FTPReply(530,"Not logged in."); 
        return ret;
    }

    if (args.size() == 1) {
        ret = FTPReply(501,"APPE command needs a parameter specifying a file to be transfered."); 
        return ret;
    }
    
    args.pop_front();
    argument = args.front(); args.pop_front();
    
    n = argument.rfind('/');
    if (n == string::npos) { //je to jen jmeno bez cesty
        file.Name(argument);
        
        string path;
        vfs.ConvertToPhysicalPath(".", path);
        file.Path(path);
        dir = ".";
    } else {
        file.Name(argument.substr(n+1, argument.size()-n));
        
        string path;  
        if (n != 0) {
            vfs.ConvertToPhysicalPath(argument.substr(0, n), path);
            dir = argument.substr(0, n);
        } else {
            vfs.ConvertToPhysicalPath("/", path);//argument je tvaru "/jmeno_souboru.txt"
            dir = "/";
        }
        file.Path(path);
    }
    
    if ( !vfs.AllowedToWriteToDir(dir) ) {
        ret = FTPReply(553, "Filename or directory not allowed.");
        return ret;
    }

    string tmp;
    if (file.Path() != "/") tmp = file.Path() + "/" + file.Name(); else tmp = "/" + file.Name();
    
    fd = fopen(tmp.c_str(),"a"); 
    if (fd == 0) {
        ret = FTPReply(450,"APPE not taken, unable to create or open file."); 
        return ret;
    }
    
//    ret = FTPReply(150,"File status Okay, about to open data connection.");
//    if (ret < 0) return ret;

    ret = CreateDataConnection();
    if (ret < 0) {
        fclose(fd);
        //Data connection nam sice nejde vytvorit, ale treba jeste bude mozna
        //pokracovat a vyjde to priste, proto vracime 1
        return 1;
    }
    
    while (1) {
        nacteno = ReceiveData(buffer, BUF_SIZE);
        if (nacteno < 0) {
            ret = FTPReply(451, "APPE aborted: local error in processing");
            if (passive) close(server_data_socket);
            passive = false;
            fclose(fd);
            return ret;
        }
        
        //muzeme dostat OOB data upozornujici na ABOR, handler SIGURGu nastavi
        //abort na true
        if (ftp_abort) {
            ret = FTPReply(426,"Transfer aborted.");
            if (ret < 0) { 
                ftp_abort = false; passive = false; 
                close(client_data_socket); if (passive) close(server_data_socket); 
                return ret;
            }
            ret = FTPReply(226,"Closing data connection.");
            if (ret < 0) { 
                ftp_abort = false; passive = false; 
                close(client_data_socket); if (passive) close(server_data_socket); 
                return ret;
            }
            fclose(fd);
            close(client_data_socket);
            if (passive) close(server_data_socket); 
            passive     = false;
            ftp_abort   = false; 
            return 1;            
        }

        if (nacteno == 0) break; //konec souboru
        
        if (transfer_type == TYPE_ASCII) { //ASCII TYPE
            //osetreni CRLF na konci bufferu (pripad, ze na konci bufferu bude
            //jen CR a priste bude na zacatku bufferu LF)
            if (buffer[0] == '\n' && CR) {
                CR = false;
               // fwrite("\n", 1, 1, fd);
               // memmove(buffer, buffer + 1, nacteno - 1); //posuneme buffer o 1 zpet
               // nacteno--;
            } else if (buffer[0] != '\n' && CR) {
                CR = false;
                fwrite("\r", 1, 1, fd);
            }
        
            if (buffer[nacteno-1] == '\r') { 
                CR = true;
                nacteno--;
            }
        
            nacteno = CRLF2LF(buffer2, buffer, nacteno);
        }
        
        if (transfer_type == TYPE_ASCII) { 
            if (transfer_mode == MODE_STREAM && file_structure == STRU_RECORD) nacteno = EraseEOR(buffer2, nacteno);
            n = fwrite(buffer2, 1, nacteno, fd);
        } else { 
            if (transfer_mode == MODE_STREAM && file_structure == STRU_RECORD) nacteno = EraseEOR(buffer, nacteno);
            n = fwrite(buffer, 1, nacteno, fd);
        }
        
        memset(buffer,0, BUF_SIZE);
        memset(buffer2,0, BUF_SIZE);
        if (n != nacteno) {
            ret = FTPReply(451, "APPE aborted: local error in processing.");
            fclose(fd);
            close(client_data_socket);
            if (passive) close(server_data_socket); 
            passive = false;
            return ret;
        }
    }
  
    fclose(fd);

    //doplnime informace o souboru
    file.UserRights(R_ALL);
    file.OthersRights(R_NONE);
    file.UserName(current_user.name);

    //a ulozime je do databaze
    ret = vfs.PutFileInfo(file);
    
#ifdef DEBUG
    if (ret < 0) cout << "*** fappe(): chyba pri praci s databazi ... " << ret << endl;
#endif
    
    ret = FTPReply(226, "Closing data connection. APPE successful.");
    close(client_data_socket);
    if (passive) close(server_data_socket); 
    passive = false;
    return ret;
}//fstor()


/** Funkce obsluhujici FTP prikaz ALLO.
 *
 * Funkce ma vyhradit misto pro ulozeni souboru. Velikost dopredu znat
 * nepotrebujeme, takze v nasem pripade se chova jako NOOP.
 *
 */
int fallo(list<string> &args, VFS &) {
    int         ret;

    if (!logged_in) {
        ret = FTPReply(530,"Not logged in."); 
        return ret;
    }
   
    ret = FTPReply(202, "No storage allocation necessary.");    
    return ret;
}

/** Funkce obsluhujici FTP prikaz RNFR.
 *
 * Informaci o souboru, ktery mame prejmenovat ulozi do globalni promenne
 * rename_from, kterou pak pouziva nasledujici prikaz od klienta RNTO.
 * 
 */
int frnfr(list<string> &args, VFS &vfs) {
    int         ret;
    int         n;
    string      argument;
    string      name;
    string      physical_path;

    if (!logged_in) {
        ret = FTPReply(530, "Not logged in.");
        return ret;
    }

    if (args.size() != 2) {
        ret = FTPReply(501, "Syntax error in parameter.");
        return ret;
    }        

    args.pop_front();
    argument = args.front(); args.pop_front();

    ret = vfs.GetFileInfo(argument.c_str(), rename_from_info);
    if ( (ret < 0) 
            || !(   (current_user.name == rename_from_info.UserName() && (rename_from_info.UserRights() & R_WRITE)) 
                                                    || (rename_from_info.OthersRights() & R_WRITE)     ) 
            ) {
        FTPReply(550, "File not available.");
#ifdef DEBUG
        cout << "RNFR ret = " << ret << " file.UserName = "<< rename_from_info.UserName() << endl;
        cout << "usr rights = " << rename_from_info.UserRights() << " oth_r = " << rename_from_info.OthersRights() << endl;
#endif
        return 1;
    }
    
    
    /* prevedeme virtualni jmeno souboru na fyzicke */
    
    n = argument.rfind('/');
    if (n == string::npos) { //je to jen jmeno bez cesty
        name = argument;
        
        string s;
        vfs.ConvertToPhysicalPath(".", s);
        physical_path = s;
    } else {
        name = argument.substr(n+1, argument.size()-n);
        
        string s;  
        if (n != 0) {
            vfs.ConvertToPhysicalPath(argument.substr(0, n), s);
        } else {
            vfs.ConvertToPhysicalPath("/", s);//argument je tvaru "/jmeno_souboru.txt"
        }
        physical_path = s;
    }
 
    if (physical_path != "/") rename_from = physical_path + "/" + name; //ulozime jmeno do globalni promenne, pro RNTO
    else rename_from = "/" + name;

    ret = FTPReply(350, "Requested file action pending further information.");
    return ret;
}


/** Funkce obsluhujici FTP prikaz RNTO.
 *
 * Prejmenuje soubor z rename_from na rename_to - pokud se soubory nachazi na
 * ruznych filesystemech, neuspeje. Kontroluje, jestli ma uzivatel k souboru
 * dostatecna prava. Do promenne rename_from ulozi prazdny retezec;
 *
 */
int frnto(list<string> &args, VFS &vfs) {
    int         ret;
    int         n;
    string      argument;
    string      dir;
    string      physical_path;
    string      name;
    
    
    if (!logged_in) {
        ret = FTPReply(530, "Not logged in.");
        return ret;
    }
    
    if (args.size() != 2) {
        ret = FTPReply(501, "Syntax error in parameter.");
        return ret;
    }
    
    if (rename_from == "") {
        ret = FTPReply(503, "Bad command sequence. RNTO needs previous RNFR.");
        return ret;
    } 
    
    args.pop_front();
    argument = args.front(); args.pop_front();
    
    //rozlozime argument na jmeno soboru a cestu k nemu, prevedeme virtualni
    //cestu na fyzickou
    n = argument.rfind('/');
    if (n == string::npos) { //je to jen jmeno bez cesty
        name = argument;
        
        string s;
        vfs.ConvertToPhysicalPath(".", s);
        physical_path = s;
        dir = ".";
    } else {
        name = argument.substr(n+1, argument.size()-n);
        
        string s;  
        if (n != 0) {
            vfs.ConvertToPhysicalPath(argument.substr(0, n), s);
            dir = argument.substr(0, n);
        } else {
            vfs.ConvertToPhysicalPath("/", s);//argument je tvaru "/jmeno_souboru.txt"
            dir = "/";
        }
        physical_path = s;
    }
    
    if ( !vfs.AllowedToWriteToDir(dir) ) {
        ret = FTPReply(553, "Filename or directory not allowed.");
        rename_from = "";
        
#ifdef DEBUG 
        cout << "frnto: dir = " << dir << endl;
        cout << "frnto: name = " << name << endl;
        cout << "frnto: path = " << physical_path << endl;
#endif
        return ret;
    }
 
    if (physical_path != "/") rename_to = physical_path + "/" + name; else rename_to = "/" + name;

    ret = rename(rename_from.c_str(), rename_to.c_str());
    if (ret == -1) {
        ret = FTPReply(553, "Some problem occured while renaming - paths are maybe on different filesystems.");
#ifdef DEBUG
        cout << "from = " << rename_from << endl;
        cout << "to   = " << rename_to << endl;
        perror("RNFR");
#endif
        rename_from = "";
        return ret;
    }

    //smazeme stary zaznam z databaze
    vfs.DeleteFileInfo(rename_from_info);
    
    unlink(rename_from.c_str()); // po rename z nejakeho duvodu zustaval puvodni soubor na miste ...
    
    rename_from_info.Name(name);
    rename_from_info.Path(physical_path);
    
    //vlozime do databaze novy zaznam
    ret = vfs.PutFileInfo(rename_from_info);
#ifdef DEBUG
    if (ret < 0) cout << "*** frnto(): chyba pri praci s databazi ... " << ret << endl;
#endif

    rename_from = "";
    ret = FTPReply(250, "Renaming completed.");
    return ret;
}//frnto()


/** Funkce obsluhujici FTP prikaz DELE.
 *
 * Smaze zadany soubor, pokud k tomu ma uzivatel prava. Pote smaze i pripadny
 * zaznam o souboru v databazi. (Respektive to udela v opacnem poradi,
 * protoze ke smazani zanamu z databaze musi trida DirectoryDatabase byt
 * schopna zjistit klic k zaznamu a k tomu musi soubor existovat - tj. funkce 
 * nejdriv smaze zaznam z databaze a pote i samotny adresar - pokud se adresar
 * smazat nepovede, ulozi zaznam zpet do databaze).

 * 
 */
int fdele(list<string> &args, VFS &vfs) {
    int         ret;
    int         n;
    VFS_file    file("","");
    string      argument;
    string      name;
    string      path;
    string      dir;
    
    
    if (!logged_in) {
        ret = FTPReply(530, "Not logged in.");
        return ret;
    }
    
    if (args.size() != 2) {
        ret = FTPReply(501, "Syntax error in parameter.");
        return ret;
    }

    args.pop_front();
    argument = args.front(); args.pop_front();

    ret = vfs.GetFileInfo(argument.c_str(), file);
    if ( (ret < 0) 
            || !(   (current_user.name == file.UserName() && (file.UserRights() & R_WRITE)) 
                                                    || (file.OthersRights() & R_WRITE)     ) 
            ) {
        FTPReply(550, "Unable to delete specified file (insufficient rights?).");
#ifdef DEBUG
        cout << "DELE ret = " << ret << " file.UserName = "<< file.UserName() << endl;
        cout << "usr rights = " << file.UserRights() << " oth_r = " << file.OthersRights() << endl;
#endif
        return 1;
    }
 

    //ziskame virtualni cestu k souboru
    n = argument.rfind('/');
    if (n == string::npos) dir = "."; //jmeno bez cesty
     else {
        if (n != 0) dir = argument.substr(0, n);
        else dir = "/";
    }
    
    //zjistime, jestli mame do toho adresare pravo zapisu
    if ( !vfs.AllowedToWriteToDir(dir) ) {
        ret = FTPReply(553, "You don't have sufficient (directory) rights to delete the file.");

#ifdef DEBUG        
        cout << "fdele: dir = " << dir << endl;
        cout << "fdele: path = " << path << endl;
#endif
        return ret;
    }
    
    //sestavime si cele fyzicke jmeno souboru
    if (file.Path()!="/") name = file.Path() + "/" + file.Name(); else name = "/" + file.Name();

    //nejdriv musime smazat zaznam z db. jinak by nemohla zjistit jeho klic
    //(cislo inodu toho souboru)
    ret = vfs.DeleteFileInfo(file);
#ifdef DEBUG
    if (ret < 0) cout << "*** fdele: chyba pri praci s databazi" << endl;
#endif
    
    //a smazeme ho
    ret = unlink(name.c_str());
    if (ret < 0) {
        ret = FTPReply(450, "An error occured while deleting the file.");
        vfs.PutFileInfo(file); //musime vratit zaznam do databaze!!
        return ret;
    }

    ret = FTPReply(250, "File deleted.");
    return ret;

}//fdele()



/** Funkce obsluhujici FTP prikaz RMD.
 *
 * Smaze zadany adresar, pokud na to ma uzivatel dostatecna prava. Pote smaze i
 * pripadny zaznam o adresari v databazi (respektive to udela v opacnem poradi,
 * protoze ke smazani zanamu z databaze musi trida DirectoryDatabase byt
 * schopna zjistit klic k zaznamu a k tomu musi soubor existovat - tj. funkce 
 * nejdriv smaze zaznam z databaze a pote i samotny adresar - pokud se adresar
 * smazat nepovede, ulozi zaznam zpet do databaze).
 *
 */
int frmd(list<string> &args, VFS &vfs) {
    int         ret;
    int         n;
    VFS_file    file("","");
    string      argument;
    string      name;
    string      path;
    string      dir;
    
    
    if (!logged_in) {
        ret = FTPReply(530, "Not logged in.");
        return ret;
    }
    
    if (args.size() != 2) {
        ret = FTPReply(501, "Syntax error in parameter.");
        return ret;
    }

    args.pop_front();
    argument = args.front(); args.pop_front();
    
    if (argument[argument.size()-1] == '/') argument.erase(argument.size()-1, 1);//umazeme pripadne posledni lomitko

    ret = vfs.GetFileInfo(argument.c_str(), file);
    if ( (ret < 0) 
            || !(   (current_user.name == file.UserName() && (file.UserRights() & R_WRITE)) 
                                                    || (file.OthersRights() & R_WRITE)     ) 
            ) {
        FTPReply(550, "Unable to delete specified directory (insufficient rights?).");
#ifdef DEBUG
        cout << "RMD ret = " << ret << " file.UserName = "<< file.UserName() << endl;
        cout << "usr rights = " << file.UserRights() << " oth_r = " << file.OthersRights() << endl;
#endif 
        return 1;
    }
 

    //ziskame virtualni cestu k adresari
    n = argument.rfind('/');
    if (n == string::npos) dir = "."; //jmeno bez cesty
     else {
        if (n != 0) dir = argument.substr(0, n);
        else dir = "/";
    }
    
    //zjistime, jestli mame do toho adresare pravo zapisu
    if ( !vfs.AllowedToWriteToDir(dir) ) {
        ret = FTPReply(553, "You don't have sufficient (directory) rights to delete the directory.");
        
#ifdef DEBUG 
        cout << "frmd: dir  = " << dir << endl;
        cout << "frmd: path = " << path << endl;
#endif
        return ret;
    }
    
    //cele jmeno adresare je uvedeno ve file.Path(), file.Name() by melo byt
    //prazdne
    if (file.Path() != "/") name = file.Path() + "/" + file.Name(); else name = "/" + file.Name();


    //smazeme nejdriv(!!!) zaznam z databaze --> nelze az po rmdir, databaze by
    //nemela jak zjistit klic (cislo inodu daneho adresare)
    ret = vfs.DeleteFileInfo(file);
#ifdef DEBUG
    if (ret < 0) cout << "*** frmd: chyba pri praci s databazi." << ret << endl;
#endif
    
    //a smazeme adresar
    ret = rmdir(name.c_str());
    if (ret < 0) {
        ret = FTPReply(450, "An error occured while removing the directory (it is not empty?).");
#ifdef DEBUG
        cout << "name = " << name << endl;
        perror("rmdir");
#endif
        vfs.PutFileInfo(file); //musime udaj do databaze vratit
        return ret;
    }

    
    ret = FTPReply(250, "Directory removed.");
    return ret;

}//frmd()


/** Funkce obsluhujici FTP prikaz MKD.
 *
 * Vytvori zadany adresar, pokud na to ma uzivatel prava. Fyzicka prava
 * adresare nastavi na 0700. Ulozi informace o nove vzniklem adresari do
 * databaze.
 * 
 */
int fmkd(list<string> &args, VFS &vfs) {
    int         ret;
    int         n;
    VFS_file    info("", "");
    string      argument;
    string      dir;
    string      name;
    string      path;

    if (!logged_in) {
        ret = FTPReply(530, "Not logged in.");
        return ret;
    }
    
    if (args.size() != 2) {
        ret = FTPReply(501, "Syntax error in parameter.");
        return ret;
    }
    
    args.pop_front();
    argument = args.front(); args.pop_front();
    
    if (argument[argument.size()-1] == '/') argument.erase(argument.size()-1, 1);//umazeme pripadne posledni lomitko
    
    //rozlozime argument na jmeno soboru a cestu k nemu, prevedeme virtualni
    //cestu na fyzickou
    n = argument.rfind('/');
    if (n == string::npos) { //je to jen jmeno bez cesty
        name = argument;
        
        string s;
        vfs.ConvertToPhysicalPath(".", s);
        path = s;
        dir = ".";
    } else {
        name = argument.substr(n+1, argument.size()-n);
        
        string s;  
        if (n != 0) {
            vfs.ConvertToPhysicalPath(argument.substr(0, n), s);
            dir = argument.substr(0, n);
        } else {
            vfs.ConvertToPhysicalPath("/", s);//argument je tvaru "/jmeno_adresare"
            dir = "/";
        }
        path = s;
    }

    //zjistime, jestli mame do toho adresare pravo zapisu
    if ( !vfs.AllowedToWriteToDir(dir) ) {
        ret = FTPReply(553, "You don't have sufficient (directory) rights to create the directory.");
        
        cout << "fmkd: dir  = " << dir << endl;
        cout << "fmkd: path = " << path << endl;
        return ret;
    }
   
    string tmp;
    if (path != "/") tmp = path + "/" + name; else tmp = "/" + name;
        
    ret = mkdir(tmp.c_str(), 0700);
    if (ret < 0) {
        ret = FTPReply(550, "Unable to create specified directory.");
        return ret;
    }
    
    
    /* ulozime informace o vytvorenem adresari do databaze */
    
    info.UserName(current_user.name);
    info.UserRights(R_ALL);
    info.OthersRights(R_NONE);
    info.Name(name);
    info.Path(path);

    ret = vfs.PutFileInfo(info);
#ifdef DEBUG
    if (ret < 0) {

        cout << "**** fmkd: chyba pri praci s databazi" << endl;
    }
#endif
    
    string msg;
    msg = "\"" + argument + "\" was successfully created.";
    ret = FTPReply(257,msg.c_str());
}//fmkd()



/** Funkce obsluhujici FTP prikaz SITE.
 *
 * Momentalne je podporovano SITE CHMOD.
 * RFC959 povoluje jen pozitivni odezvu, takze vzdy odpovidame kodem 200.
 *
 */
int fsite(list<string> &args, VFS &vfs) {
    int         ret;
    long        mod;
    string      s;
    string      argument;
    char      * np;
    VFS_file    file("","");
    
    if (!logged_in) {
        ret = FTPReply(530, "Not logged in.");
        return ret;
    }
    
    
    if (args.size() != 4) { //  --->  site chmod 0xyz <cesta>
        ret = FTPReply(501, "Syntax error in parameter.");
        cout << "fsite: pocet argumentu je " << args.size()-1 << endl;
        while (!args.empty()) {
            cout << args.front() << endl;
            args.pop_front();
        }
        return ret;
    }
    
    args.pop_front(); s = args.front(); args.pop_front();
    ToLower(s);
    if (s != "chmod") {
        ret = FTPReply(202, "This site command is not implemented.");
        return ret;
    }

    s = args.front(); args.pop_front(); //nacteme mod
    argument = args.front(); args.pop_front(); //nacteme cestu

    mod = strtol(s.c_str(), &np, 8);
    if (mod == LONG_MIN || mod == LONG_MAX || s.c_str() == np) {
#ifdef DEBUG
        cout << getpid() << " fsite: strtol ... problem pri prevodu" << endl;
#endif
        ret = FTPReply(200, "Bad mode."); //v RFC959 je povolena jen pozitivni odezva
        return ret;
    }

    ret = vfs.GetFileInfo(argument.c_str(), file);
    if ((ret < 0) || (current_user.name != file.UserName())) {
        ret = FTPReply(200, "You don't have sufficient rights to change file mode.");
#ifdef DEBUG
        cout << "SITE CHMOD ret = " << ret << " file.UserName = "<< file.UserName() << endl;
        cout << "usr rights = " << file.UserRights() << " oth_r = " << file.OthersRights() << endl;
#endif
        return ret;
    }
    
    if (file.Path()!="/") s = file.Path() + "/" + file.Name(); else s = "/" + file.Name();

    ret = chmod(s.c_str(), mod);
    if (ret == -1) {
        ret = FTPReply(200, "Error while changing the mode.");
        return ret;
    }

    ret = FTPReply(200, "File mode successfully changed.");
    return ret;
}//fsite()


/** Funkce pro obsluhu smallFTPd prikazu DENYIP.
 *
 * Zaradi prijatou IP adresu do ip_deny_list.
 *
 * Navratove hodnoty:
 *
 * viz FTPReply()
 *
 */
int fdenyip(list<string> &args, VFS &) { 
    int         ret;
    int         argc;
    string      s;
    string      path;
    string      c1, c2, c3, c4;
    long        cislo;
    FILE      * fd;
    
    argc = args.size();
    
    if (!logged_in) {
        ret = FTPReply(530,"Not logged in."); 
        return ret;
    }
    
    if (argc != 5) {
        ret = FTPReply(501,"Syntax error."); 
        return ret;
    }
    
    if (!current_user.is_admin) {
        ret = FTPReply(530,"Sorry, you have to be an administrator to deny an IP."); 
        return ret;
    } 

    args.pop_front();//vyhodime jmeno prikazu;
    
    c1 = args.front();
    args.pop_front();
    
    c2 = args.front();
    args.pop_front();
    
    c3 = args.front();
    args.pop_front();
    
    c4 = args.front();
    args.pop_front();
       
    s = c1 + "." + c2 + "." + c3 + "." + c4;
    
    fd = fopen(ip_deny_list_file.c_str(), "a");
    if (fd == 0) {
#ifdef DEBUG
        cout << "fdenyip: chyba pri otvirani souboru zakazanych IP." << endl;
#endif
        ret = FTPReply(530,"Lokalni chyba..."); 
        return ret;
    }
    
    fprintf(fd,"%s.%s.%s.%s\n", c1.c_str(), c2.c_str(), c3.c_str(), c4.c_str());
    fclose(fd);
    
    ip_deny_list.push_back(s);
   
    //posleme signal rodicovi, at si nacte konfigurak
    long        parent_pid = 0;
    char        cis[100];
    FILE *      fpid;
    char *      zn;
    
    fpid = fopen(pid_file.c_str(), "r");
    if (fpid != 0) { //pokud se povedlo pid_file otevrit, nacteme PID
        ret = fread(cis, 1, 100, fpid);
        if (!ferror(fpid)) { //pokud jsme pid nacetli, prevedeme ho
            parent_pid = strtol(cis, &zn, 10);
            if (parent_pid != LONG_MIN && parent_pid != LONG_MAX && cis != zn) { //OK, prevedeno
               ret = kill(parent_pid, 1);
#ifdef DEBUG
               if (ret == -1) cout << getpid() << " nepodarilo se poslat SIGHUP rodicovi PID " << parent_pid << endl; 
#endif
            }
        }
        fclose(fpid);
    }
    
    ret = FTPReply(200,"IP denied.");
    return ret;
}

/** Funkce obsluhujici rozsirujici FTP prikaz SIZE.
 *
 * Hodnotu vraci korektne, tj. ne velikost souboru, ale pocet prenesenych bytu
 * v pripade posilani souboru po siti vzhledem k aktualnimu nastaveni prenosu
 * (ascii type nebo type image), tak jak to vyzaduje prislusny draft Ricka
 * Adamse. Prikaz pomaha zajistit spravnou podporu obnoveni prenosu dat.
 *
 */
int fsize(list<string> &args, VFS &vfs) {
    int         ret;
    int         argc = args.size();
    unsigned long file_size = 0;
    bool        cti = true;
    int         BUF_SIZE = 1024;
    char        buffer[BUF_SIZE];
    int         i;
    string      name;
    FILE      * fd;
    long        nacteno;
    VFS_file    file("","");
    
    
    if (!logged_in) {
        ret = FTPReply(530,"Not logged in."); 
        return ret;
    }
    
    if (argc != 2) {
        ret = FTPReply(501,"Syntax error."); 
        return ret;
    }
    
    args.pop_front();
    name = args.front(); args.pop_front();

    ret = vfs.GetFileInfo(name.c_str(), file);
    if ( (ret < 0) 
            || !(   (current_user.name == file.UserName() && (file.UserRights() & R_READ)) 
                                                    || (file.OthersRights() & R_READ)     ) 
            ) {
        FTPReply(550, "File not available.");
#ifdef DEBUG
        cout << "SIZE ret = " << ret << " file.UserName = "<< file.UserName() << endl;
        cout << "usr rights = " << file.UserRights() << " oth_r = " << file.OthersRights() << endl;
#endif      
        return 1;
    }
 
    if (file.Path()!="/") name = file.Path() + "/" + file.Name(); else name = "/" + file.Name();
    
    fd = fopen(name.c_str(),"r");
    if (fd == 0) { 
        ret = FTPReply(450, "File busy."); 
        return ret;
    }

    if (transfer_type == TYPE_ASCII) {
        while (cti) {
            nacteno = fread(buffer, 1, BUF_SIZE, fd);
            if (feof(fd)) { cti = false; }
            if (ferror(fd)) {
                cti = false;
                FTPReply(450,"Error while determining file size.");
                fclose(fd);
                return 1;
            }
            file_size += nacteno;
            for (i=0; i < nacteno; i++) if (buffer[i] == '\n') file_size++;
        }//while cti
    } else {
        ret = fseek(fd, 0, SEEK_END);
        if (ret < 0) {
            FTPReply(450,"Error while determining file size.");
            return 1;
        }
        file_size = ftell(fd);
        if (file_size < 0) {
            FTPReply(450,"Error while determining file size.");
            return 1;
        }
    }//else TYPE ASCII
    fclose(fd);

    snprintf(buffer, BUF_SIZE, "%lu", file_size);
    ret = FTPReply(213, buffer);
    return ret;
}//fsize()


/** Funkce obsluhujici rozsirujici FTP prikaz MDTM.
 *
 * Vraci datum a cas posledni modifikace souboru. Viz. prislusny draft Ricka
 * Adamse. Prikaz pomaha zajistit spravnou podporu obnovy prenosu dat.
 *
 */
int fmdtm(list<string> &args, VFS &vfs) {
    int         ret;
    int         argc = args.size();
    string      name;
    VFS_file    file("","");
    struct stat statbuf;
    char        cas[256];
    struct tm * time;
    
    
    if (!logged_in) {
        ret = FTPReply(530,"Not logged in."); 
        return ret;
    }
    
    if (argc != 2) {
        ret = FTPReply(501,"Syntax error."); 
        return ret;
    }
    
    args.pop_front();
    name = args.front(); args.pop_front();

    ret = vfs.GetFileInfo(name.c_str(), file);
    if ( (ret < 0) 
            || !(   (current_user.name == file.UserName() && (file.UserRights() & R_READ)) 
                                                    || (file.OthersRights() & R_READ)     ) 
            ) {
        FTPReply(550, "File not available.");
#ifdef DEBUG
        cout << "MDTM ret = " << ret << " file.UserName = "<< file.UserName() << endl;
        cout << "usr rights = " << file.UserRights() << " oth_r = " << file.OthersRights() << endl;
#endif      
        return 1;
    }
 
    if (file.Path()!="/") name = file.Path() + "/" + file.Name(); else name = "/" + file.Name();
    ret = stat(name.c_str(), &statbuf);
    if (ret == -1){
        ret = FTPReply(450, "Error while determining file properties.");
        return ret;
    }
    
    time = gmtime(&statbuf.st_ctime);
    if (time != 0) strftime(cas,256,"%Y%m%d%H%M%S",time);
    else {
        ret = FTPReply(450,"Error while determining file modification time.");
        return ret;
    }
    
    ret = FTPReply(213,cas);
    return ret;
}//fmdtm


/** Funkce obsluhujici FTP prikaz REST.
 *
 * Inicializuje obnovu prenosu dat, konkretne jeji rozsireni pro STREAM mode,
 * tak jak je popsano v draftu Ricka Adamse.
 * 
 */
int frest(list<string> &args, VFS &vfs) {
    int         ret;
    int         argc = args.size();
    string      offset;
    VFS_file    file("","");
    char *      p;
    long        cislo;
    
    
    if (!logged_in) {
        ret = FTPReply(530,"Not logged in."); 
        return ret;
    }
    
    if (argc != 2) {
        ret = FTPReply(501,"Syntax error."); 
        return ret;
    }
    
    args.pop_front();
    offset = args.front(); args.pop_front();

    cislo = strtol(offset.c_str(), &p, 10);
    if (cislo == LONG_MIN || cislo == LONG_MAX || p == offset.c_str()) {
#ifdef DEBUG
        perror("* ** ** ** * frest(): strtol");
#endif
        restart = false;
        restart_offset = 0;
        return -1;
    }
    
    restart = true;
    restart_offset = cislo;

    ret = FTPReply(350, "REST supported. Ready to resume at given byte offset.");
    return ret;
}//frest


/** Funkce obsluhujici administratorsky prikaz FINISH.
 *
 * Pokud ma uzivatel dostatecna prava, posle rodicovskemu procesu SIGTERM,
 * cimz mu da vedet, ze ma skoncit.
 *
 */
int ffinish(list<string> &, VFS &) {
    int         ret;
    
    if (!logged_in) {
        ret = FTPReply(530, "Not logged in.");
        return ret;
    }
    
    if (!current_user.is_admin) {
        ret = FTPReply(530, "Sorry you have to be an administrator to stop " MY_NAME ".");
        return ret;
    }    

    //posleme rodicovi SIGTERM
    long        parent_pid = 0;
    char        cis[100];
    FILE *      fpid;
    char *      zn;
    
    fpid = fopen(pid_file.c_str(), "r");
    if (fpid != 0) { //pokud se povedlo pid_file otevrit, nacteme PID
        ret = fread(cis, 1, 100, fpid);
        if (!ferror(fpid)) { //pokud jsme pid nacetli, prevedeme ho
            parent_pid = strtol(cis, &zn, 10);
            if (parent_pid != LONG_MIN && parent_pid != LONG_MAX && cis != zn) { //OK, prevedeno
               ret = kill(parent_pid, SIGTERM);
#ifdef DEBUG
               if (ret == -1) cout << getpid() << " nepodarilo se poslat SIGTERM rodicovi PID " << parent_pid << endl; 
#endif
            }
        }
        fclose(fpid);
    }

    ret = FTPReply(200, "Okay, server is about to stop.");
    return ret;
}


int fsettings(list<string> &args, VFS &) {
    int         ret;
    string      s;
    int         BUF_SIZE = 100;
    char        tmp[BUF_SIZE];
    
    if (!logged_in) {
        ret = FTPReply(530, "Not logged in.");
        return ret;
    }
    
    if (!current_user.is_admin) {
        ret = FTPReply(530, "Sorry you have to be an administrator to get daemon settings.");
        return ret;
    }

    s = "Account file name: " + account_file;
    ret = FTPMultiReply(200, s.c_str());
    if (ret < 0) return ret;
    
    s = "VFS config file name: " + vfs_config_file;
    ret = FTPMultiReply(200, s.c_str());
    if (ret < 0) return ret;

    s = "IP deny list file: " + ip_deny_list_file;
    ret = FTPMultiReply(200, s.c_str());
    if (ret < 0) return ret;

    s = "Working directory: " + working_dir;
    ret = FTPMultiReply(200, s.c_str());
    if (ret < 0) return ret;

    snprintf(tmp, BUF_SIZE, "%d", server_listening_port);
    s = tmp;
    s = "Server listens on port: " + s;
    ret = FTPMultiReply(200, s.c_str());
    if (ret < 0) return ret;

    ret = FTPReply(200, "End of settings.");
    return ret;
}


int fauth(list<string> &args, VFS &) {
    int         ret;
    string      s;
    
    if (!use_tls) {
        ret = FTPReply(500, "Secure mode not available.");
        return 1;
    }
    
    args.pop_front();
    s = args.front(); args.pop_front();

    ToLower(s);
    
    if (s != "tls" && s != "tls-c") {
        ret = FTPReply(501,"Requested security mechanism is not implemented");
        return ret;
    }
    
    ret = FTPReply(234, "About to negotiate protected session.");
    
    ret = TLSNeg();   
    if (ret < 0) {
        ret = FTPReply(421, "TLS negotiation failed. Closing control connection.");
        return ret;
    }

    tls_up    = true; //handshake probehl, pouzivame tls
    secure_cc = true; //pouzivame sifrovane control connection
    return 1;
}

int fpbsz(list<string> &args, VFS &) {
    int         ret;
    string      s;

    if (!use_tls) {
        ret = FTPReply(500, "Secure mode not available.");
        return ret;
    }

    if (secure_cc) {
        args.pop_front();
        s = args.front(); args.pop_front();
        if (s == "0") {
            pbsz = true;
            ret = FTPReply(200, "PBSZ Okay");
            return ret;
        } else {
            ret = FTPReply(500, "Only PBSZ 0 supported (TLS/SSL).");
            return ret;
        }
    } else { //if secure_cc
        ret = FTPReply(503, "Bad sequence of commands. Use AUTH first.");
        return ret;
    }
}


int fprot(list<string> &args, VFS &) {
    int         ret;
    string      s;
    
    if (!use_tls) {
        ret = FTPReply(500, "Secure mode not available.");
        return ret;
    }
    
    if (!pbsz) {
        ret = FTPReply(503, "Bad sequence of commands. Use PBSZ first.");
        return ret;
    }

    pbsz = false;

    args.pop_front();
    s = args.front(); args.pop_front();
    
    ToLower(s);
    if (s == "c") {
        secure_dc = false;
        ret = FTPReply(200, "PROT C okay.");
        return ret;
    } else if (s == "p" && tls_dc) { //pokud klient chce i sifrovany prenos dat a my ho mame povolen
        secure_dc = true;
        ret = FTPReply(200, "PROT P okay.");
        return ret;
    } else {
        ret = FTPReply(500, "Only PROT C supported.");
        return ret;
    }
}


