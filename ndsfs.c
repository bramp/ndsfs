/**
 * ndsfs.c NDS Rom File system mounter (v1.1)
 * Using FUSE, a NDS rom can be mounted as a file system
 *
 * by Andrew Brampton (bramp.net)
 * using knowledge/code from ndstool by Rafael Vuijk (aka DarkFader)
 *
 * TODO
 *  Add extra checks to see if file_ids are valid
 *  Implement cache so common paths aren't checked so often
 *  Make sure this code works on big endian machines
 *  Add write support (hard)
 *  The mount directory looks really wierd unless you are a superuser
 *  We have coarse grain locking, change this to be more fine grain.
 */

#define FUSE_USE_VERSION  26
#define _FILE_OFFSET_BITS 64

#include "header.h"

#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stddef.h>
#include <assert.h>
#include <syslog.h>
#include <argp.h>
#include <pthread.h>

const char *argp_program_version = "ndsfs 1.1";
const char *argp_program_bug_address = "Andrew Brampton <bramp.net>";

static char doc[] =
       "ndsfs -- A FUSE application to mount Nintendo DS roms";

static char args_doc[] = "<rom file> <mount point>";

static struct argp_option options[] = {
       { 0 }
};

static pthread_mutex_t rom_mutex = PTHREAD_MUTEX_INITIALIZER;

static const char * rom_filename;
static FILE * rom_fp;
static struct NDSHeader rom_header;

#define FAT_IS_DIR(flags)  ((flags) & 0x80)
#define FAT_IS_FILE(flags) !FAT_IS_DIR(flags)
#define FAT_LENGTH(flags)  ((flags) & 0x7F)

static void filler_dir(void *buf, fuse_fill_dir_t filler, const char *name, uint16_t dir_id) {
	struct stat stbuf;

	stbuf.st_ino = dir_id;
	stbuf.st_mode = S_IFDIR | 0755;

	filler(buf, name, &stbuf, 0);
}

static void filler_file(void *buf, fuse_fill_dir_t filler, const char *name, uint16_t file_id) {
	struct stat stbuf;

	stbuf.st_ino = file_id;
	stbuf.st_mode = S_IFREG | 0444;

	filler(buf, name, &stbuf, 0);
}

static int read_le_uint32(FILE *fp, uint32_t *num) {
	assert (fp != NULL);
	assert (num != NULL);

	int ret = fread(num, sizeof(*num), 1, fp);
	if ( ret != 1 )
		return -1;

	// TODO check if this endian swap is needed
	//*num = (*num>>24) | ((*num<<8) & 0x00FF0000) | ((*num>>8) & 0x0000FF00) | (*num<<24);

	return 0;
}


static int read_le_uint16(FILE *fp, uint16_t *num) {
	int ret = fread(num, sizeof(*num), 1, fp);
	if ( ret != 1 )
		return -1;

	// TODO check if this endian swap is needed
	//*num = (*num>>8) | (*num<<8);

	return 0;
}

/**
 * Retreives the start and end offsets of a file
 */
static int ndsfs_get_size(const uint16_t file_id, uint32_t *start, uint32_t *end) {

	assert ( start != NULL );
	assert ( end != NULL );

	// TODO perhaps check file_id is valid
	if ( fseek(rom_fp, rom_header.fat_offset + 8*file_id, SEEK_SET) ) {
		syslog(LOG_ERR, "Error %d seeking to file entry in ROM\n", errno);
		return -EIO;
	}

	if ( read_le_uint32(rom_fp, start) ) {
		syslog(LOG_ERR, "Error %d reading start offset for file %d from ROM\n", errno, file_id);
		return -EIO;
	}

	if ( read_le_uint32(rom_fp, end) ) {
		syslog(LOG_ERR, "Error %d reading end offset for file %d from ROM\n", errno, file_id);
		return -EIO;
	}

	return 0;
}

/**
 *
 * Should be called with rom_mutex locked
 */
static int ndsfs_resolve_path(const char *path, uint16_t *file_id, uint8_t *flags) {

	const char *next_path = path;
	size_t path_len = 0;

	assert ( path != NULL );
	assert ( file_id != NULL );
	assert ( flags != NULL );

	// Start at the root dir
	if ( *path != '/' )
		return -ENOENT;

	*file_id = 0;
	*flags = 0x80;

	while ( next_path != NULL ) {
		uint32_t entry_start;
		uint16_t current_id;
		uint16_t top_file_id;

		// Get next path element
		path = next_path + 1;
		next_path = strchr(path, '/');

		if ( next_path ) {
			path_len = next_path - path;
		} else {
			path_len = strlen(path);
		}

		// Make sure path_len isn't zero!
		if ( path_len == 0 )
			break;

		// Seek to this directory's index
		if ( fseek(rom_fp, rom_header.fnt_offset + 8*(*file_id & 0xFFF), SEEK_SET) ) {
			syslog(LOG_ERR, "Error %d seeking to dir entry in ROM\n", errno);
			return -EIO;
		}

		if ( read_le_uint32(rom_fp, &entry_start) ) {
			syslog(LOG_ERR, "Error %d reading entry_start from ROM\n", errno);
			return -EIO;
		}

		if ( read_le_uint16(rom_fp, &top_file_id) ) {
			syslog(LOG_ERR, "Error %d reading top_file_id from ROM\n", errno);
			return -EIO;
		}

		if ( fseek(rom_fp, rom_header.fnt_offset + entry_start, SEEK_SET) ) {
			syslog(LOG_ERR, "Error %d seeking into ROM\n", errno);
			return -EIO;
		}

		// Search the FAT table
		for (current_id=top_file_id; ; current_id++) {
			char entry_name[128];
			unsigned int name_length;

			syslog(LOG_DEBUG, "Reading file %d at %ld\n", current_id, ftell(rom_fp));

			if ( fread(flags, sizeof(*flags), 1, rom_fp) != 1 ) {
				syslog(LOG_ERR, "Error %d reading file flags from ROM\n", errno);
				return -EIO;
			}

			name_length = FAT_LENGTH(*flags);

			// We have looped each file and not found a match :(
			if ( name_length == 0 )
				return -ENOENT;

			// If this entries is too long or short we skip to the next entry
			if ( name_length != path_len ) {
				unsigned int skip_len = name_length;
				if ( FAT_IS_DIR(*flags) )
					skip_len += 2;
				if ( fseek(rom_fp, skip_len, SEEK_CUR) ) {
					syslog(LOG_ERR, "Error %d seeking to next entry\n", errno);
					return -EIO;
				}
				continue;
			}

			if ( fread(entry_name, 1, name_length, rom_fp) != name_length ) {
				syslog(LOG_ERR, "Error %d reading file name from ROM\n", errno);
				return -EIO;
			}

			// Now check the actual names
			entry_name[ name_length ] = '\0';
			if ( strncmp( entry_name, path, path_len ) != 0 ) {
				if ( FAT_IS_DIR(*flags) )
					if ( fseek(rom_fp, 2, SEEK_CUR) ) {
						syslog(LOG_ERR, "Error %d seeking to next entry\n", errno);
						return -EIO;
					}
				continue;
			}

			// The name matches
			if ( FAT_IS_DIR(*flags) ) {
				if ( read_le_uint16(rom_fp, file_id) ) {
					syslog(LOG_ERR, "Error %d reading dir id from ROM\n", errno);
					return -EIO;
				}

				break;
			} else {
				*file_id = current_id;
				// TODO if we search file a path such as /dir1/dir2/file1/dir3
				// We will stop at file1, where perhaps we should return a error saying file1 isn't a dir
				return 0;
			}
		}
	}

	return 0;
}

static int ndsfs_getattr(const char *path, struct stat *stbuf) {

	int ret = 0;
	uint16_t file_id;
	uint8_t flags;

	memset(stbuf, 0, sizeof(struct stat));

	pthread_mutex_lock(&rom_mutex);

	ret = ndsfs_resolve_path(path, &file_id, &flags);
	if ( ret )
		goto exit;

	stbuf->st_ino = file_id;

	if ( FAT_IS_DIR(flags) ) {
		stbuf->st_mode = S_IFDIR | 0755;
		stbuf->st_nlink = 2;

	} else if ( FAT_IS_FILE(flags) ) {
		uint32_t start, end;

		ret = ndsfs_get_size(file_id, &start, &end);
		if (ret)
			goto exit;

		stbuf->st_mode = S_IFREG | 0444;
		stbuf->st_nlink = 1;
		stbuf->st_size = end - start;
	} else
		ret = -ENOENT;

exit:
	pthread_mutex_unlock(&rom_mutex);
	return ret;
}

static int ndsfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi) {

	int ret = 0;
	uint16_t file_id;

	uint32_t entry_start; // reference location of entry name
	uint16_t top_file_id; // file ID of top entry
	uint16_t parent_id;   // ID of parent directory or directory count (root)

	uint8_t flags;
	uint16_t dir_id;

	(void) offset;
	(void) fi;

	pthread_mutex_lock(&rom_mutex);

	ret = ndsfs_resolve_path(path, &dir_id, &flags);
	if ( ret )
		goto exit;

	if ( !FAT_IS_DIR(flags) ) {
		ret = -ENOTDIR;
		goto exit;
	}

	// This is set here to make the code cleaner, not because there is an IO error
	ret = -EIO;

	if ( fseek(rom_fp, rom_header.fnt_offset + 8*(dir_id & 0xFFF), SEEK_SET) ) {
		syslog(LOG_ERR, "Error %d seeking to dir entry in ROM\n", errno);
		goto exit;
	}

	if ( read_le_uint32(rom_fp, &entry_start) ) {
		syslog(LOG_ERR, "Error %d reading entry_start from ROM\n", errno);
		goto exit;
	}

	if ( read_le_uint16(rom_fp, &top_file_id) ) {
		syslog(LOG_ERR, "Error %d reading top_file_id from ROM\n", errno);
		goto exit;
	}

	if ( read_le_uint16(rom_fp, &parent_id) ) {
		syslog(LOG_ERR, "Error %d reading parent_id from ROM\n", errno);
		goto exit;
	}

	filler_dir(buf, filler, ".",  dir_id);
	filler_dir(buf, filler, "..", parent_id);

	if ( fseek(rom_fp, rom_header.fnt_offset + entry_start, SEEK_SET) ) {
		syslog(LOG_ERR, "Error %d seeking into ROM\n", errno);
		goto exit;
	}

	for (file_id=top_file_id; ; file_id++) {
		uint8_t flags;
		char entry_name[128];
		unsigned int name_length;

		if ( fread(&flags, sizeof(flags), 1, rom_fp) != 1 ) {
			syslog(LOG_ERR, "Error %d reading file flags from ROM\n", errno);
			goto exit;
		}

		name_length = FAT_LENGTH(flags);

		// No more entries in the table?
		if (name_length == 0)
			break;

		if ( fread(entry_name, 1, name_length, rom_fp) != name_length ) {
			syslog(LOG_ERR, "Error %d reading file name from ROM\n", errno);
			goto exit;
		}

		entry_name[ name_length ] = '\0';

		if ( FAT_IS_DIR(flags) ) {
			uint16_t dir_id;
			if ( read_le_uint16(rom_fp, &dir_id) ) {
				syslog(LOG_ERR, "Error %d reading dir id from ROM\n", errno);
				goto exit;
			}

			filler_dir(buf, filler, entry_name, dir_id);
		} else {
			filler_file(buf, filler, entry_name, file_id);
		}
	}

	ret = 0;

exit:
	pthread_mutex_unlock(&rom_mutex);

	return ret;
}

static int ndsfs_open(const char *path, struct fuse_file_info *fi) {
	int ret = 0;
	uint16_t file_id;
	uint8_t flags;

	pthread_mutex_lock(&rom_mutex);

	ret = ndsfs_resolve_path(path, &file_id, &flags);
	if (ret)
		goto exit;

	if((fi->flags & 3) != O_RDONLY)
		ret = -EACCES;

exit:
	pthread_mutex_unlock(&rom_mutex);
	return ret;
}

static int ndsfs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {

	int ret = 0;
	uint16_t file_id;
	uint8_t flags;
	uint32_t start, end;
	uint32_t len;

	(void) fi;

	pthread_mutex_lock(&rom_mutex);

	ret = ndsfs_resolve_path(path, &file_id, &flags);
	if ( ret )
		goto exit;

	ret = ndsfs_get_size(file_id, &start, &end);
	if ( ret )
		goto exit;

	len = end - start;
	if (offset < len) {
		if (offset + size > len)
			size = len - offset;
		if ( fseek(rom_fp, start + offset, SEEK_SET) ) {
			syslog(LOG_ERR, "Error %d seeking to file inside ROM\n", errno);
			ret = -EIO;
			goto exit;
		}
		// TODO check for errors
		ret = fread(buf, 1, size, rom_fp);
	} else
		ret = 0;

exit:
	pthread_mutex_unlock(&rom_mutex);

	return ret;
}

static struct fuse_operations ndsfs_oper = {
	.getattr = ndsfs_getattr,
	.readdir = ndsfs_readdir,
	.open    = ndsfs_open,
	.read    = ndsfs_read,
};

static error_t parse_opt (int key, char *arg, struct argp_state *state) {
	/* Get the input argument from argp_parse, which we
	   know is a pointer to our arguments structure. */
	switch (key) {
 		case ARGP_KEY_ARG:
 			if ( state->arg_num == 0 )
 				rom_filename = arg;

			if (state->arg_num >= 2)
				/* Too many arguments. */
				argp_usage (state);
		break;

		case ARGP_KEY_END:
			if (state->arg_num < 2)
				/* Not enough arguments. */
				argp_usage (state);
			break;

		default:
			return ARGP_ERR_UNKNOWN;
	}

	return 0;
}

/* Our argp parser. */
static struct argp argp = { options, parse_opt, args_doc, doc };

int main(int argc, char *argv[]) {
	int ret;

	rom_fp = NULL;
	memset(&rom_header, 0, sizeof(rom_header));

	argp_parse (&argp, argc, argv, 0, 0, NULL);

	rom_fp = fopen( rom_filename, "rb" );

	if ( rom_fp == NULL ) {
		fprintf(stderr, "ndsfs: cannot open '%s' for reading: %s\n", rom_filename, strerror(errno));
		return -1;
	}

	// Now read the ROM header
	if ( fread(&rom_header, sizeof(rom_header), 1, rom_fp) != 1 ) {
		fprintf(stderr, "ndsfs: Failed to read NDS Rom header\n");
		fclose(rom_fp);
		rom_fp = NULL;
		return -1;
	}

	// TODO add checks to test the header is valid

	openlog("ndsfs", LOG_CONS|LOG_NDELAY|LOG_PERROR|LOG_PID,LOG_DAEMON);

	// Fudge the parameters a bit
	// TODO either don't use fuse_main, OR work out how many previous parameters there were
	//      and remove them
	argc --;
	argv[1] = argv[2];

	ret = fuse_main(argc, argv, &ndsfs_oper, NULL);

	if (ret)
		printf("\n");

	fclose( rom_fp );

	closelog();

	return ret;
}
