./avl_test < avl_cmd.txt > avl_tmp.txt
diff avl_result.txt avl_tmp.txt > /dev/null
if [ "$?" != 0 ]
then
	echo "avl test fail: analyse avl_tmp.txt"
else
	echo "avl test ok"
	rm avl_tmp.txt
fi

