# source env.sh to set native build environment
# point to Android SDK / emulator
export ANDROID_SDK=$HOME/Library/Android/sdk
export ANDROID_HOME=$ANDROID_SDK
PATH=$PATH:$ANDROID_SDK/emulator:$ANDROID_SDK/tools:$ANDROID_SDK/platform-tools
# using brew ruby
PATH=/usr/local/Cellar/ruby/3.4.5/bin:$PATH
# old gradle doesn't work with newer java 24
export JAVA_HOME=$(/usr/libexec/java_home -v 19)
