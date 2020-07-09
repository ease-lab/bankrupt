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

#!/bin/bash -e
sudo apt update

echo Installing python2 pip
sudo apt -y install python-pip

echo Installing python setuptools
sudo pip2 install setuptools 
sudo pip2 install numpy natsort matplotlib

echo Installing distributed coordinator
pushd distbenchr > /dev/null
sudo python setup.py install
popd > /dev/null

echo Installing cpupower lib
sudo apt -y install linux-tools-$(uname -r)

echo installing rdma cm lib
sudo apt -y install libibverbs-dev librdmacm-dev

echo Installing perftest lib
sudo apt-get -y install libmlx4-1 libmlx5-1 ibutils rdmacm-utils libibverbs1 ibverbs-utils perftest infiniband-diags

echo Loading mobprobe
sudo modprobe ib_uverbs

echo loading rdma_ucm
sudo modprobe rdma_ucm

echo Installing numactl
sudo apt install numactl

echo Installations done
