/** @file pomocne.cpp
 *  \brief Implementace pomocnych funkci.
 * 
 * Obsahuje funkce na nahrani souboru s ucty, nebo zakazanych IP, parsovani
 * prichozich FTP prikazu od klienta, rozdeleni cesty k souboru na jednotlive
 * adresare, funkce nahrazujici v retezci znak LF znaky CRLF a naopak, atp.
 * 
 */


#include "pomocne.h"

/** Rozdeli radek na atomy podle mezer, CR, LF a tabulatoru.
 *
 * Pokud radek obsehuje jen oddelovace (mezera, CR, LF, tab), vrati 1, ale do
 * seznamu atomu nic nevklada - tj. pokud byl prazdny, necha ho prazdny. Atomy
 * jsou v seznamu v tom poradi v jakem byly na radce.
 * 
 * Navratove hodnoty:
 *
 *      -  1      rozdeleno, vse OK.
 *      - -1      radek neobsahuje zadny znak
 *
 */
int CutIntoParts(string line, list<string> &atoms) {
    string      delimiters = " \r\n\t"; //mezera, tecka, carka, CR, LF, tab
    int         zacatek_atomu;
    int         konec_atomu;
    int         velikost_atomu;
    int         mez;
    int         size;
    string      atom;

    size = line.size();
    if (size == 0) return -1;
    
    mez = 0;
    do {
        zacatek_atomu = line.find_first_not_of(delimiters, mez);
        if (zacatek_atomu == string::npos) return 1; //na radku uz zadny atom neni
        
        mez = line.find_first_of(delimiters, zacatek_atomu);
        if (mez == string::npos) { //az do konce radky zadny delimiter neni
            velikost_atomu = size - zacatek_atomu;
            atom = line.substr(zacatek_atomu, velikost_atomu);
            atoms.push_back(atom);
            return 1;
        }

        velikost_atomu = mez - zacatek_atomu;
        atom = line.substr(zacatek_atomu, velikost_atomu);
        atoms.push_back(atom);

    } while (1);
}


/** Nacita informace o uctech.
 *
 * Informace o uctech nacte ze zadaneho souboru do globalni promenne users.
 *
 * Tvar souboru s ucty je:
 * ---------------
 * user password x
 *      .
 *      .
 *      .
 * ---------------
 * kde x je 1 nebo 0 podle toho, jestli uzivatel je nebo neni admin.
 *
 * Navratove hodnoty:
 *
 *      -  1      soubor v poradku nacten
 *      - -1      chyba pri otvirani souboru
 *      - -2      chybna informace, zda je ucet administratorsky
 *      - -3      chyba pri cteni ze souboru
 *      - -4      soubor obsahuje chybny radek
 * 
 */
int LoadAccountFile(const char * path)   //nacte z fajlu 'name' info o juzrech do usr_tbl
{
  FILE       *  f;
  char          bafr[MAX_CFG_LINE_LEN];
  char       *  ret;
  int           i = 0;
  bool          chybny_radek = false;

  f = fopen(path,"r");
  if (f == 0) {
      //cout << getpid() << " - Nepovedlo se otevrit soubor s ucty (\"" << path << "\")." << endl;
#ifdef DEBUG
      perror("LoadAccountFile()"); 
#endif
      return -1;
  }

  //soubor je otevreny, smazeme pripadne zaznamy
  if (!users.empty()) users.erase(users.begin(), users.end());

      
	while ((ret = fgets(bafr, MAX_CFG_LINE_LEN, f))!=NULL) if (strlen(bafr)>1) //kratke radky ignorujeme
	{
	    user                usr;
            string              s;
            list<string>        args;
            int                 x;
            

            if (!args.empty()) args.erase(args.begin(), args.end());
            
#ifdef DEBUG
            cout << "LoadAccountFile(): radek: " << bafr << endl;
#endif      
            //rozparsujeme si radek na jednotliva slova    
            s = bafr;
            x = CutIntoParts(s, args);
            if (x == -1 || (x == 1 && args.empty()) ) continue; //prazdny radek preskocime;
            if (args.size() != 3) { chybny_radek = true; continue; } //chybny radek preskocime;
            
            usr.name     = args.front(); args.pop_front();
            usr.password = args.front(); args.pop_front();
            s            = args.front(); args.pop_front();
            
            if (s == "1") usr.is_admin = true; else if (s == "0") usr.is_admin = false; else return -2;
            
            users.push_back(usr); //ulozime ziskane informace o uzivateli
            
#ifdef DEBUG
            cout << "name = " << usr.name << " password = " << usr.password;
            if (usr.is_admin) cout << " <-- admin"; else cout << " <-- obycejny uzivatel";
            cout << endl;
#endif
	}
        if (feof(f) == 0) {
            //cout << getpid() << "Chyba pri nacitani souboru uctu \"" << path << "\"" << endl;
            return -3;
        }
    
    fclose(f);
        
    if (chybny_radek) return -4; else return 1;
}

/** Nahrava seznam zakazanych IP.
 *
 * Pokud narazi na radek se spatnym udajem, preskociho a nacita zbytek souboru.
 * 
 * Format souboru:
 * co radek to IP ve tvaru cislo.cislo.cislo.cislo
 *
 * Navratove hodnoty:
 * 
 *      - 1                     vse v poradku nacteno
 *      - 0                     nelze otevrit zadany soubor
 *      - zaporna hodnota       minus cislo prvniho radku se spatnym udajem
 *
 */
int LoadIPDenyList(const char * path)
{
    FILE *fd;
    char line[MAX_CFG_LINE_LEN];
    char *p,*np;
    char *zacatek; //zacatek IP v retezci (pred nim jsou jen prazdne znaky)
    int i;
    char tmp[MAX_CFG_LINE_LEN]; //daleko vic nez dostatecna velikost
    long cislo; //pomocna promenna pro prevod casti IP retezce na cislo
    int radek = 0; //cislo zpracovavaneho radku
    int spatna_ip_radek = 0; //cislo prvniho radku na kterem se objevil spatny udaj
    string ip_string = ""; //pomocna promenna pro ulozeni IP do globalniho seznamu ip_deny_list

    fd = fopen(path,"r");
    if (fd == 0) { 
#ifdef DEBUG
        perror("LoadIPDenyList()"); 
#endif
        return 0;
    }
   
    //soubor je otevreny, smazeme pripadne stare zaznamy
    if (!ip_deny_list.empty()) ip_deny_list.erase(ip_deny_list.begin(), ip_deny_list.end());
    
w:  while ((p=fgets(line, MAX_CFG_LINE_LEN, fd)) != 0) if (strlen(line)>5) //IP bude mit urcite vic jak 5 znaku
    {
        radek++;
        p = line;
        while (isspace(*p) && (p - line) < (MAX_CFG_LINE_LEN - 5) && (*p != 0)) p++; //preskocime prazdne znaky
        if ((p - line) > (MAX_CFG_LINE_LEN - 5) || (*p == 0)) continue; //prilis dlouhy nebo prazdny radek
        zacatek = p; //jestli je to IP, pak zacina tady
        
        for (i=0; i<4; i++) {
            cislo = strtol(p,&np,10);
            if (cislo == LONG_MIN || cislo == LONG_MAX || p == np) {
#ifdef DEBUG
                perror("LoadIPDenyList()");
#endif
                if (spatna_ip_radek == 0) spatna_ip_radek = radek;//zapamatujeme si prvni radek se spatnou IP
                goto w; // spatna IP, preskocime na dalsi radek v souboru
            }

            if ((cislo < 0 || cislo > 255) || (*np != '.' && *np != '\n')) {
#ifdef DEBUG
                cout << "LoadIPDenyList(): spatna IP v souboru " << path << " na radku cislo " << radek << endl;
                cout << " *np = \'" << *np << "\'" << endl;
#endif
                if (spatna_ip_radek == 0) spatna_ip_radek = radek;//zapamatujeme si prvni radek se spatnou IP
                goto w; // preskocime na dalsi radek v souboru
            }
            if ((np - line) < MAX_CFG_LINE_LEN) p = ++np;
            else { //ups ... uplne nesmyslne dlouhej radek v souboru, jdeme dal
                if (spatna_ip_radek == 0) spatna_ip_radek = radek;//zapamatujeme si prvni radek se spatnou IP
                goto w; // preskocime na dalsi radek v souboru
            }
                    
        } //for
        
        np--; //ted np ukazuje na prvni znak za IP adresou
        memset(tmp, 0, MAX_CFG_LINE_LEN);
        strncpy(tmp, zacatek, np - zacatek);
        ip_string = tmp;
        ip_deny_list.push_back(ip_string);
    }//while
    
    fclose(fd);
    if (spatna_ip_radek != 0) return -spatna_ip_radek; else return 1;
}

/** Umaze <CR><LF> na konci retezce (dva znaky pred nulou), a posune nulu.
 */
int CutCRLF(char *a) {
	char *p;
	p=(char *)strchr(a,0);
	if (*(p-2)=='\r' && *(p-1)=='\n')
	{
	  p-=2;
	  *p=0;
	}
	return 0;
}

/** Pripoji na konec retezce, na ktere ukazuje a <CR><LF>a za ne prida koncovou nulu.
 */
int AddCRLF(char *a) {			//predpoklada dostatek mista
	char *p;
	p=(char *)strchr(a,0);
	*p='\r'; p++; *p='\n'; p++; *p='\0';
	return 0;
}

/** V retezci nahradi kazdy znak LF dvojici znaku CRLF
 * 
 * Vrati velikost vysledneho retezce.
 */
int LF2CRLF(char *kam,char *odkud, int kolik) {
  char *p;
  int velikost;

  velikost=kolik; //vysledna velikost bude aspon velikost "odkud"
  p=odkud;
  while (kolik--){
  if (*p!='\n') {*kam=*p; kam++;}
  else {*kam='\r'; kam++; *kam='\n'; kam++; velikost++;}
  p++;
  }
  return velikost;
}

/** V retezci nahradi dvojice znaku CRLF jednim znakem LF.
 * 
 * Vrati velikost vysledneho retezce.
 *
 */
int CRLF2LF(char *kam,char *odkud, int kolik) {
  char *p;
  int velikost;

  velikost = kolik; //vysledna velikost bude nejvys velikost "odkud"
  p = odkud;
  while (kolik--) {
  if (*p!='\r') {*kam=*p; kam++;}
  else if (kolik >= 1 && *(p+1)=='\n') {*kam='\n'; kam++; p++; velikost--; kolik--; } else { *kam=*p; kam++;}
  p++;
  }
  return velikost;
}

/** V retezci data vymaze dvojice znaku indikujici FTP_EOR.
 *
 * Vrati velikost transformovanych dat. 
 *
 */
int EraseEOR(char *data, int velikost) {
    char buffer[velikost];
    char *p;
    int i = 0;

    p = data;
    while (p < data + velikost-1) {
        if (*p == FTP_EOR[0] && *(p+1) == FTP_EOR[1]) p = p + 2;
        else { buffer[i] = *p; i++; p++;}
    }

    strncpy(data, buffer, i);
    return i;
}



int IsDelim(char zn) //isdelimiter ... pokud je zn platny oddelovac vrati true (1)
{
  if (isspace(zn) || zn==',') return 1; else return 0;
}

int PocetArgumentu(const char  * line) //pocet slov (oddelenych delimitery)
{
  int i=0;

  while (*line!=0)
  {
  while (IsDelim(*line) && *line!=0) line++;
  i++;
  while (!IsDelim(*line) && *line!=0) line++;
  }

  return i;
}


/** Kontroluje jestli neni dana IP v deny listu.
 * 
 * Pokud ne, vrati 1, pokud je bannuta vrati 0.
 * IP musi byt tvaru c1,c2,c3,c4.
 * 
*/
int CheckIP(char *IP) {
    string s       = IP;
    bool   banned  = false;
    
    vector<string>::iterator it;
    
    it = ip_deny_list.begin();
    while (it != ip_deny_list.end()) {
        if (*it == s) {
            banned = true;
            break;
        }
        it++;
    }
    
    if (banned) return 0; else return 1;
}



/** Pomocna ladici funkce.
 *
 * Vypise seznam nactenych uctu.
 * 
 */
void PrintAccounts() {
    vector<user>::iterator it;
    it = users.begin();

    cout << "Loaded accounts: " << endl << endl;
    while (it != users.end()) {
        cout << it->name << endl;
        it++;
    }
}

/** Pomocna ladici funkce.
 *
 * Vypise seznam nactenych zakazanych IP.
 *
 */
void PrintDeniedIPs() {
    vector<string>::iterator it;
    it = ip_deny_list.begin();

    cout << "Denied IPs: " << endl << endl;
    while (it != ip_deny_list.end()) {
        cout << *it << endl;
        it++;
    }
}

/** Vrati k zadanemu prikazu jeho index v tabulce prikazu.
 *
 */
int GetCommandIndex(string cmd) {
  int i     = 0;

  while (i < number_of_commands) {
    if (cmd == command_table[i].name) return i;
    i++;
  }

  return -1;
}

/** Prevede vsechny znaky ve stringu na low-case.
 *
 */
void ToLower(string &x) {
    string::iterator it = x.begin();
    
    while (it != x.end()) {
        *it = tolower(*it);
        it++;
    }
}

/** Parsuje FTP prikazy.
 *
 * Pokud ma mit prikaz nejvys jeden argument, bere jako soucast argumentu i
 * mezery, carky, atp. - v tom pripade najde za jmenem prikazu prvni nebily
 * znak a jako argument vezme vse od teto pozice az do konce.
 *
 * Muze-li mit prikaz vic jak jeden argument, jsou jako oddelovace pouzity
 * mezera, carka, CR a LF. (tecka nejde jako oddelovac pouzit napr. kvuli CWD ..)
 * 
 * Jmeno prikazu prevede na mala pismena. Jednotlive atomy zaradi do seznamu
 * atoms v tom poradi v jakem byly v prikazu.
 *
 * 
 * Navratove hodnoty:
 *
 *      - nezaporna hodnota      = index prikazu v tabulce prikazu
 *
 *      - -1                     neznamy prikaz (neni v tabulce command_table)
 *      - -2                     string command je prazdny nebo obsahuje jen bile znaky
 *
 */
int ParseCommand(string command, list<string> &atoms) {
    string      delimiters = " ,\r\n"; //mezera, tecka, carka, CR, LF
    string      whitespace = " \r\n";
    int         zacatek_atomu;
    int         konec_atomu;
    int         velikost_atomu;
    int         pocet_atomu;
    int         mez;
    int         size;
    int         command_index;
    string      command_name;
    string      atom;

    size = command.size();
    if (size == 0) return -2;
    
    zacatek_atomu = command.find_first_not_of(whitespace);
    if (zacatek_atomu == string::npos) return -2;
        
    mez = command.find_first_of(whitespace, zacatek_atomu);
    if (mez == string::npos) { // command je jen samotne jmeno prikazu bez argumentu, koncime
        velikost_atomu = size - zacatek_atomu;
        command_name = command.substr(zacatek_atomu, velikost_atomu);
        ToLower(command_name);
        
        command_index = GetCommandIndex(command_name);
        if (command_index == -1) return -1; //neznamy prikaz
        
        atoms.push_back(command_name);
        return command_index;
    }
    
    velikost_atomu = mez - zacatek_atomu;
    command_name = command.substr(zacatek_atomu, velikost_atomu);
    ToLower(command_name);

    command_index = GetCommandIndex(command_name);
    if (command_index == -1) return -1; //neznamy prikaz
    
    atoms.push_back(command_name);
    if (command_table[command_index].max_args == 0) return command_index; // prikaz nema zadne argumenty, koncime
    
    zacatek_atomu = command.find_first_not_of(whitespace, mez);
    if (zacatek_atomu == string::npos) return command_index; // zadne argumenty nejsou
    
    if (command_table[command_index].max_args == 1) { // ma-li mit prikaz jen jeden argument
        //najdeme posledni neoddelovac, za command[mez] uz budou jen oddelovace;
        konec_atomu    = command.find_last_not_of(delimiters); 
        if (konec_atomu == string::npos) return command_index; // byly by tam jen mezery, to ale nikdy nenastane, diky predchozimu
        velikost_atomu = konec_atomu - zacatek_atomu + 1; 
        atom = command.substr(zacatek_atomu, velikost_atomu);
        atoms.push_back(atom);
        return command_index;
    }
    
    /* --- pokud jsme se dostali az sem, prikaz muze mit dva a vic argumentu --- */
    do {
        
        mez = command.find_first_of(delimiters, zacatek_atomu);
        if (mez == string::npos) { //az do konce commandu zadny delimiter neni
            velikost_atomu = size - zacatek_atomu;
            atom = command.substr(zacatek_atomu, velikost_atomu);
            atoms.push_back(atom);
            return command_index;
        }

        velikost_atomu = mez - zacatek_atomu;
        atom = command.substr(zacatek_atomu, velikost_atomu);
        atoms.push_back(atom);//v prvnim pruchodu cyklem se tady bude ukladat prvni argument prikazu
        
        zacatek_atomu = command.find_first_not_of(delimiters, mez);
        
        //podivame se, jestli ted uz nema nasledovat jen jeden argument
        //pokud ano, vezmeme ho jako cast az po prvni nedelimiter odzadu
        //tj. v pripade SITE CHMOD 0775 jmeno souboru.avi se vezme jmeno souboru.avi
        //jako jeden argument (SITE ma totiz maxargs 3)
        if ((command_table[command_index].max_args == atoms.size()) && command_name == "site") {
	    konec_atomu = command.find_last_not_of(delimiters);
            if (konec_atomu == string::npos) return command_index; //k tomu nedojde, musely by tam byt same delimitery
            velikost_atomu = konec_atomu - zacatek_atomu + 1;
            atom = command.substr(zacatek_atomu, velikost_atomu);
            atoms.push_back(atom);
            return command_index;
        }
    
    } while (zacatek_atomu != string::npos);
    return command_index;
}


