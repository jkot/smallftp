#
#	FTP server smallFTPd
#

all: smallFTPd




src/VFS.o: src/VFS.cpp src/VFS.h src/my_exceptions.h src/VFS_pomocne.cpp src/VFS_file.h src/VFS_file.cpp
	g++ -o src/VFS.o -c src/VFS.cpp -Isrc



src/VFS_file.o: src/VFS_file.cpp src/VFS_file.h
	g++ -o src/VFS_file.o -c src/VFS_file.cpp -Isrc




src/DirectoryDatabase.o: src/DirectoryDatabase.cpp src/DirectoryDatabase.h src/my_exceptions.h
	g++ -o src/DirectoryDatabase.o -c src/DirectoryDatabase.cpp -Isrc



src/pomocne.o: src/pomocne.cpp src/pomocne.h src/smallFTPd.h
	g++ -o src/pomocne.o -c src/pomocne.cpp -Isrc



src/signaly.o: src/signaly.h src/signaly.cpp
	g++ -o src/signaly.o -c src/signaly.cpp -Isrc



src/ftpcommands.o: src/ftpcommands.cpp src/ftpcommands.h src/pomocne.h
	g++ -o src/ftpcommands.o -c src/ftpcommands.cpp -Isrc



src/network.o: src/network.h src/network.cpp
	g++ -o src/network.o -c src/network.cpp


src/security.o: src/security.h src/security.cpp
	g++ -o src/security.o -c src/security.cpp


src/smallFTPd.o: src/smallFTPd.cpp src/smallFTPd.h src/pomocne.h src/VFS.h src/signaly.h src/ftpcommands.cpp
	g++ -o src/smallFTPd.o -c src/smallFTPd.cpp -Isrc



smallFTPd: src/VFS.o src/VFS_file.o src/DirectoryDatabase.o src/pomocne.o src/signaly.o src/smallFTPd.o \
	   src/ftpcommands.o src/network.o src/security.o
	g++ -o smallFTPd src/VFS.o src/VFS_file.o src/pomocne.o src/DirectoryDatabase.o src/smallFTPd.o \
	                 src/signaly.o src/ftpcommands.o src/network.o src/security.o -lgdbm -lssl -lcrypto -lstdc++
			 




clean:
	rm src/VFS.o
	rm src/VFS_file.o
	rm src/DirectoryDatabase.o
	rm src/pomocne.o
	rm src/smallFTPd.o
	rm src/ftpcommands.o
	rm src/network.o
	rm src/signaly.o
	rm src/security.o


install:
	echo Vytvarim ukazkovy priklad pro okamzite vyzkouseni smallFTPd
	example/install.sh

