#!/bin/sh

mkdir $PWD/+ftp
mkdir $PWD/+upload
mkdir $PWD/+blackbox
mkdir $PWD/+filmy
mkdir $PWD/+filmy/sci-fi
mkdir $PWD/+filmy/nippon
mkdir $PWD/+mp3
mkdir $PWD/+mp3/english
mkdir $PWD/+mp3/czech

touch $PWD"/+filmy/sci-fi/Bladerunner.mpeg"
touch $PWD"/+filmy/sci-fi/Space Odyssey.avi"
touch $PWD"/+mp3/english/Californication.mp3"
touch $PWD"/+mp3/english/Faith No More - I started a joke.mp3"
touch $PWD"/+mp3/czech/hrobar.mp3"
touch $PWD"/+mp3/czech/Lenka Filipova - venovani.mp3"


if [ ! -e "vfs.cfg" ]; then
cat example/vfs_cfg_template | sed "s%HOME%$PWD%" > vfs.cfg
fi

if [ ! -e "deny_list.cfg" ]; then
cp example/deny_list.cfg .
fi

if [ ! -e "account.cfg" ]; then
cp example/account.cfg .
fi

if [ ! -e "root.pem" ]; then
cp example/root.pem .
fi

if [ ! -e "dh1024.pem" ]; then
cp example/dh1024.pem .
fi

if [ ! -e "server.pem" ]; then
cp example/server.pem .
fi

