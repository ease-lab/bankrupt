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

.PHONY: clean

CFLAGS  := -Wall -g
LD      := gcc
LDLIBS  := -lrdmacm -libverbs -lpthread

APPS    := sender intermediary receiver

all: ${APPS}

sender: sender.c ../include/rdma-common.h ../include/ownExperimentC.h ../include/time_rdtsc.h
	mkdir -p ../bin
	${LD} -O3 -I../include -o ../bin/sender sender.c ${LDLIBS} 

intermediary: intermediary.c ../include/rdma-common.h ../include/ownExperimentC.h ../include/time_rdtsc.h
	mkdir -p ../bin
	${LD} -O3 -I../include -o ../bin/intermediary intermediary.c ${LDLIBS}

receiver: receiver.c ../include/rdma-common.h ../include/ownExperimentC.h ../include/time_rdtsc.h
	mkdir -p ../bin
	${LD} -O3 -I../include -o ../bin/receiver receiver.c ${LDLIBS}


clean:
	rm -f ../bin/intermediary
	rm -f ../bin/sender
	rm -f ../bin/receiver

