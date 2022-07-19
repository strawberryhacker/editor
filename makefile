.PHONY: all
all:
	@gcc -Wall -Wno-unused-function editor.c -o editor
	@./editor
	@rm -f editor
