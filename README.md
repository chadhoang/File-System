# File System
## About 
This project supports the implementation of a simple file system. The file system is based on File Allocation Table (FAT) design (link: https://en.wikipedia.org/wiki/File_Allocation_Table). 

This file system is implemented on top of a virtual disk, which is essentially a simple binary file that is stored on the actual file system provided by your computer. The virtual disk is logically split into blocks, which mimics the implementation of real hard drives split into sectors. This file system seeks to perform the same essential duties seen in other typical file systems, such as reading, writing, and deleting files, through organizing data onto these data blocks.

## Features And Specifications
For this project, the specifications for a very simple file system have been defined: it’s the  **ECS150-FS**  file system.

The layout of ECS150-FS on a disk is composed of four consecutive logical parts:

-   The  _Superblock_  is the very first block of the disk and contains information about the file system (number of blocks, size of the FAT, etc.)
-   The  _File Allocation Table_  is located on one or more blocks, and keeps track of both the free data blocks and the mapping between files and the data blocks holding their content.
-   The  _Root directory_  is in the following block and contains an entry for each file of the file system, defining its name, size and the location of the first data block for this file. **It supports up to 128 file entries.**
-   Finally, all the remaining blocks are  _Data blocks_  and are used by the content of files.

The size of virtual disk blocks is  **4096 bytes**.

### Superblock
The superblock is the first block of the file system. Its internal format is:
| Offset|Length (bytes)|Description|
| - | - | - |
|0x00|8|Signature ("ECS150")
|0x08|2|Total amount of blocks of virtual disk
|0x0A|2|Root directory block index
|0x0C|2|Data block start index
|0x0E|2|Amount of data blocks
|0x10|1|Number of blocks for FAT
|0x11|4079|Unused/Padding
### File Allocation Table (FAT)
The File Allocation Table (FAT) is a flat array, which entries are composed of **16-bit unsigned words**/**16-bit wide** and can possibly span several data blocks. *There are as many FAT entries as data blocks in this file system.*

The first entry of the FAT (entry #0) is always invalid and contains the special `FAT_EOC` (_End-of-Chain_) value which is `0xFFFF`. Entries marked as 0 correspond to free data blocks. Entries containing a positive value are part of a chainmap and represent a link to the next block in the chainmap.

Here is an example of what the content of a FAT may contain:
|FAT Index|0|1|2|3|4|5|...|
|-|-|-|-|-|-|-|-|
|Content|0xFFFF|4|3|0xFFFF|0xFFFF|0|...|
### Root Directory
The root directory is an array of 128 entries stored in the data block immediately following the File Allocation Table. Each entry is **32-byte wide** and describes a file according to the following format:
|Offset|Length (bytes)|Description|
|-|-|-|
|0x00|16|Filename (including NULL character)
|0x10|4|Size of the file (bytes)
|0x14|2|Index of the first data block
|0x16|10|Unused/Padding
*Empty entries* are defined by the first character of an entry's filename being equal to the NULL character.

The *entry for an empty file* would have its size be 0 and the index of the first data block be `FAT_EOC`.

Here's an example of what the content of a root directory can be:
|Filename|Size|Index of first data block|Padding|
|-|-|-|-|
|test1\0|18000|2|xxx...|
|test2\0|5000|1|xxx...|
|test3\0|0|`FAT_EOC`|xxx...|
|\0|xxx...|xxx...|xxx...|
|...|...|...|...|
## How To Use
This filesystem is compiled using GCC, so please make sure GCC is installed into your computer (link: https://gcc.gnu.org/install/).


The *Block API*, located in `../File_System/libfs/disk.h`, is used to open or close a virtual disk, and read or write entire blocks from it. 

The *FS API*, located in `../File_System/libfs/fs.h`, is in charge of the actual file system management and is used to mount a virtual disk, list the files that are part of the disk, add or delete new files, read from files or write to files, etc.

Please refer to `../File_System/apps/scripts/README.md` for information on how to test the file system using scripts.

Please also refer to `../File_System/apps/test_fs.c` on how you may implement the file system onto your C application file(s).

When making a new application file, make sure that it is included as a target program in `../File_System/apps/Makefile` and that proper headers are included with your application file(s). Your application executable may then be created by entering `make` into the command line while in the directory `../File_System/libfs/Makefile` .

## Lessons Learned
I was introduced to a variety of file system designs through working on this project, and in particular the FAT file system. A lot of the concepts of memory storage and file systems, in regards to operating systems, have been covered through this project. I now better understand how to manage data and memory through this whole process, strengthening my knowledge of the C language's memory management along with its data structures. 
