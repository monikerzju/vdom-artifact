import sys

res_file = sys.argv[1]

f = open("sysbench-tmp")

lines = f.readlines()

num_of_thd = 0
qps = 0

for line in lines:
	pos = line.find('Number of threads:')
	if pos != -1:
		num_of_thd = int(line[len('Number of threads:'):])
		continue
	pos = line.find('queries:')
	if pos != -1:
		pos1 = line.find('(')
		pos2 = line.find('per')
		qps = float(line[pos1+1:pos2])
		break

f.close()

f = open(res_file, 'a')
line = str(num_of_thd) + "\t" + str(qps) + '\n'
f.write(line)
f.close()
