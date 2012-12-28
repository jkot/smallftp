/** @file VFS.cpp
 *  \brief Implementace tridy VFS.
 *
 * Pouziva operatory new a delete - muzou hodit vyjimku (bad_alloc atp.), je potreba s
 * tim pocitat a odchytit ji.
 *
 */

//#define DEBUG
#include "VFS.h"
#include "VFS_pomocne.cpp"
/** Konstruktor tridy VFS_node, inicializuje promenne.
 *
 */
VFS::VFS_node::VFS_node(const char * _vname, const char * _pname, VFS_node * _parent) {
    parent = _parent;
    virtual_name = _vname;
    physical_name = _pname;
    child_it = children.begin();
}


/** Tato funkce vrati point na dalsiho potomka.
 *
 * Je-li seznam potomku prazdny, vrati 0, jinak vrati odkaz na dalsiho potomka.
 * Pokud byl naposledy vracen odkaz na posledniho potomka, vrati nyni odkaz na prvniho potomka.
 *
 * Pred pouzitim v cyklu by se melo pouzit ResetChildren()!
 * 
 */
VFS::VFS_node * VFS::VFS_node::NextChild() {
    VFS_node *tmp;
    
    if (child_it == children.end()) {
        child_it = children.begin();
        if (!children.empty()) return *(child_it++); else return 0;
        
    } else {
        return *(child_it++);
    }//else
}

/** Vraci odkaz na posledniho potomka.
 *
 * Je-li seznam potomku prazdny, vrati 0.
 *
 */
VFS::VFS_node * VFS::VFS_node::LastChild() {
    if (!children.empty()) return children.back(); else return 0;
}

/** Nastavi iterator na zacatek seznamu potomku.
 *
 */
void VFS::VFS_node::ResetChildren() {
    child_it = children.begin();
}


/** Zjisti, zda je s (virtualni) jmeno nejakeho (primeho) potomka.
 *
 * Pokud je, vrati ukazatel na nej, jinak vrati 0.
 * 
 */
VFS::VFS_node * VFS::VFS_node::FindChild(string s) {
    VFS_node * node;
    
#ifdef DEBUG    
    cout << "VFS_node::FindChild(): hledame potomka " << s << endl;    
#endif
    
    ResetChildren();
    node = NextChild();
    if (node != 0) { //nejaky potomek existuje
        while (node != LastChild()) {
            if (node->virtual_name == s) return node;
            node = NextChild();
        }
    
        if (node->virtual_name == s) return node; else return 0;
    } else return 0; //jeste zadneho potomka nema
}

/** Vraci absolutni cestu ve virtualnim stromu k danemu uzlu.
 *
 * Cesta bude s lomitkem na zacatku, bez lomitka na konci.
 * 
 */
string VFS::VFS_node::FullVirtualName() {
    string result;
    VFS_node * node;

    result = virtual_name;
    node = Parent();
    if (node == 0) return result; //byl to dotaz na roota = "/", lomitko by se jinak umazalo
    
    while (node != 0) {
        result = node->VirtualName() + "/" + result;
        node = node->Parent();
    }
    
    result.erase(0,1); 
        //jmeno virtualniho rootu je "/", takze na konci se provede "/" + "/" + result
    
    return result;
}

/** Konstruktor tridy VFS.
 *
 * Nacte konfiguracni soubor - pokud se to nepovede, hodi vyjimku VFSError,
 * s hlavnim kodem chyby -1 a vedlejsi kod bude odpovidat cislu radky, na
 * kterem se chyba vyskytla.
 *
 * path = cesta ke konfiguracnimu souboru
 * db_name = jmeno databaze pouzivane tridou DirectoryDatabase (obsahuje
 *           informace o pravech a uzivateli jednotlivych souboru)
 * 
 */
VFS::VFS(const char * path, const char * db_name)throw (VFSError, GdbmError, FileError):root_db(db_name)  {
    int    ret;
    string s;
    
    current_dir = ".";
    dir_desc = 0;
    dir_changed = true;
    ignore_hidden = false;
    ftp_user_name = ""; //jmeno toho kdo se nalogoval na server zatim nezname
    
#ifdef DEBUG 
    cout << "konstruktor VFS ... jdem nacist konfigurak"<< endl;
#endif

    ret = LoadConfigFile(path);

    if (ret != 1) {
        s = "Chyba pri nacitani konfiguracniho souboru ";
        s += path;
        throw VFSError(s.c_str(), -1, ret);
    }
}




#define PRINT_CFG_ERR(x) cout << getpid() << " - Chybny konfiguracni soubor ("#x") \"" << name << "\""; \
  cout << " - chyba na radku cislo " << radek << "." << endl;


/** Nahrava konfiguracni soubor tridy VFS.
 *
 * Kontrolujeme, jestli mame do adresare pristup. CheckDir(), ktery pouzivame zaroven
 * resolvne pripadne symbolicke linky v ceste a ulozi do physical_dir
 * cestu ktera je neobsahuje - potrebujeme mit v uzlech ve fyzickem
 * adresari resolvnutou cestu, protoze aktualni adresar pak
 * zjistujeme pomoci getcwd a to vraci cestu bez linku i kdyz jsme se do
 * adresare pres nejaky link dostali - s neresolvnutou cestou v uzlech 
 * by nam pak selhaval DirSecurityCheck().
 * V pripade uspechu vraci 1, jinak vrati zapornou hodnotu cisla radku, na
 * kterem se vyskytla chyba.
 *
 */
int VFS::LoadConfigFile(const char * name) {
    int         ret;
    FILE *      f;
    char        bafr[MAX_CFG_LINE_LEN];
    char        temp[MAX_CFG_LINE_LEN];
    char *      p;
    int         radek = 0; //cislo prave zpracovavaneho radku
    string      root_dir;

#ifdef DEBUG
    cout << "jsme ve VFS::LoadConfigFile()" << endl;
#endif
    
    if ((f = fopen(name,"r")) == 0) {
#ifdef DEBUG
        PRINT_CFG_ERR(1)
        perror("LoadVFSConfigFile()");
#endif        
        return 0;
    }
    
    radek++;
    //nacteme korenovy adresar - musi byt na prvni radce
    if (fgets(bafr, MAX_CFG_LINE_LEN, f)) {
        ret = sscanf(bafr, "%s", temp);
        if (ret == 0 || ret == EOF) {
#ifdef DEBUG
        PRINT_CFG_ERR(2)
#endif        
        return -radek;            
        }
    } else {
#ifdef DEBUG
        PRINT_CFG_ERR(3)
#endif        
        return -radek;    
    }//else u fgets
    
    root_dir = temp;
    
    ret = CheckDir(root_dir);
    if (ret != 1) {
#ifdef DEBUG
        PRINT_CFG_ERR(9)
        cout << "Nelze otevrit adresar " << root_dir << endl;
        if (ret == -2) cout << "            - nedostatecna prava." << endl;
        if (ret == -3) { cout << "            - relativni cesta neni povolena." << endl; return -radek; }
#endif
        if (ret == -1) return -radek;
    }
    
    root_node = new VFS_node("/", root_dir.c_str(), 0);
    current_node = root_node;

    string          physical_dir; 
    string          virtual_dir;
    string          user_name;
    int             user_rights;
    int             others_rights;
    list<string>    args;
    string          s;
    string          usrr, othr;

    while ((p=fgets(bafr, MAX_CFG_LINE_LEN, f)) != 0) if (strlen(bafr) > 1) {
        
        radek++;
#ifdef DEBUG
        cout << "jdem zpracovat radek c. " << radek << ": " << bafr << endl;
#endif
        if (!args.empty()) args.erase(args.begin(), args.end());
        s = bafr;
        ret = VFSCutIntoParts(s, args);
        if (args.size() != 5 || ret == -1) {
#ifdef DEBUG
            PRINT_CFG_ERR(4)
            cout << "ret = " << ret << " args.size() = " << args.size() << endl;
            cout << args.front() << endl;
#endif        
            return -radek;    
        }
        
        deque<string> parts;
        int res;

        physical_dir  = args.front(); args.pop_front();
        virtual_dir   = args.front(); args.pop_front();
        user_name     = args.front(); args.pop_front();
        usrr          = args.front(); args.pop_front();
        othr          = args.front(); args.pop_front();
        
        if ((sscanf(usrr.c_str(), "%d", &user_rights)   != 1) || 
            (sscanf(othr.c_str(), "%d", &others_rights) != 1)   ) {
#ifdef DEBUG
            PRINT_CFG_ERR(5)
#endif        
            return -radek;
        }
        
        
        if (CheckRights(others_rights)!=1) {
#ifdef DEBUG
            PRINT_CFG_ERR(6)
#endif        
            return -radek;
        }

        if (user_name!=NO_USER && CheckRights(user_rights)!=1) {
#ifdef DEBUG
            PRINT_CFG_ERR(7)
#endif        
            return -radek;
        }
        
        res = CutPathIntoParts(virtual_dir, parts);
        if (res != 1) {
#ifdef DEBUG
            PRINT_CFG_ERR(8)
#endif
            return -radek;
        }
        
        //potrebujeme fyzicky adresar na konci bez lomitka
        if (physical_dir[physical_dir.size()-1] == '/') physical_dir.erase(physical_dir.size()-1, 1);
        
        //zkontrolujeme, jestli mame do adresare pristup. CheckDir() zaroven
        //resolvne pripadne symbolicke linky v ceste a ulozi do physical_dir
        //cestu ktera je neobsahuje - potrebujeme mit v uzlech ve fyzickem
        //adresari mit resolvnutou cestu, protoze aktualni adresar pak
        //zjistujeme pomoci getcwd a to vraci cestu bez linku i kdyz jsme se do
        //adresare pres nejaky link dostali - s neresolvnutou cestou v uzlech 
        //by nam pak selhaval DirSecurityCheck().
        ret = CheckDir(physical_dir);
        if (ret != 1) {
#ifdef DEBUG
            PRINT_CFG_ERR(9)
            cout << "Nelze otevrit adresar " << physical_dir << endl;
            if (ret == -2) cout << "            - nedostatecna prava." << endl;
            if (ret == -3) { cout << "            - relativni cesta neni povolena." << endl; return -radek; }
#endif
            if (ret == -1) return -radek;
        }

#ifdef DEBUG 
        cout << "jdem stavet ten virtualni strom " << endl;
#endif

        VFS_node * node = root_node;
        VFS_node * tmp;
        string     s;
        while (!parts.empty()) {
            s = parts.front();
            if (s == "/") { 
                parts.pop_front(); 
                if (!parts.empty()) s = parts.front(); 
                else {
#ifdef DEBUG
                    PRINT_CFG_ERR(10)
                    cout << "Virtualnimu rootu nelze priradit druhy fyzicky adresar." << endl;
#endif
                    return -radek;
                }
            }//if s == "/"
            
#ifdef DEBUG
            cout << "cast " << s << endl;
#endif
            
            parts.pop_front();
            if ((tmp=node->FindChild(s)) == 0) {
                //takovy potomek jeste neexistuje
#ifdef DEBUG
                cout << "takovy potomek jeste neni" << endl;
#endif
                if (parts.empty()) { 
                    //zpracovavame posledni, tj. nejhlubsi adresar
                    tmp = new VFS_node(s.c_str(), physical_dir.c_str(), node);    
                } else tmp = new VFS_node(s.c_str(), "", node);
                
                
                tmp->SetDir(true);
                tmp->SetFile(false);
                tmp->SetUserName(user_name);
                tmp->SetUserRights(user_rights);
                tmp->SetOthersRights(others_rights);
#ifdef DEBUG
                cout << "novy uzel " << s << " inicializovan, zaradime ho do rodice " << node->VirtualName() << endl;
#endif
                node->AddChild(tmp);
                node = tmp; //prejdeme do potomka
            } else if (!parts.empty()) {
                node = tmp; //prejdeme do potomka.
            } else { //takovy virtualni adresar uz existuje, ale jeste mozna nema fyzicky protejsek
                if (tmp->PhysicalName() != "") { //konflikt
#ifdef DEBUG
                    PRINT_CFG_ERR(11)
                    cout << "Jednomu virtualnimu adresari je prirazeno vic fyzickych." << endl;
#endif  
                    return -radek;
                } else { //OK, tenhle virtualni adresar jeste zadny fyzicky nema, priradime mu ho
                    tmp->PhysicalName(physical_dir);
                }//else
            }//else
            
        }//while !empty        
        
        
    } //while p=fgets() != 0
    
    return 1;
} //LoadConfigFile()


/** Projde strom v post-orderu.
 *
 * Projde kazdy uzel stromu a pred jeho opustenim spusti funkci action, ktera
 * dostane jako parametr ukazatel na aktualni uzel a hloubku uzlu ve strome.
 * 
 */
void VFS::PostorderAction(VFS_node * n, int action(VFS_node * node, int num), int depth) {
    VFS_node * tmp;

    if (n == 0) return;

    n->ResetChildren();
    tmp = n->NextChild();
    while (tmp != n->LastChild()) {
        PostorderAction(tmp, action, depth + 1);
        tmp = n->NextChild();
    }
    PostorderAction(tmp, action, depth + 1); //zpracujeme posledniho potomka
    
    action(n, depth);
}

/** Projde strom v pre-orderu.
 *
 * Projde kazdy uzel stromu a vzdy nejdrive spusti funkci action, ktera
 * dostane jako parametr ukazatel na aktualni uzel a hloubku uzlu ve strome.
 * 
 */
void VFS::PreorderAction(VFS_node * n, int action(VFS_node * node, int num), int depth) {
    VFS_node * tmp;

    if (n == 0) return;

    action(n, depth);

    n->ResetChildren();
    tmp = n->NextChild();
    while (tmp != n->LastChild()) {
        PreorderAction(tmp, action, depth + 1);
        tmp = n->NextChild();
    }
    PreorderAction(tmp, action, depth + 1); //zpracujeme posledniho potomka

}


void tab(int t) {
    for (int i=0; i<t; i++) cout << " ";
}

/** Pomocna funkce pro PrintVirtualName()
 *
 */
int PrintName(VFS::VFS_node * node, int depth) {
    tab(depth*5);
    cout << node->VirtualName() << " ... fyzicky adresar = \'" << node->PhysicalName() << "\'" << endl;
}


/** Vypise virtualni strom nasdilenych adresaru.
 *
 * Funkce pro ucely ladeni.
 *
 */
void VFS::PrintVirtualTree() {
    if (root_node == 0) { cout << "Strom je prazdny." << endl; return; }
    
    PreorderAction(root_node, &(PrintName), 0);
    //PostorderAction(root_node, &(PrintName), 0);
}


/** Pomocna funkce pro DestroyVirtualTree() */
int DeleteNode(VFS::VFS_node * node, int) {
    delete node;
}


/** Zrusi virtualni strom.
 *
 * Pokud uspeje, vrati 1 a vynuluje root_node, jinak vrati -1 a s root_node nic
 * nedela.
 * 
 */
int VFS::DestroyVirtualTree() {
    try{
        PostorderAction(root_node, &DeleteNode, 0);
    } catch (exception x) {
#ifdef DEBUG
        cout << "DestroyVirtualTree(): CHYBA nejspis pri \'delete node\'" <<endl;
        cout << "       popis: " << x.what() << endl;
#endif
        return -1;
    }
    
    root_node = 0;
    return 1;
}

/** Znovu nahraje konfiguracni soubor.
 *
 * Kompletne prestavi virtualni strom podle novych informaci.
 *
 * Navratove hodnoty:
 *
 *      -  1      vse OK
 *      - -1      chyba pri destrukci virtualniho stromu
 *      - -2      chyba pri nahravani konfiguracniho souboru
 * 
 */
int VFS::ReloadConfigFile(const char * path) {
    int    ret;
    string s;
    
    ret = DestroyVirtualTree();
    if (ret != 1) return -1;
    
    ret = LoadConfigFile(path);
    if (ret != 1) { 
        return -2;
    }
    
    dir_changed = true;
    
    return 1;
}



/** Destruktor tridy VFS.
 *
 * Zrusi virtualni strom.
 * 
 */
VFS::~VFS() {
    DestroyVirtualTree();
}

/** Pomocna rekurzivni funkce pro DirSecurityCheck.
 *
 */
VFS::VFS_node * VFS::_DirSecurityCheck(VFS_node * n, string dir) {
    VFS_node * tmp;
    VFS_node * ret_node;
    string     name;

    if (n == 0) return 0;
    
    //je fyzicky adresar uzlu n predponou adresare dir?
    name = n->PhysicalName();
    if (name != "") { //to by sice predpona byla, ale to nechceme ...
        int x = dir.find(name);
        if (x == 0) return n; // ano, je --> vracime se
    }
    //ne, neni --> pokracujeme v hledani

    n->ResetChildren();
    tmp = n->NextChild();
    while (tmp != n->LastChild()) {
        if ((ret_node = _DirSecurityCheck(tmp, dir)) != 0) return ret_node; //predpona nalezena, vracime se
        tmp = n->NextChild(); // nenalezeno, hledame dal
    }
    if ((ret_node = _DirSecurityCheck(tmp, dir)) != 0) return ret_node; //zpracujeme posledniho potomka
    
    return 0; // predpona neni ani v nasem uzlu n, ani v jeho potomcich, vracime se
}

/** Kontroluje, jestli lze prejit do adresare dir.
 *
 * Do adresare dir muzeme prejit, pokud ukazuje nekam do podstromu nasdilenych
 * fyzickych adresaru - tj. fyzicky adresar nektereho z uzlu virtualniho stromu
 * je predponou adresare dir.
 * Pokud dir ukazuje na bezpecne misto, vrati funkce ukazatel na uzel, pod
 * ktery adresar patri, jinak vraci 0.
 *
 */
VFS::VFS_node * VFS::DirSecurityCheck(string dir) {
    VFS_node * ret;
    
    ret = _DirSecurityCheck(root_node, dir);
    return ret;
}





/** Pomocna rekurzivni funkce pro FindNode.
 *
 */
VFS::VFS_node * VFS::_FindNode(VFS_node * n, string name) {
    VFS_node * tmp;
    VFS_node * ret_node;

    if (n == 0) return 0;
    
    //fyzicky adresar uzlu n == adresare name?
    if (name == n->PhysicalName()) return n; // ano, je --> vracime se
    
    //ne, neni --> pokracujeme v hledani

    n->ResetChildren();
    tmp = n->NextChild();
    while (tmp != n->LastChild()) {
        if ((ret_node = _FindNode(tmp, name)) != 0) return ret_node; //nalezeno, vracime se
        tmp = n->NextChild(); // nenalezeno, hledame dal
    }
    if ((ret_node = _FindNode(tmp, name)) != 0) return ret_node; //zpracujeme posledniho potomka
    
    return 0; // nenalezeno ani v nasem uzlu n, ani v jeho potomcich, vracime se
}


/** Hleda uzel s fyzickym jmenem (adresarem) name.
 *
 * Pokud uzel najde, vrati odkaz na nej, pokud ho nenajde, vrati 0.
 * 
 */
VFS::VFS_node * VFS::FindNode(string name) {
    VFS_node * ret;

    ret = _FindNode(root_node, name);
    return ret;
}




/** Pomocna funkce pro VFS::ChangeDir.
 *
 * Jako argument dostane jmeno adresare bez cesty, do nej se pokusi prepnout.
 *  
 *  Navratove hodnoty:
 *  
 *      -  1      vse v nejlepsim poradku :)
 *      - -2      nedostatecna prava
 *      - -3      neexistuje
 *      - -4      prilis dlouhe jmeno
 *      - -5      nelze jit vys, jsme v rootu
 *      - -1      jina chyba
 * 
 */
int VFS::SimpleCd(const char *dir) {
    int         ret;
    string      s(dir);
    
    if (s == ".." && current_dir == ".") {
        if (current_node->Parent() != 0) current_node = current_node->Parent(); else return -5;
        return 1;
    }

    if (s == ".") { 
        if (current_node->PhysicalName() != "") {
            if (current_dir == ".") {
                chdir(current_node->PhysicalName().c_str());
            } else { 
                //current_dir!="." takze je to = fyz.jm. aktualniho uzlu + cesta kde jsme tj. + aspon jedno
                //lomitko a jmeno adresare - takze z current_diru umazeme
                //fyz.jm + 1 znaku a dostaneme relativni cestu do adresare kde
                //jsme - cesta bude relativni k fyzickemu adresari aktualniho
                //uzlu. umazat muzeme, viz prvni radek komentare...
                string tmp = current_dir;
                string pname = current_node->PhysicalName();
                int x = pname.size();
                tmp.erase(0,x+1);
                chdir(current_node->PhysicalName().c_str()); //pro jistotu
                chdir(tmp.c_str());
            }

        } 
            //protoze jsme ve virtualnim uzlu, nebudem kontrolovat navratovou
            //hodnotu chdiru - i kdyz nepujde prejit do odpovidajiciho
            //fyzickeho adresare, jeste jsou tu mozna dalsi virtualni 
        //cout << "SimpleCd: current_dir = " << current_dir << endl; 
        //cout << "SimpleCd: phdir = " << CurrentPhysicalDir() << endl; 
        return 1;
    }
           

    
    if (s == "/") {     // do rootu taky klidne pujdeme
        current_node = root_node;
        current_dir = ".";
        if (current_node->PhysicalName() != "") {
            chdir(current_node->PhysicalName().c_str()); 
            //navratovou hodnotu nekontrolujeme z duvodu viz vyse
        }
        return 1;
    }
    
    //mozna to bude chtit virtualni prechod   
    if (current_dir == ".") { 
        VFS_node * node;
        node = current_node->FindChild(s);
        if (node != 0) //virtualni prechod
            if ( ((node->UserName()==ftp_user_name && (node->UserRights() & R_READ)) || (node->OthersRights() & R_READ)) ){ 
                //mame na to (virtualni) prava, jdem
                current_node = node; 
                if (node->PhysicalName() != "") chdir(node->PhysicalName().c_str());
                return 1; 
            } else {
                // nemame na to (virtualni) prava, koncime
                return -2;
            }
        
        else // tak to chce normalni prechod
            if (current_node->PhysicalName() == "") return -3;//virtualni prechod nejde,fyzicky adresar tenhle uzel nema...
            else { //uzel ma fyzicky adresar, prejdeme do nej
                int ret = chdir(current_node->PhysicalName().c_str());
                if (ret == -1)
                switch (errno) {
                    case EPERM:  return -2; //nedostatecna prava
                    case ENOENT: return -3; //neexistuje
                    case ENAMETOOLONG: return -4; //prilis dlouhe jmeno
                    default: return -1;
                }//switch
            }
    }// if current_dir == "."
    
    //chce to normalni prechod, ted uz jsme prepnuti ve fyzickem adresari
    //aktualniho current_node, takze zkusime normalni prechod    

    FileInfo info;
    ret = root_db.GetFileInfo(s, info); //mame o tomhle adresari informace v databazi ?
    if (ret == 1) //ano, mame
        if ( ! ((info.user_name==ftp_user_name && (info.user_rights & R_READ)) || (info.others_rights & R_READ)) ) {
            //k tomuhle adresari nemame dostatecna (virtualni) prava! koncime
            return -2;
        }

    ret = chdir(s.c_str());
    if (ret == -1)
    switch (errno) {
        case EPERM: return -2; //nedostatecna prava
        case ENOENT: return -3; //neexistuje
        case ENAMETOOLONG:  return -4; //prilis dlouhe jmeno
        default: return -1;
    }//switch
            
    
    char buf[MAX_PATH_LEN];
    memset(buf, 0, MAX_PATH_LEN);
    char * x = getcwd(buf, MAX_PATH_LEN); //vrati absolutni cestu, bez lomitka na konci
    if (x == 0) { 
#ifdef DEBUG            
        cout << "VFS::SimpleCd(): getcwd() selhalo ..."; 
#endif
        return -1;
    }

    if (s == "..") { //current_dir != ".", jinak by to vyhovelo nahore
        if (current_node->PhysicalName() == buf) current_dir = "."; //vys uz jit priste nemuzeme
        else current_dir = buf; //sli jsme nahoru, ale priste jeste muzeme jit vys
    } else  current_dir = buf; //sli jsme dolu
    
    return 1;    
}

/** Meni aktualni adresar.
 *
 * Pokud nelze vejit do noveho adresare, zustane na puvodnim miste.
 * 
 * Navratove hodnoty:
 *
 * viz. VFS::SimpleCd() +:
 *
 *     - -6      cesta vede pres link mimo nasdilenou adresarovou strukturu
 *
 */
int VFS::ChangeDir(const char * path) {
    deque<string> parts;
    string        s = path;
    int           ret = 0;
    VFS_node *    old_node = current_node;
    string        old_dir  = current_dir;   
    VFS_node *    ret_node;

    ret = CutPathIntoParts(s, parts);
    switch (ret) {
        case -2: return -1; //pri deleni cesty nastala vyjimka, taky koncime ...
        case -1: return -6; //cesta neni platna, obsahuje vic lomitek za sebou
    }//switch
    
    
    while (!parts.empty()) {
        
        s = parts.front();
        parts.pop_front();
        ret = SimpleCd(s.c_str());
        if (ret != 1) {
            //mohli jsme se dostat jinam, vratime se tam, kde jsme byli
            current_node = old_node;
            current_dir = old_dir;
            chdir(old_dir.c_str());
            
            switch (ret) {
                case -1: return -1; //jina chyba
                case -2: return -2; //nedostatecna prava
                case -3: return -3; //neexistuje
                case -4: return -4; //prilis dlouhe jmeno
                case -5: return -5; //nelze jit vys, jsme v rootu
                default: return -1;
            }//switch
        }//if


        /* ---------------- osetreni linku --------------------- */
        if (current_dir != ".") {
            if ((current_dir.find(current_node->PhysicalName())) != 0) {
                // po ceste jsme presli pres nejaky link, ktery nas zavedl mimo
                // podstrom fyzickeho adresare atkualniho uzlu - musime zkontrolovat,
                // jestli na to misto muzeme jit.
                if ((ret_node = DirSecurityCheck(current_dir)) == 0) { // nemuzeme tam
                   current_node = old_node;
                    current_dir  = old_dir;
                    chdir(old_dir.c_str());
                    return -6;
                } else { //muzeme tam
                    current_node = ret_node; //aktualni fyzicky adresar patri pod ret_node
                    if (current_dir == current_node->PhysicalName()) current_dir = ".";
                }//else
            }//if !=0
        } //if != "."
        
        
    }//while

    ResetFiles(); // kvuli spravnemu fungovani NextFile() - zavre stary otevreny adresar
    return 1;
}


/** Vraci aktualni adresar - absolutni cestu ve virtualnim strome.
 *
 */
string VFS::CurrentDir() {
    string result;
    
    if (current_dir == ".") {
        result = current_node->FullVirtualName();
        return result; 
    } else {
        result = current_dir;
        int x = result.find(current_node->PhysicalName());
#ifdef DEBUG
        if (x!=0) { 
            cout << "VFS::CurrentDir(): tady je neco spatne ...." << endl;
            cout << "\'" << result << "\' pry na zacatku neobsahuje \'" << current_node->PhysicalName() << "\'" << endl;
            exit(-1);
        }
#endif
        int size = current_node->PhysicalName().size();
        result.erase(x,size);
        if (current_node != root_node) result = current_node->FullVirtualName() + result;
        // v pripade ze current_node==root_node bychom udelali neco jako
        // result = / + /temp
        //
        // normalne to vypada result = /filmy + /dokumentarni
        return result;
    }
}





/** Zjisti skutecny aktualni fyzicky adresar pomoci getcwd().
 * 
 */
string VFS::CurrentPhysicalDir() {
    string result="";
    
    char buf[MAX_PATH_LEN];
    memset(buf, 0, MAX_PATH_LEN);
    char * x = getcwd(buf, MAX_PATH_LEN); //vrati absolutni cestu, bez lomitka na konci
    if (x == 0) { 
#ifdef DEBUG            
        cout << "VFS::CurrentPhysicalDir(): getcwd() selhalo ..."; 
#endif
    } else result = buf;
    
#ifdef DEBUG
    if (current_dir != ".") {
        if (current_dir != result) {
        cout << "CurrentPhysicalDir(): Nekonzistence ve VFS: podle VFS je aktualni adresar " << current_dir;
        cout << ", kdezto doopravdy to je " << result << endl;
        }
    }
#endif
    
    return result;
}





/** Postupne vraci soubory a adresare v aktualnim adresari.
 *
 * V pripade chyby nebo vycerpani vsech podadresaru vrati 0. Po zavolani
 * ResetFiles() zacne NextFile() vracet soubory a adresare od zacatku.
 * Pokud je ignore_hidden == true, nebude vracet skryte soubory a adresare.
 *
 */
VFS_file * VFS::NextFile() {
    VFS_file * result;
    
    if (dir_changed) { //zmenil se aktualni adresar --> musime znovu nacist informace + otevrit aktualni adresar
        dir_changed = false;
        if (current_dir == "."){ current_node->GetChildren(child_buf); } //virtualni adresar --> potrebujeme jeho virt.deti
                           else { //fyzicky adresar
                               child_buf.erase(child_buf.begin(), child_buf.end()); //virtualni deti nema
                               dir_desc = opendir(current_dir.c_str()); //zato ma normalni podadresare
                               if (dir_desc == 0) return 0;
                           }//else
                           
        if (current_dir == "." && current_node->PhysicalName()!= "") { //virt.adresar s fyzickym ekvivalentem
            dir_desc = opendir(current_node->PhysicalName().c_str());
            //if (dir_desc == 0) return 0;
        }
    } //if dir_desc == 0
    
    if (!child_buf.empty()) { //jeste mame nejake nepredane virtualni adresare    
        VFS_node * n;
        n = child_buf.back(); 
        child_buf.pop_back();
        
        result = new VFS_file(n->VirtualName(), "");
        result->UserRights(n->UserRights());
        result->OthersRights(n->OthersRights());
        result->UserName(n->UserName());
        return result;
    } else if (dir_desc != 0){ //ted jdeme na fyzicke adresare a soubory... teda pokud nejake odpovidajici mame
        struct dirent * entry;
        int             ret;
        string          name;
        string          path;       
        string          absolute_name;
        FileInfo        info;
        bool            OK = false;
        
        while (!OK) {
            entry = readdir(dir_desc);
            if (entry == 0) {  ResetFiles(); return 0; }
            name = entry->d_name;
            if (name == "." || name == "..") continue;
            if (name[0] == '.' && ignore_hidden) continue;
            OK = true;
        }

        name = entry->d_name;
        if (current_dir==".") path = current_node->PhysicalName(); 
                         else path = current_dir; //cesta k ziskanemu souboru
                         
        result = new VFS_file(name, path);
        
        // jistejsi je pro vyhledavani v db. pouzit uplne jmeno souboru/adresare, pokud by se pouzilo
        // relativni jmeno, a byli bychom ve spatnem adresari (coz nejsme) mohl by se najit
        // spatny vysledek...
        if (path!="/") absolute_name = path + "/" + name; else absolute_name = "/" + name;
        ret = root_db.GetFileInfo(absolute_name, info); //mame o tomhle souboru informace v databazi ?
        if (ret == 1) { //ano, mame
#ifdef DEBUG
            cout << "udaj o " << name << " je z databaze." << endl;
#endif
            result->UserRights(info.user_rights);
            result->OthersRights(info.others_rights);
            result->UserName(info.user_name);
            return result;
        } else { //ne, v databazi o nem informace nejsou
            result->UserRights(R_READ);
            result->OthersRights(R_READ);
            result->UserName(NO_USER);
            return result;
        } // else u if ret == 1
    } else { ResetFiles(); return 0; } //dir_desc == 0 a virtualni adresare uz nemame
}



/** Restartuje nacitani souboru pomoci NextFile().
 *
 * Zavre adresar, pokud je otevreny, a vyprazdni buffer potomku - virtualnich
 * podadresaru. Tim zpusobi, ze NextFile() bude pri nasledujicich volanich
 * vracet soubory (a adresare) od zacatku.
 *
 */
void VFS::ResetFiles() {
    if (dir_desc != 0) { // zavreme pripadny otevreny adresar
        closedir(dir_desc);
        dir_desc = 0;
    }
    
    child_buf.erase(child_buf.begin(), child_buf.end()); // a smazeme zalohovane potomky - virtualni adresare
    dir_changed = true; // pokyn pro NextFile(), aby se nacetly informace znovu
}


/** Prevede virtualni cestu na jeji fyzicky ekvivalent.
 *
 * Nekontroluje jakakoliv pristupova prava, ani pristupnost nebo existenci 
 * adresare, pouze mechanicky prevede virtualni cestu na fyzickou.
 *
 * Navratove hodnoty:
 *
 *      -  1      vse OK
 *      - -1      pri prevodu nastala chyba - cesta je syntakticky spatne (obsahuje
 * dve lomitka za sebou, atp.)
 * 
 */
int VFS::ConvertToPhysicalPath(string virtual_path, string &physical_path) {
    int                 ret;
    deque<string>       parts;
    string              dir;
    VFS_node          * node;
    VFS_node          * next_node;

    ret = CutPathIntoParts(virtual_path, parts);
    if (ret < 0) return -1;
    
    dir = parts.front(); parts.pop_front();
    
    if (dir == "/") {// --- ABSOLUTNI cesta ---
        node = root_node; 
        if (parts.empty()) { physical_path = node->PhysicalName(); return 1; }
        
        do { //prejdeme virtualne kam az to jde
           if (!parts.empty()) { dir = parts.front(); parts.pop_front(); }
           else { dir=""; break; }
            
           if (dir == ".") continue; //zadna zmena adresare
           if (dir == "..") { 
               if (node != root_node) node = node->Parent(); //o adresar vys, pokud to jde
               continue;
           }
            
           next_node = node->FindChild(dir);
           if (next_node != 0) node = next_node;
           else break; //koncime, v dir je jmeno pripadneho *nevirtualniho* podadresare
        } while (1);
         
        if (dir == "") { physical_path = node->PhysicalName(); return 1; }
        
        if (node->PhysicalName()!="/") physical_path = node->PhysicalName() + "/" + dir; else physical_path = "/" + dir;
        
        while (!parts.empty()) { //doplnime zbytek cesty - ten uz je fyzicky
            dir = parts.front(); parts.pop_front();
            physical_path = physical_path + "/" + dir;
        }        
        
    } else { // --- RELATIVNI cesta ---
        node = current_node;
        parts.push_front(dir); //vlozime prvni cast cesty zpet na zacatek - hned na zacatku cyklu ji vyzvedneme
        
        if (current_dir == ".") //je-li virtualni prechod mozny
            do { //prejdeme virtualne kam az to jde
                if (!parts.empty()) { dir = parts.front(); parts.pop_front(); }
                else { dir=""; break; }
            
                if (dir == ".") continue; //zadna zmena adresare
                if (dir == "..") { 
                    if (node != root_node) node = node->Parent(); //o adresar vys, pokud to jde
                    continue;
                }
            
                next_node = node->FindChild(dir);
                if (next_node != 0) node = next_node;
                else break; //koncime, v dir je jmeno pripadneho *nevirtualniho* podadresare
            } while (1);
        else parts.pop_front(); //tahle cast uz v dir je, musime ji vyhodit!
        
        if (dir == "") { physical_path = node->PhysicalName(); return 1; }
        
        if (current_dir == ".") { 
            if (node->PhysicalName()!="/") physical_path = node->PhysicalName() + "/" + dir; 
            else physical_path = "/" + dir;
        } else {
            if (current_dir != "/") physical_path = current_dir + "/" + dir;
            else physical_path = "/" + dir;
        }
        
        while (!parts.empty()) { //doplnime zbytek cesty
            dir = parts.front(); parts.pop_front();
            physical_path = physical_path + "/" + dir;
        }         
    }//RELATIVNI cesta
    
    return 1;
}//ConvertToPhysicalPath()


/** Zjisti informace o zadanem souboru.
 *
 * Navratove hodnoty:
 * 
 *  jako ChangeDir() +
 *  
 *      - -7      neni to soubor
 * 
 */
int VFS::GetFileInfo(const char * path, VFS_file &x) {
    int         ret;
    FileInfo    info;
    string      s(path);
    int         n;
    string      old_dir  = current_dir;
    VFS_node  * old_node = current_node;
    string      dir;
    
    //if (!IsFile(path)) {cout << "GFI: prej to neni soubor" << endl; return -7; }//neni to soubor
    
    x.IsVirtual(false);
    n = s.rfind('/');
    if (n == string::npos) { // neobsahuje lomitko --> je to jen jmeno souboru bez cesty
        //cout << "npos ..." << endl;
        dir = ".";
        x.Name(s);
        if (current_dir == ".") 
            x.Path(current_node->PhysicalName()); 
        else { 
            x.Path(current_dir);
        }
    } else if (s[0] != '/') { // je to relativni cesta
        x.Name(s.substr(n+1, s.size()-n));
        string tmp;
        ConvertToPhysicalPath(s.substr(0,n), tmp);
        x.Path(tmp);
        dir = s.substr(0, n);
    } else { // je to absolutni cesta
        string virtualni_cesta = s.substr(0, n);
        dir = s.substr(0, n);
        string fyzicka_cesta;
        ConvertToPhysicalPath(virtualni_cesta, fyzicka_cesta);
        x.Path(fyzicka_cesta);
        x.Name(s.substr(n+1, s.size()-n));
    }

    //cout << "GFI: x.name = " << x.Name() << endl;
    //cout << "GFI: x.path = " << x.Path() << endl;
    //cout << "GFI vdir = " << CurrentDir()<< endl;
    //cout << "GFI pdir = " << CurrentPhysicalDir() << endl;
    
    //zmenime adresar a DirectoryDatabase predame jen jmeno souboru - db. klic zjisti z akt. adresare;
    ret = ChangeDir(dir.c_str());
    if (ret < 0) {
#ifdef DEBUG
        cout << "GFI: nelze zmenit adresar na " << dir << endl;
#endif
        return ret;
    }
    //cout << "GFI vdir = " << CurrentDir()<< endl;
    //cout << "GFI pdir = " << CurrentPhysicalDir() << endl;
    
    
    ret = root_db.GetFileInfo(x.Name(), info); //mame o tomhle souboru informace v databazi ?
    if (ret == 1) { //ano, mame
#ifdef DEBUG
        cout << "GFI: udaj o " << x.Name() << " je z databaze." << endl;
#endif
        x.UserRights(info.user_rights);
        x.OthersRights(info.others_rights);
        x.UserName(info.user_name);
    } else {
#ifdef DEBUG
        cout << "GFI: udaj o " << x.Name() << " si vymyslime." << endl;
#endif
        x.UserRights(R_READ); 
        x.UserName(NO_USER);
        x.OthersRights(R_READ);
    }

    current_dir  = old_dir;
    current_node = old_node; 
    chdir(old_dir.c_str());
    
    return 1;
}



/** Zjisti zda file ukazuje na (pristupny) soubor.
 *
 */
bool VFS::IsFile(const char * file) {
    try {
        
    int         ret;
    string      s;
    string      name;
    string      path;
    struct stat statbuf;
    int         n;
    string      old_dir  = current_dir;
    VFS_node  * old_node = current_node;
    
    if (file == 0) return false;
    
    s = file;
    n    = s.rfind('/');
    if (n == string::npos) {
        name = file;
        path = ".";
    } else if (n != 0) {
        name = s.substr(n+1, s.size() - n);
        path = s.substr(0, n);
    } else {
        name = s.substr(1, s.size() - 1);
        path = "/";
    }
    
    ret = ChangeDir(path.c_str());
    if (ret < 0) return false;
                                //VIRTUALNI SOUBORY - tady kdyztak dodelat
    ret = stat(name.c_str(), &statbuf);

    current_node = old_node;
    current_dir  = old_dir;
    chdir(old_dir.c_str());

    if (ret == -1) return false;

    if (S_ISREG(statbuf.st_mode) && !S_ISDIR(statbuf.st_mode)) return true;
        else return false; 
        
    } catch (...) {return false;}
}




/** Zjisti zda dir ukazuje na adresar.
 *
 * Pokud dir ukazuje na nejaky adresar (cesta k nemu musi vest pres virtualni
 * strom), tak odpovi true, jinak false.
 *
 */
bool VFS::IsDir(const char * dir) {
    try{

    int         ret;
    int         n;
    struct stat statbuf;
    string      s;
    string      name;
    string      path;
    string      old_dir  = current_dir;
    VFS_node  * old_node = current_node;
    VFS_node  * node;

    if (dir == 0) return false;

    s = dir;

    if (s == "/" || s == ".") return true;
    
    if (s[s.size()-1] == '/') s.erase(s.size()-1, 1);
        
    n = s.rfind('/');
    if (n == string::npos) { //neobsahuje lomitko
        path = ".";
        name = s;
    } else if (n != 0) { //obsahuje lomitko a jestli i nazacatku, tak obsahuje aspon dve lomitka
        name = s.substr(n+1, s.size() - n);
        path = s.substr(0, n);
    } else { // je to jen cesta tvaru "/adresar"
        name = s.substr(1, s.size() - 1);    
        path = "/";
    }
    
    ret = ChangeDir(path.c_str());   
    if (ret < 0) {
        return false;
    }
  
    node = current_node->FindChild(name);
    if (node != 0 && node->IsDir()) {
        current_node = old_node;
        current_dir  = old_dir;
        chdir(old_dir.c_str());
        return true; //VIRTUALNI SOUBORY - prekontrolovat pokud je doprogramujeme
    }
    
    ret = stat(name.c_str(), &statbuf);
    
    current_node = old_node;
    current_dir  = old_dir;
    chdir(old_dir.c_str());
    
    if (ret == -1) {
        return false;
    }
    
    if (S_ISDIR(statbuf.st_mode)) return true; else { return false; }
    } catch (...) {return false;}
}


/** Zjisti, zda uzivatel ftp_user_name smi zapisovat do adresare dir.
 *
 */
bool VFS::AllowedToWriteToDir(string dir) {
    int         ret;
    int         n;
    string      user_name = FtpUserName();
    string      _dir  = current_dir;
    VFS_node  * _node = current_node;
    FileInfo    info;
    VFS_file    file("","");
    
    if (dir == "/" && ((user_name == root_node->UserName() && (root_node->UserRights() & R_WRITE)) 
                       || (root_node->OthersRights() & R_WRITE)) 
            ) return true;
    if (dir == "/") return false;
    
    if (current_dir == ".") {
        if (dir == "." && ((user_name == current_node->UserName() && (current_node->UserRights() & R_WRITE)) 
                           || (current_node->OthersRights() & R_WRITE)) 
                ) return true;
        if (dir == ".") return false;
    }

    
    if (dir[dir.size()-1] == '/') dir.erase(dir.size()-1, 1); //ma-li dir na konci lomitko, umazeme ho
    
    n = dir.rfind('/');
    if (n == string::npos) {
        VFS_node * node;
        
        node = current_node->FindChild(dir);
        if (node != 0)
            if (node->PhysicalName() != "" &&
                    ( (user_name == node->UserName() && (node->UserRights() & R_WRITE)) ||
                                (node->OthersRights() & R_WRITE) )
                ) return true; //dir je virt. potomek a lze do nej zapisovat
        if (node != 0) return false; //virtualni potomek dir tam byl, ale nelze do nej zapisovat
        
        // takze to uz muze byt jen fyzicky podadresar aktualniho adresare
        ChangeDir("."); //ujistime se, ze jsme ve spravnem fyzickem adresari (kvuli databazi)
        
       // ret = root_db.GetFileInfo(dir, info); //z databaze zkusime ziskat info o adresari dir
        ret = GetFileInfo(dir.c_str(), file);
        if (ret < 0) return false;//v db. informace nejsou, implicitne se uploadovat soubory na server nesmeji
        
        if ((file.UserName() == user_name && (file.UserRights() & R_WRITE)) || (file.OthersRights() & R_WRITE))
            return true;
        else return false;
    } else { //dir obsahuje lomitko
        string path;
        string name; //jmeno adresare bez cesty k nemu
        string fyzicka_cesta;
        VFS_node * node;

        if (n != 0) {
            path = dir.substr(0, n);
            name = dir.substr(n+1, dir.size()-n);
         //   cout << "allowedToWTD> path = "<< path << " name = " << name << endl;
        } else { //dir je tvaru "/jmeno_adresare"
            path = "/";
            name = dir.substr(1, dir.size()-1);
        }
       // cout << "ATWTD change dir " << path << endl;
        ChangeDir(path.c_str());
       // cout << "ATWTD current_dir = " << current_dir << " cphd = " << CurrentPhysicalDir() << endl;
       // cout << "ATWTD virt = " << CurrentDir() << endl;

        if (current_dir == ".") {
            node = current_node->FindChild(name);
            if (node != 0)
                if (node->PhysicalName() != "" &&
                        ( (user_name == node->UserName() && (node->UserRights() & R_WRITE)) ||
                                    (node->OthersRights() & R_WRITE) )
                    ) {
                    current_dir  = _dir;
                    current_node = _node;
                    chdir(_dir.c_str());
                    return true; //dir je virt. potomek a lze do nej zapisovat
                }

            if (node != 0) {
                current_dir  = _dir;
                current_node = _node;
                chdir(_dir.c_str());             
                return false; //virtualni potomek dir tam byl, ale nelze do nej zapisovat
            }    
        } //if current_dir == "."
      //  cout << "ATWTD current_dir = " << current_dir << " cphd = " << CurrentPhysicalDir() << endl;
      //  cout << "ATWTD virt = " << CurrentDir() << endl;
      //  cout << "ATWTD ziskavame z db info o " << name << endl;
        //ret = root_db.GetFileInfo(name, info); //z databaze zkusime ziskat info o adresari name
        ret = GetFileInfo(name.c_str(), file);
        
        current_dir  = _dir;
        current_node = _node;
        chdir(_dir.c_str());
        if (ret < 0) return false;//v db. informace nejsou, implicitne se uploadovat soubory na server nesmeji
        
        if ((file.UserName() == user_name && (file.UserRights() & R_WRITE)) || (file.OthersRights() & R_WRITE))
            return true;
        else return false;
    
    }//else ... dir obsahuje lomitko

}//AllowedToWriteToDir()



/** Funkce ukladajici informace o souboru file do databaze root_db.
 *
 * Navratove hodnoty:
 *
 * viz. DirectoryDatabase::PutFileInfo()
 *
 *      -  1                    vse OK
 *      -  zaporna hodnota      chyba
 *
 */
int VFS::PutFileInfo(VFS_file file) {
    int         ret;
    FileInfo    info;

    info.user_rights   = file.UserRights();
    info.user_name     = file.UserName();
    info.others_rights = file.OthersRights();
    
    string tmp;
    if (file.Path() != "/") tmp = file.Path() + "/" + file.Name(); else tmp = "/" + file.Name();
    info.name          = tmp;
        
    ret = root_db.PutFileInfo(info);
    return ret;
}


/** Funkce maze informace o souboru file z databaze root_db;
 *
 * Navratove hodntoty:
 *
 * viz. DirectoryDatabase::DeleteFileInfo()
 *
 *      -  1                    vse OK
 *      -  zaporna hodnota      chyba
 *
 */
int VFS::DeleteFileInfo(VFS_file file) {
    int         ret;
    
    string tmp;
    if (file.Path() != "/") tmp = file.Path() + "/" + file.Name(); else tmp = "/" + file.Name();
    
    ret = root_db.DeleteFileInfo(tmp);
    return ret;
}


