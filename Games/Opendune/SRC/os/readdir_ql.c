/** @file src/os/readdir_ql.c
 * Sinclair QL replacement for ReadDir_ProcessAllFiles().
 *
 * Instead of listing the directory (opendir/readdir semantics on QDOS
 * devices are not reliable), every file Dune II 1.07 can use is tried by
 * name. Files that exist are passed to the callback with their size,
 * exactly as the POSIX version does. Missing files are simply skipped,
 * so the EU, US and HS releases all work.
 */

#include <stdio.h>
#include <string.h>
#include "types.h"
#include "readdir.h"

static const char * const s_dune2_files[] = {
	/* PAK archives */
	"ATRE.PAK", "DUNE.PAK", "ENGLISH.PAK", "FINALE.PAK", "FRENCH.PAK",
	"GERMAN.PAK", "HARK.PAK", "HERC.PAK", "INTRO.PAK", "INTROVOC.PAK",
	"MENTAT.PAK", "MERC.PAK", "ORDOS.PAK", "OVERLAY.PAK", "SCENARIO.PAK",
	"SOUND.PAK", "VOC.PAK",
	/* loose files */
	"DUNE2.EXE",
	NULL
};

bool ReadDir_ProcessAllFiles(const char *dirpath, bool (*callback)(const char *name, const char *path, uint32 size))
{
	char path[256];
	int i, found = 0;

	for (i = 0; s_dune2_files[i] != NULL; i++) {
		FILE *f;
		long size;

		/* dirpath is either "" (default directory) or ends with '_' */
		if (strlen(dirpath) + strlen(s_dune2_files[i]) >= sizeof(path)) continue;
		strcpy(path, dirpath);
		strcat(path, s_dune2_files[i]);

		f = fopen(path, "rb");
		if (f == NULL) continue;
		fseek(f, 0, SEEK_END);
		size = ftell(f);
		fclose(f);
		if (size <= 0) continue;

		found++;
		if (!callback(s_dune2_files[i], path, (uint32)size)) return false;
	}
	return found > 0;
}
