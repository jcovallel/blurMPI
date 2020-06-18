#!/bin/bash
TIMEFORMAT=%3R
declare -a ARR=("720p.png" "1080p.png" "4k.png")
echo "Imagen,Hilos,Kernel,Tiempo" > results.txt
for i in "${ARR[@]}"; do
	for (( hilos=1; hilos < 17; hilos=hilos*2 )); do
		for (( kernel=3; kernel < 16; kernel=kernel+2 )); do
			SALIDA=$( { time ./blur-effectT "$i" "./imgout/${hilos}_BLUR$i" "$kernel" "$hilos"; } 2>&1 )
			echo "$i,$hilos,$kernel,$SALIDA" >> results.txt

		done
	done
done
exit
