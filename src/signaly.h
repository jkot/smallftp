/** @file signaly.h 
 *  \brief Deklarace signalu
 *
 */

//#define DEBUG
#define ERR(num,msg) if (num<0) cout << "Nastala chyba: " << #msg << "(" << num << ")" << endl;


extern string account_file;
extern string ip_deny_list_file;
extern string pid_file;
extern bool reload_config_file;
extern bool daemonize;
extern bool use_tls;
extern bool parent;
extern bool run;
extern bool finish;
extern bool ftp_abort;
extern int client_socket;
extern bool assume_abor;
extern bool secure_cc;

void InitSignalHandlers();

