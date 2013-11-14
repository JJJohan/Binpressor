#ifndef BINPRESSOR_H
#define BINPRESSOR_H

#define MAJOR_VERSION 1
#define MINOR_VERSION 1

#define NAME_BUFFER 128
#define EXT_BUFFER 32
#define SIZE_BUFFER 32

#define READ_STEP 26214400
#define WRITE_STEP 26214400

#include <iostream>
#include <vector>
#include <string>

class Binpressor
{
public:
	Binpressor(int argc, char* argv[]);
	~Binpressor();

private:
	struct Descriptor
	{
		char name[NAME_BUFFER];
		char ext[EXT_BUFFER];
		char size[SIZE_BUFFER];
		char* data;
	};

	struct InDescriptor
	{
		char* name;
		char* ext;
		char* size;
		char* data;
	};

	int IsFileOrFolder(const char* a_filePath);
	void PrintFilePaths();
	void PrintFileDescriptors();

	void CollectFilePaths(const char* a_folderPath);
	void CollectFileInfo();

	void Package();
	void ReadFiles();
	void Unpackage();

	std::vector<std::string> m_filePaths;
	std::vector<Descriptor*> m_descriptors;
	std::vector<InDescriptor*> m_inFiles;
	std::vector<std::string> m_packages;
};

#endif // BINPRESSOR_H