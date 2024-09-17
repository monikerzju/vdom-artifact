import sys

res_file = sys.argv[1]

f = open("results/pmo-tmp")

lines = f.readlines()

time = 0

for line in lines:
	pos = line.find('Done with time:')
	if pos != -1:
		time = int(line[len('Done with time: '):])
		break

f.close()

f = open(res_file, 'a')
line = str(time) + '\t'
f.write(line)
f.close()
