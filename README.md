# aarch64-apple-gimp
Changes required to get GIMP compiling on M1 macs with big sur. Probably fixed big sur issues in general

Current tree has an issue at runtime, build completes but does not run. Will need to isolate affect library this weekend.

This is not meant to be build with circle CI. The scripts are left in place for anyone who wishes to look at what the gimp team does to try to adapt my build script to that format.

For code signing you will need to provide your apple dev details. Recommendation is making a .appledevprofile file in your home directory with info as follows:
```
export codesign_subject=8DEFF876B74E8FEF333E9978BEFC76FE87FBADCB
export notarization_login=user@example.com
export notarization_password=xxxx-xxxx-xxxx-xxxx
```
If done in this format the notarizing process will pull the details automatically when uploading and verifying.

If using apple dev credentials, you may wish to uncomment the last line of the build script to allow automatic notarizing.

# Installing

Clone the repo to your system 
```
git clone https://github.com/evultrole/aarch64-apple-gimp
cd aarch64-apple-gimp
./newsetup.sh
```

This will generate a symlink in your home directory to your current folder, labeled "Skunkworks", download rust, install the xcode command line tools, install python 3.10.1, and create a build environment for your gimp setup.

The process will require some input. Initial setup doesn't take long, once the long delay is reached there will be red text informing you that you can leave the process. When you come back, there should be a dmg file in `/tmp/artifacts/`

