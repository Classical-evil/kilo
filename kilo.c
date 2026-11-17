#include <asm-generic/ioctls.h>
#include <errno.h>
#include <stdio.h>
#include <ctype.h>
#include <termios.h>
#include <unistd.h>
#include <stdlib.h>
#include <termio.h>
#include <sys/ioctl.h>
#include <string.h>

#define KILO_VERSION "0.0.1"

#define CTRL_KEY(k) ((k) & 0x1f)

enum editorKey {
    ARROW_LEFT = 1000,
    ARROW_RIGHT,
    ARROW_UP,
    ARROW_DOWN
};

struct editorConfig {
    int mouse_x;
    int mouse_y;
    int screen_rows;
    int screen_cols;
    struct termios ori_termios;
};

struct abuf {
    char* b;
    int len;
};
#define ABUF_INIT {NULL, 0}

void abAppend(struct abuf* ab, const char* s, int len)
{
    char* new = realloc(ab->b, ab->len + len);
    if (new == NULL)
        return;
    
    memcpy(&new[ab->len], s, len);
    ab->b = new;
    ab->len += len;
}

void abFree(struct abuf* ab)
{
    free(ab->b);
}

struct editorConfig E;

void die(const char* s)
{
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);

    perror(s);
    exit(1);
}

int getWindowSize(int *rows, int *cols)
{
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == - 1 || ws.ws_col == 0) {
        return -1;
    } else {
        *rows = ws.ws_row;
        *cols = ws.ws_col;

        return 0;
    }
}

void disableRawMode()
{
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.ori_termios) == -1)
        die("tcsetattr");
}

void enableRawMode()
{
    if (tcgetattr(STDIN_FILENO, &E.ori_termios) == -1)
        die("tcgetattr");
    atexit(disableRawMode);

    struct termios raw = E.ori_termios;
    raw.c_iflag &= ~(IXON | ICRNL | BRKINT | INPCK | ISTRIP);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= CS8;
    raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
        die("tcsetattr");
}

int editorReadKey()
{
    int nread;
    char c;
    while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
        if (nread == -1 && errno != EAGAIN)
            die("read");
    } 

    if (c == '\x1b') {
        char seq[3];

        if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
        if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';

        if (seq[0] == '[') {
            switch (seq[1]) {
                case 'A': return ARROW_UP;
                case 'B': return ARROW_DOWN;
                case 'C': return ARROW_RIGHT;
                case 'D': return ARROW_LEFT;
            }
        }
        return '\x1b';
    }

    return c;
}

void editorDrawRows(struct abuf* ab)
{
    for (int i = 0; i < E.screen_rows; ++i) {
        
        if (i == E.screen_rows / 3) {
            char welcome[80];
            int welcome_len = snprintf(welcome, sizeof(welcome), "Kile editor -- version %s", KILO_VERSION);
            int padding = (E.screen_cols - welcome_len) / 2;
            if (padding) {
                abAppend(ab, "~", 1);
                --padding;
            }
            while (padding--)
                abAppend(ab, " ", 1);

            abAppend(ab, welcome, welcome_len);
        } else {
            abAppend(ab, "~", 1);
        }
        
        abAppend(ab, "\x1b[K", 3);
        if (i < E.screen_rows - 1) {
            abAppend(ab, "\r\n", 2);
        }
    }
}

void editorMoveCursor(int c)
{
    switch (c) {
        case ARROW_LEFT:
            if (E.mouse_x != 0)E.mouse_x--;
            break;
        case ARROW_DOWN:
            if (E.mouse_y != E.screen_rows - 1)E.mouse_y++;
            break;
        case ARROW_RIGHT:
            if (E.mouse_x != E.screen_cols - 1)E.mouse_x++;
            break;
        case ARROW_UP:
            if (E.mouse_y != 0)E.mouse_y--;
            break;
    }
}

void editorRefreshScreen()
{
    struct abuf ab = ABUF_INIT;

    abAppend(&ab, "\x1b[?25l", 6);
    abAppend(&ab, "\x1b[H", 3);
    
    editorDrawRows(&ab);

    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", E.mouse_y + 1, E.mouse_x + 1);
    abAppend(&ab, buf, strlen(buf)); 

    abAppend(&ab, "\x1b[?25h", 6);

    write(STDOUT_FILENO, ab.b, ab.len);
    abFree(&ab);
}

void editorProcessKeypress()
{
    int c = editorReadKey();
    switch (c) {
        case CTRL_KEY('q'):
            write(STDOUT_FILENO, "\x1b[2J", 4);
            write(STDOUT_FILENO, "\x1b[H", 3);

            exit(0);
            break;

        case ARROW_LEFT:
        case ARROW_DOWN:
        case ARROW_RIGHT:
        case ARROW_UP:
            editorMoveCursor(c);
            break;
    }
}

void initEditor()
{
    E.mouse_x = 0;
    E.mouse_y = 0;

    if (getWindowSize(&E.screen_rows, &E.screen_cols) == -1)
        die("getWindowsizes");
}

int main()
{
    enableRawMode();
    initEditor();
    
    while (1) {
        editorRefreshScreen();
        editorProcessKeypress();
    }

    return 0;
}
