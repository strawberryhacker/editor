.PHONY: all test
all:
	@gcc -Wall -ggdb -Wno-unused-function -Wno-unused-variable editor.c -o editor
	@./editor
	@rm -f editor

test:
	@gcc -Wall -ggdb -Wno-unused-function -Wno-unused-variable editor.c -o editor

debug: test
	@gdb -ex run editor


