#include <iostream>
#include "fs.h"

// TODO:
/*
  check existing filenames when creating file .
  create general findDir function
  should user be able to input lines longer than 4096 chars for file data?
  cat sometimes doesn't show last bit of long files even when lines < 4096 chars
*/

FS::FS()
{
    std::cout << "FS::FS()... Creating file system\n";
}

FS::~FS()
{

}

// formats the disk, i.e., creates an empty file system
int
FS::format()
{

    std::cout << "FS::format()\n";

    // initialize FAT
    fat[ROOT_BLOCK] = FAT_EOF;
    fat[FAT_BLOCK] = FAT_EOF;

    // rest of the blocks are marked as free
    for (int i = 2; i < BLOCK_SIZE/2; i++)
      fat[i] = FAT_FREE;

    // write entire FAT to disk
    disk.write(FAT_BLOCK, (uint8_t *) fat);

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
    dir_entry temp_array[BLOCK_SIZE/64];
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

    disk.write(ROOT_BLOCK, (uint8_t *) temp_array);

    return 0;
}

// create <filepath> creates a new file on the disk, the data content is
// written on the following rows (ended with an empty row)
int
FS::create(std::string filepath)
{
    std::cout << "FS::create(" << filepath << ")\n";
    if (filepath.length() > 56) {
      std::cout << "File name exceeds 56 character limit" << std::endl;
      return -1;
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
      if (line.length() == 0) { break; }
      line = line + "\n";
      data_str += line;
      dir_ent.size += line.length();
    }

    // check if filesize too big for number of free blocks
    int no_free_blocks = getNoFreeBlocks();
    if (!no_free_blocks || no_free_blocks < (dir_ent.size/BLOCK_SIZE) )
    {
      std::cout << "Not enough free blocks on disk" << std::endl;
      return -1;
    }

    // read FAT
    disk.read(FAT_BLOCK, (uint8_t *) fat);

    // Start writing to blocks
    int size = data_str.length();
    int blocks_to_write = size / BLOCK_SIZE + 1;
    int free_block = dir_ent.first_blk;
    int index = 0;
    int counter = 0;
    int current_block;

    for(blocks_to_write; blocks_to_write > 0; blocks_to_write--) {
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
      disk.write(free_block, (uint8_t *) block);

      // update FAT
      fat[free_block] = 1; // temp value
      current_block = free_block;
      free_block = findFirstFreeBlock();
      fat[current_block] = free_block;
      std::cout << filepath << " got FAT[" << (int) current_block << "]" << std::endl;
      counter++;
    }
    fat[current_block] = FAT_EOF; // if >2 blocks, 2 last FAT wrong



    // write FAT to disk
    disk.write(FAT_BLOCK, (uint8_t *) fat);
    printFAT();
    // write to root block
    dir_entry *ptr = &dir_ent;
    createDirEntry(ptr);

    return 0;
}


// cat <filepath> reads the content of a file and prints it on the screen
int
FS::cat(std::string filepath)
{
    std::cout << "FS::cat(" << filepath << ")\n";

    // Read root block
    dir_entry root_block[BLOCK_SIZE/64];
    disk.read(ROOT_BLOCK, (uint8_t *) root_block);

    // Read fat block
    disk.read(FAT_BLOCK, (uint8_t *) fat);

    // Find dir_entry with file_name == filepath
    // uint16_t block;
    // uint32_t size;
    // bool found_dir = findDir(filepath, &block, &size);

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
      disk.read(block, (uint8_t *) char_array);
      for (blocks_to_read; blocks_to_read > 0; blocks_to_read--)
      {
        if (blocks_to_read > 1) {
            for (int i = 0; i < BLOCK_SIZE; i++){
              to_print += char_array[i];
            }
        }
        else {
          for (int i = 0; i < size % BLOCK_SIZE; i++){
            to_print += char_array[i];
          }
        }
      }
      block = fat[block];
    } while(block != FAT_EOF);

    // Append results to string.
    std::cout << to_print << std::endl;
    return 0;
}

// ls lists the content in the currect directory (files and sub-directories)
int
FS::ls()
{
    std::cout << "FS::ls()\n";
    dir_entry root_block[BLOCK_SIZE / 64];
    disk.read(ROOT_BLOCK, (uint8_t *)root_block);

    int dir_name_width = 56, number_width = 10;
    std::cout << std::left << std::setw(dir_name_width) << std::setfill(' ') << "name";
    std::cout << std::left << std::setw(number_width) << std::setfill(' ') << "size" << std::endl;

    // save tuples (file_name, size) to a vector and then print sorted alphabetically?
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
    //printFAT();
    std::cout << std::endl;
    return 0;
}

// cp <sourcepath> <destpath> makes an exact copy of the file
// <sourcepath> to a new file <destpath>
int
FS::cp(std::string sourcepath, std::string destpath)
{
    std::cout << "FS::cp(" << sourcepath << "," << destpath << ")\n";

    // read from disk
    dir_entry root_block[BLOCK_SIZE / 64];
    disk.read(ROOT_BLOCK, (uint8_t *) root_block);
    disk.read(FAT_BLOCK, (uint8_t *) fat);

    // find sourcepath
    //findDir(sourcepath);
    uint32_t size;
    uint16_t source_block;
    uint8_t type, access_rights;
    for (auto& dir : root_block)
    {
      if (dir.file_name == sourcepath)
      {
        source_block = dir.first_blk;
        size = dir.size;
        type = dir.type;
        access_rights = dir.access_rights;
        break;
      }
    }

    int no_free = getNoFreeBlocks();
    if (no_free < size/BLOCK_SIZE)
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

    int current_block;

    // TODO: Possibly break out update FAT into own function. Repeating code in cp and create.
    // write to FAT
    for (int i = 0; i <= (dir_ent.size/BLOCK_SIZE); i++) // check later what happens if filesize == BLOCK_SIZE
    {
      fat[block_no] = 1;
      current_block = block_no;
      block_no = findFirstFreeBlock();
      fat[current_block] = block_no;
      std::cout << destpath << " got FAT[" << (int) block_no << "]" << std::endl;
    }
    fat[current_block] = FAT_EOF;
    disk.write(FAT_BLOCK, (uint8_t *) fat);

    // copy data from sourcepath to destpath
    uint16_t dest_block = dir_ent.first_blk;

    printFAT();

    // write to disk
    char block[BLOCK_SIZE] = {0};
    while (true)
    {
      std::cout << "source_block " << source_block << " copies to dest_block " << dest_block<< std::endl;
      disk.read(source_block, (uint8_t *) block);      // read source block
      disk.write(dest_block, (uint8_t *) block);    // write dest block
      if (fat[source_block] == FAT_EOF || fat[dest_block] == FAT_EOF)
        break;
      dest_block = fat[dest_block];
      source_block = fat[source_block];
    }


    return 0;
}

// mv <sourcepath> <destpath> renames the file <sourcepath> to the name <destpath>,
// or moves the file <sourcepath> to the directory <destpath> (if dest is a directory)
int
FS::mv(std::string sourcepath, std::string destpath)
{
    std::cout << "FS::mv(" << sourcepath << "," << destpath << ")\n";

    // search files for sourcepath, and destpath

    // if destpath exists
    // if TYPE_DIR, move to directory
        //
    // if TYPE_FILE, abort operation, can't rename to existing filename
    // else rename sourcepath to destpath

    return 0;
}

// rm <filepath> removes / deletes the file <filepath>
int
FS::rm(std::string filepath)
{
    std::cout << "FS::rm(" << filepath << ")\n";

    // check if filepath exists
    //     if not, exit
    // mark FAT entries as FAT_EMPTY
    // mark dir_entry in ROOT_BLOCK as TYPE_EMPTY

    return 0;
}

// append <filepath1> <filepath2> appends the contents of file <filepath1> to
// the end of file <filepath2>. The file <filepath1> is unchanged.
int
FS::append(std::string filepath1, std::string filepath2)
{
    std::cout << "FS::append(" << filepath1 << "," << filepath2 << ")\n";

    // check both files exist
    // read entire filepath1
    // decide how many new blocks filepath2 needs by filepath1.size
    // write to new block(s)
    // update FAT
    
    return 0;
}

// mkdir <dirpath> creates a new sub-directory with the name <dirpath>
// in the current directory
int
FS::mkdir(std::string dirpath)
{
    std::cout << "FS::mkdir(" << dirpath << ")\n";

    // place only files/directory directly under root, in the root block
    // a directory block holds dir_entries for its files/directories
    // to find parent, dir_entry ".." can have first_blk=parent_block
    // files in a subdirectory cannot be find in root block

    return 0;
}

// cd <dirpath> changes the current (working) directory to the directory named <dirpath>
int
FS::cd(std::string dirpath)
{
    std::cout << "FS::cd(" << dirpath << ")\n";
    return 0;
}

// pwd prints the full path, i.e., from the root directory, to the current
// directory, including the currect directory name
int
FS::pwd()
{
    std::cout << "FS::pwd()\n";
    return 0;
}

// chmod <accessrights> <filepath> changes the access rights for the
// file <filepath> to <accessrights>.
int
FS::chmod(std::string accessrights, std::string filepath)
{
    std::cout << "FS::chmod(" << accessrights << "," << filepath << ")\n";
    return 0;
}

// find free block
int
FS::findFirstFreeBlock()
{
  int block_no = -1;

  for (int i = 2; i < BLOCK_SIZE/2; i++)
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
int
FS::getNoFreeBlocks()
{
  int number = 0;
  for (int i = 2; i < BLOCK_SIZE/2; i++)
  {
    if (fat[i] == 0)
    {
      number++;
    }
  }
  return number;
}

// create dir_entry
int
FS::createDirEntry(dir_entry *de)
{
  int not_empty = 0;
  dir_entry block[BLOCK_SIZE/64];

  // read root block
  disk.read(ROOT_BLOCK, (uint8_t *) block);

  int k = 1;
  for (k; k < BLOCK_SIZE/64; k++)
  {
    if (block[k].type == TYPE_EMPTY) // first empty in root block
      break;
  }
  if (k == BLOCK_SIZE/64)
    return -1;

  // edit root block
  block[k] = *de;

  // write root block to disk
  disk.write(ROOT_BLOCK, (uint8_t *) block);

  return 0;
}

void FS::printFAT()
{
  disk.read(FAT_BLOCK, (uint8_t *) fat);

  for (int i = 0; i < 15; i++)
  {
    std::cout << (int) fat[i] << ", ";
  }
  std::cout << std::endl;
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
