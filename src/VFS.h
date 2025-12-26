/** @file VFS.h
 *  \brief Deklarace tridy VFS.
 *
 */

#ifndef __VFS_H
#define __VFS_H

#define R_READ   1
#define R_WRITE  2
#define R_ALL	 3
#define R_NONE   0

#define NO_USER "none"

#include <cstdio>
#include <vector>
#include <list>
#include <deque>
#include <string>
#include "DirectoryDatabase.h"

#include "my_exceptions.h"
#include "VFS_file.h"



#define MAX_PATH_LEN 600
#define MAX_CFG_LINE_LEN 600

/** Trida vytvarejici virtualni filesystem.
 *
 * Umoznuje pod korenovy adresar pripojit mnozstvi virtualnich adresaru a
 * kazdemu priradit nejaky fyzicky adresar (nebo take zadny). Tato struktura je
 * popsana v konfiguracnim souboru VFS, ktery nacita konstruktor a jeho znovu
 * nahrani umoznuje funkce ReloadConfigFile().
 *    Trida kontroluje pristupova prava uzivatele, podle udaju ulozenych v
 * databazi, pomoci tridy DirectoryDatabase. Pokud napriklad nema uzivatel
 * pravo na cteni urciteho adresare, vrati funkce ChangeDir() chybu. Funkce
 * ChangeDir() dostava jako argument virtualni cestu a podle ni prepne aktualni
 * adresar, popr. prislusne zmeni fyzicky adresar, je-li treba. Pritom
 * kontroluje, za prve jestli cesta existuje, jestli k ni ma uzivatel
 * dostatecna prava a zda nahodou nevede pres nejaky link mimo nasdilenou
 * strukturu (nactenou z konfiguracniho souboru). Tj. pokud ma napriklad
 * virtualni root "/" prirazeny fyzicky adresar /home/alice, pod nim je
 * pripojeny virtualni adresar "/media" s fyzickym adresarem
 * /mnt/windows/media
 * a fyzicky na disku existuje adresar /home/alice/konf, ktery je linkem na
 * fyzicky adresar /etc, pak ChangeDir("/konf") vrati chybu, protoze fyzicky
 * adresar /etc neni v nasidene strukture. Kdezto, pokud existuje adresar
 * /home/alice/filmy, ktery jako link odkazuje na fyzicky adresar
 * /mnt/windows/media/filmy, prikaz ChangeDir("/filmy") vyhovi, protoze link vede do
 * nasdilene struktury (do podadresare nasdileneho adresare
 * /mnt/windows/media).
 *    Trida pracuje s pravy R_READ, R_WRITE, R_ALL a R_NONE, umoznuje i
 * napriklad obdobu linuxovske cerne skrinky - tj. uzivatel ma k adresari pouze pravo
 * R_WRITE, takze se do adresare nemuze prepnout nebo vypsat soubory v nem
 * obsazene, ale muze do nej zapisovat (coz potvrdi funkce
 * AllowedToWriteToDir()). K souboru ci adresari lze urcit jeho vlastnika, prava
 * vlastnika a pristupova prava ostatnich uzivatelu.
 *    Pokud o souboru jeste neni v databazi zaznam, vrati funkce GetFileInfo()
 * implicitne jako vlastnika NO_USER, jeho prava R_NONE a prava ostatnich jako
 * R_READ, zaznam do databaze neuklada.
 *    Funkce NextFile() a ResetFiles() umoznuji postupny pruchod souboru a
 * adresaru (jak virtualnich tak fyzickych) obsazenych v aktualnim adresari.
 *    Nasdilena struktura adresaru je se uchovava ve stromu tvorenem uzly
 * VFS_node.
 * 
 */
class VFS {
public:
    VFS(const char * path, const char * db_name) throw(VFSError, GdbmError, FileError);
    ~VFS();
    int         ReloadConfigFile(const char * path);
    
    int         ChangeDir(const char * path);
    string      CurrentDir();
    string      CurrentPhysicalDir();
    int         ConvertToPhysicalPath(string virtual_path, string &physical_path);
    bool        AllowedToWriteToDir(string dir); 

    void        ResetFiles();
    VFS_file  * NextFile();
    int         GetFileInfo(const char * path, VFS_file &x);
    int         PutFileInfo(VFS_file file);
    int         DeleteFileInfo(VFS_file file); 
    
    bool        IgnoreHidden()     { return ignore_hidden; }
    void        IgnoreHidden(bool x) { ignore_hidden = x; root_db.IgnoreHidden(x); }
    void        PrintDb() { root_db.PrintContent(); }
    bool        IsFile(const char * path);
    bool        IsDir(const char * path);
    string      FtpUserName() {string s; s = ftp_user_name; return s; }
    void        FtpUserName(string x) { ftp_user_name = x; }
    
private:
    int  LoadConfigFile(const char * path);
    int  SimpleCd(const char *dir);
    
    class VFS_node {
    public:
        VFS_node(const char * _vname = "", const char * _pname = "", VFS_node * _parent = 0);
        VFS_node * Parent() { return parent; }
        VFS_node * NextChild(); // vraci postupne potomky
        VFS_node * LastChild(); // vrati posledniho potomka - kvuli prochazeni cyklem
        VFS_node * FindChild(string s); // zjisti zda s odpovida nejakemu potomkovi
        void ResetChildren(); // zpusobi, ze NextChild() vrati potomka na prvnim miste ve vectoru
        void AddChild(VFS_node * x)    { children.push_back(x); child_it = children.begin(); }
        int NumOfChildren() { return children.size(); }
        void GetChildren(vector<VFS_node *> &x) { x=children; }
        
        void SetDir(bool x)     { is_dir = x; }
        void SetFile(bool x)    { is_file = x; }
        void SetUserName(string &s)  { user_name = s; }
        void SetUserRights(int r)    { user_rights = r; }
        void SetOthersRights(int r)  { others_rights = r; }
        bool IsDir()     { return is_dir; }
        bool IsFile()    { return is_file; }
        bool IsLeaf()    { return children.empty(); }
        string VirtualName()  { string s; s=virtual_name; return s; }
        string FullVirtualName();
        string PhysicalName() { string s; s=physical_name; return s; }
        void   PhysicalName(string &x) { physical_name = x; }
        string UserName()     { string s; s=user_name; return s; }
        int UserRights()      { return user_rights; }
        int OthersRights()    { return others_rights; }

    private:
        bool is_dir;
        bool is_file;
        vector<VFS_node *> children;
        vector<VFS_node *>::iterator child_it;
        VFS_node * parent;
        string virtual_name; ///< jmeno virtualniho adresare (bez jakychkoliv lomitek)
        string physical_name;///< absolutni cesta k fyzickemu adresari, ktery odpovida tomu virtualnimu.bez lomitka na konci
        string user_name;
        int user_rights;
        int others_rights;
    }; //class VFS_node
    
    
    void        PreorderAction(VFS_node * n, int action(VFS_node * node, int num), int depth);
    void        PostorderAction(VFS_node * n, int action(VFS_node * node, int num), int depth);
    VFS_node * _DirSecurityCheck(VFS_node * n, string dir); 
    VFS_node * _FindNode(VFS_node * n, string name);
    
    friend int PrintName(VFS::VFS_node * node, int depth); 
    friend int DeleteNode(VFS::VFS_node * node, int);
    
    int         DestroyVirtualTree();
    VFS_node *  DirSecurityCheck(string dir);
    VFS_node *  FindNode(string name);
       
    DirectoryDatabase root_db; ///< fyzicky adresar databaze odpovida virtualnimu rootu
    VFS_node    * current_node; ///< v jakem uzlu virtualniho stromu prave jsme
    VFS_node    * root_node; ///< koren virtualniho stromu 
    DIR         * dir_desc; ///< stream pro aktualni adresar
    bool          dir_changed;
    vector<VFS_node *> child_buf;

    bool          ignore_hidden;
    string        ftp_user_name; 
    
    /** current_dir obsahuje fyzickou cestu k aktualnimu adresari
     * Pokud ale jsme zrovna ve virtualnim uzlu s fyzickym adresarem, tak
     * current_dir obsahuje ".", celou fyzickou cestu do aktualniho adresare
     * obsahuje az kdyz sestoupime do nejakeho podadresare fyzickeho adresare
     * soucasneho uzlu.
     */
    string        current_dir; 
    
public:

    void PrintVirtualTree();
    
};


#endif //__VFS_H

