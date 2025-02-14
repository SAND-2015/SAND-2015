#!/bin/bash

#
# benchmark.sh
# ------------
# (C) Jean-Pierre Lozi, Florian David, Gaël Thomas, Julia Lawall, Gilles Muller
#     2011
#
# Examples :
# - Full bench:
#   sudo ./benchmark.sh
# - RCL, 10 clients, 100 runs, r_type=5, n_req=1000, n_runs=100:
#   sudo N_CLIENTS=10 LOCKS=rcl ./benchmark.sh 5 1000 100
#


TIMEOUT=3000


if [[ $N_CLIENTS = "" ]]; then
    N_CLIENTS="1 5 10 15 20 25 30 35 40"
fi

if [[ $LOCKS = "" ]]; then
    LOCKS="orig mcs mcstp posix flat spinlock rcl saml"
fi


if [[ $# -ge 1 ]]; then
    R_TYPE=$1
else
    R_TYPE=5
fi

if [[ $# -ge 2 ]]; then
    N_REQ=$2
else
    N_REQ=2000
fi

if [[ $# -ge 3 ]]; then
    N_RUNS=$3
else
    N_RUNS=3
fi


if [[ $(/usr/bin/id -u) -ne 0 ]]; then
	echo "Error: must be run with superuser rights (e.g., sudo ./benchmark.sh)."
    	exit
fi

trap 'exit' SIGINT

rm -rf ../results/*.csv

for n in $N_CLIENTS; do
    for lock in $LOCKS; do
	rm -rf home
        mkdir home
        if [[ $lock == "orig" ]]; then
            cd TPCC-BDB-RELEASE-original
        else
            cd TPCC-BDB-RELEASE-patched
        fi

        if [[ $lock == "mcs" ]] && [[ $n -gt 48 ]]; then
            cd ..
            continue
        fi

        echo -n "Lock = "
        printf "%5s" $lock
        echo -n " n = "$n": "
        echo -n $n | tee -a ../results/$lock.csv

        i=0

        while [[ $i -lt $N_RUNS ]]; do
	rm -rf home
	mkdir home

            elapsed_time=0 
# NORMAL EXECUTION
            ./run_test.sh none $R_TYPE $((N_REQ)) $n $lock 2 > out&
# /NORMAL EXECUTION

# WITH GDB
#            echo "Once in gdb, run: run -b -h /home/zmz/ipads/saml_cloud1/rcl/berkeleydb/home -i < /home/zmz/ipads/saml_cloud1/rcl/berkeleydb/TPCC-BDB-RELEASE-patched/input"
#            ./run_test.sh none-gdb $R_TYPE $((N_REQ)) $n $lock 2
#            run -b -h /home/zmz/ipads/saml_cloud1/rcl/berkeleydb/home -i < /home/zmz/ipads/saml_cloud1/rcl/berkeleydb/TPCC-BDB-RELEASE-patched/input
#            exit
# /WITH GDB

# WITH OUTPUT
#            ./run_test.sh none $R_TYPE $((N_REQ)) $n $lock 2
#	    exit
#             continue
# /WITH OUTPUT
	

	 rtpid=$!
            pid=""

            sleep 1

            killed=0
            retcode=0
            while [[ retcode -eq 0 ]]; do
                elapsed_time=$((elapsed_time + 1))

                sleep 1
                if [[ $pid == "" ]]; then
                     pid=`ps -Ma | grep "[t]pcc_xact" |  awk '{ print $2 }'`
                fi

                if [[ $elapsed_time -gt $TIMEOUT ]]; then
                    echo -n "k"
                    kill -9 $pid
                    killed=1
                    break
                fi

                kill -0 $rtpid 2> /dev/null
                retcode=$?
            done

            if [[ $killed -eq 1 ]]; then
                continue
            fi


            if [[ $R_TYPE -ne 0 ]]; then
                value=`cat out | grep throughput | cut -d " " -f 2 \
                    | awk '{ sum += $1 } END { print sum / NR }' 2> /dev/null \
                    | tr -d '\n'`
            else
                value=`cat out | grep THROUGHPUT | tr -s " " " " \
                    | cut -d " " -f 2 \
                    | awk '{ sum += $1 } END { print sum / NR }' 2> /dev/null \
                    | tr -d '\n'`
            fi
#            if [[ $R_TYPE -ne 0 ]]; then
#                value=`cat out | grep throughput | cut -d " " -f 2 \
#                    | awk '{ sum += $1 } END { print sum / NR }' 2> /dev/null \
#                    | tr -d '\n'`
#            else
#                value=`cat out | grep THROUGHPUT | tr -s " " " " \
#                    | cut -d " " -f 2 \
#                    | awk '{ sum += $1 } END { print sum / NR }' 2> /dev/null \
#                    | tr -d '\n'`
#            fi

            if [[ $value == "" ]]; then
                echo -n "d"
                sleep 3
                continue
            else
                echo -n "," | tee -a ../results/$lock.csv
                echo -n $value | tee -a ../results/$lock.csv
            fi

            rm out

            i=$((i+1))
        done

        echo | tee -a ../results/$lock.csv

        cd ..
    done
done

