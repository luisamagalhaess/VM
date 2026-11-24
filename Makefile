vm:
	gcc -Wall -Wextra -Wpedantic -std=c11 vm.c -o vm -pthread

sem_tlb_256:
	gcc -Wall -Wextra -Wpedantic -std=c11 -DUSAR_TLB=0 -DQTD_QUADROS=256 vm.c -o vm -pthread

sem_tlb:
	gcc -Wall -Wextra -Wpedantic -std=c11 -DUSAR_TLB=0 vm.c -o vm -pthread

sem_thread:
	gcc -Wall -Wextra -Wpedantic -std=c11 -DUSAR_THREADS=0 vm.c -o vm -pthread

run:
	./vm addresses.txt lru fifo

clean:
	rm -f vm correct.txt

.PHONY: vm sem_tlb_256 sem_tlb sem_thread run clean