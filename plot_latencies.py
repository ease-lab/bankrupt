#!/usr/bin/python

import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import math
import os
import re
from natsort import natsorted

import argparse

# ######## SET ARGUMENTS #########
XLIM_0 = 0 # lower X limit
XLIM_1 = 400 # upper X limit
YLIM_0 = 1 # lower Y limit
YLIM_1 = 5 # upper Y limit
##################################

parser = argparse.ArgumentParser()
parser.add_argument('idir', help='input results directory')
args = parser.parse_args()

matplotlib.use('Agg')

RESULT_PATH = args.idir
res_dir = RESULT_PATH

for f in natsorted(os.listdir(res_dir)):
    if 'txt' not in f: # plot for all .txt files in directory
        continue
    path = res_dir + '/' + f
    exists = os.path.isfile(path)
    if exists == False:
        print "{} doesn't exist".format(f)
        exit(-1)

    cumulative = [0]
    raw = np.loadtxt(path)
    raw = raw[1000:]
    raw = raw/1000
    for i in range(1, raw.shape[0]):
        cumulative.append(cumulative[i-1] + 0.5) # X axis is discretized timeline, 500ns interval

    plt.figure(figsize=(28,7))
    plt.xlim(XLIM_0, XLIM_1)
    plt.ylim(YLIM_0,YLIM_1)
    plt.ylabel('receiver memory latency (us)', fontsize=30)
    plt.xlabel('timeline (us)', fontsize=30)
    plt.tick_params(axis='both', which='major', labelsize=28)
    plt.tick_params(axis='both', which='minor', labelsize=26)

    # Gridlines
    for i in xrange(0,1400,10):
        plt.axvline(x=i, linewidth=0.5,color='grey')
    for i in xrange(0,10,1):
        plt.axhline(y=i, linewidth=0.5,color='grey')

    plt.plot(cumulative, raw)
    plt.plot(cumulative, raw,'ro')
    plt.savefig(res_dir + '/' + f.split('.')[0] + '.png', bbox_inches='tight')
