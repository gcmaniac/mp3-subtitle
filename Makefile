SHELL = cmd.exe
CC = gcc
WINDRES = windres
CFLAGS = -std=c11 -O2 -Wall -Wextra -Werror -DUNICODE -D_UNICODE -D_WIN32_WINNT=0x0A00 -Isrc
LDFLAGS = -municode -mwindows -specs=src/no-default-manifest.specs
LIBS = -lwinmm -lcomctl32 -lcomdlg32 -lshell32 -lgdi32 -luser32

TARGET = mp3-subtitle.exe
TEST_BIN = test_srt.exe

SRCS = src/main.c src/player.c src/prefs.c src/srt.c src/trackbar.c
OBJS = $(SRCS:.c=.o)
RES = src/app.res

all: $(TARGET)

$(TARGET): $(OBJS) $(RES)
	$(CC) $(LDFLAGS) -o $@ $(OBJS) $(RES) $(LIBS)

src/%.o: src/%.c
	$(CC) $(CFLAGS) -c $< -o $@

$(RES): src/app.rc src/app.manifest src/app.ico src/resource.h
	$(WINDRES) -i src/app.rc -o $(RES) -O coff

test: $(TEST_BIN)
	./$(TEST_BIN)

$(TEST_BIN): tests/test_srt.c src/srt.c src/srt.h src/prefs.c src/prefs.h src/trackbar.c src/trackbar.h
	$(CC) -std=c11 -O2 -Wall -Wextra -Werror -DUNICODE -D_UNICODE -D_WIN32_WINNT=0x0A00 -Isrc -municode tests/test_srt.c src/srt.c src/prefs.c src/trackbar.c -o $@ -lcomctl32 -luser32

clean:
	-del /Q src\*.o src\*.res $(TARGET) $(TEST_BIN) 2>nul

.PHONY: all test clean
