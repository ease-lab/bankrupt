#!/usr/bin/python

from fabric.api import env, roles, run
from distbenchr import run_bg

env.roledefs = {
        'servers': ['icnals01'],
        'clients': ['icnals02'],
        }


@roles('servers', 'clients')
def get_info():
    run('hostname')

@run_bg('clients')
def client():
    run('echo "123" | nc icnals01 8080')

@run_bg('servers')
def server():
    run('while true; do nc -l 8080; done')
