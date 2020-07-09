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
import sys
import os
from fabric.tasks import execute
from fabfile import *
import distbenchr as dbr
import signal
import time


def main():
    ###### ARGUMENTS #####################
    # Change as appropriate
    num_sender_qps = 4 # number of QPs the sender uses to flood intermediary
    num_sender_requests_per_qp = 14 # MAX 16; number of requests per QP that the sender sends
    intermediary_machine = '1.1.1.1' # IP of intermediary
    num_sender_reps = 2 # Repetitions of each address in the burst. Each burst has burst_size/num_sender_reps UNIQUE addresses
                        # This is used to limit the memory the attack needs. It is explained further in the report
    results_file = 'test.txt' # name of results file. THIS FILE IS CREATED ON THE RECEIVER MACHINE

    num_receiver_qps = 10 # Number of receiver QPs; NOT RECOMMENDED TO CHANGE, 10 works fine
    ###### END OF ARGUMENTS ####################

    execute(build)
    execute(copy_exec)

    mnt = dbr.Monitor()
    mnt.bg_execute(intermediary, num_sender_qps, num_sender_requests_per_qp, num_sender_reps, num_receiver_qps, should_wait=True)
    time.sleep(1)
    mnt.bg_execute(sender, num_sender_qps, num_sender_requests_per_qp, intermediary_machine, num_sender_reps, should_wait=True)
    time.sleep(1)
    mnt.bg_execute(receiver, num_receiver_qps, intermediary_machine, results_file, should_wait=True)
    mnt.monitor() # wait for the client to finish
    mnt.killall() # kill the server as well

    
    execute(copy_results_file, results_file)
    execute(plot_results)
 


if __name__ == "__main__":
    os.setpgrp() # create new process group, become its leader
    try:
        main()
    except:
        import traceback
        traceback.print_exc()
    finally:
        os.killpg(0, signal.SIGKILL) # kill all processes in my group
