#/bin/bash

for OPEN in 500 1000 2000 5000 10000 20000 30000 40000 50000 60000 70000 80000 90000 100000
do
    # echo "Testing  number of open:" $OPEN
    ./probe_open $OPEN
done