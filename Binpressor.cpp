#include "easylzma/compress.h"
#include "easylzma/decompress.h"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <stdio.h>
#include <tchar.h>

#include "Binpressor.h"
#include "boost/filesystem.hpp"
#include "boost/filesystem/path.hpp"
#include <sstream>
#include <direct.h>

#if defined(_WIN64)
#define _UINT UINT64
#else
#define _UINT UINT32
#endif

struct dataStream 
{
	char* inData;
	size_t inLen;

	char* outData;
	size_t outLen;
};

struct progressStruct
{
	int totalSize;
	std::string fileName;
};

static void
	elzmaProgressFunc(void *ctx, size_t complete, size_t total)
{
	progressStruct* progress = (progressStruct*)ctx;
	
	std::cout << "\33\r[Compressing - ";
	std::cout << (int)ceil(((float)complete / (float)progress->totalSize) * 100);
	std::cout << "%] " << progress->fileName;
}

static int
	inputCallback(void *ctx, void *buf, size_t * size)
{
	size_t rd = 0;
	struct dataStream * ds = (struct dataStream *) ctx;
	assert(ds != NULL);

	rd = (ds->inLen < *size) ? ds->inLen : *size;

	if (rd > 0) {
		memcpy(buf, (void *) ds->inData, rd);
		ds->inData += rd;
		ds->inLen -= rd;
	}

	*size = rd;

	return 0;
}

static size_t
	outputCallback(void *ctx, const void *buf, size_t size)
{
	struct dataStream * ds = (struct dataStream *) ctx;
	assert(ds != NULL);

	if (size > 0) {
		ds->outData = (char*)realloc(ds->outData, ds->outLen + size);
		memcpy((void *) (ds->outData + ds->outLen), buf, size);
		ds->outLen += size;
	}

	return size;
}

///////////////////////////////////////////////////////////////////////////
// C'tor
///////////////////////////////////////////////////////////////////////////
Binpressor::Binpressor(int argc, char* argv[])
{
	// Set up console.
	SetConsoleTitle("Binpressor");
	std::cout << "Binpressor - V" << MAJOR_VERSION << "." << MINOR_VERSION;
#if defined (_WIN64)
	std::cout << " - (x64)";
#endif
	std::cout << std::endl;
	std::cout << "Proprietary binary packaging tool." << std::endl;
	std::cout << "Copyright (C) 2012 Johan Rensenbrink." << std::endl;
	std::cout << "----------------------------------" << std::endl;

	// Grab files and folders.
	for(int i = 1; i < argc; i++)
	{
		if (IsFileOrFolder(argv[i]) == 1)
		{
			if (boost::filesystem::extension(argv[i]) != ".bin")
				m_filePaths.push_back(argv[i]);
			else
				m_packages.push_back(argv[i]);
		}
		else if (IsFileOrFolder(argv[i]) == 2)
			CollectFilePaths(argv[i]);
	}

	CollectFileInfo();
	Package();

	ReadFiles();
	Unpackage();

	if (!m_filePaths.empty())
		PrintFilePaths();

	if (!m_descriptors.empty())
		PrintFileDescriptors();
}

///////////////////////////////////////////////////////////////////////////
// D'tor
///////////////////////////////////////////////////////////////////////////
Binpressor::~Binpressor()
{

}

///////////////////////////////////////////////////////////////////////////
// Returns if given argument is a file, directory or invalid.
///////////////////////////////////////////////////////////////////////////
int Binpressor::IsFileOrFolder(const char* a_filePath)
{
	DWORD dwAttrib = GetFileAttributes(a_filePath);

	// Folder
	if (dwAttrib != INVALID_FILE_ATTRIBUTES && (dwAttrib & FILE_ATTRIBUTE_DIRECTORY))
		return 2;

	// File
	if (dwAttrib != INVALID_FILE_ATTRIBUTES)
		return 1;

	// Invalid
	return 0;
}

///////////////////////////////////////////////////////////////////////////
// Recursively finds all files from parsed-in folders.
///////////////////////////////////////////////////////////////////////////
void Binpressor::CollectFilePaths(const char* a_folderPath)
{
	for (boost::filesystem::recursive_directory_iterator end, dir(a_folderPath); dir != end; ++dir) 
	{
		std::string filePath = dir->path().string();

		if (IsFileOrFolder(filePath.c_str()) == 1)
			m_filePaths.push_back(filePath);     
	}
}

///////////////////////////////////////////////////////////////////////////
// Finds all necessary file information to prepare for packaging.
///////////////////////////////////////////////////////////////////////////
void Binpressor::CollectFileInfo()
{
	if (m_filePaths.empty())
		return;

	std::cout << "Reading File(s)..." << std::endl;
	
	_UINT totalSize = 0;
	_UINT totalWholeRead = 0;
	for (auto it = m_filePaths.begin(); it != m_filePaths.end(); ++it)
		totalSize += (_UINT)boost::filesystem::file_size(boost::filesystem::path(*it));

#if !defined(_WIN64)
	if (totalSize >= 2140000000) // Leave some space for descriptors.
	{
		std::cout << "Total file size too large to package." << std::endl;
		std::cout << "Please use 64-bit version instead." << std::endl;
		std::cout << std::endl;
		std::cout << "Packaging canceled." << std::endl;

		return;
	}
#endif

	std::vector<std::string> failedFilePaths;

	auto it = m_filePaths.begin();
	while (it != m_filePaths.end())
	{
		Descriptor* descriptor = new Descriptor();

		boost::filesystem::path path(*it);
		std::string fname = boost::filesystem::basename(path).c_str();
		if (fname.length() >= NAME_BUFFER)
		{
			failedFilePaths.push_back(*it);
			delete descriptor;
			it = m_filePaths.erase(it);
			continue;
		}
		sprintf_s(descriptor->name, NAME_BUFFER, "%s", fname.c_str());
		std::string fext = boost::filesystem::extension(path);
		if (fext.length() >= EXT_BUFFER)
		{
			delete descriptor;
			continue;
		}
		sprintf_s(descriptor->ext, EXT_BUFFER, "%s", fext.c_str());
		_UINT fsize = (_UINT)boost::filesystem::file_size(path);
		sprintf_s(descriptor->size, SIZE_BUFFER, "%i", fsize);

		std::ifstream fstream;
		fstream.open((*it).c_str(), std::fstream::in | std::fstream::binary);
		if (fstream.is_open())
		{
			descriptor->data = new char[strtoul(descriptor->size, NULL, 0)];
			memset(descriptor->data, 0, strtoul(descriptor->size, NULL, 0));
			//fstream.read(descriptor->data, strtoul(descriptor->size, NULL, 0));

			_UINT totalRead = 0;
			while (totalRead < strtoul(descriptor->size, NULL, 0))
			{
				_UINT bytesToRead = READ_STEP;
				if (totalRead + bytesToRead > strtoul(descriptor->size, NULL, 0))
					bytesToRead = strtoul(descriptor->size, NULL, 0) - totalRead;

				fstream.read(&descriptor->data[totalRead], bytesToRead);
				totalRead += bytesToRead;
				totalWholeRead += bytesToRead;

				std::cout << "\33\r[Reading - " << totalRead / strtoul(descriptor->size, NULL, 0) * 100 << "%] ";
				std::cout << descriptor->name << descriptor->ext;
			}
			fstream.close();
		}
		else
		{
			std::cout << "ERROR: Could not open file for reading:" << std::endl;
			std::cout << *it << std::endl;
		}

		std::cout << std::endl;
		m_descriptors.push_back(descriptor);
		it = m_filePaths.erase(it);
	}

	std::cout << std::endl << "Reading complete." << std::endl << std::endl;

	for (auto it = failedFilePaths.begin(); it != failedFilePaths.end(); ++it)
		m_filePaths.push_back(*it);
}

///////////////////////////////////////////////////////////////////////////
// Packages the files into a binary package.
///////////////////////////////////////////////////////////////////////////
void Binpressor::Package()
{
	if (m_descriptors.empty())
		return;

	std::cout << "Packaging File(s)..." << std::endl;

	_UINT totalSize = 0;
	_UINT headerSize = 0;
	for (auto it = m_descriptors.begin(); it != m_descriptors.end(); ++it)
	{
		totalSize += atoi((*it)->size);
		headerSize += NAME_BUFFER + EXT_BUFFER + SIZE_BUFFER;
	}

	std::ofstream fstream;
	fstream.open("package.bin", std::fstream::out | std::fstream::binary);
	if (fstream.is_open())
	{
		// Write header data
		char majVersion[4];
		memset(majVersion, 0, 4);
		sprintf_s(majVersion, 4, "%i", MAJOR_VERSION);
		char minVersion[4];
		memset(minVersion, 0, 4);
		sprintf_s(minVersion, 4, "%i", MINOR_VERSION);
		char nameBuffer[8];
		memset(nameBuffer, 0, 8);
		sprintf_s(nameBuffer, 8, "%i", NAME_BUFFER);
		char extBuffer[8];
		memset(extBuffer, 0, 8);
		sprintf_s(extBuffer, 8, "%i", EXT_BUFFER);
		char sizeBuffer[8];
		memset(sizeBuffer, 0, 8);
		sprintf_s(sizeBuffer, 8, "%i", SIZE_BUFFER);

		fstream.write(majVersion, 4);
		fstream.write(minVersion, 4);
		fstream.write(nameBuffer, 8);
		fstream.write(extBuffer, 8);
		fstream.write(sizeBuffer, 8);

		auto it = m_descriptors.begin();
		while (it != m_descriptors.end())
		{
			Descriptor* descriptor = *it;
			_UINT totalWritten = 0;

			fstream.write(descriptor->name, NAME_BUFFER);
			fstream.write(descriptor->ext, EXT_BUFFER);

			int oldSize = strtoul(descriptor->size, NULL, 0);

			dataStream ds;
			ds.inData = descriptor->data;
			ds.inLen = strtoul(descriptor->size, NULL, 0);
			ds.outData = NULL;
			ds.outLen = 0;

			std::cout << "\33\r[Compressing - 0%] " << descriptor->name << descriptor->ext;

			elzma_compress_handle handle = elzma_compress_alloc();

			progressStruct pCtx;
			std::stringstream fName;
			fName << descriptor->name << descriptor->ext;
			pCtx.fileName = fName.str();
			pCtx.totalSize = ds.inLen;

			elzma_compress_run(handle, inputCallback, (void *) &ds,
				outputCallback, (void *) &ds,
				elzmaProgressFunc, &pCtx);

			memset(descriptor->size, 0, SIZE_BUFFER);
			sprintf_s(descriptor->size, SIZE_BUFFER, "%i", ds.outLen);
			delete[] descriptor->data;
			descriptor->data = new char[ds.outLen];
			memcpy(descriptor->data, ds.outData, ds.outLen);

			elzma_compress_free(&handle);

			fstream.write(descriptor->size, SIZE_BUFFER);
			_UINT fsize = strtoul(descriptor->size, NULL, 0);

			std::streamoff startPos = fstream.tellp();
			while (totalWritten < fsize)
			{
				_UINT bytesToWrite = WRITE_STEP;
				if (totalWritten + bytesToWrite > fsize)
					bytesToWrite = fsize - totalWritten;

				fstream.seekp(startPos + totalWritten);
				fstream.write(descriptor->data, bytesToWrite);
				totalWritten += bytesToWrite;

				std::cout << "\33\r[Writing - ";
				std::cout << (_UINT)((float)totalWritten / (float)fsize * 100.0f) << "%] ";
				std::cout << descriptor->name << descriptor->ext << " - " << oldSize << " -> " << ds.outLen;
				
			}
			std::cout << std::endl;

			delete[] descriptor->data;
			delete descriptor;
			it = m_descriptors.erase(it);
		}
		fstream.close();

		std::cout << std::endl;
		std::cout << "Packaging complete." << std::endl << std::endl;
		std::cout << "Data size: " << totalSize << " bytes." << std::endl;
		std::cout << "Header size: " << headerSize << " bytes." << std::endl;
		std::cout << "Total size: " << totalSize + headerSize << " bytes." << std::endl;
	}
	else
	{
		std::cout << "ERROR: Could not open file for writing:" << std::endl;
		std::cout << "package.bin" << std::endl;
	}
}

///////////////////////////////////////////////////////////////////////////
// Reads packaged files.
///////////////////////////////////////////////////////////////////////////
void Binpressor::ReadFiles()
{
	if (m_packages.empty())
		return;

	std::cout << "Reading Package File(s)..." << std::endl;

	for (auto it = m_packages.begin(); it != m_packages.end();)
	{
		std::ifstream fstream;
		fstream.open(*it, std::fstream::out | std::fstream::binary);
		if (fstream.is_open())
		{
			// Read header data
			char majVersion[4];
			char minVersion[4];
			char nameBuffer[8];
			char extBuffer[8];
			char sizeBuffer[8];

			fstream.read(majVersion, 4);
			fstream.read(minVersion, 4);
			fstream.read(nameBuffer, 8);
			fstream.read(extBuffer, 8);
			fstream.read(sizeBuffer, 8);

			if (atoi(majVersion) != MAJOR_VERSION || atoi(minVersion) != MINOR_VERSION)
			{
				std::cout << "Incompatible package version." << std::endl;
				fstream.close();
				return;		
			}

			int nameBuff = atoi(nameBuffer);
			int extBuff = atoi(extBuffer);
			int sizeBuff = atoi(sizeBuffer);

#if !defined(_WIN64)
			if (sizeBuff < 0 || sizeBuff >= 2140000000) // Leave some space for descriptors.
			{
				std::cout << "Total file size too large to read." << std::endl;
				std::cout << "Please use 64-bit version instead." << std::endl;
				std::cout << std::endl;
				std::cout << "Unpackaging canceled." << std::endl;

				return;
			}
#endif

			while (!fstream.eof())
			{
				InDescriptor* descriptor = new InDescriptor();
				descriptor->name = new char[nameBuff];
				descriptor->ext = new char[extBuff];
				descriptor->size = new char[sizeBuff];
				memset(descriptor->name, 0, nameBuff);
				memset(descriptor->ext, 0, extBuff);
				memset(descriptor->size, 0, sizeBuff);

				fstream.read(descriptor->name, nameBuff);
				fstream.read(descriptor->ext, extBuff);
				fstream.read(descriptor->size, sizeBuff);

				if (strcmp(descriptor->size, "") == 0)
				{
					delete[] descriptor->name;
					delete[] descriptor->ext;
					delete[] descriptor->size;
					delete descriptor;
					break;
				}

				descriptor->data = new char[strtoul(descriptor->size, NULL, 0)];
				memset(descriptor->data, 0, strtoul(descriptor->size, NULL, 0));

				fstream.read(descriptor->data, strtoul(descriptor->size, NULL, 0));
			
				std::cout << "[Reading - 100%] " << descriptor->name << descriptor->ext << std::endl;

				dataStream ds;
				ds.inData = descriptor->data;
				ds.inLen = strtoul(descriptor->size, NULL, 0);
				ds.outData = NULL;
				ds.outLen = 0;

				elzma_decompress_handle handle = elzma_decompress_alloc();

				int rc = elzma_decompress_run(handle, inputCallback, (void *) &ds,
					outputCallback, (void *) &ds, ELZMA_lzma);

				if (rc != ELZMA_E_OK) {
					if (ds.outData != NULL) free(ds.outData);
					elzma_decompress_free(&handle);
					return;
				}

				std::cout << "[Uncompressed - 100%] " << descriptor->name << descriptor->ext << " - " << descriptor->size << " -> " << ds.outLen << std::endl;

				memset(descriptor->size, 0, SIZE_BUFFER);
				sprintf_s(descriptor->size, SIZE_BUFFER, "%i", ds.outLen);
				delete[] descriptor->data;
				descriptor->data = new char[ds.outLen];
				memcpy(descriptor->data, ds.outData, ds.outLen);

				elzma_decompress_free(&handle);

				m_inFiles.push_back(descriptor);
			}
			std::cout << std::endl;

			it = m_packages.erase(it);
			fstream.close();
		}
		else
		{
			std::cout << "ERROR: Could not open package for reading:" << std::endl;
			std::cout << "package.bin" << std::endl;
		}
	}

	std::cout << "Reading complete." << std::endl << std::endl;
}

///////////////////////////////////////////////////////////////////////////
// Saves unpackaged files.
///////////////////////////////////////////////////////////////////////////
void Binpressor::Unpackage()
{
	if (m_inFiles.empty())
		return;

	std::cout << "Saving Unpackaged File(s)..." << std::endl;

	auto it = m_inFiles.begin();
	while (it != m_inFiles.end())
	{
		InDescriptor* descriptor = *it;
		std::stringstream fileName;
		fileName << descriptor->name << descriptor->ext;

		std::ofstream fstream;
		
		char workingDir[256];
		_getcwd(workingDir, 256);
		char workingDirFull[256];
		sprintf_s(workingDirFull, 256, "%s/package/", workingDir);
		boost::filesystem::create_directory(boost::filesystem::path(workingDirFull));
		fstream.open("package/" + fileName.str(), std::fstream::out | std::fstream::binary);
		if (fstream.is_open())
		{
			fstream.write(descriptor->data, strtoul(descriptor->size, NULL, 0));

			std::cout << "[Saved - 100%] " << descriptor->name << descriptor->ext << std::endl;
			delete[] descriptor->name;
			delete[] descriptor->ext;
			delete[] descriptor->size;
			delete[] descriptor->data;
			delete descriptor;
			it = m_inFiles.erase(it);
			fstream.close();
		}
		else
		{
			std::cout << "ERROR: Could not open file for writing:" << std::endl;
			std::cout << fileName.str() << std::endl;
			it = m_inFiles.erase(it);
		}
	}

	std::cout << std::endl;
	std::cout << "Unpackaging complete." << std::endl << std::endl;
}

///////////////////////////////////////////////////////////////////////////
// Prints out the file paths to be packaged.
///////////////////////////////////////////////////////////////////////////
void Binpressor::PrintFilePaths()
{
	// Print file list
	std::cout << std::endl;
	std::cout << "Files not packaged:" << std::endl;
	std::cout << "--------" << std::endl;
	for (auto it = m_filePaths.begin(); it != m_filePaths.end(); ++it)
	{
		std::string filePath = *it;
		if (IsFileOrFolder(filePath.c_str()) == 1)
			std::cout << filePath << std::endl;
	}
	std::cout << std::endl;
}

///////////////////////////////////////////////////////////////////////////
// Prints out all descriptors in memory.
///////////////////////////////////////////////////////////////////////////
void Binpressor::PrintFileDescriptors()
{
	// Print file descriptors
	for (auto it = m_descriptors.begin(); it != m_descriptors.end(); ++it)
	{
		Descriptor* descriptor = *it;
		std::cout << "Name: " << descriptor->name << std::endl;
		std::cout << "Size: " << descriptor->size << " bytes" << std::endl;
		std::cout << "File Type: " << descriptor->ext << std::endl;
		std::cout << "Data: " << descriptor->data << std::endl;
		std::cout << "---------------------------------------------------";
		std::cout << std::endl;
	}
	std::cout << std::endl;
}