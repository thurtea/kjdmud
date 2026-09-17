/*
 * if "global include file" is specified in the config file, this header
 * file is automatically included by all objects; otherwise, you have to
 * #include it manually
 */
#ifndef __CONFIG_H__
#define __CONFIG_H__

// AMLP note: stock Lil ships this file empty. inherit/base.c uses
// the staticv macro (globals.h) without including globals.h itself, and
// nothing else auto-supplies it, so compiling base.c standalone fails
// (confirmed against real fluffos-2.9-ds2.08's own lex.c/simulate.c:
// real MudOS resets its own macro table at the start of every single
// file compile too -- free_defines() in start_new_file() -- so this is
// not a driver gap, base.c would hit the same problem under real
// unmodified FluffOS with this same stock config). Pulling globals.h in
// here, the one place "global include file" (etc/config.test's own
// "global include file : <config.h>") actually points, is the smallest
// fix that matches this file's own header comment: this is exactly the
// customization point it describes.
#include <globals.h>

#endif
