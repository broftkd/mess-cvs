/*********************************************************************

	testimgt.c

	Imgtool testing code

*********************************************************************/

#include "testimgt.h"
#include "../imgtool/imgtool.h"
#include "../imgtool/modules.h"

#define TEMPFILE	"tempfile.tmp"

struct expected_dirent
{
	char filename[256];
	int size;
};

static imgtool_library *library;
static imgtool_image *image;
static struct expected_dirent entries[256];
static char filename_buffer[256];
static int entry_count;



static void error_imgtoolerr(struct messtest_state *state, imgtoolerr_t err)
{
	const char *msg;
	msg = imgtool_error(err);
	error_report(state, msg);
}



static void createimage_handler(struct messtest_state *state, const char **attributes)
{
	imgtoolerr_t err;
	const char *driver;

	driver = find_attribute(attributes, "driver");
	if (!driver)
	{
		error_missingattribute(state, "driver");
		return;
	}

	err = img_create_byname(library, driver, TEMPFILE, NULL);
	if (err)
	{
		error_imgtoolerr(state, err);
		return;
	}

	err = img_open_byname(library, driver, TEMPFILE, OSD_FOPEN_RW, &image);
	if (err)
	{
		error_imgtoolerr(state, err);
		return;
	}
}



static void putfile_start_handler(struct messtest_state *state, const char **attributes)
{
	const char *filename;

	filename = find_attribute(attributes, "name");
	if (!filename)
	{
		error_missingattribute(state, "name");
		return;
	}

	snprintf(filename_buffer, sizeof(filename_buffer) / sizeof(filename_buffer[0]), "%s", filename);
}



static void putfile_end_handler(struct messtest_state *state, const void *buffer, size_t size)
{
}



static void checkdirectory_start_handler(struct messtest_state *state, const char **attributes)
{
	memset(&entries, 0, sizeof(entries));
	entry_count = 0;
}



static void checkdirectory_entry_handler(struct messtest_state *state, const char **attributes)
{
	const char *name;
	const char *size;
	struct expected_dirent *entry;

	if (entry_count >= sizeof(entries) / sizeof(entries[0]))
	{
		error_report(state, "Too many directory entries");
		return;
	}

	name = find_attribute(attributes, "name");
	size = find_attribute(attributes, "size");

	entry = &entries[entry_count++];

	if (name)
		snprintf(entry->filename, sizeof(entry->filename) / sizeof(entry->filename[0]), "%s", name);
	entry->size = size ? atoi(size) : -1;
}



static void checkdirectory_end_handler(struct messtest_state *state, const void *buffer, size_t size)
{
	imgtoolerr_t err = IMGTOOLERR_SUCCESS;
	imgtool_imageenum *imageenum;
	imgtool_dirent ent;
	char filename_buffer[1024];
	int i;

	memset(&ent, 0, sizeof(ent));
	ent.filename = filename_buffer;
	ent.filename_len = sizeof(filename_buffer) / sizeof(filename_buffer[0]);

	err = img_beginenum(image, &imageenum);
	if (err)
		goto done;

	for (i = 0; i < entry_count; i++)
	{
		err = img_nextenum(imageenum, &ent);
		if (err)
			goto done; 

		if (ent.eof || strcmp(ent.filename, entries[i].filename))
		{
			error_report(state, "Misnamed file entry");
			goto done;
		}
	}

	err = img_nextenum(imageenum, &ent);
	if (err)
		goto done;
	if (!ent.eof)
	{
		error_report(state, "Extra file entries");
		goto done;
	}

done:
	if (imageenum)
		img_closeenum(imageenum);
	if (err)
		error_imgtoolerr(state, err);
}



void testimgtool_start_handler(struct messtest_state *state, const char **attributes)
{
	imgtoolerr_t err;
	
	if (!library)
	{
		err = imgtool_create_cannonical_library(&library);
		if (err)
		{
			error_imgtoolerr(state, err);
			return;
		}
	}
}



void testimgtool_end_handler(struct messtest_state *state, const void *buffer, size_t size)
{
	if (image)
	{
		img_close(image);
		image = NULL;
	}
}



static const struct messtest_tagdispatch checkdirectory_dispatch[] =
{
	{ "entry",			DATA_NONE,		checkdirectory_entry_handler },
	{ NULL }
};

const struct messtest_tagdispatch testimgtool_dispatch[] =
{
	{ "createimage",	DATA_NONE,		createimage_handler,	NULL },
	{ "checkdirectory",	DATA_NONE,		NULL,					checkdirectory_end_handler, checkdirectory_dispatch },
	{ "putfile",		DATA_BINARY,	putfile_start_handler,	putfile_end_handler },
	{ NULL }
};



