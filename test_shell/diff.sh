#!/bin/bash
##opts: -d 分隔符 -f 字段
while getopts "d:f:" arg
do
	case $arg in
	d) darg=$OPTARG ;;
	f) farg=$OPTARG ;;
	?) echo "unknown argument"
	   exit 1 ;;
	esac
done
shift $(($OPTIND -1))

#echo $darg
#echo $farg
if [ "$farg" = "" ]
then
	farg=1
fi

if [ "$darg" != "" ]
then
	args=" -F$darg"
fi

#echo $args
#echo $farg
awk $args -v fnum="$farg" '{ if(i%2){ print $fnum-prev } i++; prev=$fnum }' "$@"

