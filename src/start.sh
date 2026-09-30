#!/bin/sh
echo  Resetting Initial Configuration
./reset
echo  Registering Implementation in ./f1
./register -c ./f1/config.txt > /dev/null 2>&1  
echo  Registering Implementation in ./f2
./register -c ./f2/config.txt  > /dev/null 2>&1  
echo  Registering Implementation in ./f3
./register -c ./f3/config.txt  > /dev/null 2>&1  
echo  Registering Implementation in ./f4
./register -c ./f4/config.txt  > /dev/null 2>&1  
echo  Registering Implementation in ./f5
./register -c ./f5/config.txt  > /dev/null 2>&1  
echo  Registering Implementation in ./f6
./register -c ./f6/config.txt  > /dev/null 2>&1  
echo  Registering Implementation in ./f7
./register -c ./f7/config.txt  > /dev/null 2>&1  
echo  Registering Implementation in ./f8
./register -c ./f8/config.txt  > /dev/null 2>&1  
echo  Done

echo Creating Shared Library lib_enc.so
gcc -shared ./LIB/*.o -o ./LIB/lib_enc.so
echo Updating Paths
export LD_LIBRARY_PATH=:$PWD/LIB
mkdir -p Downloads   # the server saves received files here and does not create it
./server
