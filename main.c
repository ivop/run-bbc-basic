/*
 * Run BBC BASIC
 *
 * Copyright © 2025 by Ivo van Poorten
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS “AS IS”
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#define _DEFAULT_SOURCE 1
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>
#include <time.h>
#include <sys/select.h>
#include <sys/time.h>
#include <signal.h>
#include <setjmp.h>
#include <ctype.h>
#include <readline/readline.h>
#include <readline/history.h>
#include "fake6502/fake6502.h"

#ifdef __GNUC__
#define UNUSED __attribute__((unused))
#else
#define UNUSED
#endif

static sigjmp_buf jump_buffer;
static void sig_handler(int _ UNUSED) {
#if 0
    rl_stuff_char('O');
    rl_stuff_char('L');
    rl_stuff_char('D');
    rl_stuff_char('\n');
#endif
    siglongjmp(jump_buffer,1);
}
static void sig_handler2(int _ UNUSED) {
    exit(0);
}

// ----------------------------------------------------------------------------

#define TRAP 0x02           // 6502 KIL instruction, trap execution

static const uint16_t lomem = 0x0800;
static const uint16_t himem = 0xb800;

#define ESCFLG 0xff

#define basic_start 0xb800
#define mos_start   0xff00

static uint8_t mem[65536];
static uint8_t basic[16384];
static uint8_t mos[256];

static struct timeval start_time;

#define NHANDLES 6

static FILE *handles[NHANDLES];   // return as handle 1-NHANDLES
static char fmode[NHANDLES];

// ----------------------------------------------------------------------------

static struct termios orig_termios;

char *CLEAR     = "c";
char *HOME      = "[H";

static void reset_terminal_mode(void) {
    tcsetattr(0, TCSANOW, &orig_termios);
}

static int kbhit() {
    struct timeval tv = { 0L, 0L };
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(0, &fds);
    return select(1, &fds, NULL, NULL, &tv) > 0;
}

static char getkey() {
    char keybuf[32];
    while (!kbhit()) ;
    int len UNUSED = read(0, keybuf,32);;
    return keybuf[0] & 0x7f;
}

static void save_termios(void) {
    tcgetattr(0, &orig_termios);
}

static void make_term_raw(void) {
    struct termios new_termios;
    atexit(reset_terminal_mode);
    memcpy(&new_termios, &orig_termios, sizeof(new_termios));
    cfmakeraw(&new_termios);
    tcsetattr(0, TCSANOW, &new_termios);
}

// ----------------------------------------------------------------------------

uint8_t read6502(uint16_t a) {
    if (a >= basic_start && a < (basic_start + sizeof(basic)))
        return basic[a-basic_start];
    else if (a >= mos_start)
        return mos[a-mos_start];
    else
        return mem[a];
}

void write6502(uint16_t a, uint8_t v) {
    mem[a] = v;
}

// ----------------------------------------------------------------------------

static void load_rom(uint8_t *buf, char *fname, int size) {
    FILE *f = fopen(fname, "rb");
    if (!f) {
        fprintf(stderr, "unable to open %s\n", fname);
        exit(1);
    }
    if (fread(buf, size, 1, f) != 1) {
        fprintf(stderr, "unable to read %d bytes from %s\n", size, fname);
        exit(1);
    }
}

// ----------------------------------------------------------------------------

static inline void clear_carry(void) {
    setP(getP() & ~1);
}

static inline void set_carry(void) {
    setP(getP() | 1);
}

// ----------------------------------------------------------------------------

static void OSBYTE(void) {
    switch (A) {
    case 0x7e:
        mem[ESCFLG] = A;
        break;
    case 0x7f:      // check EOF on opened file, X is file handle
        if (X < 1 || X > NHANDLES || !handles[X-1]) {
            puts("Channel");
        } else {
            int v = fgetc(handles[X-1]);
            if (v < 0) {
                X = 0xff;   // EOF reached
            } else {
                ungetc(v, handles[X-1]);
                X = 0;
            }
        }        
        break;
    case 0x81: {    // Read key with time limit
        uint16_t timeout = X + (Y<<8);
        struct timeval now;
        gettimeofday(&now, NULL);
        now.tv_sec -= start_time.tv_sec;
        now.tv_usec -= start_time.tv_usec;
        uint64_t v = now.tv_sec * 100 + (now.tv_usec / 10000);
        make_term_raw();
        while (1) {
            if (kbhit()) {
                X = getkey();
                Y = 0;
                if (X == 0x1b) {
                    Y = 0x1b;
                    mem[ESCFLG] = 0xff;
                    set_carry();
                } else {
                    clear_carry();
                }
                reset_terminal_mode();
                return;
            }
            gettimeofday(&now, NULL);
            now.tv_sec -= start_time.tv_sec;
            now.tv_usec -= start_time.tv_usec;
            uint64_t w = now.tv_sec * 100 + (now.tv_usec / 10000);
            if (w-v > timeout) break;
        }
        Y = 0xff;
        set_carry();
        reset_terminal_mode();
        }
        break;
    case 0x82:      // Read machine high order address
        X = Y = 0xff;
        break;
    case 0x83:      // Get LOMEM in YX
        X = lomem & 0xff;
        Y = lomem >> 8;
        break;
    case 0x84:      // Get HIMEM in YX (bottom of display memory)
        X = himem & 0xff;
        Y = himem >> 8;
        break;
    case 0x85:      // read bottom of display memory if given mode was selected
                    // X=mode number, return YX=address
        X = himem & 0xff;
        Y = himem >> 8;
        break;
    case 0x86:      // Read POS and VPOS, return X=horpos, Y=verpos
        X = Y = 0;  // not implemented, vt100 [6n if you really want to
        break;
    case 0xda:      // read/write VDU queue
        break;

    case 0x80:      // Read ADC channel (ADVAL) or get buffer status
                    // X=$FF keyboard buffer, result in X,
                    // rest, return X=Y=0
    default:
        printf("Unhandled OSBYTE A=&%02x, X=&%02x, Y=&%02x\n", A, X, Y);
        break;
        exit(1);
    }
}

// ----------------------------------------------------------------------------

static void OSWRCH(void) {
    if (A==0x08) putchar(0x7f);
    else if (A == 0x0c) {
        puts(CLEAR);
        puts(HOME);
    }
    else if (A==0x0a || A==0x0d || A==0x09 || isprint(A)) putchar(A);
    fflush(stdout);
}

// ----------------------------------------------------------------------------

static void OSWORD(void) {
    uint16_t ptr = (Y<<8) | X;

    switch (A) {
    case 0x00: {     // Read line from input into memory
        uint16_t buf = mem[ptr+0] + (mem[ptr+1]<<8);
        uint8_t len = mem[ptr+2];
        uint8_t min = mem[ptr+3];
        uint8_t max = mem[ptr+4];
        char *lineptr = NULL;

        while (!(lineptr = readline(""))) {
            clearerr(stdin);    // ignore ctrl-D
        }

        if (strlen(lineptr) > 0) add_history(lineptr);
        else putchar('\n');

        int j = 0;
        for (unsigned i=0; i<strlen(lineptr); i++) {
            if (lineptr[i] < min || lineptr[i] > max) continue;
            if (lineptr[i] == 27) {
                mem[ESCFLG] = 0xff;
                Y = j;
                set_carry(); // ESC condition
                return;
            }
            mem[buf+j] = lineptr[i];
            j++;
            if (j == len) {
                mem[buf+j-1] = 0x0d;
                Y = j;
                clear_carry();
                return;
            }

        }
        mem[buf+j] = 0x0d;
        Y = j+1;
        clear_carry();
        return;
        break;
        }
    case 0x01: {            // Get system clock in centiseconds
            struct timeval now;
            gettimeofday(&now, NULL);
            now.tv_sec -= start_time.tv_sec;
            now.tv_usec -= start_time.tv_usec;
            uint64_t v = now.tv_sec * 100 + (now.tv_usec / 10000);
            uint16_t ptr = X + (Y<<8);
            mem[ptr+0] = (v>> 0) & 0xff;
            mem[ptr+1] = (v>> 8) & 0xff;
            mem[ptr+2] = (v>>16) & 0xff;
            mem[ptr+3] = (v>>24) & 0xff;
            mem[ptr+4] = (v>>32) & 0xff;
        }
        break;
    case 0x02: {            // Write system clock in centiseconds
            struct timeval now;
            gettimeofday(&now, NULL);
            uint16_t ptr = X + (Y<<8);
            uint64_t new = (mem[ptr+0]<< 0) +
                           (mem[ptr+1]<< 8) +
                           (mem[ptr+2]<<16) +
                           (mem[ptr+3]<<24) +
                           ((uint64_t)mem[ptr+4]<<32);
            start_time.tv_sec = now.tv_sec - new/100;
            start_time.tv_usec = now.tv_usec - (new%100)*10000;
        }
        break;
    case 0x07:  // SOUND
        break;
    case 0x08:  // ENVELOPE
        break;
    case 0x09: {            // Read pixel value at XY+ (two words X,Y)
            uint16_t ptr = X + (Y<<8);
            mem[ptr+4] = 0xff;          // return off screen
        }
        break;
    default:
        printf("Unhandled OSWORD A=&%02x, X=&%02x, Y=&%02x\n", A, X, Y);
        exit(1);
    }
}

// ----------------------------------------------------------------------------

static void OSNEWL(void) {
    putchar('\n');
    fflush(stdout);
}

// ----------------------------------------------------------------------------

static void OSASCI(void) {
    if (A == 0x0d) OSNEWL();
    else if (A == 0x0c) {
        puts(CLEAR);
        puts(HOME);
    }
    else OSWRCH();
}

// ----------------------------------------------------------------------------

static void OSRDCH(void) {
    make_term_raw();
    A = getkey();
    reset_terminal_mode();
    if (A == 0x1b) {
        mem[ESCFLG] = 0xff;
        set_carry();
    } else {
        clear_carry();
    }
}

// ----------------------------------------------------------------------------

struct pblock {
    uint16_t fnamep;
    uint32_t load;
    uint32_t exec;
    uint32_t save;
    uint32_t end;
};

static inline uint32_t GET16LE(uint16_t p) {
    return mem[p+0] + (mem[p+1]<<8);
}

static inline uint32_t GET32LE(uint16_t p) {
    return mem[p+0] + (mem[p+1]<<8) + (mem[p+2]<<16) + (mem[p+3]<<24);
}

static void OSFILE(void) {
    FILE *f;
    struct pblock pblock;

    uint16_t ptr = X + (Y<<8);

    pblock.fnamep = mem[ptr] + (mem[ptr+1]<<8);

    int i;
    for (i=0; mem[pblock.fnamep+i] != 0x0d; i++) ;
    char fname[i+1];
    for (i=0; mem[pblock.fnamep+i] != 0x0d; i++)
        fname[i] = mem[pblock.fnamep+i];
    fname[i] = 0;

    pblock.load = GET16LE(ptr+2);
    pblock.exec = GET16LE(ptr+6);
    pblock.save = GET16LE(ptr+10);
    pblock.end  = GET16LE(ptr+14);

    // unclear how to return error?
    // not sure if pblock.end is inclusive or not

    switch (A) {
    case 0x00:          // Save file with pblock info
        A = 0;
        if (!(f = fopen(fname, "wb"))) {
            printf("Unable to open file '%s'\n", fname);
            fflush(stdout);
            return;
        }
        // length is end-save, not +1
        if (fwrite(&mem[pblock.save], pblock.end - pblock.save, 1, f)!=1){
            puts("Error writing file");
            fflush(stdout);
        } else { 
            A = 0x01;   // File found
        }
        fclose(f);
        break;
    case 0xff: {        // Load file with pblock info
        int a = pblock.exec & 0xff ? pblock.exec : pblock.load;
        int v;
        A = 0;
        if (!(f = fopen(fname, "rb"))) {
            printf("Unable to open file '%s'\n", fname);
            fflush(stdout);
            return;
        }
        v = fgetc(f);
        while (v >= 0) {
            mem[a++] = v;
            v = fgetc(f);
        }
        fclose(f);
        }
        break;

    default:
        printf("OSFILE A=%02x not handled\n", A);
        exit(1);
    }
}

// ----------------------------------------------------------------------------

static void open_file_handle(char *fname, char *mode, char cmode) {
    FILE *f;
    int i;
    for (i=0; i<NHANDLES; i++) {
        if (handles[i] == NULL) break;
    }
    if (i == NHANDLES) {
        puts("Too many open files");
        A = 0x00;           // could not open error
        return;
    }
    if (!(f = fopen(fname, mode))) {
        printf("Unable to open file '%s'\n", fname);
        A = 0x00;
        return;
    }
    handles[i] = f;
    fmode[i] = cmode;
    A = i + 1;          // map 0-(NHANDLES-1) to  1-NHANDLES
    fseek(handles[i], 0, SEEK_SET);
}

static void OSFIND(void) {
    if (!A) {                       // close file
        if (Y >= 1 && Y <= NHANDLES) {     // close handle Y
            if (handles[Y-1]) {
                fclose(handles[Y-1]);
                handles[Y-1] = NULL;
            } else {
                puts("Channel");
            }
        } else if (!Y) {            // close all handles
            for (int i=0; i<NHANDLES; i++) {
                if (handles[i]) {
                    fclose(handles[i]);
                    handles[i] = NULL;
                }
            }
        } else {
            puts("Channel");
        }
    } else {                        // open file
        uint16_t ptr = X + (Y<<8);
        int i;
        for (i=0; mem[ptr+i] != 0x0d; i++) ;
        char fname[i+1];
        for (i=0; mem[ptr+i] != 0x0d; i++)
            fname[i] = mem[ptr+i];
        mem[ptr+i] = 0;
        switch (A) {
        case 0x40:      // open for input
            open_file_handle(fname, "rb", 'R');
            break;
        case 0x80:      // open for output
            open_file_handle(fname, "wb", 'W');
            break;
        case 0xc0:      // open for update / random access
            open_file_handle(fname, "a+", 'A');
            break;
        }
    }
}

// ----------------------------------------------------------------------------

static void OSBPUT(void) {
    if (Y < 1 || Y > NHANDLES || fmode[Y-1] == 'R' || !handles[Y-1]) {
        puts("Channel");
    } else {
        fputc(A, handles[Y-1]);
    }
}

// ----------------------------------------------------------------------------

static void OSBGET(void) {
    if (Y < 1 || Y > NHANDLES || fmode[Y-1] == 'W' || !handles[Y-1]) {
        puts("Channel");
    } else {
        A = fgetc(handles[Y-1]);
        if (feof(handles[Y-1])) {
            set_carry();
        } else {
            clear_carry();
        }
    }
}

// ----------------------------------------------------------------------------

static void OSARGS(void) {
    if (!Y) {
        puts("unhandled OSARGS Y==0");
    } else {
        long v = 0;
        switch (A) {
        case 0x00:      // PTR#
            if (handles[Y-1])
                v = ftell(handles[Y-1]);
            else
                puts("Channel");
            break;
        case 0x01:      // PTR#=
            v = GET32LE(X);
            if (handles[Y-1])
                fseek(handles[Y-1], v, SEEK_SET);
            else
                puts("Channel");
            break;
        case 0x02:      // EXT#
            if (handles[Y-1]) {
                long x = ftell(handles[Y-1]);
                fseek(handles[Y-1], 0, SEEK_END);
                v = ftell(handles[Y-1]);
                fseek(handles[Y-1], x, SEEK_SET);
            }
            else
                puts("Channel");
            break;
        default:
            puts("unhandled OSARGS Y!=0");
            break;
        }
        if (A == 0 || A == 2) {
            mem[X]   =  v        & 0xff;
            mem[X+1] = (v >>  8) & 0xff;
            mem[X+2] = (v >> 16) & 0xff;
            mem[X+3] = (v >> 24) & 0xff;
        }
    }
}

// ----------------------------------------------------------------------------

static void starloadsave(char *args, bool save) {
    // silly hack because of char fname[] being variable
    if (false) {
error:
    puts("Syntax error");
    return;
    }

    long start, end;
    char *p = args;
    while (isspace(*p)) p++;

    if (*p++ != '"') goto error;
    int s = 0;
    char *q = p;
    while (*p != 0 && *p != '"') p++, s++;
    if (!*p) goto error;
    char fname[s+1];
    for (int i=0; i<s; i++) fname[i]=q[i];
    fname[s] = 0;
    p++;

    while (isspace(*p)) p++;

    if (!*p) goto error;

    start = strtol(p, &q, 16);
    p = q;

    if (save) {
        if (!*p) goto error;
        if (!isspace(*p)) goto error;

        while (isspace(*p)) p++;

        if (!*p) goto error;
        end = strtol(p, &q, 16);
        p = q;
    }

    while (isspace(*p)) p++;

    if (*p) goto error;         // should be end of string now

    // execute command

    if (!save) end = 0xffff;

    if (start < 0 || start > 0xffff) {
        puts("start out of range");
        return;
    }
    if (save && (end < 0 || end > 0xffff || end < start)) {
        puts("end out of range");
        return;
    }

    FILE *f = fopen(fname, save ? "wb" : "rb");
    if (!f) {
        puts("unable to open file");
        return;
    }

    long len = end-start+1, r UNUSED;

    if (save) fwrite(&mem[start], 1, len, f);
    else      r = fread(&mem[start], 1, len, f);

    fclose(f);

    return;

}

static void OSCLI(void) {
    uint16_t ptr = X + (Y<<8);
    int i;
    for (i=0; mem[ptr+i] != 0x0d; i++) ;
    char line[i+1];
    for (i=0; mem[ptr+i] != 0x0d; i++)
        line[i] = mem[ptr+i];
    line[i] = 0;
    if (!strcmp(line, "*QUIT") || !strcmp(line, "*quit")) exit(0);
    else if (!strncmp(line, "*SAVE", 5)) starloadsave(line+5, true);
    else if (!strncmp(line, "*LOAD", 5)) starloadsave(line+5, false);
    else if (i>1) i = system(line+1);
}

// ----------------------------------------------------------------------------

static void trap(void) {
    //printf("trap: PC=%04x, A=%02x, X=%02x, Y=%02x\n", PC, A, X, Y);
    switch (PC) {
    case 0xffce:    OSFIND();   break;
    case 0xffd4:    OSBPUT();   break;
    case 0xffd7:    OSBGET();   break;
    case 0xffda:    OSARGS();   break;
    case 0xffdd:    OSFILE();   break;
    case 0xffe0:    OSRDCH();   break;
    case 0xffe3:    OSASCI();   break;
    case 0xffe7:    OSNEWL();   break;
    case 0xffee:    OSWRCH();   break;
    case 0xfff1:    OSWORD();   break;
    case 0xfff4:    OSBYTE();   break;
    case 0xfff7:    OSCLI();    break;
    default:
        printf("unhandled trap at %04x\n", PC);
        exit(1);
    }

    PC++;       // skip over KIL, do RTS
}

// ----------------------------------------------------------------------------

static char *words[] = {
    "AND", "ABS", "ACS", "ADVAL", "ASC", "ASN", "ATN", "AUTO", "BGET",
    "BPUT", "COLOUR", "COLOR", "CALL", "CHAIN", "CHR$", "CLEAR", "CLOSE",
    "CLG", "CLS", "COS", "COUNT", "DATA", "DEG", "DEF", "DELETE", "DIV",
    "DIM", "DRAW", "ENDPROC", "END", "ENVELOPE", "ELSE", "EVAL", "ERL",
    "ERROR", "EOF", "EOR", "ERR", "EXP", "EXT", "FOR", "FALSE", "FN", "GOTO",
    "GET$", "GET", "GOSUB", "GCOL", "HIMEM", "INPUT", "IF", "INKEY$",
    "INKEY", "INT", "INSTR", "LIST", "LINE", "LOAD", "LOMEM", "LOCAL",
    "LEFT$", "LEN", "LET", "LOG", "LN", "MID$", "MODE", "MOD", "MOVE", "NEXT",
    "NEW", "NOT", "OLD", "ON", "OFF", "OR", "OPENIN", "OPENOUT", "OPENUP",
    "OSCLI", "PRINT", "PAGE", "PTR", "PI", "PLOT", "POINT", "PROC", "POS",
    "RETURN", "REPEAT", "REPORT", "READ", "REM", "RUN", "RAD", "RESTORE",
    "RIGHT$", "RND", "RENUMBER", "STEP", "SAVE", "SGN", "SIN", "SQR", "SPC",
    "STR$", "STRING$", "SOUND", "STOP", "TAN", "THEN", "TO", "TAB", "TRACE",
    "TIME", "TRUE", "UNTIL", "USR", "VDU", "VAL", "VPOS", "WIDTH",
    NULL
};

static char *completion_generator(const char *text, int state) {
    static size_t match_index = 0, len = 0;
    char *word;
    if (!state) {
        match_index = 0;
        len = strlen(text);
    }
    while ((word = words[match_index++])) {
        if (!strncmp(word, text, len)) return strdup(word);
    }
    return NULL;
}

static char ** completer(const char *text, int start UNUSED, int end UNUSED) {
    rl_attempted_completion_over = 1;
    rl_completion_append_character = 0;   // no space after completion
    return rl_completion_matches(text, completion_generator);
}

// ----------------------------------------------------------------------------

int main(int argc UNUSED, char **argv UNUSED) {
    bool running = true;

    rl_attempted_completion_function = completer;
    if (RL_VERSION_MAJOR >= 8)
        rl_variable_bind ("enable-bracketed-paste", "off");

    load_rom(mos, "toprom/top.rom", sizeof(mos));
    load_rom(basic, "roms/basic310hi.rom", sizeof(basic));

    save_termios();

    sigsetjmp(jump_buffer, 1);
    signal(SIGINT, sig_handler);
    signal(SIGTSTP, sig_handler2);

    gettimeofday(&start_time, NULL);

    putchar('\n');
    reset6502();

    while (running) {
//        printf("%04x\n", PC);
        if (read6502(PC) == TRAP) trap();
        /* int spent = */ step6502();
    }
}
