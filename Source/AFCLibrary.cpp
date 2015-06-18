#include "AFCLibrary.h"

#include "lz4.h"
#include "lz4hc.h"

#define MAX_RESERVED_FILES 100000

size_t GetPaddedUpSize(size_t const _dwSize)
{
	return ((_dwSize - 1) / 2048 + 1) * 2048;
}

CAFC::CAFC()
{
};

CAFC::CAFC(char const *const _pArchiveName) :
	CAFC()
{
	OpenArchive(_pArchiveName);
};

CAFC::~CAFC()
{
};

EAFCErrors CAFC::CreateArchive(char const *const _pArchiveName)
{
	// create the file

	fopen_s(&m_pFile, _pArchiveName, "wb+");

	if (!m_pFile)
		return EAFCErrors::FILE_OPENING_ERROR;

	m_bIsInitialized = true;

	m_sArchiveName = _pArchiveName;

	if (fwrite(&m_ArchiveHeader, sizeof(CArchiveHeader), 1, m_pFile) != 1)
		return EAFCErrors::WRITE_ERROR;

	size_t dwEmptySize = MAX_RESERVED_FILES * sizeof(CInFileFileHeader);

	char *pData = new char[dwEmptySize];
	memset(pData, 0, dwEmptySize);

	if (fwrite(pData, dwEmptySize, 1, m_pFile) != 1)
		return EAFCErrors::WRITE_ERROR;

	delete[] pData;

	m_dwArchiveSize = dwEmptySize;

	return EAFCErrors::OK;
}

EAFCErrors CAFC::OpenArchive(char const *const _pArchiveName)
{
	// try to open in reading-writing mode
	fopen_s(&m_pFile, _pArchiveName, "rb+");

	// if file doesn't exist, try to create it
	if (!m_pFile)
	{
		if (CreateArchive(_pArchiveName) != EAFCErrors::OK)
			return EAFCErrors::FILE_OPENING_ERROR;
	}

	// read the file size
	if (fseek(m_pFile, 0, SEEK_END))
		return EAFCErrors::SEEK_ERROR;
	m_dwArchiveSize = ftell(m_pFile);
	if (fseek(m_pFile, 0, SEEK_SET))
		return EAFCErrors::SEEK_ERROR;

	m_bIsInitialized = true;

	m_ArchiveHeader.dwNumEntries = 0;

	m_sArchiveName = _pArchiveName;

	// there is no data
	if (m_dwArchiveSize < 4)
	{
		return EAFCErrors::OK;
	}

	if (fread_s(&m_ArchiveHeader, sizeof(CArchiveHeader), sizeof(CArchiveHeader), 1, m_pFile) != 1)
		return EAFCErrors::READ_ERROR;

	// for each entry header(reserved MAX_RESERVED_FILES items)
	for (size_t iEntry = 0; iEntry < m_ArchiveHeader.dwNumEntries; iEntry++)
	{
		CFileHeader FileHeader;
		FileHeader.dwFileHeaderPos = ftell(m_pFile);

		CInFileFileHeader InFileFileHeader;
		if (fread_s(&InFileFileHeader, sizeof(CInFileFileHeader), sizeof(CInFileFileHeader), 1, m_pFile) != 1)
			return EAFCErrors::READ_ERROR;

		FileHeader.dwFilePos = InFileFileHeader.dwFilePos;
		FileHeader.dwCompressedFileSize = InFileFileHeader.dwCompressedFileSize;
		FileHeader.dwOriginalFileSize = InFileFileHeader.dwOriginalFileSize;


		m_ReadedFiles.emplace(InFileFileHeader.sFileName, FileHeader);
	}

	return EAFCErrors::OK;
}

EAFCErrors CAFC::WriteFile(char const *const _pFileName, void const *const _pData, size_t const _dwFileSize)
{
	if (!m_bIsInitialized)
		return EAFCErrors::NOT_INITIALIZED;

	if (m_ArchiveHeader.dwNumEntries > MAX_RESERVED_FILES)
		return EAFCErrors::ENTRIES_OVERFLOW;

	if (_dwFileSize == 0 || !_pData || !_pFileName)
		return EAFCErrors::WRITE_ERROR;

	// LZ4_COMPRESSBOUND or LZ4_compressBound
	// first: compress the data
	char *pCompressedData = new char[LZ4_compressBound(_dwFileSize)];
	size_t dwOutputSize = LZ4_compressHC((char *)_pData, pCompressedData, _dwFileSize);

	if (dwOutputSize <= 0)
		return EAFCErrors::COMPRESS_FAILS;

	CInFileFileHeader InFileFileHeader;
	sprintf_s(InFileFileHeader.sFileName, sizeof(InFileFileHeader.sFileName), _pFileName);
	InFileFileHeader.dwOriginalFileSize = _dwFileSize;
	InFileFileHeader.dwCompressedFileSize = dwOutputSize;

	// if the file is exist
	auto iFileHeader = m_ReadedFiles.find(_pFileName);

	if (iFileHeader != m_ReadedFiles.end())
	{
		// if newsize <= oldsize, then write to old pos
		if (dwOutputSize <= iFileHeader->second.dwCompressedFileSize)
		{
			InFileFileHeader.dwFilePos = iFileHeader->second.dwFilePos;
		}
		// if newsize > oldsize
		else
		{
			InFileFileHeader.dwFilePos = m_dwArchiveSize;
			iFileHeader->second.dwFilePos = InFileFileHeader.dwFilePos;
		}
	}
	// if new file
	else
	{
		CFileHeader NewFileHeader;
		InFileFileHeader.dwFilePos = m_dwArchiveSize;
		NewFileHeader.dwFilePos = InFileFileHeader.dwFilePos;
		NewFileHeader.dwFileHeaderPos = sizeof(CArchiveHeader) + m_ArchiveHeader.dwNumEntries * sizeof(CInFileFileHeader);

		m_ReadedFiles.emplace(_pFileName, NewFileHeader);
		iFileHeader = m_ReadedFiles.find(_pFileName);

		m_ArchiveHeader.dwNumEntries++;
	}

	iFileHeader->second.dwCompressedFileSize = InFileFileHeader.dwCompressedFileSize;
	iFileHeader->second.dwOriginalFileSize = InFileFileHeader.dwOriginalFileSize;

	// first: set data of the header
	if (fseek(m_pFile, iFileHeader->second.dwFileHeaderPos, SEEK_SET))
		return EAFCErrors::SEEK_ERROR;
	if (fwrite(&InFileFileHeader, sizeof(CInFileFileHeader), 1, m_pFile) != 1)
		return EAFCErrors::WRITE_ERROR;

	// second: set data of the file
	if (fseek(m_pFile, iFileHeader->second.dwFilePos, SEEK_SET))
		return EAFCErrors::SEEK_ERROR;
	if (fwrite(pCompressedData, dwOutputSize, 1, m_pFile) != 1)
		return EAFCErrors::WRITE_ERROR;

	// fill the empty bytes(padding)
	size_t dwPaddedUpSizeSubFileSize = GetPaddedUpSize(dwOutputSize) - dwOutputSize;
	if (dwPaddedUpSizeSubFileSize > 0)
	{
		char *pStr = new char[dwPaddedUpSizeSubFileSize];
		memset(pStr, '\0', dwPaddedUpSizeSubFileSize);
		if (fwrite(pStr, dwPaddedUpSizeSubFileSize, 1, m_pFile) != 1)
			return EAFCErrors::WRITE_ERROR;
		delete[] pStr;
	}

	if (fseek(m_pFile, 0, SEEK_END))
		return EAFCErrors::SEEK_ERROR;
	m_dwArchiveSize = ftell(m_pFile);

	if (fseek(m_pFile, 0, SEEK_SET))
		return EAFCErrors::SEEK_ERROR;

	if (fwrite(&m_ArchiveHeader, sizeof(CArchiveHeader), 1, m_pFile) != 1)
		return EAFCErrors::WRITE_ERROR;

	delete[] pCompressedData;

	return EAFCErrors::OK;
}

EAFCErrors CAFC::ReadFile(char const *const _pFileName, void *&pData, size_t &dwFileSize) const
{
	if (!m_bIsInitialized)
		return EAFCErrors::NOT_INITIALIZED;

	if (!_pFileName)
		return EAFCErrors::READ_ERROR;

	auto iFile = m_ReadedFiles.find(_pFileName);

	if (iFile == m_ReadedFiles.end())
		return EAFCErrors::FILE_NOT_FOUND;

	dwFileSize = iFile->second.dwOriginalFileSize;

	pData = new char[dwFileSize];

	if (fseek(m_pFile, iFile->second.dwFilePos, SEEK_SET))
		return EAFCErrors::SEEK_ERROR;

	char *pCompressedData = new char[iFile->second.dwCompressedFileSize];

	if (fread_s(pCompressedData, iFile->second.dwCompressedFileSize, iFile->second.dwCompressedFileSize, 1, m_pFile) != 1)
		return EAFCErrors::READ_ERROR;

	size_t dwCompressedSize = LZ4_decompress_fast(pCompressedData, (char *)pData, dwFileSize);

	if (dwCompressedSize != iFile->second.dwCompressedFileSize)
		return EAFCErrors::DECOMPRESS_FAILS;

	delete[] pCompressedData;

	return EAFCErrors::OK;
}

EAFCErrors CAFC::GetFilesByPartName(char const *const _pPartOfFileName, std::vector<std::string> &FullFileNames) const
{
	if (!m_bIsInitialized)
		return EAFCErrors::NOT_INITIALIZED;

	FullFileNames.clear();

	for (auto const &iFileHeader : m_ReadedFiles)
	{
		if (iFileHeader.first.find(_pPartOfFileName) != std::string::npos)
		{
			FullFileNames.push_back(iFileHeader.first);
		}
	}

	return EAFCErrors::OK;
}

EAFCErrors CAFC::DeleteAFCFile(char const *const _pFileName)
{
	if (!m_bIsInitialized)
		return EAFCErrors::NOT_INITIALIZED;

	if (m_ArchiveHeader.dwNumEntries == 0)
		return EAFCErrors::IS_EMPTY;

	size_t dwDeletedHeaderFilePos;

	{
		auto iFileHeader = m_ReadedFiles.find(_pFileName);

		if (iFileHeader == m_ReadedFiles.end())
			return EAFCErrors::FILE_NOT_FOUND;

		m_ArchiveHeader.dwNumEntries--;

		dwDeletedHeaderFilePos = iFileHeader->second.dwFileHeaderPos;
		m_ReadedFiles.erase(iFileHeader);
	}

	if (m_ArchiveHeader.dwNumEntries > 0)
	{
		auto iEndFileHeader = m_ReadedFiles.rbegin();

		// fill the deleted header by last file in unordered_map
		CInFileFileHeader InFileFileHeader;
		sprintf_s(InFileFileHeader.sFileName, sizeof(InFileFileHeader.sFileName), iEndFileHeader->first.c_str());
		InFileFileHeader.dwFilePos = iEndFileHeader->second.dwFilePos;
		InFileFileHeader.dwCompressedFileSize = iEndFileHeader->second.dwCompressedFileSize;
		InFileFileHeader.dwOriginalFileSize = iEndFileHeader->second.dwOriginalFileSize;

		// fseek to header to update by back() file header
		if (fseek(m_pFile, dwDeletedHeaderFilePos, SEEK_SET))
			return EAFCErrors::SEEK_ERROR;
		if (fwrite(&InFileFileHeader, sizeof(CInFileFileHeader), 1, m_pFile) != 1)
			return EAFCErrors::WRITE_ERROR;
	}

	if (fseek(m_pFile, 0, SEEK_SET))
		return EAFCErrors::SEEK_ERROR;
	if (fwrite(&m_ArchiveHeader, sizeof(CArchiveHeader), 1, m_pFile) != 1)
		return EAFCErrors::WRITE_ERROR;

	return EAFCErrors::OK;
}

#include <Windows.h>

EAFCErrors CAFC::Rebuild()
{
	if (!m_bIsInitialized)
		return EAFCErrors::NOT_INITIALIZED;

	std::wstring swStr(m_sArchiveName.begin(), m_sArchiveName.end());

	if (!CopyFile(swStr.c_str(), (std::wstring(L"tmp_") + swStr).c_str(), false))
		return EAFCErrors::FILE_NOT_FOUND;

	FILE *pCopiedFile;
	fopen_s(&pCopiedFile, (std::string("tmp_") + m_sArchiveName).c_str(), "rb");

	if (fseek(m_pFile, sizeof(CArchiveHeader) + MAX_RESERVED_FILES * sizeof(CInFileFileHeader), SEEK_SET))
		return EAFCErrors::SEEK_ERROR;

	DWORD dwLastPos = 0;

	for (auto &iFileHeader : m_ReadedFiles)
	{
		void *pData = new char[iFileHeader.second.dwCompressedFileSize];

		// read data from copied file
		if (fread_s(pData, iFileHeader.second.dwCompressedFileSize, iFileHeader.second.dwCompressedFileSize, 1, pCopiedFile) != 1)
			return EAFCErrors::READ_ERROR;

		// write data to original file
		if (fwrite(pData, iFileHeader.second.dwCompressedFileSize, 1, m_pFile) != 1)
			return EAFCErrors::WRITE_ERROR;

		delete[] pData;

		DWORD dwPaddedUpSizeSubFileSize = GetPaddedUpSize(iFileHeader.second.dwCompressedFileSize) - iFileHeader.second.dwCompressedFileSize;
		if (dwPaddedUpSizeSubFileSize > 0)
		{
			char *pStr = new char[dwPaddedUpSizeSubFileSize];
			memset(pStr, '\0', dwPaddedUpSizeSubFileSize);

			if (fwrite(pStr, dwPaddedUpSizeSubFileSize, 1, m_pFile) != 1)
				return EAFCErrors::WRITE_ERROR;

			delete[] pStr;
		}


		// update header info
		if (fseek(m_pFile, iFileHeader.second.dwFileHeaderPos, SEEK_SET))
			return EAFCErrors::SEEK_ERROR;

		CInFileFileHeader InFileFileHeader;
		sprintf_s(InFileFileHeader.sFileName, sizeof(InFileFileHeader.sFileName), iFileHeader.first.c_str());
		InFileFileHeader.dwFilePos = sizeof(CArchiveHeader) + MAX_RESERVED_FILES * sizeof(CInFileFileHeader) + dwLastPos;
		InFileFileHeader.dwCompressedFileSize = iFileHeader.second.dwCompressedFileSize;
		InFileFileHeader.dwOriginalFileSize = iFileHeader.second.dwOriginalFileSize;

		if (fwrite(&InFileFileHeader, sizeof(CInFileFileHeader), 1, m_pFile) != 1)
			return EAFCErrors::WRITE_ERROR;

		dwLastPos += dwPaddedUpSizeSubFileSize + iFileHeader.second.dwCompressedFileSize;

		// goto pos exact after last file
		if (fseek(m_pFile, InFileFileHeader.dwFilePos + dwLastPos, SEEK_SET))
			return EAFCErrors::SEEK_ERROR;
	}

	fclose(pCopiedFile);

	::DeleteFile((std::wstring(L"tmp_") + swStr).c_str());

	return EAFCErrors::OK;
}

void CAFC::CloseArchive()
{
	fclose(m_pFile);

	m_bIsInitialized = false;
	m_pFile = nullptr;
	m_dwArchiveSize = 0;
	m_ReadedFiles.clear();
	m_sArchiveName.clear();
	m_ArchiveHeader.dwNumEntries = 0;
}
std::string CAFC::GetErrorString(EAFCErrors const &_Error)
{
	if (_Error == EAFCErrors::ENTRIES_OVERFLOW)
		return "Entries overflow";
	else if (_Error == EAFCErrors::FILE_NOT_FOUND)
		return "File not found";
	else if (_Error == EAFCErrors::FILE_OPENING_ERROR)
		return "File opening error";
	else if (_Error == EAFCErrors::IS_EMPTY)
		return "Archive is empty";
	else if (_Error == EAFCErrors::NOT_INITIALIZED)
		return "Archive not initialized";
	else if (_Error == EAFCErrors::READ_ERROR)
		return "Read error";
	else if (_Error == EAFCErrors::SEEK_ERROR)
		return "Seek error";
	else if (_Error == EAFCErrors::WRITE_ERROR)
		return "Write error";
	else if (_Error == EAFCErrors::COMPRESS_FAILS)
		return "Compress error";
	else if (_Error == EAFCErrors::DECOMPRESS_FAILS)
		return "Decompress error";

	return "";
}