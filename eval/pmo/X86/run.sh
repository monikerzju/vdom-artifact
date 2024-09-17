#! /bin/bash

sigterm_handler() { 
  echo "Shutdown signal received."
  exit 1
}

# Setup signal trap
trap 'trap " " SIGINT SIGTERM SIGHUP; kill 0; wait; sigterm_handler' SIGINT SIGTERM SIGHUP

# create result dir for pmo
cur_time=$(date "+%Y-%m%d-%H%M%S")
res_dir=$cur_time
mkdir -p results/$res_dir

# prepare input
gcc prepare.c -o prepare.o -DTHREAD_NUM=8 -DTOTAL_OPS=32000000
./prepare.o

for i in 1 2 4 8
do
	nas=0
	thread_num=$i
	thread_ops=4000000
	total_ops=$(($i * $thread_ops))
	
	echo "test $i thread, total $total_ops ops ..."
	echo -en ''$i'\t' >> results/$res_dir/res

	# origin
	echo "[start origin]"
	gcc main.c -o main_origin.o -lpthread -g -DTHREAD_NUM=$thread_num -DTOTAL_OPS=$total_ops -DNAS=$nas
	./main_origin.o > results/pmo-tmp
	python process.py results/$res_dir/res
	rm results/pmo-tmp
	sleep 5

	# vdom VDS switch
	echo "[start vdom VDS (pgd) switch]"
	nas=5
	gcc main.c -o main_vkeys.o -lpthread -lvkeys -g -DVKEYS -DTHREAD_NUM=$thread_num -DTOTAL_OPS=$total_ops -DNAS=$nas
	./main_vkeys.o > results/pmo-tmp
	python process.py results/$res_dir/res
	rm results/pmo-tmp
	sleep 5

	# vdom eviction
	echo "[start vdom eviction]"
	nas=1
	gcc main.c -o main_vkeys.o -lpthread -lvkeys -g -DVKEYS -DTHREAD_NUM=$thread_num -DTOTAL_OPS=$total_ops -DNAS=$nas
	./main_vkeys.o > results/pmo-tmp
	python process.py results/$res_dir/res
	rm results/pmo-tmp
	sleep 5

	echo >> results/$res_dir/res
	rm *.o
done

echo 'Plot result figure ...'
python plot.py results/$res_dir
echo 'Plot result figure done!'

rm *.txt

echo 'Please check results in ./results/'$res_dir''