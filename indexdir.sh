#!/bin/bash
ls -a $1 | grep -v '^.dir$' >$1/.dir

