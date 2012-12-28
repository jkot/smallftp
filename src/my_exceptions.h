/** @file my_exceptions.h
 *  /brief Deklarace trid vyjimek.
 *
 */

#ifndef __MY_EXCEPTIONS_H
#define __MY_EXCEPTIONS_H

extern "C" {
#include <errno.h>
#include <gdbm.h>
}

#include <exception>
#include <string>


using namespace std;
//extern int errno;




/** Trida vyjimky pro chybu pri praci se souborem.
 * Pomoci teto tridy lze predat text s informaci o chybe, pripadne ciselny kod
 * chyby.
 */
class FileError : public exception { //exception class

    int num;
    std::string msg;

public:
    char *what() { return (char *)msg.c_str();}
    int err() { return num; }
    
    FileError(const char* zprava, int i=-1) { msg = zprava; num = i; }
    ~FileError() throw() {} //pokud to tu neni, bouri se prekladac
};





/** Trida vyjimky pro praci s GDBM.
 * Pouzije se v pripade, ze selze nejaka operace s databazi. Zjisti a
 * uschova v sobe dostupne informace o chybe. Je potreba dat pozor na
 * funkci vracejici popis chyby k jejimu kodu, pokud by se pouzivala
 * vlakna.
 */
class GdbmError : public exception { 
    int    num;
    int    gdbm_err;
    char * msg;
    
public:
    /** Vyplni informace o chybe. */
    GdbmError() {
        msg      = gdbm_strerror(gdbm_errno);
        num      = errno;
        gdbm_err = gdbm_errno;
    }
    
    /** Predefinovani virtualni funkce zdedene po exception. */
    char *what() { return msg; }
    int GetErrno() { return num;}
    int GetGdbmErrno() { return gdbm_err; }
}; 


/** Trida vyjimky pro tridu VFS.
 *
 *
 */
class VFSError : public exception {
    int num; //hlavni cislo chyby
    int sub_num; //vedlejsi cislo chyby
    string msg;

public:
    VFSError(const char * _msg, int _num = -1, int _sub_num = 0) {
        msg = _msg;
        num = _num;
        sub_num = _sub_num; 
    }
    ~VFSError() throw() {} //pokud to tu neni, bouri se prekladac

    char *what() { return (char *)msg.c_str(); }
    int ErrorNum() { return num; }
    int ErrorSubNum() { return sub_num; }
};

#endif

