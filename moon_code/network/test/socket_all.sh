#!/bin/bash
./socket_test > tmp.txt
echo "performance:------------"
./socket_pfm.sh
echo "read_write_test---------"
./socket_compare.sh tmp.txt
echo "pipe sequence---------"
./pipe_seq.sh tmp.txt > tmp3.txt
if [ "$?" != "0" ]
then
	echo "error"
else
	./count.sh -d: -f19 tmp3.txt
fi
echo "socket sequence----------"
./socket_seq.sh tmp.txt > tmp2.txt
if [ "$?" != "0" ]
then
	echo "error"
else
	./count.sh -d: -f7 tmp2.txt
fi

