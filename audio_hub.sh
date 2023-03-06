#!/bin/bash

cd /home/kosiu/audio_hub

audio_hub_exit=1
while [ $audio_hub_exit = 1 ]; do
	./audio_hub.py
	audio_hub_exit=$?
	echo "Exit from audio_hub.py with code $audio_hub_exit"
done

