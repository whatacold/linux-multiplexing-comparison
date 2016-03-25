all: hb dummy mc

mc: multiplexing_compare.c
	gcc multiplexing_compare.c -o multiplexing_compare

hb: heartbeatd.c
	gcc heartbeatd.c -o heartbeatd

dummy: dummyd.c
	gcc dummyd.c -o dummyd

clean:
	rm -f multiplexing_compare heartbeatd dummyd
