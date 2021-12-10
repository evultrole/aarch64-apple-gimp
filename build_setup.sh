#REQUIREMENTS
#ensure that rust is installed
#the run python3 at least once so that the command line tools install

curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh
python3 --version

#######################################################################################
# Variables

GIMP_MACOS_BUILD_DIR=~/SkunkWorks/gimp-macos-build
GTK_OSX_DIR=~/SkunkWorks/gtk-osx
MACOS_SDK_VERSION=11.1

# Set up ~/.profile
# Remember to 'source ~/.profile' whenever you want to use jhbuild
echo 'export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX${MACOS_SDK_VERSION}.sdk' > ~/.profile
echo 'export MACOSX_DEPLOYMENT_TARGET=${MACOS_SDK_VERSION}' >> ~/.profile
echo 'export PATH="$HOME/.new_local/share/pyenv/versions/3.9.2/bin:$HOME/.cargo/bin:$HOME/.local/bin:$PATH:$HOME/.new_local/bin:$HOME/Library/Python/3.8/bin"'>> ~/.profile

#for apple silicone use the following
echo 'export ARCHFLAGS="-arch arm64"' >> ~/.profile
#for older intel chips use this
echo 'export ARCHFLAGS="-arch x86_64"' >> ~/.profile

#Setup python links in local profile to allow expected behavior of python = python3 for most programs
source ~/.profile 

#setup python environment for the build
./gtk-osx-setup.sh

#check for success of python install, it errors for no apparent reason about 20% of the time
#run again with pip environment flag, sets up pip modules for the newly installed pythong

rm -rf .new_local/bin
PIPENV=1 ./gtk-osx-setup.sh

python3 -m pip install --upgrade pip wheel pipenv setuptools meson

ln -s aarch64-apple-gimp/Skunkworks Skunkworks

#for apple silicone
cp $GIMP_MACOS_BUILD_DIR/jhbuildrc-arm64 ~/.config/jhbuildrc-custom
#for intel
cp $GIMP_MACOS_BUILD_DIR/jhbuildrc-x86_64 ~/.config/jhbuildrc-custom
cp $GIMP_MACOS_BUILD_DIR/jhbuildrc ~/.config/jhbuildrc

# Bootstrap the build environment and core tool modules
$HOME/.new_local/bin/jhbuild bootstrap-gtk-osx-gimp

# Install bundler app
python3 -m pip install --upgrade pip wheel pipenv setuptools meson pygments

cd ~/Source
git clone https://github.com/samm-git/gtk-mac-bundler -b fix-otool
cd gtk-mac-bundler
make install

# Install base/bootstrap modules - this includes gtk+
source ~/.profile && jhbuild build python icu55 meta-gtk-osx-freetype meta-gtk-osx-bootstrap meta-gtk-osx-core

# Configure jhbuild to use local moduleset file, allowing us to experiment without pushing changes to
# the samm-git repo first.  Instead, we can modify the files in ~/.cache/jhbuild/ from this point.
echo "use_local_modulesets = True" >> ~/.config/jhbuildrc-custom
echo "modulesets_dir = '~/.cache/jhbuild/'" >> ~/.config/jhbuildrc-custom

jhbuild build suitesparse lcms libunistring gmp libnettle libtasn1 gnutls libjpeg readline glib-networking openjpeg gtk-mac-integration poppler poppler-data json-glib p2tc exiv2 gexiv2 ilmbase openexr libwebp libcroco librsvg-24 json-c  libmypaint mypaint-brushes libde265 nasm x265 libheif aalib shared-mime-info iso-codes libwmf libmng ghostscript pycairo pygobject pygtk gtk-mac-integration-python

# Build gimp dependencies in (roughly correct) order, minus babl/gegl and WebKit
# Ideally this should be a short one-liner that uses jhbuild's dependency graph 
# analysis to load modules in the correct modules.
jhbuild build suitesparse lcms libunistring gmp libnettle libtasn1 gnutls libjpeg readline glib-networking openjpeg gtk-mac-integration poppler poppler-data
jhbuild build json-glib p2tc exiv2 gexiv2 ilmbase openexr libwebp libcroco librsvg-24 json-c
jhbuild build libmypaint mypaint-brushes libde265 nasm x265 libheif aalib shared-mime-info iso-codes libwmf libmng ghostscript
jhbuild build pycairo pygobject pygtk gtk-mac-integration-python

# Build webkit dependencies - again, should be a simple one-liner
jhbuild build enchant libpsl sqlite vala
jhbuild buildone libsoup

# Build webkit (there's plenty of time for tea, biscuits, deep thoughts, more tea...)
jhbuild buildone webkit

# Build and test babl and gegl
jhbuild build --check babl gegl
jhbuild build openexr ilmbase --force

# Build gimp
jhbuild build gimp
