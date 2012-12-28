/** @file VFS_file.cpp
 *  /brief Implementace tridy VFS_file
 *
 */
 
#include "VFS_file.h"

/** Konstruktor tridy VFS_file.
 *
 *
 */
VFS_file::VFS_file(string _name, string _path) {
    name = _name;
    path = _path;
    if (_path == "") is_virtual = true; else is_virtual = false;
    descriptor = 0;
}


VFS_file::~VFS_file() {
    
    if (descriptor != 0) { 
        fclose(descriptor); 
        descriptor = 0; 
    }
}

/** Vytvori k souboru informace ve tvaru prikazu ls -l.
 *
 * Vysledek je cisty text, bez znaku CR nebo LF.
 *
 * Navratove hodnoty:
 *
 *      -  1      vse OK
 *      -  2      snad OK, ale nepovedlo se zjistit/prevest cas.
 *      - -1      jina chyba
 *      - -2      nedostatecna prava
 *      - -3      soubor s danou cestou neexistuje
 *      - -4      prilis dlouhe jmeno
 *      - -5      po ceste k souboru bylo moc linku
 *
 */
int VFS_file::GetLslInfo(string &result)
{
  struct stat statbuf;
  char prava[11];
  char cas[256];
  char temp[400];
  char *p;
  struct tm *time;
  string file_name;

  file_name = path + "/" + name;
  
  if (!is_virtual) {
      
        p = prava;
        int ret = stat(file_name.c_str(), &statbuf);
        if (ret == -1)
            switch (errno) {
                case ENOENT: return -3; //soubor s danou cestou neexisstuje
                case ELOOP: return -5;  //po ceste bylo moc linku
                case EACCES: return -2; //nedostatecna prava
                case ENAMETOOLONG: return -4; //prilis dlouhe jmeno
                default: return -1;
            } //switch
  
        if (S_ISDIR(statbuf.st_mode)) *p='d'; else *p='-';  p++;
        if (S_IRUSR & statbuf.st_mode) *p='r'; else *p='-'; p++;
        if (S_IWUSR & statbuf.st_mode) *p='w'; else *p='-'; p++;
        if (S_IXUSR & statbuf.st_mode) *p='x'; else *p='-'; p++;
        if (S_IRGRP & statbuf.st_mode) *p='r'; else *p='-'; p++;
        if (S_IWGRP & statbuf.st_mode) *p='w'; else *p='-'; p++;
        if (S_IXGRP & statbuf.st_mode) *p='x'; else *p='-'; p++;
        if (S_IROTH & statbuf.st_mode) *p='r'; else *p='-'; p++;
        if (S_IWOTH & statbuf.st_mode) *p='w'; else *p='-'; p++;
        if (S_IXOTH & statbuf.st_mode) *p='x'; else *p='-'; p++; 
        *p=0;
  
        time = localtime(&statbuf.st_ctime);
        if (time != 0) {
            strftime(cas,256,"%b %d %H:%M",time); //TODO udelat zarovnany vystup casu ...
            sprintf(temp, "%s % 4d %-9d%-9d %7ld %s %s", prava, statbuf.st_nlink, statbuf.st_uid, 
                                                    statbuf.st_gid, statbuf.st_size, cas, name.c_str());
            result = temp;
            return 1;
        } else {
            sprintf(temp, "%s % 4d %-9d%-9d %7ld ...... %s", prava, statbuf.st_nlink, statbuf.st_uid, 
                                                    statbuf.st_gid, statbuf.st_size, name.c_str());
            result = temp;
            return 2;
        }//else
        
  } else { //je to virtualni adresar

        result = "drwxrwxrwx    1 0        0            1024 Jan 01 01:00 " + name;      
        return 1;
  } // else u if !is_virtual
}



/** Zjisti zda objekt zapouzdruje obycejny soubor.
 *
 * Pokud objekt zapouzdruje obyc. soubor vrati true jinak vrati false.
 *
 */
bool VFS_file::IsRegularFile() {
    int         ret;
    struct stat statbuf;
    string      file_name;

    file_name = path + "/" + name;
    ret = stat(file_name.c_str(), &statbuf);
    if (ret == -1) return false;
    if (S_ISREG(statbuf.st_mode)) return true; else return false;
}



