#!/bin/bash
curl https://sh.rustup.rs -sSf | sh -s -- -y
echo "installing xcode command line tools if not already present"
xcode-select --install
echo "please press enter when ready"
read pressenter
RED='\033[0;31m'
NC='\033[0m' # No Color
GIMPCURDIR=`pwd`
cd ~
ln -s "$GIMPCURDIR" ~/Skunkworks 
rm -rf gtk
rm -rf .new_local
rm -rf Source/jhbuild
rm .gimpprofile
GIMP_MACOS_BUILD_DIR=~/SkunkWorks/gimp-macos-build
GTK_OSX_DIR=~/SkunkWorks/gtk-osx
MACOS_SDK_VERSION=11.3
echo 'export MACOS_SDK_VERSION=11.3' >~/.gimpprofile
echo 'export MACOSX_DEPLOYMENT_TARGET=11.3' >>~/.gimpprofile
echo 'export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX${MACOS_SDK_VERSION}.sdk' >> ~/.gimpprofile
echo 'export MACOSX_DEPLOYMENT_TARGET=11.0' >> ~/.gimpprofile
echo 'export PATH="$HOME/.new_local/share/pyenv/versions/3.10.1/bin:$HOME/.cargo/bin:$HOME/.local/bin:$PATH:$HOME/.new_local/bin"'>> ~/.gimpprofile
echo 'export ARCHFLAGS="-arch arm64"' >> ~/.gimpprofile
cat ~/.appledevprofile >> ~/.gimpprofile
#echo Please paste your apple codesign subject here
#read codesignsub
#echo 'export codesign_subject='$codesignsub >> ~/.gimpprofile
#echo Please paste your apple login here in format name@example.com
#read applelogin
#echo 'export notarization_login='$applelogin >>~/.gimpprofile
#echo Please paste your apple notarized password here, in format xxxx-xxxx-xxxx-xxxx
#read notapass
#echo 'export notarization_password='$notapass >>~/.gimpprofile
#userinput=""
#echo -e "\n\n\n\n\n\n"
#echo -e "*****************************************************************************************************************"
#echo -e ${RED}"Dumping .gimpprofle file here, please check it for accuracy."
#echo -e ${RED}"If information was entered incorrectly, Press ESC key to quit and run script again to correct, or correct the issues manually before continuing"
#echo -e ${RED}"Press Return to continue if everything looks good" ${NC}
#cat ~/.gimpprofile
## read a single character
#while read -r -n1 key
#do
## if input == ESC key
#if [[ $key == $'\e' ]];
#then
#exit 0;
#else
#break;
#fi
# Add the key to the variable which is pressed by the user.
#done
source ~/.gimpprofile 
cd ~/
curl -L 'https://www.python.org/ftp/python/3.10.1/python-3.10.1-macos11.pkg' > python-3.10.1-macos11.pkg
sudo installer -pkg python-3.10.1-macos11.pkg -target /
open /Applications/Python\ 3.10/Install\ Certificates.command
mkdir -p ~/.local/bin
cd .local/bin
rm python
ln -s /Library/Frameworks/Python.framework/Versions/3.10/bin/python3 python
echo 'export PYENV_VERSION="3.10.1"' >> ~/.gimpprofile
source ~/.gimpprofile 


cd ~/Skunkworks/gtk-osx
PIPENV=1 ./gtk-osx-setup.sh
source ~/.gimpprofile
cd ~/
cp Skunkworks/gtk-osx/jhbuildrc-arm64 .config/jhbuildrc-custom
cp Skunkworks/gtk-osx/jhbuildrc-default .config/jhbuildrc
jhbuild run python3 -m pip install meson gnureadline pygments
jhbuild run python3 -m pip install --upgrade wheel setuptools meson pygments
cd ~/Source
rm -rf gtk-mac-bundler
git clone https://github.com/samm-git/gtk-mac-bundler -b fix-otool
cd gtk-mac-bundler
make install
cd ~/
echo -e ${RED}"That's it for basic setup. The rest should run pretty much hands free, so hit enter, get a cup of coffee, and come back in 30 minutes or so"
echo -e ${RED}"Hit enter and come back in a bit"${NC}
read pressenter
$HOME/.new_local/bin/jhbuild bootstrap-gtk-osx-gimp
source ~/.gimpprofile && jhbuild build icu55 meta-gtk-osx-freetype meta-gtk-osx-bootstrap meta-gtk-osx-core meta-gtk-osx-gtk3 
source ~/.gimpprofile && jhbuild build suitesparse lcms libunistring gmp libnettle libtasn1 gnutls libjpeg readline python2 glib-networking openjpeg  gtk-mac-integration-gtk2 poppler poppler-data python3
jhbuild run python3 -m pip install meson gnureadline pygments
jhbuild run python3 -m pip install --upgrade wheel setuptools meson pygments
source ~/.gimpprofile && jhbuild build json-glib p2tc exiv2 gexiv2 ilmbase openexr libwebp libcroco librsvg-24 json-c
source ~/.gimpprofile && jhbuild build libmypaint mypaint-brushes libde265 nasm x265 libheif aalib shared-mime-info iso-codes libwmf libmng ghostscript
source ~/.gimpprofile && jhbuild build pycairo pycairo-1.18 pygobject pygtk gtk-mac-integration-gtk2-python
source ~/.gimpprofile && jhbuild build enchant libpsl sqlite vala gnutls
source ~/.gimpprofile && jhbuild buildone libsoup
source ~/.gimpprofile && jhbuild buildone webkit
source ~/.gimpprofile && jhbuild build --check babl gegl
source ~/.gimpprofile && jhbuild build gimp
source ~/.gimpprofile && ALL_LINGUAS=en jhbuild build gimp-help-git
cd ~/Skunkworks/gtk-osx/package
source ~/.gimpprofile && jhbuild run ./build.sh
#uncomment this following line if you want to have apple verify your build DMG file for distribution.
#source ~/.gimpprofile && jhbuild run ./notarize.sh



