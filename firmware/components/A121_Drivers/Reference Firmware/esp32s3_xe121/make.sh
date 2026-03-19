#!/bin/env bash

if [ -z "$IDF_PATH" ]
then
      >&2 echo "\$IDF_PATH is NULL"
      exit 1
fi

set -e

# Build
. $IDF_PATH/export.sh 2>&1

for dir in samples/*/ ; do
	idf.py fullclean -C $dir
	idf.py build -C $dir
done
