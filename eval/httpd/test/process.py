import sys

res_file = sys.argv[1]

f = open("parallel-tmp")

lines = f.readlines()

total_conc_level = 0
total_req_per_sec = 0

for line in lines:
	pos = line.find('Concurrency Level')
	if pos != -1:
		conc_level = int(line[len('Concurrency Level:      '):])
		total_conc_level += conc_level
		continue
	pos = line.find('Requests per second:    ')
	if pos != -1:
		pos1 = line.find('[#/sec] (mean)')
		req_per_sec = float(line[len('Requests per second:    '):pos1])
		total_req_per_sec += req_per_sec
		continue

f.close()

f = open(res_file, 'a')
line = str(total_conc_level) + "\t" + str(round(total_req_per_sec, 2)) + '\n'
f.write(line)
f.close()

