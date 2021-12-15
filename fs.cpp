#include <iostream>
#include <sstream>
#include "fs.h"

// TODO:
/*  
  remove all "error" prints? and then only return -1 on error
  cp: update to handle copy to directory
  create general findDir function
  ls: save tuples (file_name, size) to a vector and then print sorted alphabetically?
  ls: fix better way of translating dir.access_rights to string
  "the access rights on a directory must be correct for various file operations (e.g., moving/copying a file to a directory requires write access on that directory), etc"
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

  cwd = ROOT_BLOCK;

  // Erase diskfile.bin for good
  uint8_t null_block[BLOCK_SIZE] = {0};
  for (int i = 0; i < BLOCK_SIZE / 2; i++)
  {
    disk.write(i, null_block);
  }

  // Initialize FAT
  fat[ROOT_BLOCK] = FAT_EOF;
  fat[FAT_BLOCK] = FAT_EOF;

  // Rest of the blocks are marked as free
  for (int i = 2; i < BLOCK_SIZE / 2; i++)
    fat[i] = FAT_FREE;

  // Write entire FAT to disk
  disk.write(FAT_BLOCK, (uint8_t *)fat);

  // Create dir_entry for root directory
  std::string dir_name = "/";
  dir_entry dir_ent;
  std::strcpy(dir_ent.file_name, dir_name.c_str());
  dir_ent.size = 0;
  dir_ent.first_blk = 0;
  dir_ent.type = TYPE_DIR;
  dir_ent.access_rights = READ | WRITE | EXECUTE;

  // Add dir_entry first in root
  // dir_entry* de_ptr = &dir_ent;
  working_directory[0] = dir_ent;

  // Change dir_entry object to be empty
  dir_name = "";
  std::strcpy(dir_ent.file_name, dir_name.c_str());
  dir_ent.type = TYPE_EMPTY;
  dir_ent.access_rights = 0;

  // Fill rest of root block with empty dir_entry
  for (int i = 1; i < BLOCK_SIZE / 64; i++)
  {
    working_directory[i] = dir_ent;
  }

  // Write root block to disk
  disk.write(ROOT_BLOCK, (uint8_t *)working_directory);

  return 0;
}

// create <filepath> creates a new file on the disk, the data content is
// written on the following rows (ended with an empty row)
int FS::create(std::string filepath)
{
  std::cout << "FS::create(" << filepath << ")\n";

  // TODO modify length check to work with relative/absolute paths 
  if (filepath.length() > 56) 
  {
    std::cout << "File name exceeds 56 character limit" << std::endl;
    return -1;
  }

  // Read root block and FAT block
  disk.read(cwd, (uint8_t *)working_directory);
  disk.read(FAT_BLOCK, (uint8_t *)fat);

  // Make sure filepath doesn't already exist
  for (auto &dir : working_directory)
  {
    if (dir.file_name == filepath && dir.type != TYPE_EMPTY)
    {
      std::cout << filepath << " already exists" << std::endl;
      return -1;
    }
  }

  int current_block = findFirstFreeBlock();
  if (current_block == -1)
  {
    std::cout << "No free block on disk" << std::endl;
    return -1;
  }

  // Create dir_entry
  dir_entry dir_ent;
  std::strcpy(dir_ent.file_name, filepath.c_str());
  dir_ent.size = 0;
  dir_ent.first_blk = current_block;
  dir_ent.type = TYPE_FILE;
  dir_ent.access_rights = READ | WRITE;

  // Read user input
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

  // Check if filesize too big for number of free blocks
  int no_free_blocks = getNoFreeBlocks();
  if (!no_free_blocks || no_free_blocks < (dir_ent.size / BLOCK_SIZE))
  {
    std::cout << "Not enough free blocks on disk" << std::endl;
    return -1;
  }

  // Start writing to blocks
  int size = data_str.length();

  updateFAT(dir_ent.first_blk, size);

  // Write FAT to disk
  disk.write(FAT_BLOCK, (uint8_t *)fat);

  int index = 0;
  int counter = 0;

  // Write file data
  while (current_block != FAT_EOF)
  {
    char block[BLOCK_SIZE] = {0};
    if (fat[current_block] != FAT_EOF)
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
    disk.write(current_block, (uint8_t *)block);
    current_block = fat[current_block];
    counter++;
  }

  // printFAT();

  // Write to root block
  dir_entry *ptr = &dir_ent;
  createDirEntry(ptr);

  return 0;
}

// cat <filepath> reads the content of a file and prints it on the screen
int FS::cat(std::string filepath)
{
  std::cout << "FS::cat(" << filepath << ")\n";

  // Read working directory block and FAT 
  disk.read(cwd, (uint8_t *)working_directory);
  disk.read(FAT_BLOCK, (uint8_t *)fat);

  // Find dir_entry with file_name == filepath
  // uint16_t block;
  // uint32_t size;
  // bool found_dir = findDir(filepath, &block, &size);

  bool found_dir = false;
  int current_block, size;
  for (auto &dir : working_directory)
  {
    if (dir.type != TYPE_EMPTY && dir.file_name == filepath)
    {
      // Check access rights is at least READ
      if (dir.access_rights < READ)
      {
        std::cout << filepath << " does not have read permission" << std::endl;
        return -1;
      }

      current_block = dir.first_blk;
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

  uint8_t char_array[BLOCK_SIZE];
  std::string to_print = "";

  // Read blocks
  do
  {
    disk.read(current_block, char_array);
    if (fat[current_block] != FAT_EOF)
    {
      for (int i = 0; i < BLOCK_SIZE; i++)
      {
        std::cout << char_array[i];
      }
    }
    else
    {
      for (int i = 0; i < size % BLOCK_SIZE; i++)
      {
        std::cout << char_array[i];
      }
    }

    current_block = fat[current_block];
  } while (current_block != FAT_EOF);

  return 0;
}

// ls lists the content in the currect directory (files and sub-directories)
int FS::ls()
{
  std::cout << "FS::ls()\n";

  // Read working directory block
  disk.read(cwd, (uint8_t *)working_directory);

  // Get longest filename
  int dir_name_width = 4; // at least length of "name"
  for (auto &dir : working_directory)
  {
    if (dir.type != TYPE_EMPTY && (std::string(dir.file_name).length() > dir_name_width))
    {
      dir_name_width = std::string(dir.file_name).length();
    }
  }
  dir_name_width += 2;

  // Print heading
  std::cout << std::left << std::setw(dir_name_width) << std::setfill(' ') << "name";
  std::cout << std::left << std::setw(6) << std::setfill(' ') << "type";
  std::cout << std::left << std::setw(14) << std::setfill(' ') << "accessrights";
  std::cout << std::left << std::setw(10) << std::setfill(' ') << "size" << std::endl;

  for (auto &dir : working_directory)
  {
    if (dir.type != TYPE_EMPTY)
    {
      if (dir.type == TYPE_DIR)
      {
        if (std::string(dir.file_name) != "/" && std::string(dir.file_name) != "..")
        {
          // Get access rights string (TODO: fix better solution)
          std::string rwx = accessRightsToString(dir.access_rights);
          
          std::cout << std::left << std::setw(dir_name_width) << std::setfill(' ') << std::string(dir.file_name);
          std::cout << std::left << std::setw(6) << std::setfill(' ') << "dir";
          std::cout << std::left << std::setw(14) << std::setfill(' ') << rwx;
          std::cout << std::left << std::setw(10) << std::setfill(' ') << "-";
          std::cout << std::endl;
        }
      }
      else if (dir.type == TYPE_FILE)
      {
        // Get access rights string (TODO: fix better solution)
        std::string rwx = accessRightsToString(dir.access_rights);
        
        std::cout << std::left << std::setw(dir_name_width) << std::setfill(' ') << std::string(dir.file_name);
        std::cout << std::left << std::setw(6) << std::setfill(' ') << "file";
        std::cout << std::left << std::setw(14) << std::setfill(' ') << rwx;
        std::cout << std::left << std::setw(10) << std::setfill(' ') << std::to_string(dir.size);
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

  // TODO update for copying to directory

  // Read working directory block and FAT from disk
  disk.read(cwd, (uint8_t *)working_directory);
  disk.read(FAT_BLOCK, (uint8_t *)fat);

  // Find sourcepath, make sure destpath doesn't exist
  // findDir(sourcepath);
  bool found_source_dir = false, found_dest_dir = false;
  uint32_t size;
  uint16_t source_block;
  uint8_t type, access_rights;
  for (auto &dir : working_directory)
  {
    if (dir.file_name == sourcepath)
    {
      // Check READ permission
      if (dir.access_rights < READ)
      {
        std::cout << sourcepath << " does not have read permission" << std::endl;
        return -1;
      }

      source_block = dir.first_blk;
      size = dir.size;
      type = dir.type;
      access_rights = dir.access_rights;
      found_source_dir = true;
    }
    if (dir.file_name == destpath) // && dir.type = TYPE_FILE
    {
      found_dest_dir = true;
    }
  }

  if (found_dest_dir) // and destpath is a file
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

  // Create new file
  dir_entry dir_ent;
  std::strcpy(dir_ent.file_name, destpath.c_str());
  dir_ent.size = size;
  dir_ent.type = type;
  dir_ent.access_rights = access_rights;

  int block_no = findFirstFreeBlock();
  dir_ent.first_blk = block_no;

  // Write to root block
  createDirEntry(&dir_ent);

  // Write to FAT
  updateFAT(block_no, size);
  disk.write(FAT_BLOCK, (uint8_t *)fat);

  // printFAT();

  // Copy data from sourcepath to destpath
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

  // TODO check access rights? (write permission for changing its name?)

  // if destpath exists
  //     if TYPE_DIR, move to directory
  //     if TYPE_FILE, abort operation, can't rename to existing filename
  // else rename sourcepath to destpath

  // Read working directory block and FAT
  disk.read(cwd, (uint8_t *)working_directory);
  disk.read(FAT_BLOCK, (uint8_t *)fat);

  // Search files for sourcepath and destpath
  bool found_source_dir = false, destination_is_dir = false;
  // dir_entry dir_ent;
  int dir_index = -1;
  int src_index = -1;

  for (int i = 0; i < BLOCK_SIZE / 64; i++)
  {
    if (working_directory[i].file_name == sourcepath)
    {
      found_source_dir = true;
      src_index = i;
    }
    if (working_directory[i].file_name == destpath)
    {
      if (working_directory[i].type == TYPE_FILE)
      {
        // std::cout << "Error: Destination " << destpath << " already exists" << std::endl;
        return -1;
      }
      else {
        // If destination is a directory, need some way to find the directory block
        // to move the dir_entry from root_block to new directory. Not necessary to move file blocks.
        destination_is_dir = true;
        dir_index = i;
      }
    }
  }

  if (found_source_dir) {
    std::strcpy(working_directory[src_index].file_name, destpath.c_str());
  } else {
    // std::cout << "Error: Source " << sourcepath << " not found" << std::endl;
    return -1;
  }

  disk.write(cwd, (uint8_t *)working_directory);

  return 0;
}

// rm <filepath> removes / deletes the file <filepath>
int FS::rm(std::string filepath)
{
  std::cout << "FS::rm(" << filepath << ")\n";

  // TODO should not be possible to remove nonempty directory
  // TODO check access rights?

  // Read working directory block and FAT 
  disk.read(cwd, (uint8_t *)working_directory);
  disk.read(FAT_BLOCK, (uint8_t *)fat);

  // Find filepath
  bool found_dir = false;
  int current_block, next_block;

  for (auto &dir : working_directory)
  {
    if (dir.type != TYPE_EMPTY && dir.file_name == filepath) // type file/directory?
    {
      // Mark dir_entry as empty
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

  // printFAT();

  // Free FAT entries
  do
  {
    next_block = fat[current_block];
    fat[current_block] = FAT_FREE;
    current_block = next_block;
    // std::cout << "current: " << current_block << ", next: " << next_block << std::endl;
  } while (current_block != FAT_EOF);

  // Write to FAT
  disk.write(FAT_BLOCK, (uint8_t *)fat);
  printFAT();

  // Write to working directory
  disk.write(cwd, (uint8_t *)working_directory);

  return 0;
}

// append <filepath1> <filepath2> appends the contents of file <filepath1> to
// the end of file <filepath2>. The file <filepath1> is unchanged.
int FS::append(std::string filepath1, std::string filepath2)
{
  std::cout << "FS::append(" << filepath1 << "," << filepath2 << ")\n";

  // TODO: Update this for directories.

  // Read working directory block and FAT
  disk.read(cwd, (uint8_t *)working_directory);
  disk.read(FAT_BLOCK, (uint8_t *)fat);

  // Search for filepaths, exit if not found
  int filepath1_block, filepath2_block, size1, size2;
  int no_found = 0;
  for (auto &dir : working_directory)
  {
    if (std::string(dir.file_name) == filepath1)
    {
      // Check for READ permission
      if (dir.access_rights < READ)
      {
        std::cout << filepath1 << " does not have read permission" << std::endl;
      }
      
      filepath1_block = dir.first_blk;
      size1 = dir.size;
      no_found++;
    }
    if (std::string(dir.file_name) == filepath2)
    {
      // Check for WRITE permission
      if ((dir.access_rights & WRITE) != WRITE) // bitwise and to check if 0x02 bit is true
      {
        std::cout << filepath2 << " does not have write permission" << std::endl;
        return -1;
      }
      
      filepath2_block = dir.first_blk;
      size2 = dir.size;
      no_found++;
    }
  }

  if (no_found != 2) // Expecting exactly two files found.
    return -1;

  // Check if enough free blocks
  int no_free_blocks = getNoFreeBlocks();
  if (no_free_blocks < size1 / BLOCK_SIZE + 1)
    return -1;

  // Find end of filepath2
  while (fat[filepath2_block] != FAT_EOF)
    filepath2_block = fat[filepath2_block];

  // Update FAT entries for filepath2
  updateFAT(filepath2_block, size1); // May allocate one too many fat entries, TODO
  disk.write(FAT_BLOCK, (uint8_t *)fat);

  // Read filepath1 data
  char block[BLOCK_SIZE] = {0};
  std::string filepath1_data = "";
  int current_block = filepath1_block;

  while(current_block != FAT_EOF)
  {
    disk.read(current_block, (uint8_t *)block);
    filepath1_data.append(block); // append char* to string

    current_block = fat[current_block];
  }

  // Write data to the end of filepath2
  int data_index = 0, first_iteration = 1;
  while (filepath2_block != FAT_EOF && data_index < size1)
  {
    // Write one block at a time
    if (first_iteration)
    {
      // Read last block of filepath2 and start writing at the end
      disk.read(filepath2_block, (uint8_t *)block);
      for (int i = size2 % BLOCK_SIZE; i < BLOCK_SIZE; i++)
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

  // Update filepath2.size in the working directory block
  for (auto &dir : working_directory)
  {
    if (std::string(dir.file_name) == filepath2)
    {
      dir.size = size1 + size2;
      break;
    }
  }

  disk.write(cwd, (uint8_t*)working_directory);

  return 0;
}

// mkdir <dirpath> creates a new sub-directory with the name <dirpath>
// in the current directory
int FS::mkdir(std::string dirpath)
{
  std::cout << "FS::mkdir(" << dirpath << ")\n";

  // Read working directory block and FAT
  disk.read(cwd, (uint8_t *)working_directory);
  disk.read(FAT_BLOCK, (uint8_t *)fat);

  // Make sure filepath doesn't already exist
  for (auto &dir : working_directory)
  {
    if (dir.file_name == dirpath && dir.type != TYPE_EMPTY)
    {
      std::cout << dirpath << " already exists" << std::endl;
      return -1;
    }
  }

  int current_block = findFirstFreeBlock();
  if (current_block == -1)
  {
    std::cout << "No free block on disk" << std::endl;
    return -1;
  }

  // Create dir_entry for directory
  dir_entry dir_ent;
  std::strcpy(dir_ent.file_name, dirpath.c_str());
  dir_ent.size = 0;
  dir_ent.first_blk = current_block;
  dir_ent.type = TYPE_DIR;
  dir_ent.access_rights = READ | WRITE | EXECUTE;

  updateFAT(dir_ent.first_blk, dir_ent.size);

  // Write FAT to disk
  disk.write(FAT_BLOCK, (uint8_t *)fat);

  printFAT();

  // Write to working directory block
  dir_entry *ptr = &dir_ent;
  createDirEntry(ptr); // reads, modifies, writes working directory block

  // Create dir_entry ".." first in this directory's block
  std::string filename = "..";
  std::strcpy(dir_ent.file_name, filename.c_str());
  dir_ent.size = 0; 
  dir_ent.first_blk = cwd; // parent directory block
  dir_ent.type = TYPE_DIR;
  dir_ent.access_rights = READ | WRITE | EXECUTE;

  // uint16_t orig_cwd = cwd;
  // cwd = current_block;
  working_directory[0] = dir_ent;

  // Change dir_entry to be empty
  filename = "";
  std::strcpy(dir_ent.file_name, filename.c_str());
  dir_ent.first_blk = 0;
  dir_ent.type = TYPE_EMPTY;
  dir_ent.access_rights = 0;

  // Fill rest of dir block with empty dir_entry
  for (int i = 1; i < BLOCK_SIZE / 64; i++)
  {
    working_directory[i] = dir_ent;
  }

  disk.write(current_block, (uint8_t*)working_directory);

  // Reset cwd
  // cwd = orig_cwd;

  return 0;
}

// cd <dirpath> changes the current (working) directory to the directory named <dirpath>
int FS::cd(std::string dirpath)
{
  std::cout << "FS::cd(" << dirpath << ")\n";

  // Interpret string to determine the steps required
  std::vector<std::string> filepath = interpretFilepath(dirpath);

  int temp = cwd;
  std::string delimiter = "/";

  if (filepath[0] == delimiter)
  {
    cwd == ROOT_BLOCK;
  }

  disk.read(cwd, (uint8_t*)working_directory);

  bool dir_found = false;
  for (int i = 0; i < filepath.size(); i++)
  {
    if (filepath[i] == delimiter)
      continue;

    for (auto &dir : working_directory)
    {
      if (filepath[i] == ".") // We don't have to change cwd if we encounter '.'
        continue;

      if (std::string(dir.file_name) == filepath[i])
      {
        cwd = dir.first_blk;
        dir_found = true;
        break;
      }
    }
    if (!dir_found)
    {
      cwd = temp;
      return -1;
    }
    disk.read(cwd, (uint8_t *)working_directory);
  }

  return 0;
}

// pwd prints the full path, i.e., from the root directory, to the current
// directory, including the currect directory name
int FS::pwd()
{
  std::cout << "FS::pwd()\n";

  // Read working directory
  disk.read(cwd, (uint8_t *)working_directory);
  uint16_t current_dir = cwd;
  std::string path = "";

  // Build absolute path string by walking to root directory
  while (current_dir != ROOT_BLOCK)
  {
    uint16_t parent_dir = working_directory[0].first_blk;
    disk.read(parent_dir, (uint8_t *)working_directory);

    bool found = false;
    // Search in parent directory for current directory's filename
    for (auto &dir_ent : working_directory)
    {
      if (dir_ent.file_name == "..")
        std::cout << "found '..' in directory" << std::endl; // never prints?
      if (dir_ent.first_blk == current_dir)
      {
        path = dir_ent.file_name + path;
        path = "/" + path;
        break;
      }
    }
    current_dir = parent_dir;
  }

  if (path[0] != '/')
    path = "/" + path;

  std::cout << path << std::endl;

  return 0;
}

// chmod <accessrights> <filepath> changes the access rights for the
// file <filepath> to <accessrights>.
int FS::chmod(std::string accessrights, std::string filepath)
{
  std::cout << "FS::chmod(" << accessrights << "," << filepath << ")\n";
  
  // TODO handle absolute paths and relative paths not in cwd

  // Read working directory block and FAT
  disk.read(cwd, (uint8_t *)working_directory);
  disk.read(FAT_BLOCK, (uint8_t *)fat);

  // Check that filepath exists (in cwd only)
  bool found_dir = false;
  for (auto &dir : working_directory)
  {
    if (std::string(dir.file_name) == filepath)
    {
      found_dir = true;
      dir.access_rights = (uint8_t) std::stoi(accessrights, nullptr, 16); // hex, base 16
      break;
    }
  }

  if (!found_dir)
    return -1;
  
  // Write to disk
  disk.write(cwd, (uint8_t *)working_directory);
  
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

  // Read working directory block
  disk.read(cwd, (uint8_t *)working_directory);

  int k = 1;
  for (k; k < BLOCK_SIZE / 64; k++)
  {
    if (working_directory[k].type == TYPE_EMPTY) // first empty in working directory block
      break;
  }
  if (k == BLOCK_SIZE / 64)
    return -1;

  // Edit working directory block
  working_directory[k] = *de;

  // Write working directory block to disk
  disk.write(cwd, (uint8_t *)working_directory);

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

int FS::findPath(std::string path)
{
  return 0;
}

std::vector<std::string> FS::interpretFilepath(std::string dirpath)
{
  std::vector<std::string> path_vec;
  char delimiter = '/';
  if (dirpath[0] == delimiter)
    path_vec.push_back("/");

  // Interpret string to determine the steps required

  std::istringstream iss(dirpath);
  std::string path;
  while (std::getline(iss, path, delimiter))
  {
    if (path == ".") // We don't have to change cwd if we encounter '.'
      continue;

    path_vec.push_back(path);
  }

  return path_vec;
}

// Get access rights string (TODO: fix better solution)
std::string FS::accessRightsToString(uint8_t access_rights)
{
  std::string rwx = "---";
  std::string rwx_chars = "rwx";
  uint8_t rights[3] = {READ, WRITE, EXECUTE};

  for (int i = 0; i < rwx.length(); i++)
  {
    if ((access_rights & rights[i]) == rights[i]) // bitwise and
      rwx[i] = rwx_chars[i];
  }

  return rwx;

  // if (access_rights == (READ | WRITE | EXECUTE))
  //   rwx = "rwx";
  // else if (access_rights == (READ | WRITE))
  //   rwx = "rw-";
  // else if (access_rights == (READ | EXECUTE))
  //   rwx = "r-x";
  // else if (access_rights == (WRITE | EXECUTE))
  //   rwx = "-wx";
  // else if (access_rights == READ)
  //   rwx = "r--";
  // else if (access_rights == WRITE)
  //   rwx = "-w-";
  // else if (access_rights == EXECUTE)
  //   rwx = "--x";
  // else 
  //   rwx = "---";
}
