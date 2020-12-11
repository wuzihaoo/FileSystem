#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "fs.h"
#include "fs_util.h"
#include "disk.h"

char inodeMap[MAX_INODE / 8];
char blockMap[MAX_BLOCK / 8];
Inode inode[MAX_INODE];
SuperBlock superBlock;
Dentry curDir;
int curDirBlock;

int fs_mount(char *name)
{
		int numInodeBlock =  (sizeof(Inode)*MAX_INODE)/ BLOCK_SIZE;
		int i, index, inode_index = 0;

		// load superblock, inodeMap, blockMap and inodes into the memory
		if(disk_mount(name) == 1) {
				disk_read(0, (char*) &superBlock);
				disk_read(1, inodeMap);
				disk_read(2, blockMap);
				for(i = 0; i < numInodeBlock; i++)
				{
						index = i+3;
						disk_read(index, (char*) (inode+inode_index));
						inode_index += (BLOCK_SIZE / sizeof(Inode));
				}
				// root directory
				curDirBlock = inode[0].directBlock[0];
				disk_read(curDirBlock, (char*)&curDir);

		} else {
				// Init file system superblock, inodeMap and blockMap
				superBlock.freeBlockCount = MAX_BLOCK - (1+1+1+numInodeBlock);
				superBlock.freeInodeCount = MAX_INODE;

				//Init inodeMap
				for(i = 0; i < MAX_INODE / 8; i++)
				{
						set_bit(inodeMap, i, 0);
				}
				//Init blockMap
				for(i = 0; i < MAX_BLOCK / 8; i++)
				{
						if(i < (1+1+1+numInodeBlock)) set_bit(blockMap, i, 1);
						else set_bit(blockMap, i, 0);
				}
				//Init root dir
				int rootInode = get_free_inode();
				curDirBlock = get_free_block();

				inode[rootInode].type =directory;
				inode[rootInode].owner = 0;
				inode[rootInode].group = 0;
				gettimeofday(&(inode[rootInode].created), NULL);
				gettimeofday(&(inode[rootInode].lastAccess), NULL);
				inode[rootInode].size = 1;
				inode[rootInode].blockCount = 1;
				inode[rootInode].directBlock[0] = curDirBlock;

				curDir.numEntry = 1;
				strncpy(curDir.dentry[0].name, ".", 1);
				curDir.dentry[0].name[1] = '\0';
				curDir.dentry[0].inode = rootInode;
				disk_write(curDirBlock, (char*)&curDir);
		}
		return 0;
}

int fs_umount(char *name)
{
		int numInodeBlock =  (sizeof(Inode)*MAX_INODE )/ BLOCK_SIZE;
		int i, index, inode_index = 0;
		disk_write(0, (char*) &superBlock);
		disk_write(1, inodeMap);
		disk_write(2, blockMap);
		for(i = 0; i < numInodeBlock; i++)
		{
				index = i+3;
				disk_write(index, (char*) (inode+inode_index));
				inode_index += (BLOCK_SIZE / sizeof(Inode));
		}
		// current directory
		disk_write(curDirBlock, (char*)&curDir);

		disk_umount(name);	
}

int search_cur_dir(char *name)
{
		// return inode. If not exist, return -1
		int i;

		for(i = 0; i < curDir.numEntry; i++)
		{
				if(command(name, curDir.dentry[i].name)) return curDir.dentry[i].inode;
		}
		return -1;
}

int file_create(char *name, int size)
{
		int i;

		// comment out the following error msg.
		if(size > SMALL_FILE) {
				printf("Do not support files larger than %d bytes yet.\n", SMALL_FILE);
				return -1;
		}

		if(size > LARGE_FILE) {
				printf("Do not support files larger than %d bytes.\n", LARGE_FILE);
				return -1;
		}

		if(size < 0){
				printf("File create failed: cannot have negative size\n");
				return -1;
		}

		int inodeNum = search_cur_dir(name); 
		if(inodeNum >= 0) {
				printf("File create failed:  %s exist.\n", name);
				return -1;
		}

		if(curDir.numEntry + 1 > MAX_DIR_ENTRY) {
				printf("File create failed: directory is full!\n");
				return -1;
		}

		int numBlock = size / BLOCK_SIZE;
		if(size % BLOCK_SIZE > 0) numBlock++;

		if(numBlock > superBlock.freeBlockCount) {
				printf("File create failed: not enough space\n");
				return -1;
		}

		if(superBlock.freeInodeCount < 1) {
				printf("File create failed: not enough inode\n");
				return -1;
		}

		char *tmp = (char*) malloc(sizeof(int) * size+1);

		//generate random string
		rand_string(tmp, size);
		printf("rand_string = %s\n", tmp);

		// get inode and fill it
		inodeNum = get_free_inode();
		if(inodeNum < 0) {
				printf("File_create error: not enough inode.\n");
				return -1;
		}

		inode[inodeNum].type = file;
		inode[inodeNum].owner = 1;
		inode[inodeNum].group = 2;
		gettimeofday(&(inode[inodeNum].created), NULL);
		gettimeofday(&(inode[inodeNum].lastAccess), NULL);
		inode[inodeNum].size = size;
		inode[inodeNum].blockCount = numBlock;

		// add a new file into the current directory entry
		strncpy(curDir.dentry[curDir.numEntry].name, name, strlen(name));
		curDir.dentry[curDir.numEntry].name[strlen(name)] = '\0';
		curDir.dentry[curDir.numEntry].inode = inodeNum;
		printf("curdir %s, name %s\n", curDir.dentry[curDir.numEntry].name, name);
		curDir.numEntry++;

		// get data blocks
		for(i = 0; i < numBlock; i++)
		{
				int block = get_free_block();
				if(block == -1) {
						printf("File_create error: get_free_block failed\n");
						return -1;
				}
				inode[inodeNum].directBlock[i] = block;

				disk_write(block, tmp+(i*BLOCK_SIZE));
		}

		//update last access of current directory
		gettimeofday(&(inode[ curDir.dentry[0].inode  ].lastAccess), NULL);		

		printf("file created: %s, inode %d, size %d\n", name, inodeNum, size);

		free(tmp);
		return 0;
}

int file_cat(char *name)
{
		int inodeNum, i, size;
		char str_buffer[512];
		char * str;

		//get inode
		inodeNum = search_cur_dir(name);
		size = inode[inodeNum].size;

		//check if valid input
		if( inodeNum < 0 )
		{
				printf("cat error: file not found\n");
				return -1;
		}
		if( inode[inodeNum].type == directory )
		{
				printf("cat error: cannot read directory\n");
				return -1;
		}

		//allocate a string buffer
		str = (char *) malloc( sizeof(char) * (size+1) );
		str[ size ] = '\0';


		for( i = 0; i < inode[inodeNum].blockCount; i++ ){
				int block;
				block = inode[inodeNum].directBlock[i];

				disk_read( block, str_buffer );

				if( size >= BLOCK_SIZE )
				{
						memcpy( str+i*BLOCK_SIZE, str_buffer, BLOCK_SIZE );
						size -= BLOCK_SIZE;
				}
				else
				{
						memcpy( str+i*BLOCK_SIZE, str_buffer, size );
				}
		}
		printf("%s\n", str);

		//update lastAccess
		gettimeofday( &(inode[inodeNum].lastAccess), NULL );

		free(str);

		//return success
		return 0;
}

int file_read(char *name, int offset, int size)
{
		int inodeNum, i, block;
		char str_buffer[BLOCK_SIZE];
		char * str;	

		//get inode
		inodeNum = search_cur_dir(name);

		//check if the inode is valid
		if( inodeNum < 0 )
		{
				printf("read error: file not found\n");
				return -1;
		}
		if( inode[inodeNum].type == directory )
		{
				printf("read error: cannot read directory\n");
				return -1;
		}
		if( offset > inode[inodeNum].size )
		{
				printf("read error: offset larger than file\n");
				return -1;
		}
		if( offset < 0 || size < 0 )
		{
				printf("read error: input cannot be negative\n");
				return -1;
		}
		if( offset + size > inode[inodeNum].size )
		{
				printf("read error: offset and size are out of bounds\n");
				return -1;
		}

		//Allocate a string buffer
		str = (char *) malloc( sizeof(char) * (size+1) );
		str[ size ] = '\0';

		i = 0;
		block = offset / BLOCK_SIZE;
		offset = offset % BLOCK_SIZE;

		while( i < size )
		{
				disk_read( inode[inodeNum].directBlock[block], str_buffer );

				if(offset+size-i > BLOCK_SIZE)
				{
						memcpy(str+i, str_buffer+offset, BLOCK_SIZE-offset);
						i = i + BLOCK_SIZE - offset;
				}
				else
				{
						memcpy(str+i, str_buffer+offset, size-i);
						i = size;
				}
				offset = 0;

				block++;
		}

		printf("%s\n", str);

		free(str);

		//update timestamp
		gettimeofday( &(inode[inodeNum].lastAccess), NULL );

		return 0;
}

int file_write(char *name, int offset, int size, char *buf)
{	
		int inode_index;
		
		inode_index = search_cur_dir(name);

		if(inode[inode_index].type == directory){
				printf("file write failed. %s is a directory\n",name );
				return -1;
		}
		if(strlen(buf)!=size){
			 printf("file write failed. strlen(buffer) != size\n");
				return -1;
		}
		if(inode_index < 0){
				printf("file write failed. File %s does not exist.\n", name);
				return -1;
		}

		if(offset + size > inode[inode_index].size){
				printf("file write failed. Size+Offset cannot exceed the original file size.\n");
				return -1;
		}

		int remain = size;
		int count = 0;      
		int startBlock = offset/BLOCK_SIZE;
		if(offset < BLOCK_SIZE)
				startBlock = 0;

		char *data = (char*)malloc(size);
		char *tmp = (char*)malloc(BLOCK_SIZE);
		int curBlock = startBlock;

		while(remain > 0){

				if(count == 0)
				{
						disk_read(inode[inode_index].directBlock[curBlock],tmp);
						int startOffset = offset - startBlock*BLOCK_SIZE;
						if((startOffset+ size) > BLOCK_SIZE)  // data in more than one block
						{
								memcpy(tmp+startOffset, buf, BLOCK_SIZE-startOffset);
								count = BLOCK_SIZE-startOffset;
						}
						else //data in one block
						{
								memcpy(tmp+startOffset, buf, size);
								count = size;
						}
						disk_write(inode[inode_index].directBlock[curBlock],tmp);
						curBlock++;
						remain = size - count;
						continue;
				}
				if(remain >= BLOCK_SIZE)
				{
						memcpy(tmp,buf+count,BLOCK_SIZE);
						disk_write(inode[inode_index].directBlock[curBlock],tmp);
						curBlock++;
						count = count + BLOCK_SIZE;
						remain = remain - BLOCK_SIZE;
						continue;
				}
				if(remain < BLOCK_SIZE)
				{
						disk_read(inode[inode_index].directBlock[curBlock],tmp);

						memcpy(tmp,buf+count,remain);
						disk_write(inode[inode_index].directBlock[curBlock],tmp);
						remain = 0;
						break;
				}

		}

		gettimeofday(&(inode[inode_index].lastAccess), NULL);
		printf("file write succeed.\n" );

		return 0;
}

int file_stat(char *name)
{
		char timebuf[28];
		int inodeNum = search_cur_dir(name);
		if(inodeNum < 0) {
				printf("file cat error: file is not exist.\n");
				return -1;
		}

		printf("Inode = %d\n", inodeNum);
		if(inode[inodeNum].type == file) printf("type = file\n");
		else printf("type = directory\n");
		printf("owner = %d\n", inode[inodeNum].owner);
		printf("group = %d\n", inode[inodeNum].group);
		printf("size = %d\n", inode[inodeNum].size);
		printf("num of block = %d\n", inode[inodeNum].blockCount);
		format_timeval(&(inode[inodeNum].created), timebuf, 28);
		printf("Created time = %s\n", timebuf);
		format_timeval(&(inode[inodeNum].lastAccess), timebuf, 28);
		printf("Last accessed time = %s\n", timebuf);

		return 0;
}

int file_remove(char *name)
{
		int inodeNum, i;

		//get inode number
		inodeNum = search_cur_dir(name);

		//validate
		if(inodeNum < 0) 
		{
				printf("rm error: %s does exist\n", name);
				return -1;
		}
		if(inode[inodeNum].type != file)
		{
				printf("rm error: %s is not a file\n", name);
				return -1;
		}

		//free data blocks
		for( i = 0; i < inode[inodeNum].blockCount; i++ )
		{
				set_bit(blockMap, inode[inodeNum].directBlock[i], 0);
				superBlock.freeBlockCount++;
		}	

		//free inode
		set_bit(inodeMap, inodeNum, 0);
		superBlock.freeInodeCount++;

		//shift directory contents down
		for( i = 0; i < curDir.numEntry; i++ )
		{
				if( curDir.dentry[i].inode == inodeNum )
						break;
		}
		for( i; i < curDir.numEntry - 1; i++ )
		{
				curDir.dentry[i] = curDir.dentry[i+1];
		}

		//decrement number of entries in directory
		curDir.numEntry--;
		//update last access of current directory
		gettimeofday(&(inode[ curDir.dentry[0].inode  ].lastAccess), NULL);		

		printf("file removed: %s\n", name);

		return 0;
}

int dir_make(char* name)
{
		printf("Do not support directory yet.\n");
		return 0;
}

int dir_remove(char *name)
{
		printf("Do not support directory yet.\n");
		return 0;
}

int dir_change(char* name)
{
		printf("Do not support directory yet.\n");
		return 0;
}

int ls()
{
		int i;
		for(i = 0; i < curDir.numEntry; i++)
		{
				int n = curDir.dentry[i].inode;
				if(inode[n].type == file) printf("type: file, ");
				else printf("type: dir, ");
				printf("name \"%s\", inode %d, size %d byte\n", curDir.dentry[i].name, curDir.dentry[i].inode, inode[n].size);
		}

		return 0;
}


int fs_stat()
{
		printf("File System Status: \n");
		printf("# of free blocks: %d (%d bytes), # of free inodes: %d\n", superBlock.freeBlockCount, superBlock.freeBlockCount*512, superBlock.freeInodeCount);
}

int execute_command(char *comm, char *arg1, char *arg2, char *arg3, char *arg4, int numArg)
{
		if(command(comm, "create")) {
				if(numArg < 2) {
						printf("error: create <filename> <size>\n");
						return -1;
				}
				return file_create(arg1, atoi(arg2)); // (filename, size)
		} else if(command(comm, "cat")) {
				if(numArg < 1) {
						printf("error: cat <filename>\n");
						return -1;
				}
				return file_cat(arg1); // file_cat(filename)
		} else if(command(comm, "write")) {
				if(numArg < 4) {
						printf("error: write <filename> <offset> <size> <buf>\n");
						return -1;
				}
				return file_write(arg1, atoi(arg2), atoi(arg3), arg4); // file_write(filename, offset, size, buf);
		}	else if(command(comm, "read")) {
				if(numArg < 3) {
						printf("error: read <filename> <offset> <size>\n");
						return -1;
				}
				return file_read(arg1, atoi(arg2), atoi(arg3)); // file_read(filename, offset, size);
		} else if(command(comm, "rm")) {
				if(numArg < 1) {
						printf("error: rm <filename>\n");
						return -1;
				}
				return file_remove(arg1); //(filename)
		} else if(command(comm, "mkdir")) {
				if(numArg < 1) {
						printf("error: mkdir <dirname>\n");
						return -1;
				}
				return dir_make(arg1); // (dirname)
		} else if(command(comm, "rmdir")) {
				if(numArg < 1) {
						printf("error: rmdir <dirname>\n");
						return -1;
				}
				return dir_remove(arg1); // (dirname)
		} else if(command(comm, "cd")) {
				if(numArg < 1) {
						printf("error: cd <dirname>\n");
						return -1;
				}
				return dir_change(arg1); // (dirname)
		} else if(command(comm, "ls"))  {
				return ls();
		} else if(command(comm, "stat")) {
				if(numArg < 1) {
						printf("error: stat <filename>\n");
						return -1;
				}
				return file_stat(arg1); //(filename)
		} else if(command(comm, "df")) {
				return fs_stat();
		} else {
				fprintf(stderr, "%s: command not found.\n", comm);
				return -1;
		}
		return 0;
}

