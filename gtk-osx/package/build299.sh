#!/bin/sh

# set -e

if [ -z "${JHBUILD_LIBDIR}" ]
then
  echo "JHBUILD_LIBDIR undefined. Are you running inside jhbuild?"
  exit 2
fi

printf "Determining GIMP version: "

$(cd ${HOME}/gtk/inst/bin/ && ln -sf gimp-2.99 gimp)

GIMP_VERSION="$(gimp-2.99 --version | sed 's|GNU Image Manipulation Program version ||')"

echo "$GIMP_VERSION"

cat info-2.99.plist.tmpl | sed "s|%VERSION%|${GIMP_VERSION}|g" > info-2.99.plist

echo "Copying charset.alias"
cp "/usr/lib/charset.alias" "${HOME}/gtk/inst/lib/"
echo "Creating bundle"
gtk-mac-bundler gimp-2.99.bundle

BASEDIR=$(dirname "$0")

#  target directory
PACKAGE_DIR="${HOME}/gimp299-osx-app"

echo "Move icons to right directory"

pushd ${PACKAGE_DIR}/GIMP-2.99.app/Contents/Resources/share/icons
 cp -r Adwaita ../gimp/2.99/icons
 cp -r hicolor ../gimp/2.99/icons
popd

echo "Removing pathnames from the libraries and binaries"
# fix permission for some libs
find  ${PACKAGE_DIR}/GIMP-2.99.app/Contents/Resources/lib -name '*.dylib' -type f | xargs chmod 755
# getting list of the files to fix
FILES=$(
  find ${PACKAGE_DIR}/GIMP-2.99.app -perm +111 -type f \
   | xargs file \
   | grep ' Mach-O '|awk -F ':' '{print $1}'
)
OLDPATH="${JHBUILD_LIBDIR}/"

for file in $FILES
do
  install_name_tool -id "@rpath/$(basename $file)" $file
  otool -L $file \
   | grep "\t$OLDPATH" \
   | sed "s|${OLDPATH}||" \
   | awk -v fname="$file" -v old_path="$OLDPATH" '{print "install_name_tool -change "old_path $1" @rpath/"$1" "fname}' \
   | bash
done

if [[ "$1" == "debug" ]]; then
  echo "Generating debug symbols"
  find  ${PACKAGE_DIR}/GIMP-2.99.app/ -type f -perm +111 \
     | xargs file \
     | grep ' Mach-O '|awk -F ':' '{print $1}' \
     | xargs -n1 dsymutil
fi

echo "remove @rpath to the libraries"
find  ${PACKAGE_DIR}/GIMP-2.99.app/Contents/Resources/lib/ -mindepth 1 -maxdepth 1 -perm +111 -type f \
   | xargs file \
   | grep ' Mach-O '|awk -F ':' '{print $1}' \
   | xargs -n1 install_name_tool -delete_rpath ${HOME}/gtk/inst/lib

echo "adding @rpath to the binaries"
find  ${PACKAGE_DIR}/GIMP-2.99.app/Contents/MacOS/ -type f -perm +111 \
   | xargs file \
   | grep ' Mach-O '|awk -F ':' '{print $1}' \
   | xargs -n1 install_name_tool -add_rpath @executable_path/../Resources/lib/

echo "adding @rpath to the plugins"
find  ${PACKAGE_DIR}/GIMP-2.99.app/Contents/Resources/lib/gimp/2.99/plug-ins/ -perm +111 -type f \
   | xargs file \
   | grep ' Mach-O '|awk -F ':' '{print $1}' \
   | xargs -n1 install_name_tool -add_rpath @executable_path/../../../../

echo "fixing pixmap cache"
sed -i.old 's|@executable_path/../Resources/lib/||' \
    ${PACKAGE_DIR}/GIMP-2.99.app/Contents/Resources/lib/gdk-pixbuf-2.0/2.10.0/loaders.cache

echo "fixing IMM cache"
sed -i.old 's|@executable_path/../Resources/lib/||' \
    ${PACKAGE_DIR}/GIMP-2.99.app/Contents/Resources/etc/gtk-3.0/gtk.immodules

echo "create missing links. should we use wrappers instead?"

pushd ${PACKAGE_DIR}/GIMP-2.99.app/Contents/MacOS
 ln -s gimp-console-2.99 gimp-console
 ln -s gimp-debug-tool-2.99 gimp-debug-tool
 ln -s python python3.9
popd

echo "copy xdg-email wrapper to the package"
mkdir -p ${PACKAGE_DIR}/GIMP-2.99.app/Contents/MacOS
cp xdg-email ${PACKAGE_DIR}/GIMP-2.99.app/Contents/MacOS

echo "copy pygimp.interp to the package"
mkdir -p ${PACKAGE_DIR}/GIMP-2.99.app/Contents/Resources/lib/gimp/2.99/interpreters
cp pygimp.interp ${PACKAGE_DIR}/GIMP-2.99.app/Contents/Resources/lib/gimp/2.99/interpreters

echo "Creating pyc files"
python3.9 -m compileall -q ${PACKAGE_DIR}/GIMP-2.99.app

echo "Signing libs"

if [ -n "${codesign_subject}" ]
then
  echo "Signing libraries and plugins"
  find  ${PACKAGE_DIR}/GIMP-2.99.app/Contents/Resources/lib/ -type f -perm +111 \
     | xargs file \
     | grep ' Mach-O '|awk -F ':' '{print $1}' \
     | xargs /usr/bin/codesign -s "${codesign_subject}" \
         --options runtime \
         --entitlements ${HOME}/project/package/gimp-hardening.entitlements
  echo "Signing app"
  /usr/bin/codesign -s "${codesign_subject}" \
    --timestamp \
    --deep \
    --options runtime \
    --entitlements ${HOME}/project/package/gimp-hardening.entitlements \
    ${PACKAGE_DIR}/GIMP-2.99.app
fi

echo "Building DMG"
if [ -z "${CIRCLECI}" ]
then
  DMGNAME="gimp-${GIMP_VERSION}-x86_64.dmg"
else
  DMGNAME="gimp-${GIMP_VERSION}-x86_64-b${CIRCLE_BUILD_NUM}-${CIRCLE_BRANCH////-}.dmg"
fi

mkdir -p /tmp/artifacts/
rm -f /tmp/tmp.dmg
rm -f "/tmp/artifacts/gimp-${GIMP_VERSION}-x86_64.dmg"

cd create-dmg

./create-dmg \
  --volname "GIMP 2.99 Install" \
  --background "../gimp-dmg.png" \
  --window-pos 1 1 \
  --icon "GIMP-2.99.app" 190 360 \
  --window-size 640 480 \
  --icon-size 110 \
  --icon "Applications" 110 110 \
  --hide-extension "Applications" \
  --app-drop-link 450 360 \
  --format UDBZ \
  --hdiutil-verbose \
  "/tmp/artifacts/${DMGNAME}" \
  "$PACKAGE_DIR/"
rm -f /tmp/artifacts/rw.*.dmg
cd ..

if [ -n "${codesign_subject}" ]
then
  echo "Signing DMG"
  /usr/bin/codesign  -s "${codesign_subject}" "/tmp/artifacts/${DMGNAME}"
fi

echo "Done"
