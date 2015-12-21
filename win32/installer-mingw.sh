#!/bin/bash
# License: CC0
#
# A version of the installer.bat for mingw build
# Similarily basily copy all dependent files from the host system
#  to be available for the NSIS stage
#
# Remember to have installed the generated mingw-viking package first so the
#  binaries are available from the default location
#  (e.g. as root rpm -i mingw32-viking-1.7-1.noarch.rpm)
#
# 'MINGW' and 'DESTINATION' values can be defined to override inbuilt defaults
#

if [ -z "$DESTINATION" ]; then
	DESTINATION=installer/bin
fi
mkdir -p $DESTINATION

if [ -z "$MINGW" ]; then
  MINGW=/usr/i686-w64-mingw32/sys-root/mingw
fi
MINGW_BIN=$MINGW/bin
echo MINGW=$MINGW

echo Make language copies
for x in $(ls ../po/*.gmo); do
	mkdir -p $DESTINATION/locale/$(basename -s .gmo $x)/LC_MESSAGES
	cp $MINGW/share/locale/$(basename -s .gmo $x)/LC_MESSAGES/viking.mo $DESTINATION/locale/$(basename -s .gmo $x)/LC_MESSAGES/
done

echo Copying Viking
cp $MINGW_BIN/*viking.exe $DESTINATION/viking.exe
cp ../COPYING $DESTINATION/COPYING_GPL.txt
cp ../AUTHORS $DESTINATION/AUTHORS.txt
cp ../NEWS $DESTINATION/NEWS.txt
cp ../README $DESTINATION/README.txt
# PDF generation if required
if [ ! -e ../help/C/viking.pdf ]; then
	pushd ../help/C
	dblatex viking.xml
	if [ $? != 0 ]; then
		echo "Help PDF generation failed."
		exit
	fi
fi
cp ../help/C/viking.pdf $DESTINATION
cp ../tools/viking-cache.py $DESTINATION
cp installer/translations/*nsh $DESTINATION
cp installer/pixmaps/viking_icon.ico $DESTINATION

echo Copying Extension Configuration Data
mkdir $DESTINATION/data
cp ../data/*.xml $DESTINATION/data
cp ../data/latlontz.txt $DESTINATION/data

echo Copying Helper Apps
# Needed when spawning other programs (e.g. when invoking GPSBabel)
cp $MINGW_BIN/gspawn-win32-helper.exe $DESTINATION

echo Copying Libraries
# Core libs
cp $MINGW_BIN/libatk*.dll $DESTINATION
cp $MINGW_BIN/libcairo*.dll $DESTINATION
cp $MINGW_BIN/libgcc*.dll $DESTINATION
cp $MINGW_BIN/libgcrypt*.dll $DESTINATION
cp $MINGW_BIN/libgdk*.dll $DESTINATION
cp $MINGW_BIN/libgettext*.dll $DESTINATION
cp $MINGW_BIN/libgio*.dll $DESTINATION
cp $MINGW_BIN/libglib*.dll $DESTINATION
cp $MINGW_BIN/libgmodule*.dll $DESTINATION
cp $MINGW_BIN/libgnurx*.dll $DESTINATION
cp $MINGW_BIN/libgobject*.dll $DESTINATION
cp $MINGW_BIN/libgpg*.dll $DESTINATION
cp $MINGW_BIN/libgtk*.dll $DESTINATION
cp $MINGW_BIN/libintl*.dll $DESTINATION
cp $MINGW_BIN/libffi*.dll $DESTINATION
cp $MINGW_BIN/libfontconfig*.dll $DESTINATION
cp $MINGW_BIN/libfreetype*.dll $DESTINATION
cp $MINGW_BIN/libharfbuzz*.dll $DESTINATION
cp $MINGW_BIN/libjasper*.dll $DESTINATION
cp $MINGW_BIN/libjpeg*.dll $DESTINATION
cp $MINGW_BIN/liblzma*.dll $DESTINATION
cp $MINGW_BIN/libpng*.dll $DESTINATION
cp $MINGW_BIN/libpango*.dll $DESTINATION
cp $MINGW_BIN/libpixman*.dll $DESTINATION
cp $MINGW_BIN/libtiff*.dll $DESTINATION
cp $MINGW_BIN/libxml2*.dll $DESTINATION
cp $MINGW_BIN/zlib1.dll $DESTINATION
cp $MINGW_BIN/libzip*.dll $DESTINATION

# Extras
cp $MINGW_BIN/libexpat*.dll $DESTINATION
# Curl 7.17+ has quite a few dependencies for SSL support
cp $MINGW_BIN/libcurl*.dll $DESTINATION
cp $MINGW_BIN/libssh*.dll $DESTINATION
cp $MINGW_BIN/libidn*.dll $DESTINATION
cp $MINGW_BIN/libnspr*.dll $DESTINATION
cp $MINGW_BIN/libplc*.dll $DESTINATION
cp $MINGW_BIN/libplds*.dll $DESTINATION
cp $MINGW_BIN/nss*.dll $DESTINATION
cp $MINGW_BIN/ssl*.dll $DESTINATION
cp /usr/share/doc/packages/mingw32-libcurl-devel/COPYING $DESTINATION/COPYING_curl.txt

cp $MINGW_BIN/libexif*.dll $DESTINATION
cp $MINGW_BIN/libbz*.dll $DESTINATION
cp $MINGW_BIN/libmagic*.dll $DESTINATION
cp $MINGW/share/misc/magic* $DESTINATION
cp $MINGW_BIN/libsqlite3*.dll $DESTINATION

echo Copying GPSBabel Installer
mkdir $DESTINATION/Optional
# ATM GPSBabel needs to be in 'cache' dir
cp cache/GPSBabel-1.5.2-Setup.exe $DESTINATION/Optional

pushd installer
if [ -z "$DEBUG" ]; then
	makensis -X"SetCompressor lzma" viking-installer.nsi
else
	# Speedier install generation when testing
	makensis -X"SetCompress off" viking-installer.nsi
fi
popd