/** @file DirectoryDatabase.cpp
 *  \brief Implementace trid DirectoryDatabase a FileInfo
 *
 */



/*  -------
 * | NOTES |
 *  -------
 *
 * asi bude lepsi ukladat do databaze jen struktury, ne cele objekty - s temi
 * se nejspis ukladaji i nejake "rezijni pointry", ktere jsou pri obnoveni z
 * databaze davno neplatne ...
 *
 * KKP = Kompletni Kapesni Pruvodce Jazyky C a C++
 */

#include <DirectoryDatabase.h>
     
//#define DD_DEBUG


/** Konstruktor.
 * Otevre databazi pro zadany adresar (dir), predpoklada, ze se soubor databaze (name)
 * nachazi v aktualnim adresari. Pokud selze otevreni databaze (napriklad nekdo
 * zmenil prava souboru na 000), hodi vyjimku GdbmError. Databazi otevre jen
 * pro cteni.
 * Implicitne da ingore_hidden na true.
 */ 
DirectoryDatabase::DirectoryDatabase(const char *name) throw(FileError, GdbmError)
{
    
    db_name = name;
    lock_name = name;
    lock_name = lock_name + "_gdbm_lock";
    // je mozne, ze name bude obsahovat i cestu - zamek se tedy bude vytvaret v
    // miste, kde je ulozena databaze - nemohou tam byt dva soubory stejneho
    // jmena, takze je vse OK.


#ifdef DD_DEBUG
    cout << getpid() << " - creating database " << db_name << endl;
#endif

    ignore_hidden = true;
    num_of_deleted_items = 0;
    locked = false;
        
    /* vytvorime novou databazi, nastavime souboru pravo cteni a zapisu,
     * fatal_func() nechame defaultni (0) */
    if (LockDatabase() == -1) {
        string msg;
        msg = "Nepodarilo se zamknout databazi. Program mozna nebyl naposledy ciste ukoncen. Zkontrolujte prosim, ";
        msg = msg + "jestli neexistuje soubor " + lock_name + " a smazte jej.";
        throw FileError(msg.c_str(), -1);
    }
    
// --- zacatek KRITICKE SEKCE ---

    //pokud databaze neexistuje, vytvorime ji
    
    db_file = gdbm_open((char *)db_name.c_str(), GDBM_BLOCK_SIZE, GDBM_WRCREAT, S_IRUSR | S_IWUSR, 0);
    if (db_file == 0) throw GdbmError();
    gdbm_close(db_file);
    if (UnlockDatabase() == -1) throw FileError("DirectoryDatabase(): Nepodarilo se odemknout databazi.", -1);
    
// --- konec KRITICKE SEKCE ---
    
}





DirectoryDatabase::~DirectoryDatabase() {
#ifdef DD_DEBUG
    cout << getpid()<< " - destroying database " << db_name << endl;  
#endif
    int ret=0;

/*
    if (num_of_deleted_items > 0) ret = gdbm_reorganize(db_file); //je-li potreba, procistime databazi
#ifdef DD_DEBUG
    if (ret!=0) cout << "Nepovedla se reorganizace databaze v destruktoru.";
    cout << gdbm_strerror(gdbm_errno) << endl;
#endif 
*/

}




/** K danemu souboru vytvori jednoznacny klic.
 * 
 * K zadanemu souboru vytvori string s jednoznacnym klicem, kterym je cislo
 * i-nodu daneho souboru. Pokud nebude mozne cislo i-nodu zjistit, vrati -1.
 * Pokud uspeje vrati 1.
 * 
 */
int DirectoryDatabase::File2Key(const char *path, string &key) { 
    struct stat buf;
    // polozka st_ino struktury stat je typu UQUAD_TYPE, ci tak nejak --> mel by to byt unsigned long long
    // (viz types.h a dalsi ...)

    // stat nam vrati informace o souboru na ktery pripadny soft link ukazuje
    // (jinak se musi pouzit lstat)
    if (stat(path, &buf) == -1) {
        //perror("File2Key: stat");
	//cout << "stat se provadel na " << path << endl;
	/* char *error_msg;
        
        error_msg = strerror(errno);
        string tmp("In File2Key: stat(\"");
        tmp = tmp + path + "\", ...):  " + error_msg;
        throw( FileError(tmp.c_str(),errno) ); */
        return -1;
    }

    
    // 2^64 = 1.844674407371e+19...cili unsigned long long bude mit max.20 cifer
    char tmp[25]; //koncova nula = +1, plus radsi neco navic

    //PROBLEM: sprintf(tmp,"%llu",buf.st_ino) dava spatny vysledky! takze
    //bohuzel pouzijeme jen %lu, to se zda ze uz funguje...

    if (sprintf(tmp,"%lu",buf.st_ino) < 0) { 
#ifdef DD_DEBUG
        cout << "selhal sprintf ve File2Key ... spatne .... spatne ..." << endl;
#endif
        return -1;
    }
    
    string result(tmp);
    
#ifdef DD_DEBUG
    cout << getpid() << " - File2Key - mame pozadavek \"" << path << "\" odpovidajici klic je " << result << endl;
#endif
    
    key = result;
    return 1;
}



/** Zamyka databazi pro zapis.
 *
 * Pokousi se kazdych LOCKING_SLEEP_TIME mikrosekund zamknout databazi. Pokud se to
 * behem LOCKING_TIMEOUT pokusu nepovede, skonci a vrati -1.
 * Pokud uspeje, vrati 1.
 *
 */
int DirectoryDatabase::LockDatabase() {
    int fd;
    int pokus = 0;

    while (!locked && pokus < LOCKING_TIMEOUT) {
        pokus++;
#ifdef DD_DEBUG
        cout << endl << getpid() << " - zkousime zamknout databazi, pokus c. " << pokus << " ... ";
#endif
        fd = open(lock_name.c_str(), O_RDWR | O_CREAT | O_EXCL, 0444);
        if (fd != -1) {
            locked = true;
            close(fd);
#ifdef DD_DEBUG
            cout << "OK :)" << endl;
#endif
        } else { 
#ifdef DD_DEBUG
            cout << "failed .... dem si pospat." << endl;
            cout << "zamek = " << lock_name << endl;
            perror("open");
#endif
            usleep(LOCKING_SLEEP_TIME);
        }//else
    }//while

    if (locked) return 1; else return -1;
}



/** Odemyka zamknutou databazi.
 *
 * Pokud uspeje vrati 1, jinak -1.
 * 
 */
int DirectoryDatabase::UnlockDatabase() {
    int result;

    result = unlink(lock_name.c_str());

#ifdef DD_DEBUG
    if (result != 0) {
        cout << getpid() << " - Nebylo mozne odstranit zamek " << lock_name << endl;
        perror("UnlockDatabase()");
    }
    else {cout << getpid() << " - databaze odemknuta" << endl; }
#endif

    if (result == 0) { locked = false; return 1; } else return -1;
}



/** Uklada do databaze informace o souboru.
 * 
 * K vytvoreni klice pouziva File2Key.
 *
 * Navratove hodnoty:
 *
 *      -  1      vse OK.
 *      - -1      chyba pri vytvareni klice k souboru
 *      - -2      chyba pri zamykani/odemykani databaze
 *      - -3      chyba pri praci s databazi
 */
int DirectoryDatabase::PutFileInfo(FileInfo &file) {
    string              key;
    datum               keyd, datad;
    FileInfoGdbmRecord  file_record; //data, ktera budou ulozena do databaze
    int                 store_err;
    int                 open_err;
    int                 ret;
    
    ret = File2Key(file.name.c_str(), key);
    if (ret != 1) return -1;
    
    keyd.dptr   = (char *)key.c_str();
    keyd.dsize  = key.size();
    
    file_record = (FileInfoGdbmRecord) file; //pretypujeme FileInfo na nas zaznam
    datad.dptr  = (char *)&file_record;
    datad.dsize = sizeof(struct FileInfoGdbmRecord);

    
#ifdef DD_DEBUG
    cout << getpid() << " - DD::PutFileInfo ... storing " << file_record.name << " juzra: " << file_record.user_name
         << " with i-node# " << key << " user_rights = " << file_record.user_rights << " in da dejtabejz" << endl;
#endif
    
// --- zacatek KRITICKE SEKCE ---
    if (LockDatabase() != 1) return -2;

    // otevreme databazi pro zapis
    db_file = gdbm_open((char *)db_name.c_str(), GDBM_BLOCK_SIZE, GDBM_WRITER, S_IRUSR | S_IWUSR, 0);
    if (db_file == 0) { //nastala chyba
#ifdef DD_DEBUG
        cout << getpid() << "DD::PFI open gdbm_errno = " << gdbm_errno << " ... " << gdbm_strerror(gdbm_errno) << endl;
#endif
        UnlockDatabase(); 
        return -3; 
    }
    
    //ulozime data
    int err = gdbm_store(db_file, keyd, datad, GDBM_REPLACE);
    if (err != 0) { 
#ifdef DD_DEBUG
        cout << getpid() << "DD::PFI store gdbm_errno = " << gdbm_errno << " ... " << gdbm_strerror(gdbm_errno) << endl;
#endif
        gdbm_close(db_file); //musime, viz komentar niz
        UnlockDatabase(); 
        return -3; 
    }
    
    //databazi zase musime zavrit!!! aby ostatni procesy mohly cist a
    //zapisovat !!!
    gdbm_close(db_file);
    ret = UnlockDatabase();
    if (ret != 1){ 
        return -2;
    }
// --- konec KRITICKE SEKCE ---
    
#ifdef DD_DEBUG
    cout << getpid() << " - DD::PutFileInfo ... stored" << endl;
#endif
    
    return 1;
}



/** Ziska z databaze informace o zadanem souboru.
 * 
 * Navratove hodnoty:
 *
 *      -  1      vse OK.
 *      - -1      chyba pri vytvareni klice k souboru
 *      - -3      chyba pri praci s databazi
 *
 */
int DirectoryDatabase::GetFileInfo(string name, FileInfo &info) {
    string      key;
    datum       keyd, datad;
    int         ret;
    
    
    ret = File2Key(name.c_str(), key);
    if (ret != 1) return -1;
    
    keyd.dptr   = (char *) key.c_str();
    keyd.dsize  = key.size();
    
// --- zacatek KRITICKE SEKCE ---
    if (LockDatabase() != 1) return -2;

    // otevreme databazi pro cteni
    db_file = gdbm_open((char *)db_name.c_str(), GDBM_BLOCK_SIZE, GDBM_READER, S_IRUSR | S_IWUSR, 0);
    if (db_file == 0) { //nastala chyba
#ifdef DD_DEBUG
        cout << getpid() << "DD::GFI open gdbm_errno = " << gdbm_errno << " ... " << gdbm_strerror(gdbm_errno) << endl;
#endif
        UnlockDatabase(); 
        return -3; 
    }

    datad = gdbm_fetch(db_file, keyd);
    if (datad.dptr == 0) {
#ifdef DD_DEBUG
        cout << getpid() << " - soubor " << name << " nebyl v databazi nalezen. ";
        cout << "gdbm_errno = " << gdbm_errno << " ... " << gdbm_strerror(gdbm_errno) << endl;
#endif
        gdbm_close(db_file);
        UnlockDatabase(); 
        return -3;
    }

    //databazi zase musime zavrit!!! aby ostatni procesy mohly cist a
    //zapisovat !!!
    gdbm_close(db_file);
    
    ret = UnlockDatabase();
    if (ret != 1){ 
        return -2;
    }
// --- konec KRITICKE SEKCE ---
    
    FileInfoGdbmRecord record;
    memcpy(&record, datad.dptr, datad.dsize);
    //cout << "get file info: free(datad.dptr)" << endl;
    free(datad.dptr); // nutne uvolnit misto na ktere ukazuje datad.dptr --> gdbm to samo neudela!!!

#ifdef DD_DEBUG
    cout << getpid() << " - GetFileInfo: ke klici " << key << "jsme dostali informace o " << record.name << endl;
#endif
    
    FileInfo result;
    result = record;
   
    info = result;
    
    return 1;
}


/** Maze z databaze informace o souboru.
 *
 * Presahne-li pocet smazanych polozek MAX_DELETED_ITEMS, reorganizuje
 * databazi.
 *
 * Navratove hodnoty:
 *
 *      -  1      vse OK.
 *      - -1      chyba pri vytvareni klice k souboru
 *      - -2      chyba pri zamykani/odemykani databaze
 *      - -3      chyba pri praci s databazi
 *
 */
int DirectoryDatabase::DeleteFileInfo(string &name) {
    string      key;
    datum       keyd, datad;
    int         ret;

    ret = File2Key(name.c_str(), key);
    if (ret != 1) return -1;
    
    keyd.dptr   = (char *) key.c_str();
    keyd.dsize  = key.size();

// --- zacatek KRITICKE SEKCE ---
    if (LockDatabase() != 1) return -2; 

    // otevreme databazi pro zapis
    db_file = gdbm_open((char *)db_name.c_str(), GDBM_BLOCK_SIZE, GDBM_WRITER, S_IRUSR | S_IWUSR, 0);
    if (db_file == 0) { 
#ifdef DD_DEBUG
        cout << getpid() << "DD::DFI open gdbm_errno = " << gdbm_errno << " ... " << gdbm_strerror(gdbm_errno) << endl;
#endif
        UnlockDatabase(); 
        return -3; 
    }
    

    if (gdbm_delete(db_file, keyd) != 0) {
#ifdef DD_DEBUG
        cout << getpid() << " - Informace o souboru " << name << " nelze z databaze odstranit." << endl;
#endif
        gdbm_close(db_file);
        UnlockDatabase();
        return -3;
    } else {
        
        num_of_deleted_items++; 
        if (num_of_deleted_items >= MAX_DELETED_ITEMS) {
            // gdbm samo nezmensi velikost souboru databaze, je potreba ho
            // donutit pomoci gdbm_reorganize(), ktere by se nemelo volat moc
            // casto
            if (gdbm_reorganize(db_file) != 0) {
#ifdef DD_DEBUG
                cout << getpid() << " - Nejspis se nepovedla reorganizace databaze ..." << endl;
#endif
            }
        
        }
        
    }//else
    
    gdbm_close(db_file);
    if (UnlockDatabase() != 1) return -2;
// --- konec KRITICKE SEKCE ---

    return 1;
}



/** Nahraje do databaze soubory ze zadaneho adresare.
 * Pokud uz v databazi nektery ze souboru je, nebude prepsan.
 *
 * Navratove hodnoty:
 *
 *      -  1      vse OK.
 *      - -1      chyba pri vytvareni klice k souboru
 *      - -2      chyba pri zamykani/odemykani databaze
 *      - -3      chyba pri praci s databazi
 *      - -5      chyba pri otvirani adresare
 * 
 */
int DirectoryDatabase::LoadSubDir(string &name) {
    DIR                 *dir;
    struct dirent       *entry;
    int                 ret;
    
    if ((dir = opendir(name.c_str()) ) == 0) {
       // string msg;
       // msg = "Adresar " + name + " nebylo mozne otevrit.";
        return -5;
    }
    
    string              tmp;
    string              key;
    datum               keyd, datad;
    FileInfoGdbmRecord  file_record; //data, ktera budou ulozena do databaze
    
// --- zacatek KRITICKE SEKCE ---
    if (LockDatabase() != 1) return -2; 
    gdbm_close(db_file);
    // otevreme databazi pro zapis
    db_file = gdbm_open((char *)db_name.c_str(), GDBM_BLOCK_SIZE, GDBM_WRITER, S_IRUSR | S_IWUSR, 0);
    if (db_file == 0) { UnlockDatabase(); return -3; }

    while ((entry = readdir(dir)) != 0) {
        tmp = entry->d_name;
        if (tmp == "." || tmp == "..") continue;
        if (tmp.substr(0,1) == "." && ignore_hidden) continue;
    
        ret = File2Key(tmp.c_str(), key);
        if (ret != 1) return -1;
        keyd.dptr   = (char *)key.c_str();
        keyd.dsize  = key.size();
    
        file_record.user_name[0] = 0;
        strncpy(file_record.name, tmp.c_str(), MAX_FILE_NAME_LEN);
        file_record.name[MAX_FILE_NAME_LEN-1] = 0;// pro jistotu, kdyby byl nazev souboru moc dlouhy
        file_record.user_rights   = R_ALL;
        file_record.others_rights = R_ALL;
        
        datad.dptr  = (char *)&file_record;
        datad.dsize = sizeof(struct FileInfoGdbmRecord);


#ifdef DD_DEBUG
        cout << getpid() << " - LoadSubDir ... storing " << file_record.name << " juzra: " << file_record.user_name 
            << " with i-node# " << key << " rights = " << file_record.user_rights << " in da dejtabejz" << endl;
#endif
    
        int err = gdbm_store(db_file, keyd, datad, GDBM_INSERT);
        if (err != 0 && gdbm_errno != GDBM_CANNOT_REPLACE) return -3;
        // pokud bychom prepsali data v databazi, mohli bychom prijit o
        // spravne udaje o pravech a juzrovi, proto store s GDBM_INSERT
        // pokud uz zaznam existuje hodi, hodi chybu GDBM_CANNOT_REPLACE,
        // definice kodu chyb je v /usr/inlcude/gdbm/gdbm.h
    } //while
    
    gdbm_close(db_file);
    if (UnlockDatabase() != 1) return -2;
// --- konec KRITICKE SEKCE ---
    
    //otevreme databazi zpet pro cteni
    db_file = gdbm_open((char *)db_name.c_str(), GDBM_BLOCK_SIZE, GDBM_READER, S_IRUSR | S_IWUSR, 0);
    if (db_file == 0)  { UnlockDatabase(); return -3; }
    return 1;
}


/** Pomocna funkce, vytiskne obsah databaze na stdout.
 *
 */
void DirectoryDatabase::PrintContent() {
    datum keyd;
    datum nextkeyd;
    datum datad;
    FileInfoGdbmRecord record;
    
    keyd = gdbm_firstkey(db_file);
    while (keyd.dptr) {
        datad = gdbm_fetch(db_file, keyd);
        if (datad.dptr == 0) {
#ifdef DD_DEBUG
            cout << getpid() << " - soubor nebyl v databazi nalezen. ";
            cout << "gdbm_errno = " << gdbm_errno << " ... " << gdbm_strerror(gdbm_errno) << endl;
#endif
        }
        
        nextkeyd = gdbm_nextkey(db_file, keyd);
        memcpy(&record, datad.dptr, datad.dsize);
        free(datad.dptr); // nutne uvolnit misto na ktere ukazuje datad.dptr --> gdbm to samo neudela!!!
#ifdef DD_DEBUG
        cout << record.name << " user = " << record.user_name << " user_rights = " << record.user_rights << endl;
#endif
        free(keyd.dptr);
        keyd = nextkeyd;
    }
}


/* ------------------------- class FileInfo -------------------------------- */

/** Copy constructor.
 */
FileInfo::FileInfo(const FileInfo &x) {
    this->user_name     = x.user_name;
    this->name          = x.name;
    this->user_rights   = x.user_rights;
    this->others_rights = x.others_rights;
}



/** Umozni FileInfu operatorem = priradit FileInfoGdbmRecord.
 */
FileInfo& FileInfo::operator=(FileInfoGdbmRecord &x) {
    user_rights         = x.user_rights;
    others_rights       = x.others_rights;
    user_name           = x.user_name;
    name                = x.name;
    
    return *this; //aby slo operator = retezit za sebe, viz str. 144 KKP
}




/** Umozni pretypovani FileInfa na FileInfoGdbmRecord
 * Pokud bude string user nebo name delsi nez je kapacita odpovidajicich
 * polozek ve strukture FileInfoGdbmRecord, pak bude oriznut.
 */
FileInfo::operator FileInfoGdbmRecord() {
    FileInfoGdbmRecord result;

    result.user_rights   = user_rights;
    result.others_rights = others_rights;
    
    strncpy(result.user_name, user_name.c_str(),MAX_USER_NAME_LEN);
    strncpy(result.name, name.c_str(),MAX_FILE_NAME_LEN);
    
    //pokud by nakej cvok mel fajly s silene dlouhejma jmenama, nemuseli bychom
    //po strncpy dostat nulou ukoncenej string. radsi si tam tu nulu dame.
    result.user_name[MAX_USER_NAME_LEN-1] = 0;
    result.name[MAX_FILE_NAME_LEN-1] = 0;
    
    return result;
}




