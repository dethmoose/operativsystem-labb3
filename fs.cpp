#include <iostream>
#include "fs.h"

// TODO:
/*
  create general findDir function
  ls: save tuples (file_name, size) to a vector and then print sorted alphabetically?
*/

FS::FS()
{
  std::cout << "FS::FS()... Creating file system\n";
}

FS::~FS()
{
}

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
  std::string dir_name = "/";
  dir_entry dir_ent;
  std::strcpy(dir_ent.file_name, dir_name.c_str());
  dir_ent.size = 0;
  dir_ent.first_blk = 0;
  dir_ent.type = TYPE_DIR;
  dir_ent.access_rights = READ | WRITE | EXECUTE;

  // write dir_entry for root to disk
  // dir_entry* de_ptr = &dir_ent;
  dir_entry temp_array[BLOCK_SIZE / 64];
  temp_array[0] = dir_ent;

  // change dir_entry object to be empty
  dir_name = "";
  std::strcpy(dir_ent.file_name, dir_name.c_str());
  dir_ent.type = TYPE_EMPTY;
  dir_ent.access_rights = 0;

  // fill rest of root block with empty dir_entry
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
  if (filepath.length() > 56)
  {
    std::cout << "File name exceeds 56 character limit" << std::endl;
    return -1;
  }

  // read root block and FAT block
  dir_entry root_block[BLOCK_SIZE / 64];
  disk.read(ROOT_BLOCK, (uint8_t *)root_block);
  disk.read(FAT_BLOCK, (uint8_t *)fat);

  // make sure filepath doesn't already exist
  for (auto &dir : root_block)
  {
    if (dir.file_name == filepath)
    {
      std::cout << filepath << " already exists" << std::endl;
      return -1;
    }
  }

  int block_no = findFirstFreeBlock();
  if (block_no == -1)
  {
    std::cout << "No free block on disk" << std::endl;
    return -1;
  }

  // create dir_entry
  dir_entry dir_ent;
  std::strcpy(dir_ent.file_name, filepath.c_str());
  dir_ent.size = 0;
  dir_ent.first_blk = block_no;
  dir_ent.type = TYPE_FILE;
  dir_ent.access_rights = READ | WRITE | EXECUTE;

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
  }

  // check if filesize too big for number of free blocks
  int no_free_blocks = getNoFreeBlocks();
  if (!no_free_blocks || no_free_blocks < (dir_ent.size / BLOCK_SIZE))
  {
    std::cout << "Not enough free blocks on disk" << std::endl;
    return -1;
  }

  // Start writing to blocks
  int size = data_str.length();
  int blocks_to_write = size / BLOCK_SIZE + 1;

  int index = 0;
  int counter = 0;

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
    disk.write(block_no, (uint8_t *)block);
    counter++;
  }

  updateFAT(block_no, size);

  // write FAT to disk
  disk.write(FAT_BLOCK, (uint8_t *)fat);
  printFAT();

  // write to root block
  dir_entry *ptr = &dir_ent;
  createDirEntry(ptr);

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
  // uint16_t block;
  // uint32_t size;
  // bool found_dir = findDir(filepath, &block, &size);

  bool found_dir = false;
  int block, size;
  for (auto &dir : root_block)
  {
    if (dir.type != TYPE_EMPTY && dir.file_name == filepath)
    {
      block = dir.first_blk;
      size = dir.size;
      found_dir = true;
      break;
    }
  }

  if (!found_dir)
  {
    std::cout << "File " << filepath << " does not exist." << std::endl;
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

  // Print string.
  std::cout << to_print << std::endl;
  return 0;
}

// ls lists the content in the currect directory (files and sub-directories)
int FS::ls()
{
  std::cout << "FS::ls()\n";

  // read root block
  dir_entry root_block[BLOCK_SIZE / 64];
  disk.read(ROOT_BLOCK, (uint8_t *)root_block);

  // print heading
  int dir_name_width = 56, number_width = 10;
  std::cout << std::left << std::setw(dir_name_width) << std::setfill(' ') << "name";
  std::cout << std::left << std::setw(number_width) << std::setfill(' ') << "size" << std::endl;

  for (auto &dir : root_block)
  {
    if (dir.type != TYPE_EMPTY)
    {
      if (dir.type == TYPE_DIR)
      {
        if (std::string(dir.file_name) != "/") // skip root
        {
          std::cout << std::left << std::setw(dir_name_width) << std::setfill(' ') << std::string(dir.file_name) + '/';
          std::cout << std::left << std::setw(number_width) << std::setfill(' ') << std::to_string(dir.size);
          std::cout << std::endl;
        }
      }
      else // TYPE_FILE
      {
        std::cout << std::left << std::setw(dir_name_width) << std::setfill(' ') << std::string(dir.file_name);
        std::cout << std::left << std::setw(number_width) << std::setfill(' ') << std::to_string(dir.size);
        std::cout << std::endl;
      }
    }
  }
  // printFAT();
  std::cout << std::endl;
  return 0;
}

// cp <sourcepath> <destpath> makes an exact copy of the file
// <sourcepath> to a new file <destpath>
int FS::cp(std::string sourcepath, std::string destpath)
{
  std::cout << "FS::cp(" << sourcepath << "," << destpath << ")\n";

  // read root block and FAT block from disk
  dir_entry root_block[BLOCK_SIZE / 64];
  disk.read(ROOT_BLOCK, (uint8_t *)root_block);
  disk.read(FAT_BLOCK, (uint8_t *)fat);

  // find sourcepath, make sure destpath doesn't exist
  // findDir(sourcepath);
  bool found_source_dir = false, found_dest_dir = false;
  uint32_t size;
  uint16_t source_block;
  uint8_t type, access_rights;
  for (auto &dir : root_block)
  {
    if (dir.file_name == sourcepath)
    {
      source_block = dir.first_blk;
      size = dir.size;
      type = dir.type;
      access_rights = dir.access_rights;
      found_source_dir = true;
      // break;
    }
    if (dir.file_name == destpath)
    {
      found_dest_dir = true;
    }
  }

  if (found_dest_dir)
  {
    std::cout << destpath << " already exists" << std::endl;
    return -1;
  }

  if (!found_source_dir)
  {
    std::cout << sourcepath << " does not exist" << std::endl;
    return -1;
  }

  int no_free = getNoFreeBlocks();
  if (no_free < size / BLOCK_SIZE)
    return -1;

  // create new file
  dir_entry dir_ent;
  std::strcpy(dir_ent.file_name, destpath.c_str());
  dir_ent.size = size;
  dir_ent.type = type;
  dir_ent.access_rights = access_rights;

  int block_no = findFirstFreeBlock();
  dir_ent.first_blk = block_no;

  // write to root block
  createDirEntry(&dir_ent);

  // write to FAT
  updateFAT(block_no, size);
  disk.write(FAT_BLOCK, (uint8_t *)fat);

  printFAT();

  // copy data from sourcepath to destpath
  uint16_t dest_block = dir_ent.first_blk;
  char block[BLOCK_SIZE] = {0};

  while (true)
  {
    // std::cout << "source_block " << source_block << " copies to dest_block " << dest_block << std::endl;
    disk.read(source_block, (uint8_t *)block); // read source block
    disk.write(dest_block, (uint8_t *)block);  // write dest block
    if (fat[source_block] == FAT_EOF || fat[dest_block] == FAT_EOF)
      break;
    dest_block = fat[dest_block];
    source_block = fat[source_block];
  }

  return 0;
}

// mv <sourcepath> <destpath> renames the file <sourcepath> to the name <destpath>,
// or moves the file <sourcepath> to the directory <destpath> (if dest is a directory)
int FS::mv(std::string sourcepath, std::string destpath)
{
  std::cout << "FS::mv(" << sourcepath << "," << destpath << ")\n";

  // if destpath exists
  // if TYPE_DIR, move to directory
  //
  // if TYPE_FILE, abort operation, can't rename to existing filename
  // else rename sourcepath to destpath

  // read root block and FAT block
  dir_entry root_block[BLOCK_SIZE / 64];
  disk.read(ROOT_BLOCK, (uint8_t *)root_block);
  disk.read(FAT_BLOCK, (uint8_t *)fat);

  // search files for sourcepath and destpath
  bool found_source_dir = false, found_dest_dir = false;
  // dir_entry dir_ent;
  // int index = -1;

  // for (int i = 0; i < BLOCK_SIZE / 64; i++)
  //{
  //   if (root_block[i].file_name == sourcepath)
  //   {
  //     found_source_dir = true;
  //     dir_ent = root_block[i];
  //     index = i;
  //   }
  //   if (root_block[i].file_name == destpath)
  //   {
  //     found_dest_dir = true;
  //   }
  // }

  for (auto &dir : root_block)
  {
    if (dir.file_name == destpath)
    {
      std::cout << destpath << " already exists" << std::endl;
      return -1;
    }
  }

  for (auto &dir : root_block)
  {
    if (dir.file_name == sourcepath)
    {
      std::strcpy(dir.file_name, destpath.c_str());
      found_source_dir = true;
      break;
    }
  }

  if (!found_source_dir)
  {
    std::cout << sourcepath << " does not exist" << std::endl;
    return -1;
  }

  // rename filename on source dir_entry in root block
  // std::strcpy(dir_ent.file_name, destpath.c_str());
  // root_block[index] = dir_ent;

  disk.write(ROOT_BLOCK, (uint8_t *)root_block);

  return 0;
}

// rm <filepath> removes / deletes the file <filepath>
int FS::rm(std::string filepath)
{
  std::cout << "FS::rm(" << filepath << ")\n";

  // read root block and FAT block from disk
  dir_entry root_block[BLOCK_SIZE / 64];
  disk.read(ROOT_BLOCK, (uint8_t *)root_block);
  disk.read(FAT_BLOCK, (uint8_t *)fat);

  // find filepath
  bool found_dir = false;
  int current_block, next_block;

  for (auto &dir : root_block)
  {
    if (dir.type != TYPE_EMPTY && dir.file_name == filepath)
    {
      // mark dir_entry as empty
      dir.type = TYPE_EMPTY;
      current_block = dir.first_blk;
      found_dir = true;
      break;
    }
  }

  if (!found_dir)
  {
    std::cout << filepath << " does not exist" << std::endl;
    return -1;
  }

  printFAT();

  // free FAT entries
  do
  {
    next_block = fat[current_block];
    fat[current_block] = FAT_FREE;
    current_block = next_block;
    // std::cout << "current: " << current_block << ", next: " << next_block << std::endl;
  } while (current_block != FAT_EOF);

  // write to FAT
  disk.write(FAT_BLOCK, (uint8_t *)fat);
  printFAT();

  // write to root
  disk.write(ROOT_BLOCK, (uint8_t *)root_block);

  return 0;
}

// append <filepath1> <filepath2> appends the contents of file <filepath1> to
// the end of file <filepath2>. The file <filepath1> is unchanged.
int FS::append(std::string filepath1, std::string filepath2)
{
  std::cout << "FS::append(" << filepath1 << "," << filepath2 << ")\n";

  // check both files exist
  // read entire filepath1
  // decide how many new blocks filepath2 needs by filepath1.size
  // write to new block(s)
  // update FAT
  // write from FAT_EOF of filepath2 and set new EOF

  // Read root block and FAT
  // TODO: Update this for directories.
  dir_entry root_block[BLOCK_SIZE / 64];
  disk.read(ROOT_BLOCK, (uint8_t *)root_block);
  disk.read(FAT_BLOCK, (uint8_t *)fat);

  // Search for filepaths, exit if not found
  int filepath1_block, filepath2_block, size1, size2;
  int no_found = 0;
  for (auto &dir : root_block)
  {
    if (std::string(dir.file_name) == filepath1)
    {
      filepath1_block = dir.first_blk;
      size1 = dir.size;
      no_found++;
    }
    if (std::string(dir.file_name) == filepath2)
    {
      filepath2_block = dir.first_blk;
      size2 = dir.size;
      no_found++;
    }
  }

  if (no_found != 2) // Expecting exactly two files found.
    return -1;
  
  // Check if enough free blocks
  int no_free_blocks = getNoFreeBlocks();
  int new_blocks_needed = ((size1 - ( size2 % BLOCK_SIZE)) / BLOCK_SIZE) + 1; // sometimes needs +1, sometimes not
  if (no_free_blocks < new_blocks_needed)
    return -1;

  // tried using float for deciding when to add +1 and when not to
  // float new_blocks_needed = (( (float) size1 - ( size2 % BLOCK_SIZE)) / (float) BLOCK_SIZE); // sometimes +1, sometimes not
  // if ((new_blocks_needed - (int) new_blocks_needed) != 0)
  //   new_blocks_needed = (int) new_blocks_needed + 1;

  std::cout << std::endl << "Number of free blocks on disk: " << no_free_blocks << std::endl;
  std::cout << "Size filepath1: " << size1 << std::endl;
  std::cout << "Size filepath2: " << size2 << std::endl;
  std::cout << "First block filepath1: " << filepath1_block << std::endl;
  std::cout << "First block filepath2: " << filepath2_block << std::endl;
  std::cout << "(size2 % BLOCK_SIZE): " << size2 % BLOCK_SIZE << std::endl;
  std::cout << "New size filepath2: " << size1 + size2 << std::endl;
  std::cout << "New blocks needed for filepath2: " << new_blocks_needed << std::endl;
  std::cout << "Total number of blocks needed for filepath2 after append: " << (size1 + size2) / BLOCK_SIZE + 1 << std::endl << std::endl;

  // Update FAT entries for filepath2

  // Find end of filepath2
  while (fat[filepath2_block] != FAT_EOF)
    filepath2_block = fat[filepath2_block];
  
  // Read filepath1 data
  int blocks_to_read = size1 / BLOCK_SIZE + 1;
  char block[BLOCK_SIZE] = {0};
  std::string filepath1_data = "";

  for (blocks_to_read; blocks_to_read > 0; blocks_to_read--)
  {
    disk.read(filepath1_block, (uint8_t *)block);
    if (blocks_to_read > 1)
    {
      // filepath1_data.append(block); // append char* to string
      for (int i = 0; i < BLOCK_SIZE; i++)
      {
        filepath1_data += block[i];
      }
    }
    else
    {
      for (int i = 0; i < size1 % BLOCK_SIZE; i++)
      {
        filepath1_data += block[i];
      }
    }
    filepath1_block = fat[filepath1_block];
  }

  // Checking filepath1_data
  std::cout << filepath1_data.size() << std::endl;
  std::cout << filepath1_data << std::endl;

  // Write data to the end of filepath2
  int data_index = 0, first_iteration = 1;
  while (filepath2_block != FAT_EOF && data_index < size1)
  {
    // Write one block at a time
    if (first_iteration)
    {
      // Read last block of filepath2 and start writing at the end
      disk.read(filepath2_block, (uint8_t *)block);
      for (int i = size2 % BLOCK_SIZE + 1; i < BLOCK_SIZE; i++)
      {
        block[i] = filepath1_data[data_index];
        data_index++;
      }
      first_iteration = 0;
    }
    else
    {
      // Write to a new block
      for (int i = 0; i < BLOCK_SIZE; i++)
      {
        block[i] = filepath1_data[data_index];
        data_index++;
      }      
    }
    disk.write(filepath2_block, (uint8_t *)block);
    filepath2_block = fat[filepath2_block];
  }

  // Update filepath2.size in root block
  
  return 0;
}

// mkdir <dirpath> creates a new sub-directory with the name <dirpath>
// in the current directory
int FS::mkdir(std::string dirpath)
{
  std::cout << "FS::mkdir(" << dirpath << ")\n";

  // place only files/directory directly under root, in the root block
  // a directory block holds dir_entries for its files/directories
  // to find parent, dir_entry ".." can have first_blk=parent_block
  // files in a subdirectory cannot be find in root block

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
    if (fat[i] == FAT_FREE)
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
  dir_entry block[BLOCK_SIZE / 64];

  // read root block
  disk.read(ROOT_BLOCK, (uint8_t *)block);

  int k = 1;
  for (k; k < BLOCK_SIZE / 64; k++)
  {
    if (block[k].type == TYPE_EMPTY) // first empty in root block
      break;
  }
  if (k == BLOCK_SIZE / 64)
    return -1;

  // edit root block
  block[k] = *de;

  // write root block to disk
  disk.write(ROOT_BLOCK, (uint8_t *)block);

  return 0;
}

void FS::printFAT()
{
  disk.read(FAT_BLOCK, (uint8_t *)fat);

  for (int i = 0; i < 15; i++)
    std::cout << (int)fat[i] << ", ";

  std::cout << std::endl;
}

void FS::updateFAT(int block_start, uint32_t size)
{
  int current_block;
  for (int i = 0; i <= (size / BLOCK_SIZE); i++) // check later what happens if filesize == BLOCK_SIZE
  {
    fat[block_start] = 1;
    current_block = block_start;
    block_start = findFirstFreeBlock();
    fat[current_block] = block_start;
  }
  fat[current_block] = FAT_EOF;
}

// Find dir_entry with file_name == filepath
// int FS::findDir(std::string filepath, uint16_t &block, uint32_t &size)
// {
//     // Read root block
//     dir_entry root_block[BLOCK_SIZE/64];
//     disk.read(ROOT_BLOCK, (uint8_t *) root_block);
//
//     for (auto& dir : root_block)
//     {
//       if (std::string(dir.file_name) == filepath)
//       {
//         block = dir.first_blk;
//         size = dir.size;
//         std::cout << "FS::findDir first_blk=" << (int) dir.first_blk;
//         std::cout << ", size=" << (int) dir.size << std::endl;
//         return 1;
//       }
//     }
//     return 0;
// }
