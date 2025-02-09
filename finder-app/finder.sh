#!/bin/sh

if [ "$#" -lt 2 ]; then
    echo "Not enoght arguments"
    exit 1
elif [ ! -d "$1" ]; then
    echo "File not found!"
    exit 1
else
    numFiles=$(ls -1 "$1" | wc -l)
    numResults=$(grep -r "$2" "$1" | wc -l)
    echo "The number of files are $numFiles and the number of matching lines are $numResults"
fi
