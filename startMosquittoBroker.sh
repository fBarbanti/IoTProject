#!/bin/bash

sudo service mosquitto stop
echo "Clear mosquitto retain messages.."
sudo rm /var/lib/mosquitto/mosquitto.db 
echo "Start Mosquitto"
sudo service mosquitto start
echo "Mosquitto ready.."
ip4=$(/sbin/ip -o -4 addr list wlp2s0 | awk '{print $4}' | cut -d/ -f1)
mosquitto_sub -v -h $ip4 -t '#' | xargs -d$'\n' -L1 bash -c 'date "+%Y-%m-%d %T.%3N $0"'
