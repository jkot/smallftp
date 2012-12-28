/** @file network.cpp
 *  /brief Implementace sitovych funkci
 *
 *  K normalni funkci ClientRequest() zde je implementovan jeji bezbecny
 *  ekvivalent ClientSecureRequest(). Ostatni funkce deklarovane v hlavickovem
 *  souboru se rozhodnou samy, jestli posilat/prijimat data sifrovane. Taktez
 *  funkce CreateDataConnection() rozhodne podle promenne secure_dc, jestli
 *  navazat normalni nebo sifrovane spojeni.
 *
 */

#include "network.h"

//#define DEBUG
#ifdef DEBUG
#include <iostream>
#endif


/** Cte pozadavek od klienta.
 *
 * Pokud je read prerusen signalem, zkousi cist znova.
 *
 * Navratove hodnoty:
 * 
 *      -  1   vse OK
 *      -  0   klient ukoncil spojeni
 *      - -1   jina chyba
 *      - -2   prilis dlouhy pozadavek od klienta
 *      - -3   spatny deskriptor
 *
 */
int ClientRequest(string &req) {
    char        msg[MAX_CLIENT_REPLY_LEN];
    int         n;
    
    memset(msg, 0, MAX_CLIENT_REPLY_LEN);
    do {
    n = read(client_socket, msg, MAX_CLIENT_REPLY_LEN-1);
    } while (n == -1 && errno == EINTR); //cteme dokud nas prerusujou signaly
    
    if (n == 0) return 0; // klient asi zavrel spojeni
    if (n == MAX_CLIENT_REPLY_LEN) {
        req = msg;
        return -2; // to by se nemelo stat, pochybny pozadavek od klienta
    }
    if (n == -1) {
        switch (errno) {
            case EBADF: return -3; // spatny deskriptor
            default: return -1; //jina chyba
        } //switch
    }// if n == -1
    
    req = msg;

    return 1;
}

/** TLS/SSL verze funkce ClientRequest().
 *
 */
int ClientSecureRequest(string &req) {
    char        msg[MAX_CLIENT_REPLY_LEN];
    int         ret;

//    cout << "waiting for secure request" << endl;
    ret = BIO_gets(io,msg,MAX_CLIENT_REPLY_LEN-1);
//    cout << "got it" << endl;

    switch (SSL_get_error(ssl,ret)) {
        case SSL_ERROR_NONE:
            //len = ret;
            break;
        default:
            return -1;
    }

    req = msg;
    return 1;
}


/** Odesila data klientovi po control connection.
 *
 * Navratove hodnoty:
 *
 *      -  1    vse OK.
 *      - -1    jina chyba
 *      - -2    spatny deskriptor
 *      - -3    klient ukoncil spojeni
 *
 */
int SendReply(const char * reply) {
    int         ret;
    
    do {
        ret = write(client_socket, reply, strlen(reply));
    } while (ret == -1 && errno == EINTR); //pokud nas prerusil signal, zapiseme data znovu
    
    if (ret == -1) 
        switch (errno) {
            case EBADF: return -2; // spatny deskriptor
            case EPIPE: return -3; // klient ukoncil spojeni
            default: return -1; //jina chyba;                    
        }//switch
    
#ifdef DEBUG
    if (ret != strlen(reply)) { // tahle moznost nema vubec v blokujicim rezimu nastat
        cout << "SendReply(): nebyla odeslana cela odpoved." << endl;
    }
#endif

    return 1;
}


/** TLS/SSL verze funkce SendReply().
 *
 */
int SendSecureReply(const char * reply) {
    int         ret;

    ret = BIO_puts(io, reply);
    if (ret <= 0) return -1;

    //protoze nepouzivame SSL_write bylo by asi dobre flushnout BIO - ssl
    //objekt o nem a jeho bafru totiz nema paru
    ret = BIO_flush(io);
    if (ret < 0) return -1;
}


/** Sestavuje a odesila odpoved pro klienta.
 *
 * Automaticky se rozhodne podle promenne secure_cc, jestli posilat 
 * odpoved sifrovane, nebo ne.
 * 
 * Navratove hodnoty:
 *
 * stejne jako SendReply()
 *
 */
int FTPReply(int code, const char * msg) {
    string      odpoved;
    char        cislo[5]; // jen pro jistotu, kod bude mit vzdy jen 3 cislice
    int         ret;
    
#ifdef DEBUG
    cout << getpid() << ": " << code << " " << msg << endl;
#endif
    
    ret = snprintf(cislo, 5, "%d", code);
    if (ret < 0) {
#ifdef DEBUG
        cout << "FTPReply(): chyba pri snprintf." << endl;
#endif
        odpoved = "555 Server error.";  
    } else {
        odpoved = cislo;
        odpoved = odpoved + " ";
        odpoved = odpoved + msg;
    }
    
    odpoved = odpoved + "\r\n"; // Pridame CR LF
  //if (write(client_socket,odpoved,strlen(odpoved))==-1) perror("reply: write");

    if (secure_cc) ret = SendSecureReply(odpoved.c_str());
        else ret = SendReply(odpoved.c_str());
    if (ret < 0) return ret; else return 1;
}


/** Sestavuje a odesila odpoved pro klienta.
 *
 * Vytvari odpovedi ve stylu multiline reply - tesne za kod neumisti mezeru,
 * ale pomlcku.
 *
 * Navratove hodnoty:
 *
 * stejne jako SendReply()
 *
 */
int FTPMultiReply(int code, const char * msg) {
    string    odpoved;
    char        cislo[5]; // jen pro jistotu, kod bude mit vzdy jen 3 cislice
    int         ret;
    
#ifdef DEBUG
    cout << getpid() << ": " << code << " " << msg << endl;
#endif
    
    ret = snprintf(cislo, 5, "%d", code);
    if (ret < 0) {
#ifdef DEBUG
        cout << "FTPMultilineReply(): chyba pri snprintf." << endl;
#endif
        odpoved = "555 Server error.";  
    } else {
        odpoved = cislo;
        odpoved = odpoved + "-";
        odpoved = odpoved + msg;
    }
    
    odpoved = odpoved + "\r\n"; // Pridame CR LF
  //if (write(client_socket,odpoved,strlen(odpoved))==-1) perror("reply: write");
    
    if (secure_cc) ret = SendSecureReply(odpoved.c_str());
        else ret = SendReply(odpoved.c_str());
    if (ret < 0) return ret; else return 1;
}


/** Funkce vytvarejici data connection.
 * 
 * Jakym zpusobem funkce vytvori data connection zavisi na globalni promenne
 * passive, kterou nastavuji prikazy PASV a PORT. Spojeni bude pristupne pres
 * globalni promennou client_data_socket;
 *
 * Zajistuje i odpoved s kodem 150, je totiz nutne dat si pozor jestli se tahle
 * odpoved posila pred nebo po connectu/acceptu - viz. draft_murray... diagramy
 * na stranach 16 a 17.
 *
 * Pokud failne TLSNeg(), vrati -20.
 * 
 */
int CreateDataConnection() {
    int                 ret;
    int                 _errno;
    int                 delka;
    struct sockaddr_in  tmp; //snad nebudeme potrebovat a nechceme si prepsat client_data_addr ...

    if (!passive) { // jsme aktivni, budeme se pripojovat
        client_data_socket = socket(PF_INET, SOCK_STREAM, 0);
        if (client_data_socket == -1) return -1;

        ret = FTPReply(150,"Ok, about to open data connection.");
        if (ret < 0) return ret;

        ret = connect(client_data_socket,(struct sockaddr *)&client_data_address, sizeof(client_data_address));
        if (ret == -1) {
            _errno = errno;
#ifdef DEBUG
            perror("CreateDataConnection():connect");
#endif
            FTPReply(425,"Ooops, can't open data connection.");

            switch (_errno) {
                case EBADF:     return -2;
                case ENOTSOCK:  return -2;
                case ECONNREFUSED: return -7; 
                case ENETUNREACH:  return -8;
                case ETIMEDOUT:    return -8;
                default: return -1;
            }
        }//if ret == -1

        //Pokud mame pouzivat TLS, provedeme ted handshake
        if (secure_dc) {
            ret = TLSDataNeg();   
            if (ret < 0) { 
                FTPReply(522,"TLS negotiation for data connection failed.");
                return -20;
            }
        }//if secure data connection
    } else { // jsme v pasivnim modu, cekame na spojeni
	do {
            client_data_socket = accept(server_data_socket, (struct sockaddr *)&tmp, (socklen_t *)&delka);
        } while (client_data_socket == -1 && errno == EINTR);
        
        passive = false;
        
        if (client_data_socket == -1) {
            _errno = errno;
#ifdef DEBUG
            perror("CreateDataConnection():accept");
#endif
            FTPReply(425,"Ooops, can't open data connection.");
            close(server_data_socket); 

            switch (_errno) {
                case ENOMEM: return -4;
                case ENOTSOCK: return -2;
                case EBADF: return -2;
		case EOPNOTSUPP: return -1;
                default: return -1;
            }
        }// if client_data_socket == -1

        ret = FTPReply(150,"Ok, about to open data connection.");
        if (ret < 0) return ret;

        //Pokud mame pouzivat TLS, provedeme ted handshake
        if (secure_dc) {
            ret = TLSDataNeg();   
            if (ret < 0) {
                close(server_data_socket);
                FTPReply(522,"TLS negotiation for data connection failed.");
                return -20;
            }
        }//if secure data connection
    }
    return 1;
}


/** TLS/SSL verze funkce SendDataLine.
 *
 */
int SendSecureDataLine(const char * data) {
    int         ret;
    
    ret = BIO_puts(data_io,data);
    if (ret < 0) {
        //nezkouset ret <= 0, vraci 0 i kdyz se zda, ze je vse Ok.
        return -1; 
    }

    ret = BIO_flush(data_io);
    if (ret < 0) return -1; else return 1;
}


/** Funkce posilajici data po data connection.
 *
 * Data musi byt nulou ukonceny retezec.
 * 
 * Navratove hodnoty:
 *
 *      -  1      vse OK.
 *      - -1      jina chyba
 *      - -2      spatny deskriptor
 *      - -3      klient ukoncil spojeni
 *
 */
int SendDataLine(const char * data) {
    int         ret;
    
    if (secure_dc) {
        ret = SendSecureDataLine(data);
        return ret;
    }
    
    do {
        ret = write(client_data_socket, data, strlen(data));
    } while (ret == -1 && errno == EINTR); //pokud nas prerusil signal, zapiseme data znovu
    
    if (ret == -1)
        switch (errno) {
            case EBADF: return -2; // spatny deskriptor
            case EPIPE: return -3; // klient ukoncil spojeni
            default: return -1; //jina chyba;                    
        }//switch
#ifdef DEBUG
    if (ret != strlen(data)) { // tahle moznost nema vubec v blokujicim rezimu nastat
        cout << "SendDataLine(): nebyla odeslana vsechna data." << endl;
    }
#endif

    return 1;

}


/** TLS/SSL verze funkce SendData().
 *
 */
int SendSecureData(const char * data, int size) {
    int         ret;

    ret = BIO_write(data_io, data, size);
    if (ret < 0) {
        //tady nezkouset ret <= 0, vraci 0 i kdyz se zda, ze je vse Ok.
#ifdef DEBUG
        cout << "SendSecureData: BIO_write chyba c. " << ret << endl;
#endif
        return -1; 
    }

    ret = BIO_flush(data_io);
    if (ret < 0) return -1; else return 1;
}


/** Funkce posilajici data po data connection.
 *
 * V data musi byt ulozeno size znaku.
 * Automaticky se podle promenne secure_dc rozhodne, jestli posilat data
 * sifrovane nebo normalne.
 * 
 * Navratove hodnoty:
 *
 *      -  1      vse OK.
 *      - -1      jina chyba
 *      - -2      spatny deskriptor
 *      - -3      klient ukoncil spojeni
 *
 */
int SendData(const char * data, int size) {
    int         ret;
    
    
    if (secure_dc) {
        ret = SendSecureData(data, size);
        return ret;
    }
    
    do {
        ret = write(client_data_socket, data, size);
    } while (ret == -1 && errno == EINTR); //pokud nas prerusil signal, zapiseme data znovu
    
    if (ret == -1)
        switch (errno) {
            case EBADF: return -2; // spatny deskriptor
            case EPIPE: return -3; // klient ukoncil spojeni
            default: return -1; //jina chyba;                    
        }//switch
#ifdef DEBUG
    if (ret != size) { // tahle moznost nema vubec v blokujicim rezimu nastat
        cout << "SendData(): nebyla odeslana vsechna data." << endl;
    }
#endif

    return 1;

}


/** TLS/SSL verze funkce ReceiveData().
 *
 */
int ReceiveSecureData(char * data, int size) {
    int         ret;

    ret = BIO_read(data_io, data, size);
    if (ret < 0) {
#ifdef DEBUG
	cout << "BIO_read error" << endl;
#endif
	return -1;
    }
/*
    switch (SSL_get_error(ssl,ret)) {
        case SSL_ERROR_NONE:
            //len = ret;
            break;
        default:
            cout << "ReceiveSecureData: BIO_read error" << endl;
            return -1;
    }
*/
    return ret;    
}

/** Funkce prijimajici data po data connection.
 *
 * V promenne data musi byt ulozeno size znaku. Automaticky se podle promenne
 * secure_dc rozhodne, jestli prijimat data sifrovane nebo ne.
 * 
 * Navratove hodnoty:
 *
 *      -  kladna hodnota       pocet prectenych bytu
 *      -  0                    "konec souboru"
 *      - -1                    jina chyba
 *      - -2                    spatny deskriptor
 *
 */
int ReceiveData(char * data, int size) {
    int         ret;

    if (secure_dc) {
        ret = ReceiveSecureData(data, size);
        return ret;
    }
    
    do {
        ret = read(client_data_socket, data, size);
    } while (ret == -1 && errno == EINTR); //pokud nas prerusil signal, zapiseme data znovu
    
    if (ret == -1)
        switch (errno) {
            case EBADF:  return -2; // spatny deskriptor
            case EINVAL: return -2; // ze socketu nelze cist
            default: return -1; //jina chyba;                    
        }//switch
    
    return ret;
}



