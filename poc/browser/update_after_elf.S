/* Copyright (C) 2026 BrowserPS5 contributors. GPL-3.0-or-later.

   update_after.elf (../update_after/main.c) inside the app (BrowserPS5 RN): with a newer release's installer
   fetched and checked, the browser sends it to the console's payload loader, then leaves as "Sair" does
   (main.cpp, update_apply()). */

	.section .rodata
	.balign 16
	.global update_after_elf
	.type update_after_elf, @object
update_after_elf:
	.incbin "update_after.elf"
	.global update_after_elf_end
update_after_elf_end:
	.size update_after_elf, . - update_after_elf
