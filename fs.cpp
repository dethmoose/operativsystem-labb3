#include <iostream>
#include <sstream>
#include "fs.h"

// TODO:
/*
  cp: update to handle copy to directory
  create general findDir function
  ls: save tuples (file_name, size) to a vector and then print sorted alphabetically?
  "the access rights on a directory must be correct for various file operations
  (e.g., moving/copying a file to a directory requires write access on that directory), etc"
*/

FS::FS()
{
  std::cout << "FS::FS()... Creating file system\n";

  // Initialize empty working directory
  std::string empty_name = "";
  dir_entry dir_ent;
  std::strcpy(dir_ent.file_name, empty_name.c_str());
  dir_ent.size = 0;
  dir_ent.first_blk = 0;
  dir_ent.type = TYPE_EMPTY;
  dir_ent.access_rights = 0;

  for (auto &dir : working_directory)
  {
    dir = dir_ent;
  }
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

  std::vector<std::string> filepath_vec = interpretFilepath(filepath);
  std::string new_filename = filepath_vec.back();
  filepath_vec.pop_back();

  int current_block = findFirstFreeBlock();
  if (current_block == -1)
  {
    std::cout << "No free block on disk" << std::endl;
    return -1;
  }

  if (new_filename.length() > 56)
  {
    std::cout << "File name exceeds 56 character limit" << std::endl;
    return -1;
  }

  // Read cwd block and FAT block
  disk.read(cwd, (uint8_t *)working_directory);
  disk.read(FAT_BLOCK, (uint8_t *)fat);

  int temp_cwd = cwd;
  if (filepath_vec.size())
    temp_cwd = traverseToDir(filepath_vec);

  if (temp_cwd == -1 || temp_cwd >= BLOCK_SIZE)
  {
    std::cout << "Invalid path " << filepath << std::endl;
    return -1;
  }

  for (auto &dir : working_directory)
  {
    if (dir.file_name == new_filename && dir.type != TYPE_EMPTY)
    {
      std::cout << new_filename << " already exists" << std::endl;
      return -1;
    }
  }

  int access;
  if (access = getDirAccessRights(temp_cwd) == -1) // unsigned int so does -1 work? added comment in getDirAccessRights
    return -1;

  if (access & WRITE != WRITE)
  {
    std::cout << "No writing permission in directory" << std::endl;
    return -1;
  }

  // Create dir_entry
  dir_entry dir_ent;
  std::strcpy(dir_ent.file_name, new_filename.c_str());
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

  // Write FAT to disk
  updateFAT(dir_ent.first_blk, size);
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

  // Write to cwd block
  dir_entry *ptr = &dir_ent;
  createDirEntry(ptr, temp_cwd);

  return 0;
}

// cat <filepath> reads the content of a file and prints it on the screen
int FS::cat(std::string filepath)
{
  std::cout << "FS::cat(" << filepath << ")\n";

  // Read working directory block and FAT
  disk.read(cwd, (uint8_t *)working_directory);
  disk.read(FAT_BLOCK, (uint8_t *)fat);

  // Go to directory
  std::vector<std::string> filepath_vec = interpretFilepath(filepath);
  std::string file = filepath_vec.back();
  filepath_vec.pop_back();

  int temp_cwd = cwd;
  if (filepath_vec.size())
    temp_cwd = traverseToDir(filepath_vec);

  if (temp_cwd == -1)
  {
    std::cout << filepath << " Error: Invalid filepath." << std::endl;
    return -1;
  }

  // Find file
  bool found_dir = false;
  int current_block, size;
  for (auto &dir : working_directory)
  {
    if (dir.file_name == file && dir.type == TYPE_FILE)
    {
      if ((dir.access_rights & READ) != READ)
      {
        std::cout << "File " << file << " does not have read permission" << std::endl;
        return -1;
      }

      current_block = dir.first_blk;
      size = dir.size;
      found_dir = true;
      break;
    }
    else if (dir.file_name == file && dir.type == TYPE_DIR)
    {
      std::cout << file << " is a directory" << std::endl;
      return -1;
    }
  }

  if (!found_dir)
  {
    std::cout << "File " << file << " does not exist." << std::endl;
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
    if (dir.type != TYPE_EMPTY && std::string(dir.file_name).length() > dir_name_width)
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
    if (dir.type != TYPE_EMPTY && std::string(dir.file_name) != "/" && std::string(dir.file_name) != "..")
    {
      std::string rwx = accessRightsToString(dir.access_rights);

      std::cout << std::left << std::setw(dir_name_width) << std::setfill(' ') << std::string(dir.file_name);
      std::cout << std::left << std::setw(6) << std::setfill(' ') << "dir";
      std::cout << std::left << std::setw(14) << std::setfill(' ') << rwx;

      if (dir.type == TYPE_DIR)
        std::cout << std::left << std::setw(10) << std::setfill(' ') << "-";
      else if (dir.type == TYPE_FILE)
        std::cout << std::left << std::setw(10) << std::setfill(' ') << "-";
      
      std::cout << std::endl;
    }
  }
  std::cout << std::endl;
  return 0;
}

// cp <sourcepath> <destpath> makes an exact copy of the file
// <sourcepath> to a new file <destpath>
int FS::cp(std::string sourcepath, std::string destpath)
{
  std::cout << "FS::cp(" << sourcepath << "," << destpath << ")\n";

  // Read working directory block and FAT from disk
  disk.read(cwd, (uint8_t *)working_directory);
  disk.read(FAT_BLOCK, (uint8_t *)fat);

  std::vector<std::string> source_vec = interpretFilepath(sourcepath);
  std::vector<std::string> dest_vec = interpretFilepath(destpath);

  std::string source = source_vec.back();
  source_vec.pop_back();

  std::string destination = dest_vec.back();
  dest_vec.pop_back();

  int temp_cwd = cwd;
  if (source_vec.size())
    temp_cwd = traverseToDir(source_vec);

  // Find sourcepath, make sure destpath doesn't exist
  bool found_source_dir = false, found_dest_dir = false;
  uint32_t size;
  uint16_t source_block;
  uint8_t type, access_rights;
  for (auto &dir : working_directory)
  {
    if (dir.file_name == source)
    {
      if (dir.access_rights & READ != READ)
      {
        std::cout << "Missing read permission on " << source << std::endl;
        return -1;
      }

      source_block = dir.first_blk;
      size = dir.size;
      type = dir.type;
      access_rights = dir.access_rights;
      found_source_dir = true;
    }
  }

  if (!found_source_dir)
  {
    std::cout << sourcepath << " does not exist" << std::endl;
    return -1;
  }

  if (dest_vec.size())
    temp_cwd = traverseToDir(dest_vec);
  // else
  // {
  //   temp_cwd = cwd                   // what if source had a path but dest should be in original cwd?
  //   disk.read(cwd, (uint8_t *)working_directory);
  // }

  // What if destination is a directory?
  bool is_dir = false;
  for (auto &dir : working_directory)
  {
    if (dir.file_name == destination)
    {
      found_dest_dir = true;
      if (dir.type = TYPE_DIR)
      {
        if (dir.access_rights & WRITE != WRITE)
        {
          std::cout << "Missing write permission on " << destination << std::endl;
          return -1;
        }

        temp_cwd = dir.first_blk;
        destination = source;
        is_dir = true;
      }
      break;
    }
  }

  if (found_dest_dir && !is_dir) // If destpath is found, and it's not a directory, cannot copy.
  {
    std::cout << "File " << destpath << " already exists" << std::endl;
    return -1;
  }

  int no_free = getNoFreeBlocks();
  if (no_free < size / BLOCK_SIZE)
  {
    std::cout << "Not enough free blocks on disk" << std::endl;
    return -1;
  }

  // Create new file
  dir_entry dir_ent;
  std::strcpy(dir_ent.file_name, destination.c_str());
  dir_ent.size = size;
  dir_ent.type = type;
  dir_ent.access_rights = access_rights;

  int block_no = findFirstFreeBlock();
  dir_ent.first_blk = block_no;

  // Write to destpath
  createDirEntry(&dir_ent, temp_cwd);

  // Write to FAT
  updateFAT(block_no, size);
  disk.write(FAT_BLOCK, (uint8_t *)fat);

  // Copy data from sourcepath to destpath
  uint16_t dest_block = dir_ent.first_blk;
  char block[BLOCK_SIZE] = {0};

  while (true)
  {
    disk.read(source_block, (uint8_t *)block); // read source block
    disk.write(dest_block, (uint8_t *)block);  // write dest block
    if (fat[source_block] == FAT_EOF || fat[dest_block] == FAT_EOF)
      break;
    dest_block = fat[dest_block];
    source_block = fat[source_block];
  }

  return 0;
}

// TODO NOT DONE FIX THIS
//  mv <sourcepath> <destpath> renames the file <sourcepath> to the name <destpath>,
//  or moves the file <sourcepath> to the directory <destpath> (if dest is a directory)
int FS::mv(std::string sourcepath, std::string destpath)
{
  std::cout << "FS::mv(" << sourcepath << "," << destpath << ")\n";

  // Is it better to start searching for destination so we know if we're renaming or
  // moving the dir_entry to another directory, before searching for the source?
  // Could then rename directly if dest was a file

  // Read working directory block and FAT
  disk.read(cwd, (uint8_t *)working_directory);
  disk.read(FAT_BLOCK, (uint8_t *)fat); // don't need FAT?

  std::vector<std::string> source_vec = interpretFilepath(sourcepath);
  std::vector<std::string> dest_vec = interpretFilepath(destpath);

  std::string source = source_vec.back();
  std::string destination = source_vec.back();

  source_vec.pop_back();
  dest_vec.pop_back();

  dir_entry temp1;
  dir_entry temp2;

  int source_cwd = cwd;
  if (source_vec.size())
    source_cwd = traverseToDir(source_vec);

  if (source_cwd == -1)
  {
    std::cout << "Invalid path " << sourcepath << std::endl;
    return -1;
  }

  // Search for sourcepath
  bool found_source_dir = false, found_dest_dir = false, destination_is_dir = false;

  for (auto &dir : working_directory)
  {
    if (dir.file_name == source)
    {
      if (dir.type == TYPE_DIR)
      {
        std::cout << source << " is a directory" << std::endl;
        return -1;
      }

      found_source_dir = true;
      temp1 = dir;
    }
  }

  if (!found_source_dir)
  {
    std::cout << sourcepath << " does not exist" << std::endl;
    return -1;
  }

  // Search for destpath
  int dest_cwd = cwd;
  if (dest_vec.size())
    dest_cwd = traverseToDir(dest_vec);

  if (dest_cwd == -1)
  {
    std::cout << "Invalid path " << destpath << std::endl;
    return -1;
  }

  for (auto &dir : working_directory)
  {
    if (dir.type != TYPE_EMPTY && dir.file_name == destination)
    {
      found_dest_dir = true;

      if (dir.type == TYPE_FILE)
      {
        std::cout << "File" << destination << " already exists" << std::endl;
        return -1;
      }
      else // TYPE_DIR
      {
        // If destination is a directory, need some way to find the directory block
        // to move the dir_entry from root_block to new directory. Not necessary to move file blocks.
        destination_is_dir = true;
      }
      break;
    }
  }

  // Do the mv
  disk.read(source_cwd, (uint8_t *)working_directory);

  if (!found_dest_dir) // Rename source to destination
  {
    for (auto &dir : working_directory) // repeating code
    {
      if (dir.type != TYPE_EMPTY && dir.file_name == source)
      {
        std::strcpy(dir.file_name, destination.c_str());
        disk.write(source_cwd, (uint8_t *)working_directory);
        break;
      }
    }
  }
  else if (found_dest_dir && destination_is_dir) // Move source to destination
  {
    for (auto &dir : working_directory) // repeating code
    {
      if (dir.type != TYPE_EMPTY && dir.file_name == source)
      {
        dir.type = TYPE_EMPTY;
        disk.write(source_cwd, (uint8_t *)working_directory);
        break;
      }
    }
    // source dir_entry in temp1
    disk.read(dest_cwd, (uint8_t *)working_directory);
    for (auto &dir : working_directory)
    {
      if (dir.type == TYPE_EMPTY)
      {
        std::strcpy(dir.file_name, temp1.file_name);
        dir.size = temp1.size;
        dir.first_blk = temp1.first_blk;
        dir.access_rights = temp1.access_rights;
        dir.type = temp1.type;

        disk.write(dest_cwd, (uint8_t *)working_directory);
        break;
      }
    }
  }
  else // for debugging
  {
    std::cout << "Error (last else in mv)" << std::endl;
    return -1;
  }

  // disk.write(cwd, (uint8_t *)working_directory);

  return 0;
}

// rm <filepath> removes / deletes the file <filepath>
int FS::rm(std::string filepath)
{
  std::cout << "FS::rm(" << filepath << ")\n";

  // TODO should not be possible to remove directory the user stands in

  // Read working directory block and FAT
  disk.read(cwd, (uint8_t *)working_directory);
  disk.read(FAT_BLOCK, (uint8_t *)fat);

  std::vector<std::string> path_vec = interpretFilepath(filepath);
  std::string file = path_vec.back();
  path_vec.pop_back();

  // Go to path
  int temp_cwd = cwd;
  if (path_vec.size())
    temp_cwd = traverseToDir(path_vec);

  if (temp_cwd == -1)
  {
    std::cout << "Invalid path " << filepath << std::endl;
    return -1;
  }

  int access;
  if (access = getDirAccessRights(temp_cwd) == -1) // does it work with -1 ?
    return -1;

  if (access & (WRITE | READ) != (WRITE | READ)) // Requires Write and Read perm on directory
  {
    std::cout << "Missing read and write permission" << std::endl;
    return -1;
  }

  // Find filepath
  bool found_dir = false, found_file = false;
  int current_block, next_block;

  for (auto &dir : working_directory)
  {
    if (dir.file_name == file && dir.type == TYPE_FILE)
    {
      // Mark dir_entry as empty
      dir.type = TYPE_EMPTY;
      current_block = dir.first_blk;
      found_file = true;
      std::cout << "file" << std::endl;
      break;
    }
    else if (dir.file_name == file && dir.type == TYPE_DIR)
    {
      // Check that directory doesn't have any files/directories
      dir_entry temp_working_directory[BLOCK_SIZE / 64];
      disk.read(dir.first_blk, (uint8_t *)temp_working_directory);
      for (auto &dir2 : temp_working_directory)
      {
        if (dir2.type != TYPE_EMPTY && std::string(dir2.file_name) != "..")
        {
          if (std::string(dir2.file_name) != "/")
          {
            std::cout << "Cannot remove root" << std::endl;
            return -1;
          }
          std::cout << "Directory " << filepath << " is not empty" << std::endl;
          return -1;
        }
      }

      // Mark dir_entry as empty
      dir.type = TYPE_EMPTY;
      current_block = dir.first_blk;
      found_dir = true;
      break;
    }
  }

  if (!found_dir && !found_file)
  {
    std::cout << filepath << " does not exist" << std::endl;
    return -1;
  }

  // Write to working directory
  disk.write(temp_cwd, (uint8_t *)working_directory);

  // printFAT();
  // Free FAT entries
  do
  {
    next_block = fat[current_block];
    fat[current_block] = FAT_FREE;
    current_block = next_block;
  } while (current_block != FAT_EOF);

  // Write to FAT
  disk.write(FAT_BLOCK, (uint8_t *)fat);
  // printFAT();
  return 0;
}

// append <filepath1> <filepath2> appends the contents of file <filepath1> to
// the end of file <filepath2>. The file <filepath1> is unchanged.
int FS::append(std::string filepath1, std::string filepath2)
{
  std::cout << "FS::append(" << filepath1 << "," << filepath2 << ")\n";

  // Read working directory block and FAT
  disk.read(cwd, (uint8_t *)working_directory);
  disk.read(FAT_BLOCK, (uint8_t *)fat);

  std::vector<std::string> file1_vec = interpretFilepath(filepath1);
  std::vector<std::string> file2_vec = interpretFilepath(filepath2);

  // Search for filepaths, exit if not found
  int filepath1_block, filepath2_block, size1, size2;
  int no_found = 0;
  int temp_cwd = traverseToDir(file1_vec);

  for (auto &dir : working_directory)
  {
    if (std::string(dir.file_name) == file1_vec.back())
    {
      // Check for READ permission
      if (dir.access_rights & READ != READ)
      {
        std::cout << file1_vec.back() << " does not have read permission" << std::endl;
        return -1;
      }

      filepath1_block = dir.first_blk;
      size1 = dir.size;
      no_found++;
    }
  }

  temp_cwd = traverseToDir(file2_vec);
  for (auto &dir : working_directory)
  {
    if (std::string(dir.file_name) == file2_vec.back())
    {
      // Check for READ and WRITE permission
      if (dir.access_rights & (READ | WRITE) != (READ | WRITE))
      {
        std::cout << file2_vec.back() << " does not have read and write permission" << std::endl;
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

  while (current_block != FAT_EOF)
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

  disk.write(cwd, (uint8_t *)working_directory);

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

  std::vector<std::string> filepath = interpretFilepath(dirpath);
  std::string new_directory = filepath.back();
  filepath.pop_back();

  // int temp_cwd = cwd;
  // if (filepath.length())
  int temp_cwd = traverseToDir(filepath);

  if (temp_cwd == -1)
  {
    std::cout << "Invalid path " << dirpath << std::endl;
    return -1;
  }

  disk.read(temp_cwd, (uint8_t *)working_directory);

  // Make sure filepath doesn't already exist
  for (auto &dir : working_directory)
  {
    if (dir.file_name == new_directory && dir.type != TYPE_EMPTY)
    {
      std::cout << new_directory << " already exists" << std::endl;
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
  std::strcpy(dir_ent.file_name, new_directory.c_str());
  dir_ent.size = 0;
  dir_ent.first_blk = current_block;
  dir_ent.type = TYPE_DIR;
  dir_ent.access_rights = READ | WRITE | EXECUTE;

  // Write FAT to disk
  updateFAT(dir_ent.first_blk, dir_ent.size);
  disk.write(FAT_BLOCK, (uint8_t *)fat);
  // printFAT();

  // Write to working directory block
  dir_entry *ptr = &dir_ent;
  createDirEntry(ptr, temp_cwd);

  // Create dir_entry ".." first in this directory's block
  std::string filename = "..";
  std::strcpy(dir_ent.file_name, filename.c_str());
  dir_ent.size = 0;
  dir_ent.first_blk = temp_cwd; // parent directory block
  dir_ent.type = TYPE_DIR;
  dir_ent.access_rights = READ | WRITE | EXECUTE;

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

  disk.write(current_block, (uint8_t *)working_directory);

  return 0;
}

// cd <dirpath> changes the current (working) directory to the directory named <dirpath>
int FS::cd(std::string dirpath)
{
  std::cout << "FS::cd(" << dirpath << ")\n";

  // Interpret string to determine the steps required
  std::vector<std::string> filepath = interpretFilepath(dirpath);

  int temp = cwd;

  disk.read(cwd, (uint8_t *)working_directory);
  temp = traverseToDir(filepath);

  // std::cout << temp << std::endl;
  if (temp == -1) // || temp >= BLOCK_SIZE
  {
    std::cout << "Invalid path " << dirpath << std::endl;
    return -1;
  }
  cwd = temp;

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

  // Refresh working_directory
  disk.read(cwd, (uint8_t *)working_directory);

  std::vector<std::string> file_vec = interpretFilepath(filepath);

  int temp_cwd = traverseToDir(file_vec);

  if (temp_cwd == -1)
  {
    std::cout << filepath << " Error: Invalid filepath." << std::endl;
    return -1;
  }

  // Read working directory block
  disk.read(temp_cwd, (uint8_t *)working_directory);

  // Check that filepath exists (in cwd only)
  bool found_dir = false;
  for (auto &dir : working_directory)
  {
    if (std::string(dir.file_name) == file_vec.back())
    {
      found_dir = true;
      dir.access_rights = std::stoi(accessrights, nullptr, 16); // hex, base 16
      break;
    }
  }

  if (!found_dir)
    return -1;

  // Write to disk
  disk.write(temp_cwd, (uint8_t *)working_directory);

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
int FS::createDirEntry(dir_entry *de, int dir_block)
{
  int not_empty = 0;

  // Read working directory block
  disk.read(dir_block, (uint8_t *)working_directory);

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
  disk.write(dir_block, (uint8_t *)working_directory);

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

    if (path.length())
      path_vec.push_back(path);
    path = "";
  }
  return path_vec;
}

// Get access rights string
std::string FS::accessRightsToString(uint8_t access_rights)
{
  std::string rwx = "---";
  std::string rwx_chars = "rwx";
  uint8_t rights[3] = {READ, WRITE, EXECUTE};

  for (int i = 0; i < rwx.length(); i++)
  {
    if ((access_rights & rights[i]) == rights[i]) 
      rwx[i] = rwx_chars[i];
  }

  return rwx;
}

uint8_t FS::getDirAccessRights(int dir_block)
{
  // Find parent directory
  for (auto &dir : working_directory)
    if (std::string(dir.file_name) == "..")
    {
      disk.read(dir.first_blk, (uint8_t *)working_directory);
      break;
    }

  // Get access rights for directory
  uint8_t access = 0; // unsigned int, so -1 becomes 255?
  for (auto &dir : working_directory)
    if (dir.first_blk == dir_block)
      access = dir.access_rights;

  disk.read(dir_block, (uint8_t *)working_directory);
  return access;
}

int FS::traverseToDir(std::vector<std::string> filepath)
{
  int temp = cwd;
  for (int i = 0; i < filepath.size(); i++)
  {
    if (filepath[i] == "/") // absolute path, start from ROOT_BLOCK
    {
      temp = ROOT_BLOCK;
      disk.read(temp, (uint8_t *)working_directory);
      continue;
    }

    bool dir_found = false;
    bool execute = false;
    for (auto &dir : working_directory)
    {
      if (dir.file_name == filepath[i] && dir.type == TYPE_FILE)
      {
        if (std::string(dir.file_name) == filepath.back() && i == filepath.size() - 1) // last element
          return temp;
        return -1;
      }
      if (dir.file_name == filepath[i] && dir.type == TYPE_DIR)
      {
        temp = dir.first_blk;
        dir_found = true;

        if (dir.access_rights & EXECUTE == EXECUTE)
          execute = true;

        break;
      }
    }
    if (!dir_found || !execute)
    {
      return -1;
    }
    disk.read(temp, (uint8_t *)working_directory);
  }

  return temp; // new cwd
}
