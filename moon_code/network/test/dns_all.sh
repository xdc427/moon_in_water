./dns_cmd_create.sh > dns_tmp2.txt
./dns_test dns_tmp2.txt > dns_tmp.txt
echo "pipe_info:"
./pipe_seq.sh dns_tmp.txt | ./count.sh -d: -f19
echo "query ok"
grep "domain ok" dns_tmp.txt | wc -l
echo "query fail"
grep "domain fail" dns_tmp.txt | wc -l
echo "query cancel"
grep "domain cancel" dns_tmp.txt | wc -l
echo "domain mem"
./dns_mem.sh dns_tmp.txt | ./count.sh -d: -f3
echo "addrs mem"
./dns_mem.sh dns_tmp.txt | ./count.sh -d: -f5
echo "domain status"
./dns_test_status.sh dns_tmp.txt

