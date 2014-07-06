#!/bin/bash

echo "10 kB file"

for p in 2 5 10
do
  for x in {1..3}
  do
    time ./wfc -p $p -i file_10kB.txt -o out_10kB.txt
  done
done

echo "1 MB file"

for p in 2 5 10
do
  for x in {1..3}
  do
    time ./wfc -p $p -i file_1MB.txt -o out_1MB.txt
  done
done

echo "100 MB file"

for p in 2 5 10
do
  for x in {1..3}
  do
    time ./wfc -p $p -i file_100MB.txt -o out_100MB.txt
  done
done

exit 0

