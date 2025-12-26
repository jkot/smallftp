
/** @file VFS_pomocne.cpp
 *  \brief Pomocne funkce pro tridu VFS.
 *
 */


/** Rozdeli cestu tvorenou lomitky / na jednotlive casti.
 *
 * Pokud uspeje, vrati 1. Pokud nastane pri zpracovani cesty vyjimka, vrati -2.
 * Pokud bude cesta obsahovat vice lomitek za sebou (napr. /usr//local), vrati -1.
 * Je-li lomitko na zacatku cesty, vlozi ho do vysledku na prvni misto.
 *
 */
int CutPathIntoParts(string path, deque<string> &x) {
    int  i = 0;
    int  j = 0;
    bool bad_path = false;
    string s;
    
try {
        
    if (path[path.size()-1] != '/') path = path + "/"; //neni-li na konci lomitko, pridame si ho
    
//    cout << "zkoumame \"" << bla << "\"" << endl;
    while (i != path.npos) {
        i = path.find('/',j);
        if (i == 0) { j++; s="/"; x.push_back(s); continue; } //lomitko na zacatku preskocime
        
        if (i != path.npos) {
            s = path.substr(j, i-j);
            if (s != "") x.push_back(s); else bad_path = true;
//            cout << path.substr(j,i-j) << " i-j=" << i-j << endl;
            j = i+1;//znovu budeme hledat tesne za nalezenym lomitkem
        }
    }//while

} catch (exception x) {
    //string by nekde mohl hodit vyjimku...
    return -2;    
}//catch
    if (bad_path) return -1; else return 1;
}




/** Rozdeli radek na atomy podle mezer, CR a LF.
 *
 * Pokud radek obsehuje jen oddelovace (mezera, CR, LF, tabulator), vrati 1, ale do
 * seznamu atomu nic nevklada - tj. pokud byl prazdny, necha ho prazdny. Atomy
 * jsou v seznamu v tom poradi v jakem byly na radce.
 * 
 * Navratove hodnoty:
 *
 *      -  1      rozdeleno, vse OK.
 *      - -1      radek neobsahuje zadny znak
 *
 */
int VFSCutIntoParts(string line, list<string> &atoms) {
    string      delimiters = " \r\n\t"; //mezera, tecka, carka, CR, LF
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




/** Zkontroluje, zda dana hodnota patri do mnoziny hodnot prav.
 *
 */
int CheckRights(int x) {
    if (x == 0 || x == 1 || x == 2 || x == 3) return 1; else return -1;
}





/** Zkontroluje, jestli lze zadany adresar otevrit.
 *
 * Take resolvne pripadne symbolicke linky po ceste a do path ulozi opravdovou
 * cestu - resp. cestu neobsahujici (snad) symbolicke linky. Udela to prepnutim
 * se do zadaneho adresare a zjistenim aktualniho adresare.
 * Pokud uspeje, vrati 1, jinak pokud nejsou pro otevreni dostatecna prava,
 * vrati -2, pokud cesta nezacina lomitkem, vrati -3, v ostatnich pripadech
 * vraci -1.
 *
 */
int CheckDir(string &path) {
    DIR * dir;
    int   tmp; //kdyz zavolam perror, tak se zmeni hodnota errno, je potreba si ji ulozit

    if (path[0] != '/') return -3;

    if ((dir=opendir(path.c_str())) == 0) {
        tmp = errno;
#ifdef DEBUG
        cout << getpid() << " - Nelze otevrit adresar " << path << "." << endl;
        perror("CheckDir()");
#endif
        switch (tmp) {
            case EACCES:  return -2;
            case ENOTDIR: return -1;
            default: return -1;
        }//switch
    } else {
        int ret = closedir(dir);
#ifdef DEBUG
        if (ret != 0) cout << "CheckDir(): nepovedlo se zavrit adresar " << path << endl;
#endif
        
        ret = chdir(path.c_str());
#ifdef DEBUG
        if (ret == -1) {
            cout << "CheckDir(): neuspel chdir (" << path << ")";
            perror("chdir");
        }
#endif
        //zjistime aktualni adresar
        char buf[MAX_PATH_LEN];
        memset(buf, 0, MAX_PATH_LEN);
        char * x = getcwd(buf, MAX_PATH_LEN); //vrati absolutni cestu, bez lomitka na konci
        if (x == 0) { 
#ifdef DEBUG            
            cout << "CheckDir: getcwd() selhalo ..."; 
#endif
        } else path = buf;

        return 1;
    }
}

