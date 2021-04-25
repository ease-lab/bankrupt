# Bankrupt communication channel framework

This repository contains software that allows to set up communication 
across an RDMA network that uses network round-trip latency modulation 
in the remote memory of one of the nodes (intermediary) within an RDMA cluster.
The sender software performs the latency modulation that the receiver 
software can detect and decode by issuing periodic probe packets to 
the intermediary node.

The [full paper](https://www.usenix.org/conference/woot20/presentation/ustiugov) by Ustiugov, Petrov, Katebzadeh, and Grot is published 
at the USENIX Workshop on Offensive Technologies (WOOT) co-located with USENIX 
Security Symposium 2020.

## Disclaimer
This code is provided as-is. You are responsible for protecting yourself, 
your property and data, and others from any risks caused by this code. 
This code may not detect vulnerabilities of your applications. 
This code is only for testing purposes. Use it only on test systems which 
contain no sensitive data.

## Setup instructions
1. Turn on 1GB pages on the intermediary. Allocating 6 pages was enough
for all our experiments.

2. Install dependencies and load modules on all the machines:
```
./install_load_modules.sh
```

3. Install distbenchr (we provide a copy in this repository) on the intermediary. 
This is the machine from which one will drive the proof-of-concept experiment. 
[Source](https://github.com/marioskogias/distbenchr)

4. Set up passwordless ssh between machines.

5. Download this code on intermediary.

6. Set channel parameters in `driver.py`, namely num_sender_qps, num_sender_requests_per_qp,
num_receiver_qps, intermediary_machine, num_sender_reps, and results_file.

7. Set the IP addresses of all three machines, and the path to the code
on the intermediary in `fabfile.py`.

