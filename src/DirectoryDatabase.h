/** @file DirectoryDatabase.h
 *  /brief Deklarace trid DirectoryDatabase a FileInfo
 *
 */

#ifndef __DirectoryDatabase_h
#define __DirectoryDatabase_h

extern "C" {
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <dirent.h>
#include <gdbm.h>
#include <errno.h>
}
#include "my_exceptions.h"
#include <iostream>
#include <string>

#define GDBM_BLOCK_SIZE 512

#define MAX_USER_NAME_LEN 20
#define MAX_FILE_NAME_LEN 256
#define MAX_DELETED_ITEMS 50 //pocet smazanych polozek, po kterem se provede reorganizace databaze
#define LOCKING_TIMEOUT 200  //kolikrat se budeme pokouset ziskat pristup k databazi, nez to polozime
#define LOCKING_SLEEP_TIME 10000 //na kolik mikrosekund usneme, nez zkusime opet ziskat pristup k databazi
#define R_ALL 3

using namespace std;
// v knihovnach by se tohle nemelo delat. nam to ale nevadi :-)


extern int errno;



/** Struktura zapouzdrujici data ukladana do databaze.
 *  Nelze ukladat primo objekt kvuli pointrum, ktere obsahuje, proto tato
 *  struktura obsahuje pouze cista data.
 */
struct FileInfoGdbmRecord {
    int         user_rights;
    int         others_rights;
    char        user_name[MAX_USER_NAME_LEN];
    char        name[MAX_FILE_NAME_LEN];
};



/** Informace o souboru.
 * 
 * Trida umoznuje pohodlne zachazeni s informacemi o souboru a jejich automaticky
 * prevod na strukturu FileInfoGdbmRecord, kterou pouziva trida
 * DirectoryDatabase k ukladani polozek do databaze.
 * 
 */
class FileInfo {
public:
    int user_rights;   ///< prava vlastnika
    int others_rights; ///< prava ostatnich
    string user_name;  ///< jmeno vlastnika
    string name;       ///< jmeno souboru
    
    FileInfo(const FileInfo &x); ///<copy konstruktor
    FileInfo(){} ///<konstruktor
    FileInfo& operator=(FileInfoGdbmRecord &x);
    operator FileInfoGdbmRecord();
};


/** Trida zapouzdrujici "databazi" gdbm.
 * 
 * Slouzi k praci s informacemi o souborech a adresarich. Umoznuje vkladani
 * informaci, jejich ziskavani a mazani. 
 *    Zajistuje, ze operace probehnou bezpecne - pristup k databazi gdbm totiz 
 * muze mit v jeden okamzik pouze jeden proces, tudiz to tato trida resi 
 * zamykanim souboru s databazi (doporucujicim zamkem). Lze nastavit 
 * LOCKING_TIMEOUT, tj. kolikrat se bude trida pokouset databazi zamknout, a 
 * LOCKING_SLEEP_TIME, coz je casovy interval v mikrosekundach mezi 
 * jednotlivymi pokusy.
 *    Vytvareni klice zajistuje privatni funkce File2Key, ktera jako klic vraci
 * cislo inodu zadaneho souboru.
 *    Pokud pocet smazanych polozek presahne MAX_DELETED_ITEMS, provede se
 * reorganizace databaze (coz ma za nasledek napriklad zmenseni souboru
 * databaze).
 *    Konstruktor databaze muze hodit vyjimky FileError (v pripade chyby prace
 * se souborem) a GdbmError (napr. pokud se databazi nepodari vytvorit).
 * 
 */
class DirectoryDatabase {
public:
    DirectoryDatabase(const char *db_name) throw(FileError, GdbmError);
   ~DirectoryDatabase();
    int PutFileInfo(FileInfo &file);
    int GetFileInfo(string name, FileInfo &info);
    int DeleteFileInfo(string &name);
    int LoadSubDir(string &name);
    void IgnoreHidden(bool x) { ignore_hidden = x; }
    bool IgnoreHidden() { return ignore_hidden; }
    void PrintContent();

private:
    GDBM_FILE db_file; ///< file descriptor databaze
    string db_name;  ///< jmeno souboru databaze
    string lock_name; ///< jmeno zamku pro uzamceni databaze pro zapis
    bool locked; ///< je databaze uzamcena pro zapis?
    bool ignore_hidden; ///< zahrnout do databaze i skryte soubory?
    int num_of_deleted_items; ///< pocet smazanych polozek z databaze - kvuli reorganizaci
    
    int File2Key(const char *path, string &key);
    int LockDatabase();
    int UnlockDatabase();
};




#endif //__DirectoryDatabase_h


