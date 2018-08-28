# CC=gcc
CFLAGS=-Wall -g

all: mrcc mrcc-map

mrcc_obj=src/mrcc.o    	   \
         src/files.o   	   \
		 src/stringutils.o \
		 src/args.o		   \
		 src/utils.o       \
		 src/tempfile.o    \
		 src/cleanup.o     \
		 src/io.o          \
		 src/safeguard.o   \
		 src/compile.o     \
		 src/exec.o        \
		 src/remote.o      \
		 src/trace.o       \
		 src/traceenv.o    \
		 src/netfsutils.o  \
		 src/mrutils.o

mrcc: $(mrcc_obj)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(mrcc_obj) $(LIBS)

mrcc-map_obj=src/mrcc-map.o    \
	         src/files.o   	   \
			 src/stringutils.o \
			 src/args.o		   \
			 src/utils.o       \
			 src/tempfile.o    \
			 src/cleanup.o     \
			 src/io.o          \
			 src/safeguard.o   \
			 src/compile.o     \
			 src/exec.o        \
			 src/remote.o      \
			 src/trace.o       \
			 src/traceenv.o    \
			 src/netfsutils.o  \
			 src/mrutils.o

mrcc-map: $(mrcc-map_obj)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(mrcc-map_obj) $(LIBS)

install:
	echo "Copy mrcc and mrcc-map to /usr/bin/:"
	mkdir -p /usr/bin
	cp ./mrcc /usr/bin/
	cp ./mrcc-map /usr/bin/
uninstall:
	rm -f /usr/bin/mrcc
	rm -f /usr/bin/mrcc-map

clean:
	rm -f mrcc $(mrcc_obj) mrcc-map $(mrcc-map_obj)

