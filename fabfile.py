# MIT License
# 
# Copyright (c) 2020 Edinburgh Architecture and Systems (EASE) Lab @ University of Edinburgh
# 
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
# 
# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.
# 
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.

#!/usr/bin/python
from fabric.api import env, roles, run
from distbenchr import run_bg
import os

# CHANGE ARGUMENTS AS APPROPRIATE

# location of tarball on intermediary machine. from here, the files are copied to each machine
tarball_dir = 'tarball/dir'

executables_dir = '/tmp' # Directory where the executables are run from on each machine
                         # This is where they are copied to from the intermediary NOT RECOMMENDED TO CHANGE
intermediary_ip = '1.1.1.1' # intermediary IP
sender_ip = '1.1.1.2' # sender IP
receiver_ip = '1.1.1.3' # receiver IP

#############################################################

env.roledefs = {
        'intermediary': [intermediary_ip],
        'sender': [sender_ip],
        'receiver': [receiver_ip]
        }
RESULTS_DIR = executables_dir + '/results'

@run_bg('sender')
def sender(num_qps=5, num_requests_per_qp=16, intermediary='philly', num_sender_reps=2):
    sender_cmd = 'numactl --membind=0 ' + executables_dir + '/sender' + ' ' + str(intermediary) + ' ' + str(num_qps) + ' ' + str(num_requests_per_qp) + ' ' + str(num_sender_reps)
    run(sender_cmd)


@run_bg('intermediary')
def intermediary(num_sender_qps=5, sender_num_outstanding_reads_per_qp=16, num_sender_repetitions=1, num_receiver_qps=10):
    numactl_cmd = 'numactl --membind=0 ' + executables_dir + '/intermediary'
    intermediary_cmd = numactl_cmd + ' ' + str(num_sender_qps) + ' ' + str(sender_num_outstanding_reads_per_qp) + ' ' + str(num_sender_repetitions) + ' ' + str(num_receiver_qps)
    run('mkdir -p ' + RESULTS_DIR)
    run(intermediary_cmd)


@run_bg('receiver')
def receiver(num_receiver_qps=10, intermediary='philly', results_file='/dev/null'):
    numactl_cmd = 'numactl --membind=0 ' + executables_dir + '/receiver'
    receiver_cmd = numactl_cmd + ' ' + str(intermediary) + ' ' + str(num_receiver_qps) + ' > ' + RESULTS_DIR + '/' + results_file
    run('mkdir -p ' + RESULTS_DIR)
    run(receiver_cmd)

@roles('intermediary')
def copy_exec():
    intermediary_cmd = 'scp ' + tarball_dir + '/bin/intermediary ' + intermediary_ip + ':' + executables_dir
    sender_cmd = 'scp ' + tarball_dir + '/bin/sender ' + sender_ip + ':' + executables_dir
    receiver_cmd = 'scp ' + tarball_dir + '/bin/receiver ' + receiver_ip + ':' + executables_dir
    run(receiver_cmd)
    run(intermediary_cmd)
    run(sender_cmd)

@roles('intermediary')
def build():
    build_command = '( cd ' + tarball_dir + '/src ; make )'
    run(build_command)

@roles('receiver')
def copy_results_file(results_file=None):
    if results_file == None:
        return
    copy_cmd = 'scp ' + RESULTS_DIR + '/' + results_file + ' ' + intermediary_ip + ':' + RESULTS_DIR
    run(copy_cmd)

@roles('intermediary')
def plot_results():
    plot_cmd = 'python ' + tarball_dir + '/plot_latencies.py ' + RESULTS_DIR
    run(plot_cmd)
