#include "yoc.h"
#include <string.h>
#include <stdio.h>
static const unsigned char SCROLLBAR_CHAR[] = "¦";
static void render_main(void);
static void render_rows(void);
static void render_status_bar(void);
static void ensure_screen_buffer(void);
static void draw_line(size_t row, const unsigned char *line, size_t len);
static char *rowbuf = NULL;
static size_t rowbuf_cap = 0;
static char *spaces = NULL;
static size_t spaces_cap = 0;
static size_t lineno_pad = 0;
static bool_t prev_scrollbar_visible = FALSE;
bool_t show_line_numbers = TRUE;
void render_refresh(void) {
	size_t digits = 1;
	size_t n;
	term_get_win_size(&editor.cols, &editor.rows);
	editor.rows = SCREEN_ROWS(editor.rows);
	for (n = editor.file.buffer.num_lines; n >= 10; n /= 10) ++digits;
	lineno_pad = show_line_numbers ? digits + 2 : 0;
	ensure_screen_buffer();
	render_scroll();
	render_main();
	render_status_bar();
	term_set_cursor((show_line_numbers ? lineno_pad : 0) + editor.file.cursor.rx - editor.window.x, editor.file.cursor.y - editor.window.y);
	term_show_cursor();
}
void render_scroll(void) {
	size_t margin = VSCROLL_MARGIN;
	if (editor.rows <= VSCROLL_MARGIN * 2)
		margin = 0;
	editor.file.cursor.rx = 0;
	if (editor.file.cursor.y < editor.file.buffer.num_lines)
		editor.file.cursor.rx = x_to_rx(editor.file.buffer.curr, editor.file.cursor.x);
	if (editor.file.cursor.y < editor.window.y + margin)
		editor.window.y = (editor.file.cursor.y < margin) ? 0 : editor.file.cursor.y - margin;
	else if (editor.file.cursor.y >= editor.window.y + editor.rows - margin)
		editor.window.y = editor.file.cursor.y - editor.rows + margin + 1;
	if (editor.file.cursor.rx < editor.window.x + HSCROLL_MARGIN)
		editor.window.x = (editor.file.cursor.rx < HSCROLL_MARGIN) ? 0 : editor.file.cursor.rx - HSCROLL_MARGIN;
	else if (editor.file.cursor.rx >= editor.window.x + editor.cols - (show_line_numbers ? lineno_pad : 0) - HSCROLL_MARGIN)
		editor.window.x = editor.file.cursor.rx - (editor.cols - (show_line_numbers ? lineno_pad : 0)) + HSCROLL_MARGIN + 1;
	if (editor.file.buffer.curr != editor.top_line) {
		Line *line = editor.file.buffer.begin;
		size_t y = 0;
		while (y < editor.window.y && line->next) {
			line = line->next;
			y++;
		}
		editor.top_line = line;
	}
}
static void render_main(void) {
	term_hide_cursor();
	term_set_cursor(0, 0);
	render_rows();
}
static void render_rows(void) {
	Line *line = editor.top_line;
	size_t y;
	size_t digits = lineno_pad ? lineno_pad - 2 : 1;
	bool_t scrollbar = (editor.file.buffer.num_lines > editor.rows);
	size_t bar_height = 0;
	size_t bar_start = 0;
	size_t total_lines = 0;
	size_t scroll_pos = 0;
	size_t max_scroll_pos = 0;
	size_t avail_cols;
	if (scrollbar) {
		total_lines = editor.file.buffer.num_lines;
		bar_height = (editor.rows * editor.rows) / total_lines;
		if (bar_height == 0) bar_height = 1;
		scroll_pos = editor.window.y;
		max_scroll_pos = total_lines - editor.rows;
		if (scroll_pos > max_scroll_pos) {
			scroll_pos = max_scroll_pos;
		}
		bar_start = (scroll_pos * (editor.rows - bar_height)) / max_scroll_pos;
	}
	if (!scrollbar && prev_scrollbar_visible) {
		term_clear_screen();
		if (editor.screen_lens) {
			size_t r;
			for (r = 0; r < editor.screen_rows; ++r)
				editor.screen_lens[r] = 0;
		}
	}
	if (!rowbuf)
		ensure_screen_buffer();
	avail_cols = editor.cols - (scrollbar ? 1 : 0);
	for (y = 0; y < editor.rows; ++y) {
		size_t row_len = 0;
		rowbuf[0] = '\0';
		if (line && show_line_numbers) {
			size_t lnum = editor.window.y + y + 1;
			row_len = (size_t)snprintf(rowbuf, rowbuf_cap, " %*lu ", (int)digits, (unsigned long)lnum);
		}
		if (!line) {
			if (y == editor.rows / 3 && editor.file.buffer.num_lines == 1 && editor.file.buffer.begin->len == 0) {
				char msg[32];
				size_t welcomelen;
				size_t padding;
				size_t pos;
				snprintf(msg, sizeof(msg), "yoc ~ %s", YOC_VERSION);
				welcomelen = strlen(msg);
				padding = (avail_cols > welcomelen) ? (avail_cols - welcomelen) / 2 : 0;
				pos = 0;
				if (padding) {
					rowbuf[pos++] = '~';
					--padding;
				}
				while (padding-- > 0 && pos < avail_cols)
					rowbuf[pos++] = ' ';
				if (pos + (size_t)welcomelen > avail_cols)
					welcomelen = (size_t)(avail_cols - pos);
				memcpy(rowbuf + pos, msg, (size_t)welcomelen);
				pos += (size_t)welcomelen;
				rowbuf[pos] = '\0';
				row_len = pos;
			} else {
				rowbuf[0] = '~';
				rowbuf[1] = '\0';
				row_len = 1;
			}
		} else {
			const unsigned char *s = line->s;
			size_t i, width, pos = row_len;
			size_t text_cols = avail_cols - (show_line_numbers ? lineno_pad : 0);
			if (editor.window.x > 0) {
				i = width_to_length(s, editor.window.x);
				width = editor.window.x;
			} else {
				i = 0;
				width = 0;
			}
			while (i < line->len && width < editor.window.x + text_cols) {
				unsigned char c = s[i];
				size_t char_len = 1;
				if (c == '\t') {
					size_t spaces_to_add = editor.tabsize - (width & (editor.tabsize - 1));
					size_t k;
					for (k = 0; k < spaces_to_add; ++k) {
						if (width >= editor.window.x && pos < editor.cols * MAXCHARLEN)
							rowbuf[pos++] = ' ';
						++width;
					}
					i += 1;
					continue;
				}
				if (!is_continuation_byte(c)) {
					size_t char_width;
					if (c < 0x80u) {
						char_len = 1;
						char_width = 1;
					} else {
						char_len = utf8_len(c);
						if (char_len == 0 || i + char_len > line->len)
							char_len = 1;
						char_width = char_display_width(s + i);
					}
					if (char_len == 0 || i + char_len > line->len)
						char_len = 1;
					if (width + char_width > editor.window.x + text_cols)
						break;
					if (width >= editor.window.x && pos + char_len <= editor.cols * MAXCHARLEN) {
						memcpy(rowbuf + pos, s + i, char_len);
						pos += char_len;
					}
					width += char_width;
				}
				i += char_len;
			}
			rowbuf[pos] = '\0';
			row_len = pos;
			line = line->next;
		}
		draw_line(y, (unsigned char *)rowbuf, row_len);
		if (scrollbar) {
			term_set_cursor(editor.cols - 1, y);
			if (y >= bar_start && y < bar_start + bar_height)
				term_write(SCROLLBAR_CHAR, sizeof(SCROLLBAR_CHAR) - 1);
			else
				term_write((const unsigned char *)" ", 1);
		}
	}
	prev_scrollbar_visible = scrollbar;
}
static void render_status_bar(void) {
	term_set_cursor(0, editor.rows);
	status_print();
}
static void ensure_screen_buffer(void) {
	size_t r;
	const size_t rows_needed = editor.rows + 1;
	bool_t size_changed = !(editor.screen_lines && editor.screen_rows == rows_needed && editor.screen_cols == editor.cols);
	if (!size_changed) return;
	term_clear_screen();
	if (editor.screen_lines) {
		for (r = 0; r < editor.screen_rows; ++r)
			free(editor.screen_lines[r]);
		free(editor.screen_lines);
	}
	if (editor.screen_lens) {
		free(editor.screen_lens);
	}
	editor.screen_lines = (char **)xmalloc(rows_needed * sizeof(char *));
	editor.screen_lens = (size_t *)xmalloc(rows_needed * sizeof(size_t));
	for (r = 0; r < rows_needed; ++r) {
		editor.screen_lines[r] = (char *)xmalloc((editor.cols * MAXCHARLEN) + 1);
		editor.screen_lines[r][0] = '\0';
		editor.screen_lens[r] = 0;
	}
	editor.screen_rows = rows_needed;
	editor.screen_cols = editor.cols;
	if (rowbuf_cap < (editor.cols * MAXCHARLEN) + 1) {
		rowbuf_cap = (editor.cols * MAXCHARLEN) + 1;
		rowbuf = (char *)xrealloc(rowbuf, rowbuf_cap);
	}
	if (spaces_cap < editor.cols + 1) {
		spaces_cap = editor.cols + 1;
		spaces = (char *)xrealloc(spaces, spaces_cap);
		memset(spaces, ' ', editor.cols);
		spaces[editor.cols] = '\0';
	}
}
static void draw_line(size_t row, const unsigned char *line, size_t len) {
	size_t prev_len;
	size_t i;
	size_t rx_common;
	size_t new_width, clear_from, clear_to;
	if (row >= editor.screen_rows) return;
	prev_len = editor.screen_lens[row];
	if (prev_len == len && memcmp(editor.screen_lines[row], line, len) == 0) return;
	i = 0;
	rx_common = 0;
	while (i < prev_len && i < len) {
		size_t char_len, prev_char_len;
		unsigned char c = (unsigned char)editor.screen_lines[row][i];
		unsigned char new_c = line[i];
		if (c != new_c) break;
		if (c == '\t') {
			rx_common += editor.tabsize - (rx_common % editor.tabsize);
			i++;
			continue;
		}
		char_len = utf8_len(c);
		prev_char_len = utf8_len(new_c);
		if (char_len == 0 || prev_char_len == 0 || char_len != prev_char_len) break;
		if (i + char_len > prev_len || i + char_len > len) break;
		if (memcmp(editor.screen_lines[row] + i, line + i, char_len) != 0) break;
		rx_common += char_display_width(&line[i]);
		i += char_len;
	}
	term_set_cursor(rx_common, row);
	if (i < len)
		term_write(line + i, len - i);
	new_width = rx_common + length_to_width(line + i, len - i);
	clear_from = new_width;
	clear_to = str_width((unsigned char *)editor.screen_lines[row], prev_len);
	if (clear_from < clear_to)
		term_write((unsigned char *)spaces, clear_to - clear_from);
	if (len > editor.cols * MAXCHARLEN)
		len = editor.cols * MAXCHARLEN;
	memcpy(editor.screen_lines[row], line, len);
	editor.screen_lines[row][len] = '\0';
	editor.screen_lens[row] = len;
}
void render_free(void) {
	size_t r;
	if (rowbuf) {
		free(rowbuf);
		rowbuf = NULL;
		rowbuf_cap = 0;
	}
	if (spaces) {
		free(spaces);
		spaces = NULL;
		spaces_cap = 0;
	}
	if (editor.screen_lines) {
		for (r = 0; r < editor.screen_rows; ++r)
			free(editor.screen_lines[r]);
		free(editor.screen_lines);
		editor.screen_lines = NULL;
	}
	if (editor.screen_lens) {
		free(editor.screen_lens);
		editor.screen_lens = NULL;
	}
	editor.screen_rows = 0;
	editor.screen_cols = 0;
}
