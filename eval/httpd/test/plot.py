#!/usr/bin/python

import sys
import matplotlib.pyplot as plt
import numpy as np

def Average(lst):
    return sum(lst) / len(lst)

plt.rcParams.update({'font.size': 12})
plt.rcParams['hatch.linewidth'] = 0.1
plt.rcParams["figure.figsize"] = (6, 3.5)

width = 1
edge_width = 0.5

path = sys.argv[1]
file_size = sys.argv[2]

path_origin = path + '/origin'
path_vdom = path + '/vdom'

f = open(path_origin)
lines = f.readlines()

x_values = []
y_values_origin = []
for line in lines:
	tmp = list(map(float, line.split()))
	x_values.append(int(tmp[0]))
	y_values_origin.append(tmp[1])

# print(x_values)
# print(y_values_origin)
f.close()

f = open(path_vdom)
lines = f.readlines()

y_values_vdom = []
for line in lines:
	tmp = list(map(float, line.split()))
	y_values_vdom.append(tmp[1])

# print(y_values_vdom)
f.close()

overhead = [0 for i in range(len(y_values_origin))]
for i in range(len(y_values_origin)):
	overhead[i] = (y_values_origin[i] - y_values_vdom[i]) / y_values_origin[i]
overhead_avg = Average(overhead)

print("Average overhead: ", round(overhead_avg * 100, 2), "%")

interval = 0.05
x_values_origin = [i - width / 2 - interval for i in x_values]
x_values_vdom = [i + width / 2 + interval for i in x_values]

fig = plt.subplot()

color1 = '#ffffff'
color2 = '#ffffff'
hatch1 = 'xxxxxxx'
hatch2 = '////'
hatch_color1 = '#005CAB'
hatch_color2 = '#E31B23'
plt.bar(x_values_origin, y_values_origin, width=width, label='original', color=color1, edgecolor=hatch_color1, lw=edge_width, hatch=hatch1)
plt.bar(x_values_vdom, y_values_vdom, width=width, label='$\it{VDom}$ protected', color=color2, edgecolor=hatch_color2, lw=edge_width, hatch=hatch2)

plt.legend()

grid_width = 0.4
fig.grid(linestyle='--', axis='y', linewidth=grid_width)
fig.set_axisbelow(True)
fig.set_xlabel('Number of concurrent clients')
fig.set_ylabel('Requests per second')
fig.set_xticks(np.arange(4, 50, 4))
fig.set_yticks(np.arange(0, 17000, 2500))
fig.spines['right'].set_visible(False)
fig.spines['top'].set_visible(False)

title = str(file_size) + 'KB file with average overhead ' + str(round(overhead_avg * 100, 2)) + '%'
fig.set_title(title)

plt.savefig(path+"/res.png", format="png", bbox_inches="tight")