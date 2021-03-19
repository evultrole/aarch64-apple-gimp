#!/usr/bin/env bash
 ####################################################################
 # gtk-osx-setup.sh: gtk-osx setup with python virtual environments.#
 #                                                                  #
 # Copyright 2018 John Ralls <jralls@ceridwen.us>                   #
 #                                                                  #
 # This program is free software; you can redistribute it and/or    #
 # modify it under the terms of the GNU General Public License as   #
 # published by the Free Software Foundation; either version 2 of   #
 # the License, or (at your option) any later version.              #
 #                                                                  #
 # This program is distributed in the hope that it will be useful,  #
 # but WITHOUT ANY WARRANTY; without even the implied warranty of   #
 # MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the    #
 # GNU General Public License for more details.                     #
 #                                                                  #
 # You should have received a copy of the GNU General Public License#
 # along with this program; if not, contact:                        #
 #                                                                  #
 # Free Software Foundation           Voice:  +1-617-542-5942       #
 # 51 Franklin Street, Fifth Floor    Fax:    +1-617-542-2652       #
 # Boston, MA  02110-1301,  USA       gnu@gnu.org                   #
 ####################################################################

# Function envvardir. Tests environment variable, sets default value if unset,
# and creates the specified directory if it doesn't already exist. This
# will barf if the name indicates a file that isn't a directory.
envvar ()
{
    local _varname=$1
    eval local _var=\$$_varname
    if test -z "$_var"; then
        eval export $_varname="$2"
        _var=$2
    fi
    if test ! -d "$_var"; then
        mkdir -p "$_var"
    fi
}

pip_remove()
{
    local _package=$1
    local _local_package=`$PIP show $_package | grep Location | grep $PYTHONUSERBASE`
    if test -n "$_local_package"; then
        echo $_local_package
        $PIP uninstall -y $_package
    fi
}

# Environment variable defaults:
#
envvar DEVROOT "$HOME"
envvar DEVPREFIX "$DEVROOT/.new_local"
envvar PYTHONUSERBASE "$DEVROOT/.new_local"
envvar DEV_SRC_ROOT "$DEVROOT/Source"
envvar PYENV_INSTALL_ROOT "$DEV_SRC_ROOT/pyenv"
envvar PYENV_ROOT "$DEVPREFIX/share/pyenv"
envvar PIP_CONFIG_DIR "$HOME/.config/pip"

export PYTHONWARNINGS=ignore:DEPRECATION::pip._internal.cli.base_command
export PYTHONPATH="$PYTHONUSERBASE/lib/python/site-packages":"$PYTHONPATH"

if test ! -d "$DEVPREFIX/bin" ; then
    mkdir -p "$DEVPREFIX/bin"
fi

GITLAB="https://gitlab.gnome.org/GNOME"
GITHUB="https://github.com"

# We need to have a local copy of bash when compiling to prevent SIP problems.
if test ! -x "$DEVPREFIX/bin/bash"; then
    cp /bin/bash "$DEVPREFIX/bin"
fi

# Setup pyenv
if test ! -x "$PYENV_INSTALL_ROOT/libexec/pyenv"; then
  if test -d "$PYENV_INSTALL_ROOT"; then
     rm -rf "$PYENV_INSTALL_ROOT";
  fi
    git clone $GITHUB/pyenv/pyenv.git "$PYENV_INSTALL_ROOT"
fi

if test ! -x "$DEVPREFIX/bin/pyenv" ; then
    ln -s "$PYENV_INSTALL_ROOT/bin/pyenv" "$DEVPREFIX/bin"
fi

# Setup PIP; note that we're assuming that python is the system python
# at this point. Having set $PYTHONUSERBASE, pip will be installed in
# $PYTHONUSERBASE/bin and the requisite modules will go in
# $PYTHONUSERBASE/lib/python/site-packages.

if test ! -f "`eval echo $PIP_CONFIG_FILE`" ; then
    export PIP_CONFIG_FILE="$PIP_CONFIG_DIR/pip.conf"
    mkdir -p "$PIP_CONFIG_DIR"
fi
PIP=`python3 -m pip`
if test ! -x "`eval echo $PIP`" ; then
    mv=`python --version 2>&1 | cut -b 12-13`
    if test $mv -lt 11 ; then
        curl https://bootstrap.pypa.io/pip/3.4/get-pip.py -o "$DEVPREFIX/get-pip.py"
        python "$DEVPREFIX/get-pip.py" --user
        rm "$DEVPREFIX/get-pip.py"
    else
        python -m ensurepip --user
    fi
    PIP="$PYTHONUSERBASE/bin/pip"
    $PIP install --upgrade --user pip
fi

# Install pipenv
$PIP install --upgrade --user pipenv
pip_remove typing
PIPENV="$PYTHONUSERBASE/bin/pipenv"

# Install jhbuild
if test ! -d "$DEV_SRC_ROOT/jhbuild/.git" ; then
    git clone -b "3.35.1" $GITLAB/jhbuild.git "$DEV_SRC_ROOT/jhbuild"
    cd "$DEV_SRC_ROOT/jhbuild"
else #Get the latest if it's already installed
    cd "$DEV_SRC_ROOT/jhbuild"
    git pull origin
fi

# Install Ninja
NINJA=`which ninja`
if test ! -x "$NINJA" -a ! -x "$DEVPREFIX/bin/ninja"; then
    curl -kLs https://github.com/ninja-build/ninja/releases/download/v1.10.2/ninja-mac.zip -o "$DEVPREFIX/ninja-mac.zip"
    unzip -d "$DEVPREFIX/bin" "$DEVPREFIX/ninja-mac.zip"
    rm "$DEVPREFIX/ninja-mac.zip"
fi

if test ! -d "$DEVPREFIX/etc" ; then
    mkdir -p "$DEVPREFIX/etc"
fi
if test ! -f "$DEVPREFIX/etc/Pipfile" ; then
    cat  <<EOF > "$DEVPREFIX/etc/Pipfile"
[[source]]
url = "https://pypi.python.org/simple"
verify_ssl = true

[packages]
six = "*"
meson = {version="==0.51.1"}

[scripts]
jhbuild = "$DEVPREFIX/libexec/run_jhbuild.py"

[requires]
python_version = "3.8"
EOF
    cat <<EOF > "$DEVPREFIX/etc/pipenv-env"
export PYTHONUSERBASE="$PYTHONUSERBASE"
export DEV_SRC_ROOT="$DEV_SRC_ROOT"
export PYENV_ROOT="$PYENV_ROOT"
export PIP_CONFIG_DIR="$PIP_CONFIG_DIR"
export LANG=C
EOF
fi
if test ! -f "$DEVPREFIX/bin/jhbuild" ; then
    cat <<EOF > "$DEVPREFIX/bin/jhbuild"
#!$DEVPREFIX/bin/bash
export DEVROOT="$DEVROOT"
export DEVPREFIX="$DEVPREFIX"
export WORKON_HOME="$DEVPREFIX/share/venv"
export PYTHONPATH="$PYTHONPATH"
export PIPENV_DOTENV_LOCATION="$DEVPREFIX/etc/pipenv-env"
export PIPENV_PIPFILE="$DEVPREFIX/etc/Pipfile"
export PATH="$PYENV_ROOT/shims:$DEVPREFIX/bin:$PATH"
export PYENV_ROOT="$PYENV_ROOT"

exec pipenv run jhbuild \$@
EOF
fi
if test ! -d "$DEVPREFIX/libexec" ; then
    mkdir -p "$DEVPREFIX/libexec"
fi
if test ! -f "$DEVPREFIX/libexec/run_jhbuild.py" ; then
    cat <<EOF > "$DEVPREFIX/libexec/run_jhbuild.py"
#!/usr/bin/python
# -*- coding: utf-8 -*-

import sys
import os
import __builtin__
sys.path.insert(0, '$DEV_SRC_ROOT/jhbuild')
pkgdatadir = None
datadir = None
import jhbuild
srcdir = os.path.abspath(os.path.join(os.path.dirname(jhbuild.__file__), '..'))
__builtin__.__dict__['PKGDATADIR'] = pkgdatadir
__builtin__.__dict__['DATADIR'] = datadir
__builtin__.__dict__['SRCDIR'] = srcdir

import jhbuild.main
jhbuild.main.main(sys.argv[1:])

EOF
fi
if test ! -x "$DEVPREFIX/bin/jhbuild" ; then
    chmod +x "$DEVPREFIX/bin/jhbuild"
fi
if test ! -x "$DEVPREFIX/libexec/run_jhbuild.py" ; then
    chmod +x "$DEVPREFIX/libexec/run_jhbuild.py"
fi
if test "x`echo $PATH | grep "$DEVPREFIX/bin"`" == x ; then
    echo "PATH does not contain $DEVPREFIX/bin. You probably want to fix that."
    export PATH="$DEVPREFIX/bin:$PATH"
fi
# pipenv wants enum34 because it's installed with Py2 but that conflicts
# with Py3 so remove it.
pip_remove enum34

SDKROOT=`xcrun --show-sdk-path`

export PIPENV_DOTENV_LOCATION="$DEVPREFIX/etc/pipenv-env"
export PIPENV_PIPFILE="$DEVPREFIX/etc/Pipfile"
export PATH="$PYENV_ROOT/shims:$DEVPREFIX/bin:$PYENV_INSTALL_ROOT/plugins/python-build/bin:$PATH"
export PYENV_ROOT
if test -d "$SDKROOT"; then
    export CFLAGS="-isysroot $SDKROOT -I$SDKROOT/usr/include"
fi
export PYTHON_CONFIGURE_OPTS="--enable-shared"
export WORKON_HOME="$DEVPREFIX/share/venv"

$PIPENV install

BASEURL="https://gitlab.gnome.org/GNOME/gtk-osx/raw/master"

config_dir=""
if test -n "$XDG_CONFIG_HOME"; then
   config_dir="$XDG_CONFIG_HOME"
else
   config_dir="$HOME/.config"
fi
if test ! -d "$config_dir"; then
    mkdir -p "$config_dir"
fi

if test -e "$HOME/.jhbuildrc"; then
    JHBUILDRC="$HOME/.jhbuildrc"
else
    JHBUILDRC="$config_dir/jhbuildrc"
fi
echo "Installing jhbuild configuration at $JHBUILDRC"
curl -ks $BASEURL/jhbuildrc-gtk-osx -o "$JHBUILDRC"

if test -z "$JHBUILDRC_CUSTOM"; then
    JHBUILDRC_CUSTOM="$config_dir/jhbuildrc-custom"
fi
if test ! -e "$JHBUILDRC_CUSTOM" -a ! -e "$HOME/.jhbuildrc-custom"; then
    JHBUILDRC_CUSTOM_DIR=`dirname $JHBUILDRC_CUSTOM`
    echo "Installing jhbuild custom configuration at $JHBUILDRC_CUSTOM"
    if test ! -d "$JHBUILDRC_CUSTOM_DIR"; then
        mkdir -p "$JHBUILDRC_CUSTOM_DIR"
    fi
    curl -ks $BASEURL/jhbuildrc-gtk-osx-custom-example -o "$JHBUILDRC_CUSTOM"
fi
