#pragma once
#include <nds.h>

// Date sim: the BIG bonus "date notice" route of the original (partner select, a date that moves one
// step per bonus game with questions that raise or lower her affection, a confession after the BIG).
// Top screen: still + dialogue box on the main BG bitmap (VRAM A, video stopped). Bottom screen:
// choices on the reels' borrowed overlay layer. Text and art: tools/make_date.py.

// once per game session, after ui_init() (allocates the reaction icon sprites)
void date_init(void);
// partner select (blocking); true if a date starts. The top screen must be in static bitmap mode.
bool date_pick_partner(void);
// Date Mode: start the Ichigo stage date directly (top screen in static bitmap mode)
void date_begin_mode(void);
// affection so far (sum of the answers' UP / DOWN)
int date_score(void);
// show the "hot" variant of the scenes from now on (a bonus is hidden: Date Mode hint)
void date_set_hot(bool hot);
// next step of the date, after every bonus game (blocking while the player answers a question)
void date_advance(void);
// after the BIG: confession sequence (blocking). Returns true if she confessed.
bool date_finale(void);
// Date Mode ending (blocking): confession if won, else goodbye
void date_finale_mode(bool won);
// every frame while dating: hides the reaction icons after a while
void date_tick(void);

// Bottom-screen panels shared with the other modes (reels stopped, drawn on their borrowed overlay layer):
// a blocking choice (touch or up/down + A), and a title with up to two lines (instructions, results).
int date_menu(const char *prompt, const char *const *options, int n);
void date_panel_show(const char *title, const char *line1, const char *line2);
void date_panel(const char *title, const char *line1, const char *line2); // redraw while shown
void date_panel_hide(void);
