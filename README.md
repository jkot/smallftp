smallftp
========

smallftp is a basic FTP server (implementing RFC959, and partly RFC2228) that I created over a long weekend in 2003 as a school project at Charles University in Prague.

I decided to put it on GitHub so that I can play with it and polish it a bit when I feel like it. Currently, comments and docs are in Czech only :-/

 * *docs/* - doxygen-generated API documentation, a user manual 
 * *src/* - source code
 * *example/* - a script to generate an example configuration



soubory vznikle po make install a ./smallFTPd:

vfs.cfg    	... konfiguracni soubor virtualniho filesystemu - nasdilene adresare
deny_list.cfg	... seznam zakazanych IP adres
account.cfg	... uzivatelska jmena a hesla
vfsdb		... soubor databaze gdbm s informacemi o souborech - jejich vlastnicich a pravech k nim


za behu serveru se objevi soubor:

smallFTPd.PID	... obsahuje PID rodicovskeho procesu serveru

prikaz
smallFTPd -h	... vypise kratky popis prepinacu



Mozne problemy a upozorneni:
---------------------------

 * U prepinacu urcujicich jmena kofiguracnich souboru se zadavaji 
   jen jmena bez cesty.

 * Fyzicke adresare uvedene ve vfs.cfg musi existovat, byt pristupne 
   a musi byt zadany absolutni cestou.
   
 * Pokud nejde nejaky soubor smazat / prepsat nebo zkopirovat, je to 
   pravdepodobne zpusobeno tim, ze uzivatel k tomu nema dostatecna 
   prava.
   
 * Spolehlive se prelozi a funguje na RedHatu 8.0 Psyche, s gcc verze 3.2, 
   gdbm verze 1.8, a nutne je openssl verze >= 0.9.6 ( hlavne z
   bezpecnostnich duvodu, toto omezeni lze odstranit v hlavickovem souboru
   security.h ).
   
 * Objevuji se problemy pri prekladu programu na RedHatu 7.3 s prekladacem
   gcc verze 2.96. Jsou uz z vetsi casti vyreseny, muze se objevit problem 
   s uploadem souboru v pasivnim modu. 

 * Bezpecne funguje pouziti TLS/SSL pro sifrovani prikazu (control connection),
   pri pouziti sifrovani data connection se v nekterych pripadech mohou
   objevit problemy.
 
 
 
