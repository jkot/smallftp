/** @file VFS_file.h
 *  /brief Deklarace tridy VFS_file.
 *
 */

#ifndef __VFS_FILE_H
#define __VFS_FILE_H

#include <string>

extern "C" {
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
}

#include <iostream>

using namespace std;


/** Trida VFS_file - zapouzdruje soubory pro tridu VFS.
 *
 * Umoznuje praci se soubory - napriklad ziskani stringu s informacemi o
 * souboru ve tvaru prikazu ls -l.
 *
 */
class VFS_file {
public:
    VFS_file(string _name, string _path);
    ~VFS_file();
    int GetLslInfo(string &result);
    
    void Name(string x)        { name = x; }
    void Path(string x)        { path = x; }
    void UserName(string x)    { user_name = x; }
    void UserRights(int x)      { user_rights = x; }
    void OthersRights(int x)    { others_rights = x; }

    string Name()       { string s; s=name; return s; }
    string Path()       { string s; s=path; return s; }
    string UserName()   { string s; s=user_name; return s; }
    int UserRights()    { return user_rights; }
    int OthersRights()  { return others_rights; }
    bool IsVirtual()    { return is_virtual; }
    void IsVirtual(bool x) { is_virtual = x; }
    bool IsRegularFile();
    

private:
    FILE      * descriptor;
    string      name;
    string      path;    
    bool        is_virtual;

    string      user_name;
    int         user_rights;
    int         others_rights;
};



#endif //__VFS_FILE_H
