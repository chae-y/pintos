#include "filesys/fat.h"
#include "devices/disk.h"
#include "filesys/filesys.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include <stdio.h>
#include <string.h>

/* Should be less than DISK_SECTOR_SIZE */
struct fat_boot {
	unsigned int magic;
	unsigned int sectors_per_cluster; /* Fixed to 1 */
	unsigned int total_sectors;
	unsigned int fat_start;
	unsigned int fat_sectors; /* Size of FAT in sectors. */
	unsigned int root_dir_cluster;
};

/* FAT FS */
struct fat_fs {
	struct fat_boot bs;
	unsigned int *fat;
	unsigned int fat_length;
	disk_sector_t data_start;
	cluster_t last_clst;
	struct lock write_lock;
};

static struct fat_fs *fat_fs;

void fat_boot_create (void);
void fat_fs_init (void);

void
fat_init (void) {
	fat_fs = calloc (1, sizeof (struct fat_fs));
	if (fat_fs == NULL)
		PANIC ("FAT init failed");

	// Read boot sector from the disk
	unsigned int *bounce = malloc (DISK_SECTOR_SIZE);
	if (bounce == NULL)
		PANIC ("FAT init failed");
	disk_read (filesys_disk, FAT_BOOT_SECTOR, bounce);
	memcpy (&fat_fs->bs, bounce, sizeof (fat_fs->bs));
	free (bounce);

	// Extract FAT info
	if (fat_fs->bs.magic != FAT_MAGIC)
		fat_boot_create ();
	fat_fs_init ();
}

void
fat_open (void) {
	fat_fs->fat = calloc (fat_fs->fat_length, sizeof (cluster_t));
	if (fat_fs->fat == NULL)
		PANIC ("FAT load failed");

	// Load FAT directly from the disk
	uint8_t *buffer = (uint8_t *) fat_fs->fat;
	off_t bytes_read = 0;
	off_t bytes_left = sizeof (fat_fs->fat);
	const off_t fat_size_in_bytes = fat_fs->fat_length * sizeof (cluster_t);
	for (unsigned i = 0; i < fat_fs->bs.fat_sectors; i++) {
		bytes_left = fat_size_in_bytes - bytes_read;
		if (bytes_left >= DISK_SECTOR_SIZE) {
			disk_read (filesys_disk, fat_fs->bs.fat_start + i,
			           buffer + bytes_read);
			bytes_read += DISK_SECTOR_SIZE;
		} else {
			uint8_t *bounce = malloc (DISK_SECTOR_SIZE);
			if (bounce == NULL)
				PANIC ("FAT load failed");
			disk_read (filesys_disk, fat_fs->bs.fat_start + i, bounce);
			memcpy (buffer + bytes_read, bounce, bytes_left);
			bytes_read += bytes_left;
			free (bounce);
		}
	}
}

void
fat_close (void) {
	// Write FAT boot sector
	uint8_t *bounce = calloc (1, DISK_SECTOR_SIZE);
	if (bounce == NULL)
		PANIC ("FAT close failed");
	memcpy (bounce, &fat_fs->bs, sizeof (fat_fs->bs));
	disk_write (filesys_disk, FAT_BOOT_SECTOR, bounce);
	free (bounce);

	// Write FAT directly to the disk
	uint8_t *buffer = (uint8_t *) fat_fs->fat;
	off_t bytes_wrote = 0;
	off_t bytes_left = sizeof (fat_fs->fat);
	const off_t fat_size_in_bytes = fat_fs->fat_length * sizeof (cluster_t);
	for (unsigned i = 0; i < fat_fs->bs.fat_sectors; i++) {
		bytes_left = fat_size_in_bytes - bytes_wrote;
		if (bytes_left >= DISK_SECTOR_SIZE) {
			disk_write (filesys_disk, fat_fs->bs.fat_start + i,
			            buffer + bytes_wrote);
			bytes_wrote += DISK_SECTOR_SIZE;
		} else {
			bounce = calloc (1, DISK_SECTOR_SIZE);
			if (bounce == NULL)
				PANIC ("FAT close failed");
			memcpy (bounce, buffer + bytes_wrote, bytes_left);
			disk_write (filesys_disk, fat_fs->bs.fat_start + i, bounce);
			bytes_wrote += bytes_left;
			free (bounce);
		}
	}
}

void
fat_create (void) {
	// Create FAT boot
	fat_boot_create ();
	fat_fs_init ();

	// Create FAT table
	fat_fs->fat = calloc (fat_fs->fat_length, sizeof (cluster_t));
	if (fat_fs->fat == NULL)
		PANIC ("FAT creation failed");

	// Set up ROOT_DIR_CLST
	fat_put (ROOT_DIR_CLUSTER, EOChain);

	// Fill up ROOT_DIR_CLUSTER region with 0
	uint8_t *buf = calloc (1, DISK_SECTOR_SIZE);
	if (buf == NULL)
		PANIC ("FAT create failed due to OOM");
	disk_write (filesys_disk, cluster_to_sector (ROOT_DIR_CLUSTER), buf);
	free (buf);
}

void
fat_boot_create (void) {
	unsigned int fat_sectors =
	    (disk_size (filesys_disk) - 1)
	    / (DISK_SECTOR_SIZE / sizeof (cluster_t) * SECTORS_PER_CLUSTER + 1) + 1;
	fat_fs->bs = (struct fat_boot){
	    .magic = FAT_MAGIC,
	    .sectors_per_cluster = SECTORS_PER_CLUSTER,
	    .total_sectors = disk_size (filesys_disk),
	    .fat_start = 1,
	    .fat_sectors = fat_sectors,
	    .root_dir_cluster = ROOT_DIR_CLUSTER,
	};
}

/*FAT 파일 시스템을 초기화합니다. 
fat_fs의 fat_length 및 data_start 필드를 초기화해야 합니다. 
fat_length는 파일 시스템의 클러스터 수를 저장하고 data_start는 파일 저장을 시작할 수 있는 섹터를 저장합니다. 
fat_fs->bs에 저장된 일부 값을 악용할 수 있습니다. 
또한 이 함수에서 다른 유용한 데이터를 초기화할 수 있습니다.*/
void
fat_fs_init (void) {
	/* TODO: Your code goes here. */
	unsigned int data_sectors = fat_fs->bs.total_sectors - fat_fs->bs.fat_sectors -1;
	fat_fs->fat_length = data_sectors / fat_fs->bs.sectors_per_cluster; //FLOOR(data_sectors, fat_fs->bs.sectors_per_cluster); #define FLOOR(x, y) ((x) / (y))
	fat_fs->data_start = fat_fs->bs.fat_start + fat_fs->bs.fat_sectors;
	fat_fs->last_clst = ROOT_DIR_CLUSTER + 1; //라스트 추가해주는...? 1+1??
	lock_init (&fat_fs->write_lock); // 락 초기화
}

/*----------------------------------------------------------------------------*/
/* FAT handling                                                               */
/*----------------------------------------------------------------------------*/

/* Add a cluster to the chain.
 * If CLST is 0, start a new chain.
 * Returns 0 if fails to allocate a new cluster. */
/*clst(클러스터 인덱싱 번호)에 지정된 클러스터 뒤에 클러스터를 추가하여 체인을 확장합니다.
clst가 0이면 새 체인을 만듭니다. 
새로 할당된 클러스터의 클러스터 번호를 반환합니다. */
cluster_t
fat_create_chain (cluster_t clst) {
	/* TODO: Your code goes here. */
	cluster_t testing = fat_fs->last_clst;
	if (testing >= ROOT_DIR_CLUSTER + fat_fs->fat_length) // testing이 범위를 넘어서면? (1+length) 안돼
		return 0;
	// while (fat_get (testing) != 0) { // last가 길이가 얼마나 될지 모르니까 while문 쓴건가 0이 되면 그자리에 없는거니까
	// 	testing += 1;
	// 	if (testing >= ROOT_DIR_CLUSTER + fat_fs->fat_length) // testing이 범위를 넘어서면? (1+length) 안돼
	// 		return 0;
	// }

	if (clst != 0) {
		fat_put (clst, testing);//testing 다음 위치에  넣어줌
	}
	fat_put (testing, EOChain); // 마지막 표시 end of chain

	while(true){//내가 바꿈
		if(fat_get(++testing)==0){
			lock_acquire (&fat_fs->write_lock);
			fat_fs->last_clst = testing + 1; //왜 보호되어야하는거지...
			lock_release (&fat_fs->write_lock);
		}
	}

	// lock_acquire (&fat_fs->write_lock);
	// fat_fs->last_clst = testing + 1; //왜 보호되어야하는거지...
	// lock_release (&fat_fs->write_lock);

	return testing;
}

/* Remove the chain of clusters starting from CLST.
 * If PCLST is 0, assume CLST as the start of the chain. */
/*clst에서 시작하여 체인에서 클러스터를 제거합니다. 
pclst는 체인의 직접 이전 클러스터여야 합니다. 
이것은 이 함수를 실행한 후 pclst가 업데이트된 체인의 마지막 요소여야 함을 의미합니다. 
clst가 체인의 첫 번째 요소인 경우 pclst는 0이어야 합니다.*/
void
fat_remove_chain (cluster_t clst, cluster_t pclst) { //clst에서 시작하고 pclst는 마지막요소
	/* TODO: Your code goes here. */
	if (pclst != 0) { //체인의 첫번째 요소가 아닌경우
		ASSERT (fat_get (pclst) == clst); //get하면 다음꺼니까
		fat_put(pclst, EOChain); //다음꺼에 endof chain 그치 다음꺼부터 비울거니까
	}

	cluster_t current_clst = clst;
	cluster_t next;
	while (current_clst != 0) {
		next = fat_get (current_clst);
		fat_put (current_clst, 0); //지우니까 0 넣고
		if (current_clst < fat_fs->last_clst) { //last_clst업데이트 해주기 위해
			lock_acquire (&fat_fs->write_lock);
			fat_fs->last_clst = current_clst;
			lock_release (&fat_fs->write_lock);
		}
		if (next == EOChain) break;
		current_clst = next;
	}
}

/* Update a value in the FAT table. */
/*클러스터 번호 clst가 가리키는 FAT 항목을 val로 업데이트합니다. 
FAT의 각 항목은 체인의 다음 클러스터를 가리키기 때문에(존재하는 경우, 그렇지 않은 경우 EOChain) 
연결을 업데이트하는 데 사용할 수 있습니다.*/
void
fat_put (cluster_t clst, cluster_t val) {
	/* TODO: Your code goes here. */
	*(fat_fs->fat + clst) = val;
}

/* Fetch a value in the FAT table. */
//주어진 클러스터 clst가 가리키는 클러스터 번호를 반환합니다.
cluster_t
fat_get (cluster_t clst) {
	/* TODO: Your code goes here. */
	return *(fat_fs->fat + clst);
}

/* Covert a cluster # to a sector number. */
//클러스터 번호 clst를 해당 섹터 번호로 변환하고 섹터 번호를 반환합니다.
disk_sector_t
cluster_to_sector (cluster_t clst) {
	/* TODO: Your code goes here. */
	return fat_fs->data_start + (clst - 1) * fat_fs->bs.sectors_per_cluster;
}
