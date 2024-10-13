#!/bin/bash

if [ "$#" -lt 2 ]; then
    echo "Not enoght arguments"
    exit 1
else
    file="$1"
    if [ ! -d "${file%/*}" ]; then
        mkdir -p "${file%/*}"  # Create directory if it doesn't exist
    fi
    
    # Create the file
    touch "$file"
    
    if [ "$?" -eq 0 ]; then
        echo "$2" > "$file"  # Write the content of $2 into the file
    else
        echo "Error creating file"
        exit 1
    fi
fi
