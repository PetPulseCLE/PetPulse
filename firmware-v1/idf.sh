#! /bin/bash

APP=""
BUILD=0
FLASH=0
MONITOR=0
FULLCLEAN=0

while getopts ":bfmrca:" opt; do
    case $opt in
        b) BUILD=1 ;;
            
        f) FLASH=1 ;;

        m) MONITOR=1 ;;

        r) BUILD=1 ; FLASH=1 ; MONITOR=1 ;;

        c) FULLCLEAN=1 ;;

        a)
            APP=$OPTARG
            ;;
        *)
            echo "Usage: $0 [-b] [-f] [-m] [-r] [-a <app>]"
            echo "-b: build, -f: flash, -m: monitor -r: build flash monitor"
            echo "no args defaults to -r -a <> "
            exit 1
            ;;
    esac
done

[ $OPTIND -eq 1 ] && idf.py fullclean && idf.py -DAPP="${APP}" build flash monitor


[ $FULLCLEAN -eq 1 ] && idf.py fullclean 

[ $BUILD -eq 1 ] && idf.py fullclean && idf.py -DAPP="${APP}" build

[ $FLASH -eq 1 ] && idf.py fullclean && idf.py -DAPP="${APP}" flash

[ $MONITOR -eq 1 ] && idf.py fullclean && idf.py -DAPP="${APP}" monitor
