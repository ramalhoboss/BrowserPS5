/* Copyright (C) 2026 BrowserPS5 contributors. GPL-3.0-or-later.

   play_after.elf (../play_after/main.c) inside the app (BrowserPS5 RN): the start page's "Play" sends it to
   the console's payload loader, then leaves as "Sair" does (main.cpp, game_play()). */

	.section .rodata
	.balign 16
	.global play_after_elf
	.type play_after_elf, @object
play_after_elf:
	.incbin "play_after.elf"
	.global play_after_elf_end
play_after_elf_end:
	.size play_after_elf, . - play_after_elf
