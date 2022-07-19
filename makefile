.PHONY: all
all:
	@gcc -Wall editor.c -o editor
	@./editor
	@rm -f editor
