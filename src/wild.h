// wild.h - Setup98 path patterns (portable): case-insensitive ASCII, '\' separates folders.
//   ?     one character except '\'
//   *     any run of characters except '\' (stays inside one folder)
//   **    any run of characters including '\' (any depth)
//   **\   zero or more whole folders: "**\*.log" matches "a.log" and "logs\old\a.log"
// A pattern without wildcards matches exactly that path.
#ifndef S98_WILD_H
#define S98_WILD_H

int wild_match(const char* pat, const char* path);
int wild_has(const char* pat);            // contains ? or *
int path_cmp_i(const char* a, const char* b);   // case-insensitive ASCII compare (<0, 0, >0)

#endif
