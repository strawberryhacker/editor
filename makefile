.PHONY: all
all:
	@gcc -Wall -Wno-unused-function -Wno-unused-variable editor.c -o editor
	@./editor
	@rm -f editor
