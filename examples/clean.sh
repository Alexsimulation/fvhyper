#!/bin/bash

names='advection eulerTube heatTransfer eulerNaca navier forwardStep'

for name in $names
do
    rm $name/*.vtu
    rm $name/*.pvtu
    rm $name/*.msh
    rm $name/log.txt
    rm -r $name/logs
    rm -r $name/results
    rm $name/times/*
    rm -r $name/anim
    ls */** | grep -v -e "\." -e "\makefile" | xargs -r rm
done

