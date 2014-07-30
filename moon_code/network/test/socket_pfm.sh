echo "epoll_pfm"
grep ":epoll_pfm_" tmp.txt | ./diff.sh -d: -f4 |./count.sh
echo "accept_pfm"
grep ":accept_pfm_" tmp.txt | ./diff.sh -d: -f4 |./count.sh
echo "connect_pfm"
grep ":connect_pfm_" tmp.txt | ./diff.sh -d: -f4 |./count.sh 
echo "newsocket_pfm"
grep ":newsocket_pfm_" tmp.txt | ./diff.sh -d: -f4 |./count.sh 
echo "read_pfm"
grep ":read_pfm_" tmp.txt | ./diff.sh -d: -f4 |./count.sh 
echo "write_pfm"
grep ":write_pfm_" tmp.txt | ./diff.sh -d: -f4 |./count.sh 
echo "puttask_pfm"
grep ":puttask_pfm_" tmp.txt | ./diff.sh -d: -f4 |./count.sh 
echo "epolldel_pfm"
grep ":epolldel_pfm_" tmp.txt | ./diff.sh -d: -f4 |./count.sh 
echo "onready_pfm"
grep ":onready_pfm_" tmp.txt | ./diff.sh -d: -f4 |./count.sh 
echo "testrs_pfm"
grep ":testrs_pfm_" tmp.txt | ./diff.sh -d: -f4 |./count.sh 
echo "findi_pfm"
grep ":findi_pfm_" tmp.txt | ./diff.sh -d: -f4 |./count.sh 
echo "setclosed_pfm"
grep ":setclosed_pfm_" tmp.txt | ./diff.sh -d: -f4 |./count.sh 

