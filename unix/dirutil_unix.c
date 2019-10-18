/* Directory listing like 'ls' for NOS.
 *
 * Copyright 2017 Jeremy Cooper, KE6JJJ
 */
#include "top.h"

#include "global.h"
#include "lib/std/stdio.h"
#include "lib/std/dirutil.h"
#include "commands.h"

kFILE *
dir(char *path,int full)
{
	kFILE *fp;

	fp = ktmpfile();
	if (fp != NULL) {
		getdir(path, full, fp);
		krewind(fp);
	}
	return fp;
}

int
filedir(char *name,int times,char *ret_str)
{
	ret_str[0] = '\0';	
	return -1;
}

static void
notimp()
{
	kfputs("NOT IMPLEMENTED YET\n", kstdout);
}

int
getdir(char *path,int full,kFILE *file)
{
	kfputs("NOT IMPLEMENTED YET\n", file);
	return -1;
}

int
dodir(int argc, char **argv, void *p)
{
	notimp();
	return 0;
}

int
docd(int argc, char **argv, void *p)
{
	notimp();
	return 0;
}

int
domkd(int argc, char **argv, void *p)
{
	notimp();
	return 0;
}

int
dormd(int argc, char **argv, void *p)
{
	notimp();
	return 0;
}
