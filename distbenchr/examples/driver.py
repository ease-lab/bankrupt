#!/usr/bin/python

import os
from fabric.tasks import execute
from fabfile import *
import distbenchr as dbr
import signal
import time

def main():
    # Normal fabric usage - synchronous
    execute(get_info)
    # Asynchronous execution
    mnt = dbr.Monitor()
    mnt.bg_execute(server, should_wait=False)
    time.sleep(1)
    mnt.bg_execute(client, should_wait=True)
    mnt.monitor() # wait for the client to finish
    mnt.killall() # kill the server as well


if __name__ == "__main__":
    # FIXME: Terminal is messed up without this
    os.setpgrp() # create new process group, become its leader
    try:
        main()
    except:
        import traceback
        traceback.print_exc()
    finally:
        os.killpg(0, signal.SIGKILL) # kill all processes in my group
