/*Aqui van las instrucciones para ejecutar el programa, mediante comandos*/

//este es el compilador que vamos a usar
CC = gcc

//estas son las flags que vamos a usar 
CFLAGS = -std=c11 -Wall -Wextra -Werror -pthread
LDFLAGS = -lm

//si escribimos "make banco", se ejecutara el programa
all: banco

banco: banco.c banco.h
	$(CC) $(CFLAGS) -o banco banco.c $(LDFLAGS)

//esto quita el ejecutable creado al compilar banco.c y banco.h
clean:
	rm -f banco
