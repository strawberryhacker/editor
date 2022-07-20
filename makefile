.PHONY: all test
all:
	@gcc -Wall -Wno-unused-function -Wno-unused-variable editor.c -o editor
	@./editor
	@rm -f editor

test:
	@gcc -Wall -Wno-unused-function -Wno-unused-variable editor.c -o editor
