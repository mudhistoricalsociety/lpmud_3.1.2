#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#ifdef __STDC__
#include <memory.h>
#endif

#include "lint.h"
#include "interpret.h"
#include "object.h"
#include "config.h"
#include "exec.h"

/*
 * Swap out programs from objects.
 */

int num_swapped;
int total_bytes_swapped;
char file_name[100];

FILE *swap_file;		/* The swap file is opened once */

int total_num_prog_blocks, total_prog_block_size;

extern int d_flag;

/*
 * marion - adjust pointers for swap out and later relocate on swap in
 *   program
 *   line_numbers
 *   functions
 *   strings
 *   variable_names
 *   inherit
 *   argument_types
 *   type_start
 */
int
locate_out (prog) struct program *prog; {
    char *p = 0; /* keep cc happy */

    if (!prog) return 0;
    if (d_flag > 1) {
	debug_message ("locate_out: %lX %lX %lX %lX %lX %lX %lX %lX\n",
	    prog->program, prog->line_numbers, prog->functions,
	    prog->strings, prog->variable_names, prog->inherit,
	    prog->argument_types, prog->type_start);
    }
    prog->program	= &p[prog->program - (char *)prog];
    prog->line_numbers	= (unsigned short *)
	&p[(char *)prog->line_numbers - (char *)prog];
    prog->functions	= (struct function *)
	&p[(char *)prog->functions - (char *)prog];
    prog->strings	= (char **)
	&p[(char *)prog->strings - (char *)prog];
    prog->variable_names= (struct variable *)
	&p[(char *)prog->variable_names - (char *)prog];
    prog->inherit	= (struct inherit *)
	&p[(char *)prog->inherit - (char *)prog];
    if (prog->type_start) {
	prog->argument_types = (unsigned short *)
	    &p[(char *)prog->argument_types - (char *)prog];
	prog->type_start = (unsigned short *)
	    &p[(char *)prog->type_start - (char *)prog];
    }
    return 1;
}


/*
 * marion - relocate pointers after swap in
 *   program
 *   line_numbers
 *   functions
 *   strings
 *   variable_names
 *   inherit
 *   argument_types
 *   type_start
 */
int
locate_in (prog) struct program *prog; {
    char *p = (char *)prog;

    if (!prog) return 0;
    prog->program	= &p[prog->program - (char *)0];
    prog->line_numbers	= (unsigned short *)
	&p[(char *)prog->line_numbers - (char *)0];
    prog->functions	= (struct function *)
	&p[(char *)prog->functions - (char *)0];
    prog->strings	= (char **)
	&p[(char *)prog->strings - (char *)0];
    prog->variable_names= (struct variable *)
	&p[(char *)prog->variable_names - (char *)0];
    prog->inherit	= (struct inherit *)
	&p[(char *)prog->inherit - (char *)0];
    if (prog->type_start) {
	prog->argument_types = (unsigned short *)
	    &p[(char *)prog->argument_types - (char *)0];
	prog->type_start     = (unsigned short *)
	    &p[(char *)prog->type_start - (char *)0];
    }
    if (d_flag > 1) {
	debug_message ("locate_in: %lX %lX %lX %lX %lX %lX %lX\n",
	    prog->program, prog->line_numbers, prog->functions,
	    prog->strings, prog->variable_names, prog->inherit,
	    prog->argument_types, prog->type_start);
    }
    return 1;
}

/*
 * Swap out an object. Only the program is swapped, not the 'struct object'.
 *
 * marion - the swap seems to corrupt the function table
 */
int swap(ob)
    struct object *ob;
{
    if (ob->flags & O_DESTRUCTED)
	return 0;
    if (d_flag > 1) { /* marion */
	debug_message("Swap object %s (ref %d)\n", ob->name, ob->ref);
    }
    if (swap_file == 0) {
#ifndef MSDOS
	char host[50];
	gethostname(host, sizeof host);
	sprintf(file_name, "%s.%s", SWAP_FILE, host);
	swap_file = fopen(file_name, "w+");
#else
	swap_file = fopen(strcpy(file_name,"LPMUD.SWAP"),"w+b");
#endif
	/* Leave this file pointer open ! */
	if (swap_file == 0)
	    return 0;
    }
    if (!ob->prog)
    {
	fprintf(stderr, "warning:no program in object %s, don't swap it\n",
		ob->name);
	/* It's no good freeing a NULL pointer */
	return 0;
    }
    if ((ob->flags & O_HEART_BEAT) || (ob->flags & O_CLONE)) {
	if (d_flag > 1) {
	    debug_message ("  object not swapped - heart beat or cloned.\n");
	}
	return 0;
    }
    if (ob->prog->ref > 1 || ob->interactive) {
	if (d_flag > 1) {
	    debug_message ("  object not swapped - inherited or interactive.\n");
	}
	return 0;
    }
    /*
     * Has this object already been swapped, and read in again ?
     * Then it is very easy to swap it out again.
     */
    if (ob->swap_num >= 0) {
	total_bytes_swapped += ob->prog->total_size;
	free_prog(ob->prog, 0);		/* Do not free the strings */
	ob->prog = 0;
	ob->flags |= O_SWAPPED;
	num_swapped++;
	return 1;
    }
    /*
     * marion - it is more efficient to write one item the size of the
     *   program to the file than many items of size one. besides, it's
     *   much more reasonable, as the fwrite only fails for the whole
     *   block and not for a part of it.
     */
    ob->swap_num = ftell(swap_file);
    locate_out (ob->prog); /* relocate the internal pointers */
    if (fwrite((char *)ob->prog, ob->prog->total_size, 1, swap_file) != 1) {
	debug_message("I/O error in swap.\n");
	ob->swap_num = -1;
	return 0;
    }
    total_bytes_swapped += ob->prog->total_size;
    num_swapped++;
    free_prog(ob->prog, 0);	/* Don't free the shared strings */
    ob->prog = 0;
    ob->flags |= O_SWAPPED;
    return 1;
}

void load_ob_from_swap(ob)
    struct object *ob;
{
    extern int errno;
    struct program tmp_prog;

    if (ob->swap_num == -1)
	fatal("Loading not swapped object.\n");
    if (fseek(swap_file, ob->swap_num, 0) == -1)
	fatal("Couldn't seek the swap file, errno %d, offset %d.\n",
	      errno, ob->swap_num);
    if (d_flag > 1) { /* marion */
	debug_message("Unswap object %s (ref %d)\n", ob->name, ob->ref);
    }
    /*
     * The size of the program is unkown, so read first part to
     * find out.
     *
     * marion - again, the read in a block is more efficient
     */
    if (fread((char *)&tmp_prog, sizeof tmp_prog, 1, swap_file) != 1) {
	fatal("Couldn't read the swap file.\n");
    }
    ob->prog = (struct program *)xalloc(tmp_prog.total_size);
    memcpy((char *)ob->prog, (char *)&tmp_prog, sizeof tmp_prog);
    fread((char *)ob->prog + sizeof tmp_prog,
	  tmp_prog.total_size - sizeof tmp_prog, 1, swap_file);
    /*
     * to be relocated:
     *   program
     *   line_numbers
     *   functions
     *   strings
     *   variable_names
     *   inherit
     *	 argument_types
     *	 type_start
     */
    locate_in (ob->prog); /* relocate the internal pointers */

    /* The reference count will already be 1 ! */
    ob->flags &= ~O_SWAPPED;
    total_bytes_swapped -= ob->prog->total_size;
    num_swapped--;
    total_prog_block_size += ob->prog->total_size;
    total_num_prog_blocks += 1;
    if (fseek(swap_file, 0L, 2) == -1) { /* marion - seek back for more swap */
	fatal("Couldn't seek end the swap file, errno %d.\n", errno);
    }
}

void remove_swap_file(ob)
    struct object *ob;
{
    /* Haven't implemented this yet :-( */
}

/*
 * This one is called at shutdown. Remove the swap file.
 */
void unlink_swap_file() {
    if (swap_file == 0)
	return;
#ifndef MSDOS
    unlink(file_name);
    fclose(swap_file);
#else
    fclose(swap_file);
    unlink(file_name);
#endif
}
