ROOT	:= ../../compiler
SOURCE	:= $(ROOT)/parser/main.cc 
SOURCE	+= $(ROOT)/lexer/lexer.cc
SOURCE 	+= $(ROOT)/parser/parser.cc
CC	:= clang++
FLAGS	:= -g -D_GLIBCXX_DEBUG -O0 -std=c++17 -w 
FLAGS	+= -I $(ROOT)
TARGET	:= parser

all: $(TARGET)

$(TARGET): $(SOURCE)
	$(CC) $(FLAGS) $(SOURCE) -o $(TARGET)

clean:
	rm -f $(TARGET)
