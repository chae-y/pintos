#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "devices/disk.h"
#include "filesys/fat.h"

/* The disk that contains the file system. */
struct disk *filesys_disk;

static void do_format (void);

/* Initializes the file system module.
 * If FORMAT is true, reformats the file system. */
void
filesys_init (bool format) {
	filesys_disk = disk_get (0, 1);
	if (filesys_disk == NULL)
		PANIC ("hd0:1 (hdb) not present, file system initialization failed");

	inode_init ();

#ifdef EFILESYS
	fat_init ();

	if (format)
		do_format ();

	fat_open ();
#else
	/* Original FS */
	free_map_init ();

	if (format)
		do_format ();

	free_map_open ();
#endif
}

/* Shuts down the file system module, writing any unwritten data
 * to disk. */
void
filesys_done (void) {
	/* Original FS */
#ifdef EFILESYS
	fat_close ();
#else
	free_map_close ();
#endif
}

/* Creates a file named NAME with the given INITIAL_SIZE.
 * Returns true if successful, false otherwise.
 * Fails if a file named NAME already exists,
 * or if internal memory allocation fails. */
bool
filesys_create (const char *name, off_t initial_size) {
	disk_sector_t inode_sector = 0;
	struct dir *dir = dir_open_root ();
	bool success = (dir != NULL
			&& free_map_allocate (1, &inode_sector)
			&& inode_create (inode_sector, initial_size)
			&& dir_add (dir, name, inode_sector));
	if (!success && inode_sector != 0)
		free_map_release (inode_sector, 1);
	dir_close (dir);

	return success;
}

/* Opens the file with the given NAME.
 * Returns the new file if successful or a null pointer
 * otherwise.
 * Fails if no file named NAME exists,
 * or if an internal memory allocation fails. */
struct file *
filesys_open (const char *name) {
	struct dir *dir = dir_open_root ();
	struct inode *inode = NULL;
	if (dir != NULL)
		dir_lookup (dir, name, &inode);
	dir_close (dir);

	return file_open (inode);
}

/* Deletes the file named NAME.
 * Returns true if successful, false on failure.
 * Fails if no file named NAME exists,
 * or if an internal memory allocation fails. */
bool
filesys_remove (const char *name) {
	struct dir *dir = dir_open_root ();
	bool success = dir != NULL && dir_remove (dir, name);
	dir_close (dir);

	return success;
}

bool filesys_create_dir(const char* name) {

    bool success = false;

    /* name의파일경로를cp_name에복사*/
    char* cp_name = (char *)malloc(strlen(name) + 1);
    strlcpy(cp_name, name, strlen(name) + 1);

    /* name 경로분석*/
    char* file_name = (char *)malloc(strlen(name) + 1);
    struct dir* dir = parse_path(cp_name, file_name);


    /* bitmap에서inodesector번호할당*/
    cluster_t inode_cluster = fat_create_chain(0);
    struct inode *sub_dir_inode;
    struct dir *sub_dir = NULL;


    /* 할당받은sector에file_name의디렉터리생성*/
    /* 디렉터리엔트리에file_name의엔트리추가*/
    /* 디렉터리엔트리에‘.’, ‘..’ 파일의엔트리추가*/
    success = (//! ADD : ".", ".." 추가
                dir != NULL
               && dir_create(inode_cluster, 16)
               && dir_add(dir, file_name, inode_cluster)
               && dir_lookup(dir, file_name, &sub_dir_inode)
               && dir_add(sub_dir = dir_open(sub_dir_inode), ".", inode_cluster)
               && dir_add(sub_dir, "..", inode_get_inumber(dir_get_inode(dir))));


    if (!success && inode_cluster != 0)
        fat_remove_chain(inode_cluster, 0);

    dir_close(sub_dir);
    dir_close(dir);

    free(cp_name);
    free(file_name);
    return success;
}

struct dir *parse_path(char *path_name, char *file_name)
{
    struct dir *dir = NULL;
    if (path_name == NULL)// || file_name == NULL)//방금 멜록한거 아님? 내맘대로 빼기 chae
        return NULL;
    if (strlen(path_name) == 0)
        return NULL;
    /* PATH_NAME의절대/상대경로에따른디렉터리정보저장(구현)*/

    if(path_name[0] == '/')
    {
        dir = dir_open_root();
    }
    else
        dir = dir_reopen(thread_current()->cur_dir);


    char *token, *nextToken, *savePtr;
    token = strtok_r(path_name, "/", &savePtr);
    nextToken = strtok_r(NULL, "/", &savePtr);

    // 처음부터 /가 나오는 경우->절대 경로??? 현재인가봐
    // 여기서 의미하는거는 그냥 /일떄 현재로 해주는거 같은데 잘 모르겠어^^
    if(token == NULL)
    {
        token = (char*)malloc(2);
        strlcpy(token, ".", 2);
    }

    struct inode *inode;
    while (token != NULL && nextToken != NULL)
    {
        /* dir에서token이름의파일을검색하여inode의정보를저장*/
        if (!dir_lookup(dir, token, &inode))
        {
            dir_close(dir);
            return NULL;
        }

        if(inode->data.is_link)
        {   //! 링크 파일인 경우

            char* new_path = (char*)malloc(sizeof(strlen(inode->data.link_name)) + 1);
            strlcpy(new_path, inode->data.link_name, strlen(inode->data.link_name) + 1);

            //_ 복사를 해야만 제대로 뽑아지더라..
            strlcpy(path_name, new_path, strlen(new_path) + 1);
            free(new_path);
 
            strlcat(path_name, "/", strlen(path_name) + 2);
            strlcat(path_name, nextToken, strlen(path_name) + strlen(nextToken) + 1);
            strlcat(path_name, savePtr, strlen(path_name) + strlen(savePtr) + 1);

            dir_close(dir);

            //! 파싱된 경로로 다시 시작한다
            if(path_name[0] == '/')
            {
                dir = dir_open_root();
            }
            else
                dir = dir_reopen(thread_current()->cur_dir);


            token = strtok_r(path_name, "/", &savePtr);
            nextToken = strtok_r(NULL, "/", &savePtr);

            continue;
        }
        
        /* inode가파일일경우NULL 반환*/
        if(!inode_is_dir(inode))
        {
            dir_close(dir);
            inode_close(inode);
            return NULL;
        }
        /* dir의디렉터리정보를메모리에서해지*/
        dir_close(dir);

        /* inode의디렉터리정보를dir에저장*/
        dir = dir_open(inode);

        /* token에검색할경로이름저장*/
        token = nextToken;
        nextToken = strtok_r(NULL, "/", &savePtr);
    }
    /* token의파일이름을file_name에저장*/
    strlcpy (file_name, token, strlen(token) + 1);

    /* dir정보반환*/
    return dir;
}

/* Formats the file system. */
static void
do_format (void) {
	printf ("Formatting file system...");

#ifdef EFILESYS
	/* Create FAT and save it to the disk. */
	fat_create ();
	fat_close ();
#else
	free_map_create ();
	if (!dir_create (ROOT_DIR_SECTOR, 16))
		PANIC ("root directory creation failed");
	free_map_close ();
#endif

	printf ("done.\n");
}
