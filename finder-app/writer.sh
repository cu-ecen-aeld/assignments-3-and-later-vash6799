#!/bin/sh
#Here we will implement the same process like did in finder.sh

#Step 1 To check both args are present
if [ $# -ne 2 ]; then
    echo "Error: Two arguments required."
    echo "Usage: $0 <writefile> <writestr>"
    exit 1
fi

#As described in assignment declaring two variables
writefile=$1
writestr=$2

# Extract directory path from writefile
writedir=$(dirname "$writefile")

# Create directory path if it does not exist
mkdir -p "$writedir"
if [ $? -ne 0 ]; then
    echo "Error: Could not create directory path $writedir"
    exit 1
fi

# Write content to file (overwrite if exists)
echo "$writestr" > "$writefile"
if [ $? -ne 0 ]; then
    echo "Error: Could not create or write to file $writefile"
    exit 1
fi