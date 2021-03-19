# Gtk-OSX #

This project along with
[gtk-mac-bundler](https://gitlab.gnome.org/GNOME/gtk-mac-bundler) and
[gtk-mac-integration](https://gitlab.gnome.org/GNOME/gtk-mac-integration)
aims to simplify building MacOS application bundles for Gtk+-based
applications. Gtk-OSX provides modulesets, patches, and jhbuild
enhancements for building Gtk+ applications on your Mac. Unlike
"package manager" style systems like MacPorts or Homebrew, Gtk-OSX
installs products into user-configurable "silo" subdirectories that
stand on their own, making it easy to have multiple independent
installations of the Gtk+ stack and other dependencies to accommodate
the requirements of different applications. The build environment
created by Gtk-OSX is suitable for development of Gnome libraries and
Gtk+-based applications on MacOS; indeed the author has been using it
for just that purpose for almost 10 years.

## Modulesets ##

There are 3 modulesets. You can select the one which most suits you by
adding the line
      moduleset="https://gitlab.gnome.org/GNOME/gtk-osx/raw/master/modulesets-stable/gtk-osx.modules"

to your ~/.jhbuildrc-custom, replacing "modulesets-stable" (the default) with

 * modulesets-stable: The default, all tarball-based released sources.
 * modulesets: Sources from VCS repositories when that's available,
   with stable release revisions set for each module.
 * modulesets-unstable: The bleeding edge modulesets, pulled from VCS
   repositories with no release revisions.

## Customization ##

Jhbuild has a hook that opens a special file ~/.jhbuildrc (the
name and path can be overridden with the ```-f``` command-line
parameter). Gtk-OSX provides a file intended to be installed as
~/.jhbuildrc that configures the build parameters according to
the version of MacOS that you're running or that you intend to target,
i.e. MACOS_DEPLOYMENT_TARGET. That file, jhbuildrc-gtk-osx, includes
it's own hook to load a file named ~/.jhbuildrc-custom (not
overrideable, sorry). This second rc file is for local customization,
including the usual things contemplated by the jhbuild developers for
the [configuration
file](https://developer.gnome.org/jhbuild/unstable/config-reference.html.en).
While Gtk-OSX provides a sample file with extensive comments
explaining possible options, users should in no way feel constrained
by those comments. The file is Python and is loaded and executed just
as is the rest of jhbuild so there are huge opportunities for further
customization.

## MacOS Version Support ##

We tried for a long time to maintain compatibility with Mac OS X 10.4
(Tiger), but the core Gnome projects decided at some point that this
wasn't reasonable; Apple's changes to MacOS, particularly with the
dropping of PPC support in Mac OS X 10.6 and the impending removal of
32-bit support in 10.15 mean that it's not really feasible to both
keep up with changes in Gnome and in MacOS and also to support older
versions of MacOS.

The current policy is that we'll support whatever version of MacOS was
released 5 years previously. In practice older systems will continue
to work as long as Apple didn't break anything between that older
system and the 5-year-old release, but we'll periodically *and without
notice* clean up ifdefs and such supporting older systems. If you
insist on using an old system, check out the last version of Gtk-OSX
that supports it and never pull again. Make sure that your
configuration is set up to use local modulesets, the ones in the
public repositories are frequently upgraded and are unlikely to work
on obsolete versions of MacOS.

## Python ##

Gnome is transitioning to requiring Python3.2+ for just about
everything else. In particular they have adopted a new build system,
[meson](https://mesonbuild.com/), that requires Python3 and has
dropped support for autotools builds in several of its core packages.

Apple provided Python3 in Mac OS X 10.6 and dropped it in 10.7, so we
need to arrange for Python 3 to be available for jhbuild to build
meson files with as well as meson itself. Gtk-OSX takes care of that
at installation. It relies on two Python standard packages,
[Pipenv](https://docs.pipenv.org/) and
[pyenv](https://github.com/pyenv/pyenv). In combination they create a
Python3 [virtual
environment](https://docs.python.org/3/tutorial/venv.html) and
populate it with to required dependencies, the afore-mentioned meson
and [six](https://pypi.org/project/six/), a python 2/3 compatibility
library used by gtk-doc.

### Python Configuration ###

gtk-osx-setup.sh uses several environment variables to control where it
puts things. Override the defaults by setting the corresponding
variable in the environment before running gtk-osx-setup.sh.

* DEVROOT         default: $HOME                Base directory
* DEVPREFIX       default: $DEVROOT/.new_local  Prefix for jhbuild tools
* PYTHONUSERBASE  default: $DEVROOT/.new_local  Location where PIP installs packages when --user is given as an option; new-setup.sh uses --user when invoking pip.
* DEV_SRC_ROOT    default: $DEVROOT/Source      Location of pyenv and jhbuild sources
* PYENV_ROOT      default: $DEV_SRC_ROOT/pyenv  Prefix for pyenv's sources and virtual environments.
* PIP_CONFIG_DIR  default: $HOME/.config/pip    Pip's configuration, see the PIP documentation.

PYTHONUSERBASE, PYENV_ROOT, and PIP_CONFIG_DIR are actually specified
by PIP and PYENV. Pipenv and pyenv have other control environment
variables, consult their documentation for more information.

gtk-osx-setup.sh will create several shell-script and configuration files,
but it will not overwrite any that already exist so it's safe to
customize your setup after running it. The files are:

* $DEVPREFIX/etc/Pipfile             Configures the python version and installed packages.
* $DEVPREFIX/etc/pipenv-env          Environment variables for pipenv-controlled virtual environment.
* $DEVPREFIX/bin/jhbuild             Shell script to run jhbuild inside a pipenv-controlled virtual environment using the above Pipfile and pipenv environment file.
* $DEVPREFIX/libexec/run_jhbuild.py  Python file replacing the jhbuild executable provided by jhbuild itself. Called from the jhbuild shell script.

gtk-osx-setup.sh will also copy /usr/bin/bash to $DEVPREFIX/bin and jhbuildrc-gtk-osx will set $SHELL to that path to work around SIP.

The pipenv control file sets Python3.6 as its required version, and gtk-osx-setup.sh will create a pyenv virtual environment for that. If you don't already have Python3.6 in your $PATH it will offer to install the latest Python3.6 release for you.

Before you build there's one manual step needed: Python3 insists on
setting `LINKFORSHARED: -Wl,stack_size,1000000 -framework
CoreFoundation` even though stack_size is allowed only when compiling
executables. It's in
`$PYENV_ROOT/versions/3.6.x/lib/python3.6/_sysconfigdata_m-darwin.py`. Open
that file with your programming editor and find and change it to
`LINKFORSHARED: -framework CoreFoundation`.

## Bootstrapping ##

Unlike previous versions of gtk-osx-build-setup.sh, gtk-osx-setup.sh
does *not* copy the Gtk-OSX boostrap moduleset over the jhbuild one.
Instead, jhbuildrc provides a new command

```
jhbuild bootstrap-gtk-osx
```

This command will by default load the bootstrap.modules from gtk-osx's
Gitlab repository. If ```use_local_modules``` is true (default is
false, override it in jhbuildrc-custom) and a file named
bootstrap.modules exists in the same directory as the moduleset you've
told jhbuild to use (set with ```moduleset = ``` in jhbuildrc-custom
or the ```-m``` option to jhbuild) it will build that. This allows you
to have a custom bootstrap.modules for your project.

If you set ```use_local_modules``` to true and set ```modulesets_dir =```
to a valid path containing a file named bootstrap.modules then
bootstrap-gtk-osx will build that moduleset instead.

Note that in order to actually work the bootstrap.modules moduleset
file must contain a meta-module named ```meta-bootstrap``` that
depends on all of the modules that you want to build.


## Build Problems ##

I encountered two build problems while creating this:

* gdk-pixbuf builds its modules as shared libraries instead of
  loadable modules so gdk-pixbuf-query-loaders can't find them. Stop
  the build, rename them (they're in
  `$PREFIX/lib/gdk-pixbuf/2.10.0/loaders`) from
  e.g. libpixbufloader-png.dylib to libpixbufloader-png.so and rerun
  `gdk-pixbuf-query-loaders --update`.

* Pango's meson passes a bogus `-framework CoreFoundation
  ApplicationServices` arg at the end of the g-ir-scanner command, and
  that fails. I couldn't figure out where it came from so I wound up
  editing meson.build in the build directory and removing the argument
  from the command.
