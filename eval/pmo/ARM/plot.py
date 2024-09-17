#!/usr/bin/python

import sys
import matplotlib.pyplot as plt
import numpy as np

def Average(lst):
    return sum(lst) / len(lst)

plt.rcParams.update({'font.size': 12})
plt.rcParams["figure.figsize"] = (7, 4)

path = sys.argv[1]
path_res = path + '/res'

f = open(path_res)
lines = f.readlines()

x_values = []
origin = []
vds_switch = []
eviction = []

for line in lines:
	tmp = list(map(float, line.split()))
	x_values.append(int(tmp[0]))
	origin.append(tmp[1])
	vds_switch.append(tmp[2])
	eviction.append(tmp[3])

y_values_vds_switch = [0 for i in range(len(x_values))]
y_values_eviction = [0 for i in range(len(x_values))]

for i in range(len(x_values)):
	y_values_vds_switch[i] = round(((vds_switch[i] - origin[i]) / origin[i]) * 100, 2)
	y_values_eviction[i] = round(((eviction[i] - origin[i]) / origin[i]) * 100, 2)

# print(y_values_vds_switch, y_values_eviction)

fig, sub_fig = plt.subplots(1, 1)

ms_size = 4
line_width = 0.8
maker_line_width = 0.8
fillstyle = 'none'

labels = x_values
x_values = np.arange(len(x_values))

sub_fig.plot(x_values, y_values_eviction, label='VDom eviction', ms=ms_size, marker='o', linewidth=line_width, color='#005CAB', fillstyle=fillstyle, markeredgewidth=maker_line_width)
sub_fig.plot(x_values, y_values_vds_switch, label='VDS switch', ms=ms_size, marker='s', linewidth=line_width, color='#E31B23', fillstyle=fillstyle, markeredgewidth=maker_line_width)

grid_width = 0.4
sub_fig.grid(linestyle='--', axis='y', linewidth=grid_width)
sub_fig.set_axisbelow(True)

sub_fig.set_xlabel('Number of threads')
sub_fig.set_ylabel('Overhead')

sub_fig.set_xticks(x_values)
sub_fig.set_xticklabels(labels)
sub_fig.set_yticks(np.arange(0, 30, 4))

plt.legend()

plt.savefig(path+"/res.png", format="png", bbox_inches="tight")