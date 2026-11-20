/*
 * test_cmd_editor.c — host unit tests for cmd_editor.c (the serial line editor).
 *
 * Focus: finding C4 (08-findings.md) — a full input line both HANGS the editor
 * (continue without advancing the ring read pointer) and OVERFLOWS cmd_buffer by
 * one byte (guard off-by-one lets cmd_line_len reach CMD_BUF_SIZE). The line
 * is con_line now, the editor's own (D3); cmd_buffer is the parser's.
 *
 * These tests are RED against the current source and GREEN after the fix, with
 * no hardware and no flash cycle. The hang is caught by a SIGALRM watchdog; the
 * overflow is caught by AddressSanitizer redzones on cmd_buffer[].
 */
#include "sdcc_shim.h"
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include "rtl837x_common.h"     /* CMD_BUF_SIZE */
#include <test/support.h>

/* editor state (globals in cmd_editor.c, external linkage) */
extern __xdata uint8_t cmd_line_len;
extern __xdata uint8_t con_line[];       /* the editor's own line (D3) */
extern __xdata uint8_t con_line_ready;   /* a complete line waits for cli */

extern __xdata uint8_t cmd_history[CMD_HISTORY_SIZE];
extern __xdata uint8_t cmd_history_ptr;

static void test_basic_line(void) {
    printf("[test] basic line entry + Enter\n");
    env_reset();
    feed("vlan 199\n");
    CHECK(con_line_ready == 1, "Enter marks a line ready for cli");
    CHECK(t_strcmp((char *)con_line, "vlan 199") == 0, "con_line holds the typed line");
}

static void test_backspace(void) {
    printf("[test] backspace edits the line\n");
    env_reset();
    feed("helo");
    feed("\b");            /* delete the 'o' -> wait, cursor at end: removes last char */
    feed("lo\n");
    CHECK(t_strcmp((char *)con_line, "hello") == 0, "backspace then retype yields 'hello'");
}

/* C4 part 1: a line that fills the buffer must not spin forever. */
static void test_full_line_no_hang(void) {
    char big[200];
    printf("[test] C4: overlong line does not hang the editor\n");
    env_reset();
    t_memset(big, 'a', sizeof(big));
    watchdog_arm(5, "C4 full-line hang (editor spun without advancing ring ptr)");
    feed_n(big, sizeof(big));      /* 200 printable chars, no newline */
    watchdog_disarm();
    /* Reaching here means no hang. With the fix the line caps one short of full. */
    CHECK(cmd_line_len == CMD_BUF_SIZE - 1,
          "line length caps at CMD_BUF_SIZE-1 (room reserved for NUL)");
}

/* C4 part 2: filling the buffer then Enter must not write cmd_buffer[CMD_BUF_SIZE]. */
static void test_full_line_no_overflow(void) {
    char full[CMD_BUF_SIZE];       /* 128 printable chars, exactly buffer size */
    printf("[test] C4: full line + Enter does not overflow cmd_buffer\n");
    env_reset();
    t_memset(full, 'b', sizeof(full));
    watchdog_arm(5, "C4 overflow-path hang");
    feed_n(full, sizeof(full));
    feed("\n");                    /* under the bug, writes cmd_buffer[128] -> ASan trap */
    watchdog_disarm();
    CHECK(1, "128-char line + Enter completes without ASan overflow");
}

/* U2: input while a line waits for cli stays in the RX ring; the pending
 * line is untouched, and the next one is read once cli has taken it. */
static void test_pending_line_kept(void) {
    printf("[test] U2: typing while a line is pending doesn't clobber it\n");
    env_reset();
    feed("first\n");
    feed("second\n");
    CHECK(con_line_ready == 1 && t_strcmp((char *)con_line, "first") == 0,
          "the pending line stays 'first' while 'second' is typed");
    con_line_ready = 0;            /* cli takes the line */
    feed("");
    CHECK(con_line_ready == 1 && t_strcmp((char *)con_line, "second") == 0,
          "then 'second' is read from the ring");
}

/* Up and Down over the recall ring (cmd_parser.c's cmd_save() layout:
 * entries end in '\n', the next goes at cmd_history_ptr) */
static void test_recall(void) {
    static const char h[] = "vlan 199\nget port\n";
    printf("[test] recall: up, up, down, down back to the typed line\n");
    env_reset();
    memcpy(cmd_history, h, sizeof(h) - 1);
    cmd_history_ptr = sizeof(h) - 1;
    feed("ab");
    feed("\033[A");
    CHECK(cmd_line_len == 8 && !strncmp((char *)con_line, "get port", 8), "up: the newest entry");
    feed("\033[A");
    CHECK(cmd_line_len == 8 && !strncmp((char *)con_line, "vlan 199", 8), "up: the one before");
    feed("\033[A");
    CHECK(cmd_line_len == 8 && !strncmp((char *)con_line, "vlan 199", 8), "up at the oldest: stays");
    feed("\033[B");
    CHECK(cmd_line_len == 8 && !strncmp((char *)con_line, "get port", 8), "down: the newer one");
    feed("\033[B");
    CHECK(cmd_line_len == 2 && !strncmp((char *)con_line, "ab", 2), "down past the newest: the typed line");
    feed("\033[2~x\033[3~\n");
    CHECK(t_strcmp((char *)con_line, "abx") == 0, "an unknown sequence is dropped, not typed");

    /* the ring wrapped: an entry across its end */
    env_reset();
    memcpy(cmd_history + 250, "get sy", 6);
    memcpy(cmd_history, "s\n", 2);
    cmd_history_ptr = 2;
    feed("\033[A\n");
    CHECK(t_strcmp((char *)con_line, "get sys") == 0, "up: an entry across the ring's end");
}

static void test_edit_keys(void) {
    printf("[test] left, insert, right, backspace, delete\n");
    env_reset();
    feed("ab\033[Dc\033[C\177d\033[D\033[D\033[3~\n");
    CHECK(t_strcmp((char *)con_line, "ad") == 0, "acb, then b and c deleted: ad");
}

extern __xdata uint8_t cursor;
extern char out_buf[];
extern size_t out_len;

/* the line as Enter leaves it, after keys */
static int line_is(const char *keys, const char *want) {
    env_reset();
    feed(keys);
    feed("\r");
    return con_line_ready && t_strcmp((char *)con_line, want) == 0;
}

static void test_more_keys(void) {
    char big[CMD_BUF_SIZE + 10];
    printf("[test] mid-line insert, the limit, CR/LF, ESC and CSI forms, BS at 0\n");
    CHECK(line_is("ac\033[Db", "abc"), "a key inserted mid-line");
    CHECK(line_is("abc\033[D\033[D\033[Dx", "xabc"), "and at the start");

    env_reset();
    t_memset(big, 'a', sizeof(big));
    feed_n(big, CMD_BUF_SIZE - 1);
    CHECK(cmd_line_len == CMD_BUF_SIZE - 1, "127 characters fill the line");
    feed("\033[Dz");
    CHECK(cmd_line_len == CMD_BUF_SIZE - 1 && con_line[CMD_BUF_SIZE - 3] == 'a',
          "a full line drops a key mid-line too");
    feed("\r");
    CHECK(con_line_ready && t_strlen((char *)con_line) == CMD_BUF_SIZE - 1, "Enter: the 127");

    env_reset();
    feed("a\r");
    CHECK(con_line_ready && !t_strcmp((char *)con_line, "a"), "CR ends a line");
    env_reset();
    feed("b\n");
    CHECK(con_line_ready && !t_strcmp((char *)con_line, "b"), "LF ends a line");
    env_reset();
    feed("c\r\n");
    CHECK(con_line_ready && !t_strcmp((char *)con_line, "c"), "CRLF: the line");
    con_line_ready = 0;
    feed("");
    CHECK(con_line_ready && !t_strcmp((char *)con_line, ""), "then its LF an empty line (as before)");

    env_reset();
    feed("ab");
    feed("\033");
    feed("[");
    CHECK(cmd_line_len == 2 && cursor == 2, "an ESC [ split across steps: nothing yet");
    feed("D");
    feed("x\r");
    CHECK(!t_strcmp((char *)con_line, "axb"), "its final byte, a step later: left");

    CHECK(line_is("ab\033[1;5Dx", "axb"), "ESC [ 1 ; 5 D (Ctrl-left): left, no \"5D\" typed");
    CHECK(line_is("ab\033[15~x", "abx"), "ESC [ 1 5 ~ (F5): dropped, no '~' typed");
    CHECK(line_is("ab\033[D\033[Ex", "axb"), "ESC [ E: dropped, not DEL");
    CHECK(line_is("ab\033[D\033[3~x", "ax"), "ESC [ 3 ~: DEL");
    CHECK(line_is("a\033xy", "axy"), "Alt-x (ESC x): x, and y not eaten");
    CHECK(line_is("a\033\033[Dx", "xa"), "ESC ESC [ D: the second ESC starts it");

    env_reset();
    feed("\b\177\b");
    CHECK(cmd_line_len == 0 && cursor == 0 && out_len == 0, "BS at 0: nothing, nothing printed");
    feed("a\r");
    CHECK(!t_strcmp((char *)con_line, "a"), "and the line types on");

    env_reset();
    feed("ab\033[D\033[B");
    CHECK(cmd_line_len == 2 && cursor == 1, "Down when not recalling: nothing");
}

static void test_recall_tab_pending(void) {
    static const char h[] = "get port\n";
    printf("[test] Tab during recall; keys while a line is pending\n");
    env_reset();
    memcpy(cmd_history, h, sizeof(h) - 1);
    cmd_history_ptr = sizeof(h) - 1;
    feed("ab\033[A\t");
    CHECK(cmd_line_len == 8 && !strncmp((char *)con_line, "get port", 8), "Tab on a recalled line leaves it");
    feed("\033[B");
    CHECK(cmd_line_len == 2 && !strncmp((char *)con_line, "ab", 2), "and Down still returns to the typed line");

    env_reset();
    feed("first\r");
    feed("x\033[D\033[A\ty\r");
    CHECK(con_line_ready && !t_strcmp((char *)con_line, "first"), "keys, ESC sequences and Tab wait while a line is pending");
    con_line_ready = 0;
    feed("");
    CHECK(con_line_ready && !t_strcmp((char *)con_line, "yx"), "then they edit the next line: x, left, Tab, y");
}

int main(void) {
    printf("== cmd_editor host tests ==\n");
    test_basic_line();
    test_backspace();
    test_full_line_no_hang();
    test_full_line_no_overflow();
    test_pending_line_kept();
    test_recall();
    test_edit_keys();
    test_more_keys();
    test_recall_tab_pending();
    printf("\n%d checks, %d failed\n", tests_run, tests_failed);
    return tests_failed ? 1 : 0;
}
