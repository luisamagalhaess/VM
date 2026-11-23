vm:
	gcc -Wall -Wextra -Wpedantic -std=c11 vm.c -o vm

run:
	./vm addresses.txt lru fifo

clean:
	rm -f vm correct.txt

.PHONY: vm run clean