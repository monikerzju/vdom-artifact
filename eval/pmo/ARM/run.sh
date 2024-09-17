#! /bin/bash

sigterm_handler() { 
  echo "Shutdown signal received."
  exit 1
}

# Setup signal trap
trap 'trap " " SIGINT SIGTERM SIGHUP; kill 0; wait; sigterm_handler' SIGINT SIGTERM SIGHUP

sudo sysctl vm.swappiness=0

# create result dir for pmo
cur_time=$(date "+%Y-%m%d-%H%M%S")
res_dir=$cur_time
mkdir -p results/$res_dir

# prepare input
gcc prepare.c -o prepare.o -DTHREAD_NUM=4 -DTOTAL_OPS=1600000
./prepare.o


for i in 1 2 4
do
	nas=0
	thread_num=$i
	thread_ops=400000
	total_ops=$(($i * $thread_ops))
	
	echo "test $i thread, total $total_ops ops ..."
	echo -en ''$i'\t' >> results/$res_dir/res

	# origin
	echo "[start origin]"
	gcc main.c -o main_origin.o -lpthread -g -DTHREAD_NUM=$thread_num -DTOTAL_OPS=$total_ops -DNAS=$nas -I. -lpthread -lvkeys -L.
	./main_origin.o > results/pmo-tmp
	python process.py results/$res_dir/res
	rm results/pmo-tmp
	sleep 5

	# vdom VDS switch
	echo "[start vdom VDS (pgd) switch]"
	nas=6
	gcc main.c -o main_vkeys.o -lpthread -lvkeys -g -DVKEYS -DTHREAD_NUM=$thread_num -DTOTAL_OPS=$total_ops -DNAS=$nas -I. -lpthread -lvkeys -L.
	./main_vkeys.o > results/pmo-tmp
	python process.py results/$res_dir/res
	rm results/pmo-tmp
	sleep 5

	# vdom eviction
	echo "[start vdom eviction]"
	nas=1
	gcc main.c -o main_vkeys.o -lpthread -lvkeys -g -DVKEYS -DTHREAD_NUM=$thread_num -DTOTAL_OPS=$total_ops -DNAS=$nas -I. -lpthread -lvkeys -L.
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

echo 'Please check results in ./results/'$res_dir''