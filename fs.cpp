#include <iostream>
#include <iomanip>
#include "fs.h"

FS::FS()
{
  std::cout << "FS::FS()... Creating file system\n";
}

FS::~FS()
{
}

// TODO:
/*
  check existing filenames when creating file .
  create general findDir function
  ls command (show all files in root)
*/
// formats the disk, i.e., creates an empty file system
int FS::format()
{
  std::cout << "FS::format()\n";

  // initialize FAT
  fat[ROOT_BLOCK] = FAT_EOF;
  fat[FAT_BLOCK] = FAT_EOF;

  // rest of the blocks are marked as free
  for (int i = 2; i < BLOCK_SIZE / 2; i++)
    fat[i] = FAT_FREE;

  // write entire FAT to disk
  disk.write(FAT_BLOCK, (uint8_t *)fat);

  // create dir_entry for root directory
  dir_entry dir_ent;
  dir_ent.file_name[0] = '/';
  dir_ent.size = 0;
  dir_ent.first_blk = 0;
  dir_ent.type = TYPE_DIR;
  dir_ent.access_rights = READ | WRITE | EXECUTE;

  // write dir_entry for root to disk
  // dir_entry* de_ptr = &dir_ent;
  dir_entry temp_array[BLOCK_SIZE / 64];
  temp_array[0] = dir_ent;

  dir_ent.type = TYPE_EMPTY;
  dir_ent.access_rights = 0;

  for (int i = 1; i < BLOCK_SIZE / 64; i++)
  {
    temp_array[i] = dir_ent;
  }

  disk.write(ROOT_BLOCK, (uint8_t *)temp_array);

  return 0;
}

// create <filepath> creates a new file on the disk, the data content is
// written on the following rows (ended with an empty row)
int FS::create(std::string filepath)
{
  std::cout << "FS::create(" << filepath << ")\n";

  int block_no = findFirstFreeBlock();
  // check if the dir_entry got a free block
  if (block_no == -1)
  {
    std::cout << "FS::create: No free block on disk\n"
              << std::endl;
    return -1;
  }

  // create dir_entry
  dir_entry dir_ent;
  std::strcpy(dir_ent.file_name, filepath.c_str());
  dir_ent.size = 0;
  dir_ent.first_blk = block_no;
  dir_ent.type = TYPE_FILE;
  dir_ent.access_rights = READ | WRITE | EXECUTE; // bitwise or

  // read user input
  std::string line = "", data_str = "";
  while (std::getline(std::cin, line))
  {
    if (line.length() == 0)
    {
      break;
    }
    line = line + "\n";
    data_str += line;
    dir_ent.size += line.length();
    std::cout << line.length() << std::endl;
  }
  std::cout << data_str << std::endl;
  std::cout << "size: " << dir_ent.size << std::endl;

  // check if filesize too big for number of free blocks
  int no_free_blocks = getNoFreeBlocks();
  if (!no_free_blocks || no_free_blocks < (dir_ent.size / BLOCK_SIZE))
  {
    std::cout << "FS::create: Not enough free blocks on disk\n"
              << std::endl;
    return -1;
  }

  // read FAT
  disk.read(FAT_BLOCK, (uint8_t *)fat);

  // write to FAT
  int i = 0, variable = -1;
  for (i; i <= (dir_ent.size / BLOCK_SIZE); i++) // check later what happens if filesize == BLOCK_SIZE
  {
    block_no = findFirstFreeBlock();
    fat[dir_ent.first_blk] = block_no;
  }
  fat[fat[block_no]] = FAT_EOF;

  // Start reading to blocks
  int size = data_str.length();
  int blocks_to_write = size / BLOCK_SIZE + 1;
  int free_block = dir_ent.first_blk;
  int index = 0;
  int counter = 0;
  int previous_block;

  for (blocks_to_write; blocks_to_write > 0; blocks_to_write--)
  {
    char block[BLOCK_SIZE] = {0};
    if (blocks_to_write > 1)
    {
      for (int j = 0; j < BLOCK_SIZE; j++)
      {
        index = counter * BLOCK_SIZE;
        block[j] = data_str[index + j];
      }
    }
    else
    {
      for (int j = 0; j < size % BLOCK_SIZE; j++)
      {
        index = counter * BLOCK_SIZE;
        block[j] = data_str[index + j];
      }
    }
    disk.write(free_block, (uint8_t *)block);

    // update FAT
    previous_block = free_block;
    free_block = findFirstFreeBlock();
    fat[previous_block] = free_block;
    counter++;
  }
  fat[previous_block] = FAT_EOF;

  // write FAT to disk
  disk.write(FAT_BLOCK, (uint8_t *)fat);

  dir_entry *ptr = &dir_ent;
  createDirEntry(ptr);

  // printFAT();

  return 0;
}

// cat <filepath> reads the content of a file and prints it on the screen
int FS::cat(std::string filepath)
{
  std::cout << "FS::cat(" << filepath << ")\n";

  // Read root block
  dir_entry root_block[BLOCK_SIZE / 64];
  disk.read(ROOT_BLOCK, (uint8_t *)root_block);

  // Read fat block
  disk.read(FAT_BLOCK, (uint8_t *)fat);

  // Find dir_entry with file_name == filepath
  // TODO: make function findDir
  bool found_dir = 0;
  int block, size;
  for (auto &dir : root_block)
  {
    if (dir.file_name == filepath)
    {
      block = dir.first_blk;
      size = dir.size;
      found_dir = 1;
      break;
    }
  }

  if (!found_dir)
  {
    std::cout << "Not found" << std::endl;
    return -1;
  }

  char char_array[BLOCK_SIZE];
  std::string to_print = "";

  int blocks_to_read = 0;
  blocks_to_read = size / BLOCK_SIZE + 1;

  // read blocks
  do
  {
    disk.read(block, (uint8_t *)char_array);
    for (blocks_to_read; blocks_to_read > 0; blocks_to_read--)
    {
      if (blocks_to_read > 1)
      {
        for (int i = 0; i < BLOCK_SIZE; i++)
        {
          to_print += char_array[i];
        }
      }
      else
      {
        for (int i = 0; i < size % BLOCK_SIZE; i++)
        {
          to_print += char_array[i];
        }
      }
    }
    block = fat[block];
  } while (block != FAT_EOF);

  // Append results to string.
  std::cout << to_print << std::endl;

  return 0;
}

// ls lists the content in the currect directory (files and sub-directories)
int FS::ls()
{
  std::cout << "FS::ls()\n";
  /*
    When implementing cd and coming functionality, consider that this function
    would need to know the current directory to know which dir_entries to display.
    What would this look like? Is all dir_entries stored in the ROOT_BLOCK?
  */

  int dir_name_width = 20;
  int number_width = 10;

  // Read ROOT_BLOCK, need to read the directory entries for the current directory later.
  dir_entry root_block[BLOCK_SIZE / 64];
  disk.read(ROOT_BLOCK, (uint8_t *)root_block);

  std::cout << std::left << std::setw(dir_name_width) << std::setfill(' ') << "name";
  std::cout << std::left << std::setw(number_width) << std::setfill(' ') << "size";
  std::cout << std::endl;

  std::string toPrint = "";
  for (auto &dir : root_block)
  {
    if (dir.type != TYPE_EMPTY && std::string(dir.file_name) != "/")
    {
      if (dir.type == TYPE_DIR)
      { // Two types of directory entry currently. (Three including EMPTY)
        std::cout << std::left << std::setw(dir_name_width) << std::setfill(' ') << '/' + std::string(dir.file_name);
        std::cout << std::left << std::setw(number_width) << std::setfill(' ') << std::to_string(dir.size);
        std::cout << std::endl;
        // toPrint += '/' + std::string(dir.file_name) + '\t' + std::to_string(dir.size) + "\n";
      }
      else
      {
        std::cout << std::left << std::setw(dir_name_width) << std::setfill(' ') << std::string(dir.file_name);
        std::cout << std::left << std::setw(number_width) << std::setfill(' ') << std::to_string(dir.size);
        std::cout << std::endl;
      }
    }
  }

  std::cout << toPrint;
  return 0;
}

// cp <sourcepath> <destpath> makes an exact copy of the file
// <sourcepath> to a new file <destpath>
int FS::cp(std::string sourcepath, std::string destpath)
{
  std::cout << "FS::cp(" << sourcepath << "," << destpath << ")\n";
  return 0;
}

// mv <sourcepath> <destpath> renames the file <sourcepath> to the name <destpath>,
// or moves the file <sourcepath> to the directory <destpath> (if dest is a directory)
int FS::mv(std::string sourcepath, std::string destpath)
{
  std::cout << "FS::mv(" << sourcepath << "," << destpath << ")\n";
  return 0;
}

// rm <filepath> removes / deletes the file <filepath>
int FS::rm(std::string filepath)
{
  std::cout << "FS::rm(" << filepath << ")\n";
  return 0;
}

// append <filepath1> <filepath2> appends the contents of file <filepath1> to
// the end of file <filepath2>. The file <filepath1> is unchanged.
int FS::append(std::string filepath1, std::string filepath2)
{
  std::cout << "FS::append(" << filepath1 << "," << filepath2 << ")\n";
  return 0;
}

// mkdir <dirpath> creates a new sub-directory with the name <dirpath>
// in the current directory
int FS::mkdir(std::string dirpath)
{
  std::cout << "FS::mkdir(" << dirpath << ")\n";
  return 0;
}

// cd <dirpath> changes the current (working) directory to the directory named <dirpath>
int FS::cd(std::string dirpath)
{
  std::cout << "FS::cd(" << dirpath << ")\n";
  return 0;
}

// pwd prints the full path, i.e., from the root directory, to the current
// directory, including the currect directory name
int FS::pwd()
{
  std::cout << "FS::pwd()\n";
  return 0;
}

// chmod <accessrights> <filepath> changes the access rights for the
// file <filepath> to <accessrights>.
int FS::chmod(std::string accessrights, std::string filepath)
{
  std::cout << "FS::chmod(" << accessrights << "," << filepath << ")\n";
  return 0;
}

// find free block
int FS::findFirstFreeBlock()
{
  int block_no = -1;
  for (int i = 2; i < BLOCK_SIZE / 2; i++)
  {
    if (fat[i] == 0)
    {
      block_no = i;
      break;
    }
  }
  return block_no;
}

// number of free blocks on disk
int FS::getNoFreeBlocks()
{
  int number = 0;
  for (int i = 2; i < BLOCK_SIZE / 2; i++)
  {
    if (fat[i] == 0)
    {
      number++;
    }
  }
  return number;
}

// create dir_entry
int FS::createDirEntry(dir_entry *de)
{
  int not_empty = 0;
  uint8_t block_arr[BLOCK_SIZE];
  dir_entry block[BLOCK_SIZE / 64];

  // read root block
  disk.read(ROOT_BLOCK, (uint8_t *)&block);

  // block = (dir_entry *) block_arr;
  int k = 1;
  for (k; k < BLOCK_SIZE / 64; k++)
  {
    if (block[k].type == TYPE_EMPTY) // first empty in root block
      break;

    if (k == BLOCK_SIZE / 64)
      return -1;
  }

  // edit block
  block[k] = *de;

  // write to root block at k
  disk.write(ROOT_BLOCK, (uint8_t *)block);

  return 0;
}

void FS::printFAT()
{
  disk.read(FAT_BLOCK, (uint8_t *)fat);

  for (auto &element : fat)
  {
    std::cout << (int)element << ", ";
  }
  std::cout << std::endl;
}
