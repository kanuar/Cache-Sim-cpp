all: setup runtime

setup:
	sudo apt-get install valgrind
	sudo sysctl kernel.perf_event_paranoid=-1
	mkdir bin
	mkdir test

runtime:
	echo [+] runtime block
	g++ -w -fopenmp src/main.cpp -o test/test
	./test/test
	echo [+] runtime block Complete

run_valgrind:
	echo [+] Testing valgrind
	echo [+] Testing code
	valgrind ./test/test
	valgrind --tool=cachegrind --cachegrind-out-file=bin/parallel1.txt --cache-sim=yes --branch-sim=yes --I1=131072,4,512 --D1=131072,4,512 ./test/test
	valgrind --tool=cachegrind --cachegrind-out-file=bin/parallel2.txt --cache-sim=yes --branch-sim=yes --I1=8192,4,512 --D1=8192,4,512 ./test/test
	valgrind --tool=cachegrind --cachegrind-out-file=bin/parallel3.txt --cache-sim=yes --branch-sim=yes --I1=4096,4,512 --D1=4096,4,512 ./test/test

clean:
	rm -r -f test
	rm -r -f bin
