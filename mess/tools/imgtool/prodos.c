/****************************************************************************

	prodos.c

	Apple II ProDOS disk images

*****************************************************************************

  Notes:
  - ProDOS disks are split into 512 byte blocks.

  ProDOS directory structure:

  Offset  Length  Description
  ------  ------  -----------
       0       2  ???
       2       2  Next block (0 if end)
       4      39  Directory Entry
      43      39  Directory Entry
                  ...
     472      39  Directory Entry
     511       1  ???


  ProDOS directory entry structure:

  Offset  Length  Description
  ------  ------  -----------
       0       1  Storage type (bits 7-4)
	                  10 - Seedling File (1 block)
					  20 - Sapling File (2-256 blocks)
					  30 - Tree File (257-32768 blocks)
					  E0 - Subdirectory Header
				      F0 - Volume Header
       1      15  File name (NUL padded)
      16       1  File type
      17       2  Key pointer
      19       2  Blocks used
      21       3  File size
      24       4  Creation date
      28       1  ProDOS version that created the file
      29       1  Minimum ProDOS version needed to read the file
      30       1  Access byte
      31       2  Auxilary file type
      33       4  Last modified date
      37       2  Header pointer


  In "seedling files", the key pointer points to a single block that is the
  whole file.  In "sapling files", the key pointer points to an index block
  that contains 256 2-byte index pointers that point to the actual blocks of
  the files.  These 2-byte values are not contiguous; the low order byte is
  in the first half of the block, and the high order byte is in the second
  half of the block.  In "tree files", the key pointer points to an index
  block of index blocks.

  ProDOS dates are 32-bit little endian values

	bits  0- 4	Day
	bits  5- 8	Month (0-11)
	bits  9-15	Year (0-49 is 2000-2049, 50-99 is 1950-1999)
	bits 16-21	Minute
	bits 24-28	Hour


  ProDOS directory and volume headers have this information:

  Offset  Length  Description
  ------  ------  -----------
      31       1  Length of the entry; generally is 39
	  32       1  Number of entries per block; generally is 13
	  33       2  Active entry count in directory
	  35       2  Volume bitmap block number
	  37       2  Total blocks on volume

*****************************************************************************/

#include "imgtool.h"
#include "formats/ap2_dsk.h"
#include "formats/ap_dsk35.h"
#include "iflopimg.h"

#define	ROOTDIR_BLOCK			2
#define BLOCK_SIZE				512

struct prodos_diskinfo
{
	imgtoolerr_t (*load_block)(imgtool_image *image, int block, void *buffer);
	imgtoolerr_t (*save_block)(imgtool_image *image, int block, const void *buffer);
	UINT8 dirent_size;
	UINT8 dirents_per_block;
	UINT16 volume_bitmap_block;
	UINT16 total_blocks;
};

struct prodos_direnum
{
	UINT32 block;
	UINT32 index;
	UINT8 block_data[BLOCK_SIZE];
};

struct prodos_dirent
{
	char filename[16];
	UINT8 storage_type;
	UINT16 key_pointer;
	UINT32 filesize;
	UINT32 lastmodified_time;
	UINT32 creation_time;
};

typedef enum
{
	CREATE_NONE,
	CREATE_FILE,
	CREATE_DIR,
} creation_policy_t;



static UINT32 pick_integer(const void *data, size_t offset, size_t length)
{
	const UINT8 *data_int = (const UINT8 *) data;
	UINT32 result = 0;

	data_int += offset;

	while(length--)
	{
		result <<= 8;
		result |= data_int[length];
	}
	return result;
}



static void place_integer(void *ptr, size_t offset, size_t length, UINT32 value)
{
	UINT8 b;
	size_t i = 0;

	while(length--)
	{
		b = (UINT8) value;
		value >>= 8;
		((UINT8 *) ptr)[offset + i++] = b;
	}
}



static time_t prodos_crack_time(UINT32 prodos_time)
{
	struct tm t;
	time_t now;

	time(&now);
	t = *localtime(&now);

	t.tm_sec	= 0;
	t.tm_min	= (prodos_time >> 16) & 0x3F;
	t.tm_hour	= (prodos_time >> 24) & 0x1F;
	t.tm_mday	= (prodos_time >>  0) & 0x1F;
	t.tm_mon	= (prodos_time >>  5) & 0x0F;
	t.tm_year	= (prodos_time >>  9) & 0x7F;

	if (t.tm_year <= 49)
		t.tm_year += 100;
	return mktime(&t);
}



static UINT32 prodos_setup_time(time_t ansi_time)
{
	struct tm t;
	UINT32 result = 0;

	t = *localtime(&ansi_time);
	if ((t.tm_year >= 100) && (t.tm_year <= 149))
		t.tm_year -= 100;

	result |= (((UINT32) t.tm_min)	& 0x003F) << 16;
	result |= (((UINT32) t.tm_hour)	& 0x001F) << 24;
	result |= (((UINT32) t.tm_mday)	& 0x001F) <<  0;
	result |= (((UINT32) t.tm_mon)	& 0x000F) <<  5;
	result |= (((UINT32) t.tm_year)	& 0x007F) <<  9;
	return result;
}



static UINT32 prodos_time_now(void)
{
	time_t now;
	time(&now);
	return prodos_setup_time(now);
}



static int is_file_storagetype(UINT8 storage_type)
{
	return (storage_type >= 0x10) && (storage_type <= 0x3F);
}



static int is_dir_storagetype(UINT8 storage_type)
{
	return (storage_type >= 0xE0) && (storage_type <= 0xEF);
}



static struct prodos_diskinfo *get_prodos_info(imgtool_image *image)
{
	struct prodos_diskinfo *info;
	info = (struct prodos_diskinfo *) imgtool_floppy_extrabytes(image);
	return info;
}



/* ----------------------------------------------------------------------- */

static void prodos_find_block_525(imgtool_image *image, int block,
	UINT32 *track, UINT32 *head, UINT32 *sector1, UINT32 *sector2)
{
	static const UINT8 skewing[] =
	{
		0x00, 0x02, 0x04, 0x06, 0x08, 0x0A, 0x0C, 0x0E,
		0x01, 0x03, 0x05, 0x07, 0x09, 0x0B, 0x0D, 0x0F
	};

	block *= 2;

	*track = block / APPLE2_SECTOR_COUNT;
	*head = 0;
	*sector1 = skewing[block % APPLE2_SECTOR_COUNT + 0];
	*sector2 = skewing[block % APPLE2_SECTOR_COUNT + 1];
}



static imgtoolerr_t prodos_load_block_525(imgtool_image *image,
	int block, void *buffer)
{
	floperr_t ferr;
	UINT32 track, head, sector1, sector2;

	prodos_find_block_525(image, block, &track, &head, &sector1, &sector2);

	/* read first sector */
	ferr = floppy_read_sector(imgtool_floppy(image), head, track, 
		sector1, 0, ((UINT8 *) buffer) + 0, 256);
	if (ferr)
		return imgtool_floppy_error(ferr);

	/* read second sector */
	ferr = floppy_read_sector(imgtool_floppy(image), head, track,
		sector2, 0, ((UINT8 *) buffer) + 256, 256);
	if (ferr)
		return imgtool_floppy_error(ferr);

	return IMGTOOLERR_SUCCESS;
}



static imgtoolerr_t prodos_save_block_525(imgtool_image *image,
	int block, const void *buffer)
{
	floperr_t ferr;
	UINT32 track, head, sector1, sector2;

	prodos_find_block_525(image, block, &track, &head, &sector1, &sector2);

	/* read first sector */
	ferr = floppy_write_sector(imgtool_floppy(image), head, track, 
		sector1, 0, ((const UINT8 *) buffer) + 0, 256);
	if (ferr)
		return imgtool_floppy_error(ferr);

	/* read second sector */
	ferr = floppy_write_sector(imgtool_floppy(image), head, track,
		sector2, 0, ((const UINT8 *) buffer) + 256, 256);
	if (ferr)
		return imgtool_floppy_error(ferr);

	return IMGTOOLERR_SUCCESS;
}



static void prodos_setprocs_525(imgtool_image *image)
{
	struct prodos_diskinfo *info;
	info = get_prodos_info(image);
	info->load_block = prodos_load_block_525;
	info->save_block = prodos_save_block_525;
}



/* ----------------------------------------------------------------------- */

static imgtoolerr_t prodos_find_block_35(imgtool_image *image, int block,
	UINT32 *track, UINT32 *head, UINT32 *sector)
{
	int sides = 2;

	*track = 0;
	while(block >= (apple35_tracklen_800kb[*track] * sides))
	{
		block -= (apple35_tracklen_800kb[(*track)++] * sides);
		if (*track >= 80)
			return IMGTOOLERR_SEEKERROR;
	}

	*head = block / apple35_tracklen_800kb[*track];
	*sector = block % apple35_tracklen_800kb[*track];
	return IMGTOOLERR_SUCCESS;
}



static imgtoolerr_t prodos_load_block_35(imgtool_image *image,
	int block, void *buffer)
{
	imgtoolerr_t err;
	floperr_t ferr;
	UINT32 track, head, sector;

	err = prodos_find_block_35(image, block, &track, &head, &sector);
	if (err)
		return err;

	ferr = floppy_read_sector(imgtool_floppy(image), head, track, sector, 0, buffer, 512);
	if (ferr)
		return imgtool_floppy_error(ferr);

	return IMGTOOLERR_SUCCESS;
}



static imgtoolerr_t prodos_save_block_35(imgtool_image *image,
	int block, const void *buffer)
{
	imgtoolerr_t err;
	floperr_t ferr;
	UINT32 track, head, sector;

	err = prodos_find_block_35(image, block, &track, &head, &sector);
	if (err)
		return err;

	ferr = floppy_write_sector(imgtool_floppy(image), head, track, sector, 0, buffer, 512);
	if (ferr)
		return imgtool_floppy_error(ferr);

	return IMGTOOLERR_SUCCESS;
}



static void prodos_setprocs_35(imgtool_image *image)
{
	struct prodos_diskinfo *info;
	info = get_prodos_info(image);
	info->load_block = prodos_load_block_35;
	info->save_block = prodos_save_block_35;
}



/* ----------------------------------------------------------------------- */

static imgtoolerr_t prodos_load_block(imgtool_image *image,
	int block, void *buffer)
{
	struct prodos_diskinfo *diskinfo;
	diskinfo = get_prodos_info(image);
	return diskinfo->load_block(image, block, buffer);
}



static imgtoolerr_t prodos_save_block(imgtool_image *image,
	int block, const void *buffer)
{
	struct prodos_diskinfo *diskinfo;
	diskinfo = get_prodos_info(image);
	return diskinfo->save_block(image, block, buffer);
}



/* ----------------------------------------------------------------------- */

static imgtoolerr_t prodos_diskimage_open(imgtool_image *image)
{
	imgtoolerr_t err;
	UINT8 buffer[BLOCK_SIZE];
	struct prodos_diskinfo *di;
	const UINT8 *ent;

	di = get_prodos_info(image);

	/* specify defaults */
	di->dirent_size = 39;
	di->dirents_per_block = 13;

	/* load the first block, hoping that the volume header is first */
	err = prodos_load_block(image, ROOTDIR_BLOCK, buffer);
	if (err)
		return err;

	ent = &buffer[4];

	/* did we find the volume header? */
	if ((ent[0] & 0xF0) == 0xF0)
	{
		di->dirent_size			= pick_integer(ent, 31, 1);
		di->dirents_per_block	= pick_integer(ent, 32, 1);
		di->volume_bitmap_block	= pick_integer(ent, 35, 2);
		di->total_blocks		= pick_integer(ent, 37, 2);
	}

	/* sanity check these values */
	if (di->dirent_size < 39)
		return IMGTOOLERR_CORRUPTIMAGE;
	if (di->dirents_per_block * di->dirent_size >= BLOCK_SIZE)
		return IMGTOOLERR_CORRUPTIMAGE;
	if (di->volume_bitmap_block >= di->total_blocks)
		return IMGTOOLERR_CORRUPTIMAGE;

	return IMGTOOLERR_SUCCESS;
}



static imgtoolerr_t prodos_diskimage_open_525(imgtool_image *image)
{
	prodos_setprocs_525(image);
	return prodos_diskimage_open(image);
}



static imgtoolerr_t prodos_diskimage_open_35(imgtool_image *image)
{
	prodos_setprocs_35(image);
	return prodos_diskimage_open(image);
}



/* ----------------------------------------------------------------------- */

static imgtoolerr_t prodos_load_volume_bitmap(imgtool_image *image, UINT8 **bitmap)
{
	imgtoolerr_t err;
	struct prodos_diskinfo *di;
	UINT8 *alloc_bitmap;
	UINT32 bitmap_blocks, i;

	di = get_prodos_info(image);

	bitmap_blocks = (di->total_blocks + (BLOCK_SIZE * 8) - 1) / (BLOCK_SIZE * 8);
	alloc_bitmap = malloc(bitmap_blocks * BLOCK_SIZE);
	if (!alloc_bitmap)
	{
		err = IMGTOOLERR_OUTOFMEMORY;
		goto done;
	}

	for (i = 0; i < bitmap_blocks; i++)
	{
		err = prodos_load_block(image, di->volume_bitmap_block + i,
			&alloc_bitmap[i * BLOCK_SIZE]);
		if (err)
			goto done;
	}

	err = IMGTOOLERR_SUCCESS;

done:
	if (err && alloc_bitmap)
	{
		free(alloc_bitmap);
		alloc_bitmap = NULL;
	}
	*bitmap = alloc_bitmap;
	return err;
}



static imgtoolerr_t prodos_save_volume_bitmap(imgtool_image *image, const UINT8 *bitmap)
{
	imgtoolerr_t err;
	struct prodos_diskinfo *di;
	UINT32 bitmap_blocks, i;

	di = get_prodos_info(image);

	bitmap_blocks = (di->total_blocks + (BLOCK_SIZE * 8) - 1) / (BLOCK_SIZE * 8);

	for (i = 0; i < bitmap_blocks; i++)
	{
		err = prodos_save_block(image, di->volume_bitmap_block + i,
			&bitmap[i * BLOCK_SIZE]);
		if (err)
			return err;
	}
	return IMGTOOLERR_SUCCESS;
}



static void prodos_set_volume_bitmap_bit(UINT8 *buffer, UINT32 block, int value)
{
	UINT8 mask;
	buffer += block / 8;
	mask = 1 << (7 - (block % 8));
	if (value)
		*buffer &= ~mask;
	else
		*buffer |= mask;
}



static int prodos_get_volume_bitmap_bit(const UINT8 *buffer, UINT32 block)
{
	UINT8 mask;
	buffer += block / 8;
	mask = 1 << (7 - (block % 8));
	return (*buffer & mask) ? 1 : 0;
}



static imgtoolerr_t prodos_alloc_block(imgtool_image *image, UINT8 *bitmap,
	UINT32 *block)
{
	imgtoolerr_t err = IMGTOOLERR_SUCCESS;
	struct prodos_diskinfo *di;
	UINT32 bitmap_blocks, i;
	UINT8 *alloc_bitmap = NULL;

	di = get_prodos_info(image);
	*block = 0;
	bitmap_blocks = (di->total_blocks + (BLOCK_SIZE * 8) - 1) / (BLOCK_SIZE * 8);

	if (!bitmap)
	{
		err = prodos_load_volume_bitmap(image, &alloc_bitmap);
		if (err)
			goto done;
		alloc_bitmap = bitmap;
	}

	for (i = (di->volume_bitmap_block + bitmap_blocks); i < di->total_blocks; i++)
	{
		if (!prodos_get_volume_bitmap_bit(bitmap, i))
		{
			prodos_set_volume_bitmap_bit(bitmap, i, 1);
			*block = i;
			break;
		}
	}

	if (*block > 0)
	{
		if (alloc_bitmap)
		{
			err = prodos_save_volume_bitmap(image, bitmap);
			if (err)
				goto done;
		}
	}
	else
	{
		err = IMGTOOLERR_NOSPACE;
	}

done:
	if (err)
		*block = 0;
	if (alloc_bitmap)
		free(alloc_bitmap);
	return err;
}



/* ----------------------------------------------------------------------- */

static imgtoolerr_t prodos_diskimage_create(imgtool_image *image, option_resolution *opts)
{
	imgtoolerr_t err;
	UINT32 heads, tracks, sectors, sector_bytes;
	UINT32 dirent_size, volume_bitmap_block, i;
	UINT32 volume_bitmap_block_count, total_blocks;
	UINT8 buffer[BLOCK_SIZE];

	heads = option_resolution_lookup_int(opts, 'H');
	tracks = option_resolution_lookup_int(opts, 'T');
	sectors = option_resolution_lookup_int(opts, 'S');
	sector_bytes = option_resolution_lookup_int(opts, 'L');

	dirent_size = 39;
	volume_bitmap_block = 6;
	total_blocks = tracks * heads * sectors * sector_bytes / BLOCK_SIZE;
	volume_bitmap_block_count = (total_blocks + (BLOCK_SIZE * 8) - 1) / (BLOCK_SIZE * 8);

	/* prepare initial dir block */
	memset(buffer, 0, sizeof(buffer));
	place_integer(buffer, 4 +  0, 1, 0xF0);
	place_integer(buffer, 4 + 31, 1, dirent_size);
	place_integer(buffer, 4 + 32, 1, BLOCK_SIZE / dirent_size);
	place_integer(buffer, 4 + 35, 2, volume_bitmap_block);
	place_integer(buffer, 4 + 37, 2, total_blocks);

	err = prodos_save_block(image, ROOTDIR_BLOCK, buffer);
	if (err)
		return err;

	/* setup volume bitmap */
	memset(buffer, 0, sizeof(buffer));
	for (i = 0; i < (volume_bitmap_block + volume_bitmap_block_count); i++)
		prodos_set_volume_bitmap_bit(buffer, i, 1);
	prodos_save_block(image, volume_bitmap_block, buffer);

	/* and finally open the image */
	return prodos_diskimage_open(image);
}



static imgtoolerr_t prodos_diskimage_create_525(imgtool_image *image, option_resolution *opts)
{
	prodos_setprocs_525(image);
	return prodos_diskimage_create(image, opts);
}



static imgtoolerr_t prodos_diskimage_create_35(imgtool_image *image, option_resolution *opts)
{
	prodos_setprocs_35(image);
	return prodos_diskimage_create(image, opts);
}



/* ----------------------------------------------------------------------- */

static imgtoolerr_t prodos_enum_seek(imgtool_image *image,
	struct prodos_direnum *appleenum, UINT32 block, UINT32 index)
{
	imgtoolerr_t err;
	UINT8 buffer[BLOCK_SIZE];

	if (appleenum->block != block)
	{
		if (block != 0)
		{
			err = prodos_load_block(image, block, buffer);
			if (err)
				return err;
			memcpy(appleenum->block_data, buffer, sizeof(buffer));
		}
		appleenum->block = block;
	}

	appleenum->index = index;
	return IMGTOOLERR_SUCCESS;
}



static imgtoolerr_t prodos_get_next_dirent(imgtool_image *image,
	struct prodos_direnum *appleenum, struct prodos_dirent *ent)
{
	imgtoolerr_t err;
	struct prodos_diskinfo *di;
	UINT32 next_block, next_index;
	UINT32 offset;

	di = get_prodos_info(image);
	memset(ent, 0, sizeof(*ent));

	/* have we hit the end of the file? */
	if (appleenum->block == 0)
		return IMGTOOLERR_SUCCESS;

	/* populate the resulting dirent */
	offset = appleenum->index * di->dirent_size + 4;
	ent->storage_type = appleenum->block_data[offset + 0];
	memcpy(ent->filename, &appleenum->block_data[offset + 1], 15);
	ent->filename[15] = '\0';
	ent->key_pointer		= pick_integer(appleenum->block_data, offset + 17, 2);
	ent->filesize			= pick_integer(appleenum->block_data, offset + 21, 3);
	ent->creation_time		= pick_integer(appleenum->block_data, offset + 24, 4);
	ent->lastmodified_time	= pick_integer(appleenum->block_data, offset + 33, 4);

	/* identify next entry */
	next_block = appleenum->block;
	next_index = appleenum->index + 1;
	if (next_index >= di->dirents_per_block)
	{
		next_block = pick_integer(appleenum->block_data, 2, 2);
		next_index = 0;
	}

	/* seek next block */
	err = prodos_enum_seek(image, appleenum, next_block, next_index);
	if (err)
		return err;

	return IMGTOOLERR_SUCCESS;
}



static imgtoolerr_t prodos_put_dirent(imgtool_image *image,
	struct prodos_direnum *appleenum, const struct prodos_dirent *ent)
{
	imgtoolerr_t err;
	struct prodos_diskinfo *di;
	UINT32 offset;

	di = get_prodos_info(image);
	offset = appleenum->index * di->dirent_size + 4;

	appleenum->block_data[offset + 0] = ent->storage_type;
	memcpy(&appleenum->block_data[offset + 1], ent->filename, 15);
	place_integer(appleenum->block_data, offset + 17, 2, ent->key_pointer);
	place_integer(appleenum->block_data, offset + 21, 3, ent->filesize);
	place_integer(appleenum->block_data, offset + 24, 4, ent->creation_time);
	place_integer(appleenum->block_data, offset + 33, 4, ent->lastmodified_time);

	err = prodos_save_block(image, appleenum->block, appleenum->block_data);
	if (err)
		return err;

	return IMGTOOLERR_SUCCESS;
}



static imgtoolerr_t prodos_lookup_path(imgtool_image *image, const char *path,
	creation_policy_t create, struct prodos_direnum *direnum, struct prodos_dirent *ent)
{
	imgtoolerr_t err;
	struct prodos_direnum my_direnum;
	UINT32 block = ROOTDIR_BLOCK;
	const char *old_path;
	UINT32 free_block = 0;
	UINT32 free_index = 0;

	if (!direnum)
		direnum = &my_direnum;

	while(*path)
	{
		memset(direnum, 0, sizeof(*direnum));
		err = prodos_enum_seek(image, direnum, block, 0);
		if (err)
			return err;

		do
		{
			err = prodos_get_next_dirent(image, direnum, ent);
			if (err)
				return err;

			/* if we need to create a file entry and this is free, track it */
			if (create && direnum->block && !free_block && !ent->storage_type)
			{
				free_block = direnum->block;
				free_index = direnum->index;
			}
		}
		while(direnum->block && (strcmp(path, ent->filename) || (
			!is_file_storagetype(ent->storage_type) &&
			!is_dir_storagetype(ent->storage_type))));

		old_path = path;
		path += strlen(path) + 1;	
		if (*path)
		{
			/* next part of the file */
			if (!is_dir_storagetype(ent->storage_type))
				return IMGTOOLERR_FILENOTFOUND;
			block = ent->key_pointer;
		}
		else if (!direnum->block)
		{
			/* did not find file; maybe we need to create it */
			if (create == CREATE_NONE)
				return IMGTOOLERR_FILENOTFOUND;

			/* we dont support expanding the directory yet */
			if (!free_block)
				return IMGTOOLERR_UNIMPLEMENTED;

			/* seek back to the free space */
			err = prodos_enum_seek(image, direnum, free_block, free_index);
			if (err)
				return err;

			/* prepare the dirent */
			memset(ent, 0, sizeof(*ent));
			ent->storage_type = 0x10;
			ent->creation_time = ent->lastmodified_time = prodos_time_now();
			strncpy(ent->filename, old_path, sizeof(ent->filename) / sizeof(ent->filename[0]));

			/* and place it */
			err = prodos_put_dirent(image, direnum, ent);
			if (err)
				return err;
		}
	}
	return IMGTOOLERR_SUCCESS;
}



static imgtoolerr_t prodos_grow_file(imgtool_image *image, struct prodos_direnum *direnum,
	struct prodos_dirent *ent, UINT8 *bitmap, UINT32 new_blockcount)
{
	imgtoolerr_t err;
	int depth, new_depth;
	UINT32 new_block;
	UINT8 buffer[BLOCK_SIZE];

	if (ent->key_pointer == 0)
		depth = 0;
	else
		depth = ent->storage_type / 0x10;

	if (new_blockcount == 0)
		new_depth = 0;
	else if (new_blockcount <= 1)
		new_depth = 1;
	else if (new_blockcount <= 256)
		new_depth = 2;
	else
		new_depth = 3;

	while(new_depth > depth)
	{
		err = prodos_alloc_block(image, bitmap, &new_block);
		if (err)
			return err;

		memset(buffer, 0, sizeof(buffer));
		buffer[0] = (UINT8) (ent->key_pointer >> 0);
		buffer[256] = (UINT8) (ent->key_pointer >> 8);
		err = prodos_save_block(image, new_block, buffer);
		if (err)
			return err;

		depth++;
		ent->key_pointer = new_block;
		ent->storage_type &= ~0xF0;
		ent->storage_type |= depth * 0x10;
	}

	if (ent->key_pointer)
	{
/*		err = prodos_fill_file(image, ent->key_pointer, depth, new_blockcount);
		if (err)
			return err;
*/	}
	return IMGTOOLERR_UNIMPLEMENTED;
}



static imgtoolerr_t prodos_set_file_size(imgtool_image *image, struct prodos_direnum *direnum,
	struct prodos_dirent *ent, UINT32 new_size)
{
	imgtoolerr_t err = IMGTOOLERR_SUCCESS;
	UINT32 blockcount, new_blockcount;
	UINT8 *bitmap = NULL;

	if (ent->filesize != new_size)
	{
		blockcount = (ent->filesize + BLOCK_SIZE - 1) / BLOCK_SIZE;
		new_blockcount = (new_size + BLOCK_SIZE - 1) / BLOCK_SIZE;

		/* do we need to change the block chain? */
		if (new_blockcount != blockcount)
		{
			err = prodos_load_volume_bitmap(image, &bitmap);
			if (err)
				goto done;

			if (new_blockcount > blockcount)
				err = prodos_grow_file(image, direnum, ent, bitmap, new_blockcount);
			else
				err = IMGTOOLERR_UNIMPLEMENTED;
			if (err)
				goto done;

			err = prodos_save_volume_bitmap(image, bitmap);
			if (err)
				goto done;
		}

		ent->filesize = new_size;
		err = prodos_put_dirent(image, direnum, ent);
		if (err)
			goto done;
	}

done:
	if (bitmap)
		free(bitmap);
	return err;
}



static UINT32 prodos_get_storagetype_maxfilesize(UINT8 storage_type)
{
	UINT32 max_filesize = 0;
	switch(storage_type & 0xF0)
	{
		case 0x10:
			max_filesize = BLOCK_SIZE * 1;
			break;
		case 0x20:
			max_filesize = BLOCK_SIZE * 256;
			break;
		case 0x30:
			max_filesize = BLOCK_SIZE * 32768;
			break;
	}
	return max_filesize;
}



static imgtoolerr_t prodos_diskimage_beginenum(imgtool_imageenum *enumeration, const char *path)
{
	imgtoolerr_t err;
	imgtool_image *image;
	struct prodos_direnum *appleenum;

	image = img_enum_image(enumeration);
	appleenum = (struct prodos_direnum *) img_enum_extrabytes(enumeration);

	/* only partially implemented */
	if (*path)
		return IMGTOOLERR_UNIMPLEMENTED;

	/* seek initial block */
	err = prodos_enum_seek(image, appleenum, ROOTDIR_BLOCK, 0);
	if (err)
		return err;

	return IMGTOOLERR_SUCCESS;
}



static imgtoolerr_t prodos_diskimage_nextenum(imgtool_imageenum *enumeration, imgtool_dirent *ent)
{
	imgtoolerr_t err;
	imgtool_image *image;
	struct prodos_direnum *appleenum;
	struct prodos_dirent pd_ent;
	UINT32 max_filesize;

	image = img_enum_image(enumeration);
	appleenum = (struct prodos_direnum *) img_enum_extrabytes(enumeration);

	do
	{
		err = prodos_get_next_dirent(image, appleenum, &pd_ent);
		if (err)
			return err;
	}
	while(appleenum->block
		&& !is_file_storagetype(pd_ent.storage_type)
		&& !is_dir_storagetype(pd_ent.storage_type));

	/* end of file? */
	if (pd_ent.storage_type == 0x00)
	{
		ent->eof = 1;
		return IMGTOOLERR_SUCCESS;
	}

	strcpy(ent->filename, pd_ent.filename);
	ent->directory			= is_dir_storagetype(pd_ent.storage_type);
	ent->creation_time		= prodos_crack_time(pd_ent.creation_time);
	ent->lastmodified_time	= prodos_crack_time(pd_ent.lastmodified_time);

	if (!ent->directory)
	{
		ent->filesize = pd_ent.filesize;

		max_filesize = prodos_get_storagetype_maxfilesize(pd_ent.storage_type);
		if (ent->filesize > max_filesize)
		{
			ent->corrupt = 1;
			ent->filesize = max_filesize;
		}
	}
	return IMGTOOLERR_SUCCESS;
}



static imgtoolerr_t prodos_send_file(imgtool_image *image, UINT32 *filesize,
	UINT32 block, int nest_level, imgtool_stream *destf)
{
	imgtoolerr_t err;
	UINT8 buffer[BLOCK_SIZE];
	UINT16 subblock;
	size_t bytes_to_write;
	int i;

	err = prodos_load_block(image, block, buffer);
	if (err)
		return err;

	if (nest_level > 0)
	{
		/* this is an index block */
		for (i = 0; i < 256; i++)
		{
			/* retrieve the block pointer; the two bytes are on either half
			 * of the block */
			subblock = buffer[i + 256];
			subblock <<= 8;
			subblock |= buffer[i + 0];

			if (subblock != 0)
			{
				err = prodos_send_file(image, filesize, subblock, nest_level - 1, destf);
				if (err)
					return err;
			}
		}
	}
	else
	{
		/* this is a leaf block */
		bytes_to_write = MIN(*filesize, sizeof(buffer));
		stream_write(destf, buffer, bytes_to_write);
		*filesize -= bytes_to_write;
	}
	return IMGTOOLERR_SUCCESS;
}



static imgtoolerr_t prodos_diskimage_readfile(imgtool_image *image, const char *filename, imgtool_stream *destf)
{
	imgtoolerr_t err;
	struct prodos_dirent ent;

	err = prodos_lookup_path(image, filename, CREATE_NONE, NULL, &ent);
	if (err)
		return err;

	if (is_dir_storagetype(ent.storage_type))
		return IMGTOOLERR_FILENOTFOUND;

	err = prodos_send_file(image, &ent.filesize, ent.key_pointer, 
		(ent.storage_type >> 4) - 1, destf);
	if (err)
		return err;

	return IMGTOOLERR_SUCCESS;
}



static imgtoolerr_t prodos_diskimage_writefile(imgtool_image *image, const char *filename, imgtool_stream *sourcef, option_resolution *opts)
{
	imgtoolerr_t err;
	struct prodos_dirent ent;
	struct prodos_direnum direnum;

	err = prodos_lookup_path(image, filename, CREATE_FILE, &direnum, &ent);
	if (err)
		return err;

	if (is_dir_storagetype(ent.storage_type))
		return IMGTOOLERR_FILENOTFOUND;

	err = prodos_set_file_size(image, &direnum, &ent, stream_size(sourcef));
	if (err)
		return err;

	return IMGTOOLERR_SUCCESS;
}



static imgtoolerr_t apple2_prodos_module_populate(imgtool_library *library, struct ImgtoolFloppyCallbacks *module)
{
	module->initial_path_separator		= 1;
	module->open_is_strict				= 1;
	module->supports_creation_time		= 1;
	module->supports_lastmodified_time	= 1;
	module->image_extra_bytes			+= sizeof(struct prodos_diskinfo);
	module->imageenum_extra_bytes		+= sizeof(struct prodos_direnum);
	module->eoln						= EOLN_CR;
	module->path_separator				= '/';
	module->begin_enum					= prodos_diskimage_beginenum;
	module->next_enum					= prodos_diskimage_nextenum;
	module->read_file					= prodos_diskimage_readfile;
	module->write_file					= prodos_diskimage_writefile;
	return IMGTOOLERR_SUCCESS;
}



static imgtoolerr_t apple2_prodos_module_populate_525(imgtool_library *library, struct ImgtoolFloppyCallbacks *module)
{
	apple2_prodos_module_populate(library, module);
	module->create = prodos_diskimage_create_525;
	module->open = prodos_diskimage_open_525;
	return IMGTOOLERR_SUCCESS;
}



static imgtoolerr_t apple2_prodos_module_populate_35(imgtool_library *library, struct ImgtoolFloppyCallbacks *module)
{
	apple2_prodos_module_populate(library, module);
	module->create = prodos_diskimage_create_35;
	module->open = prodos_diskimage_open_35;
	return IMGTOOLERR_SUCCESS;
}



FLOPPYMODULE(prodos_525, "ProDOS format", apple2,       apple2_prodos_module_populate_525)
FLOPPYMODULE(prodos_35,  "ProDOS format", apple35_iigs, apple2_prodos_module_populate_35)