#include <stddef.h>
#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

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
#define KILO_TAB 8

#define CTRL_KEY(k) ((k) & 0x1f)

enum editorKey {
    ARROW_LEFT = 1000,
    ARROW_RIGHT,
    ARROW_UP,
    ARROW_DOWN,
    HOME_KEY,
    END_KEY,
    DEL_KEY,
    PAGE_UP,
    PAGE_DOWN
};

typedef struct erow {
    int size;
    int rsize;
    char* chars;
    char* render;
} erow;

struct editorConfig {
    int mouse_x;
    int mouse_y;
    int rx;
    int screen_rows;
    int screen_cols;
    erow* rows;
    int numrows;
    int rows_off;
    int cols_off;
    char* filename;
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
            if (seq[1] >= '0' && seq[1] <= '9') {
                if (read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b';
                if (seq[2] == '~') {
                    switch (seq[1]) {
                        case '5': return PAGE_UP;
                        case '6': return PAGE_DOWN;
                        case '1': return HOME_KEY;
                        case '7': return HOME_KEY;
                        case '4': return END_KEY;
                        case '8': return END_KEY;
                        case '3': return DEL_KEY;
                    }
                }
            } else {
                switch (seq[1]) {
                    case 'A': return ARROW_UP;
                    case 'B': return ARROW_DOWN;
                    case 'C': return ARROW_RIGHT;
                    case 'D': return ARROW_LEFT;
                    case 'H': return HOME_KEY;
                    case 'F': return END_KEY;
                }
            }
        } else if (seq[0] == 'O') {
            switch (seq[1]) {
                case 'H': return HOME_KEY;
                case 'F': return END_KEY;
            }
        }

        return '\x1b';
    }

    return c;
}

int editorRowCxToRx(erow* row, int cx)
{
    int rx = 0;
    int j;
    for (j = 0; j < cx; ++j) {
        if (row->chars[j] == '\t') {
            rx += KILO_TAB - 1 + (rx % KILO_TAB);
        }
        rx++;
    }
    return rx;
}

void editorScroll()
{
    E.rx = 0;
    if (E.mouse_y < E.numrows) {
        E.rx = editorRowCxToRx(&E.rows[E.mouse_y], E.mouse_x);
    }

    if (E.mouse_y < E.rows_off) {
        E.rows_off = E.mouse_y;
    }

    if (E.mouse_y >= E.rows_off + E.screen_rows) {
        E.rows_off = E.mouse_y - E.screen_rows + 1;
    }

    if (E.rx < E.cols_off) {
        E.cols_off = E.rx;
    }

    if (E.rx >= E.cols_off + E.screen_cols) {
        E.cols_off = E.rx - E.screen_cols + 1;
    }
}

void editorDrawRows(struct abuf* ab)
{
    for (int i = 0; i < E.screen_rows; ++i) {
        int file_row = i + E.rows_off;
        if (file_row >= E.numrows) {
            if (E.numrows == 0 && i == E.screen_rows / 3) {
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
        } else {
            int len = E.rows[file_row].rsize - E.cols_off;
            if (len < 0) len = 0;
            if (len > E.screen_cols) len = E.screen_cols;
            abAppend(ab, &E.rows[file_row].render[E.cols_off], len);
        }
                
        abAppend(ab, "\x1b[K", 3);
        abAppend(ab, "\r\n", 2);
    }
}

void editorMoveCursor(int c)
{
    erow* row = (E.mouse_y >= E.numrows) ? NULL : &E.rows[E.mouse_y];

    switch (c) {
        case ARROW_LEFT:
            if (E.mouse_x != 0)E.mouse_x--;
            else if (E.mouse_y > 0) {
                E.mouse_y--;
                E.mouse_x = E.rows[E.mouse_y].size;
            }
            break;
        case ARROW_DOWN:
            if (E.mouse_y < E.numrows)E.mouse_y++;
            break;
        case ARROW_RIGHT:
            if (row && E.mouse_x < row->size) {
                E.mouse_x++;
            } else if (row && E.mouse_x == row->size) {
                E.mouse_y++;
                E.mouse_x = 0;
            }
            break;
        case ARROW_UP:
            if (E.mouse_y != 0)E.mouse_y--;
            break;
    }

    row = (E.mouse_y >= E.numrows) ? NULL : &E.rows[E.mouse_y];
    int row_len = row ? row->size : 0;
    if (E.mouse_x > row_len) {
        E.mouse_x = row_len;
    }
}

void editorDrawStatusBar(struct abuf* ab)
{
    abAppend(ab, "\x1b[7m", 4);
    char status[80];
    int len = snprintf(status, sizeof(status), "%.20s - %d lilnes", (E.filename ? E.filename : "[No name]"), E.numrows);
    if (len > E.screen_cols) len = E.screen_cols;
    abAppend(ab, status, len);

    while (len < E.screen_cols) {
        abAppend(ab, " ", 1);
        len++;
    }
    abAppend(ab, "\x1b[m", 3);
}

void editorRefreshScreen()
{
    editorScroll();

    struct abuf ab = ABUF_INIT;

    abAppend(&ab, "\x1b[?25l", 6);
    abAppend(&ab, "\x1b[H", 3);
    
    editorDrawRows(&ab);
    editorDrawStatusBar(&ab);

    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", E.mouse_y - E.rows_off + 1, E.rx - E.cols_off + 1);
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

        case HOME_KEY:
            E.mouse_x = 0;
            break;
        case END_KEY:
            if (E.mouse_y < E.numrows)
                E.mouse_x = E.rows[E.mouse_y].size;
            break;

        case PAGE_UP:
        case PAGE_DOWN:
            {
                if (c == PAGE_UP) {
                    E.mouse_y = E.rows_off;
                } else if (c == PAGE_DOWN) {
                    E.mouse_y = E.rows_off + E.screen_rows - 1;
                    if (E.mouse_y > E.numrows) E.mouse_y = E.numrows;
                }

                int times = E.screen_rows;
                while (times--) {
                    editorMoveCursor( c == PAGE_UP ? ARROW_UP : ARROW_DOWN  );
                }
            }
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
    E.numrows = 0;
    E.rows_off = 0;
    E.cols_off = 0;
    E.rows = NULL;
    E.rx = 0;
    E.filename = NULL;

    if (getWindowSize(&E.screen_rows, &E.screen_cols) == -1)
        die("getWindowsizes");
    E.screen_rows--;
}

void editorUpdateRow(erow* row)
{
    int tabs = 0;
    for (int i = 0; i < row->size; ++i) {
        if (row->chars[i] == '\t') {
            tabs++;
        }
    }

    free(row->render);
    row->render = malloc(row->size+ tabs * (KILO_TAB - 1) + 1);

    int j;
    int idx = 0;
    for (j = 0; j < row->size; ++j) {
        if (row->chars[j] == '\t') {
            row->render[idx++] = ' ';
            while (idx % KILO_TAB != 0) row->render[idx++] = ' ';
        } else {
            row->render[idx++] = row->chars[j];
        }
    }
    row->render[idx] = '\0';
    row->rsize = idx;
}

void editorAppendRow(char* s, size_t len)
{
    E.rows = realloc(E.rows, sizeof(erow) * (E.numrows + 1));

    int at = E.numrows;
    E.rows[at].size = len;
    E.rows[at].chars = malloc(len + 1);
    memcpy(E.rows[at].chars, s, len);
    E.rows[at].chars[len] = '\0';

    E.rows[at].rsize = 0;
    E.rows[at].render = NULL;
    editorUpdateRow(&E.rows[at]);

    E.numrows++;
}

void editorOpen(char* filename)
{
    free(E.filename);
    E.filename = strdup(filename);

    FILE* fp = fopen(filename, "r");
    if (!fp) die("fopen");

    char* line = NULL;
    size_t line_cap = 0;
    int line_len = 0;
    while ((line_len = getline(&line, &line_cap, fp)) != -1) {
        while (line_len > 0 && (line[line_len - 1] == '\r' || line[line_len - 1] == '\n'))
            line_len--;
        editorAppendRow(line, line_len);
    }
    free(line);
    fclose(fp);
}

int main(int argc, char* argv[])
{
    enableRawMode();
    initEditor();
    if (argc >= 2) {
        editorOpen(argv[1]);
    }
    
    while (1) {
        editorRefreshScreen();
        editorProcessKeypress();
    }

    return 0;
}
