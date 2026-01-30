#!/bin/sh

echo changing files
for i in `find dnn | grep "_data\.[ch]" | grep -v dump_data`
do
	cat $i | sed -e 's/OPUS/OAC/g' -e 's/Opus/Oac/g' -e 's/opus_/oac_/g' -e 's/opus/oac/g' -e 's/linear_init/oaci_linear_init/' -e 's/^const WeightArray /const WeightArray oaci_/' -e 's/^int init_/int oaci_init_/' -e 's/oac_uint8 dred_/oac_uint8 oaci_dred_/' -e 's/conv2d_init/oaci_conv2d_init/' > tmp.c
	cp tmp.c $i; rm tmp.c
done
