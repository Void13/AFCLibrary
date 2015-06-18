#ifndef _H__AFCLIBRARY
#define _H__AFCLIBRARY

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>

enum class EAFCErrors : size_t
{
	NONE = 0,
	OK = 0,
	FILE_OPENING_ERROR,
	NOT_INITIALIZED,
	IS_EMPTY,
	FILE_NOT_FOUND,
	ENTRIES_OVERFLOW,
	READ_ERROR,
	SEEK_ERROR,
	WRITE_ERROR,
	COMPRESS_FAILS,
	DECOMPRESS_FAILS
};

class CAFC
{
public:
	CAFC();
	CAFC(char const *const _pArchiveName);

	~CAFC();

	EAFCErrors CreateArchive(char const *const _pArchiveName);
	EAFCErrors OpenArchive(char const *const _pArchiveName);
	void CloseArchive();

	EAFCErrors WriteFile(char const *const _pFileName, void const *const _pData, size_t const _dwFileSize);

	EAFCErrors ReadFile(char const *const _pFileName, void *&pData, size_t &dwFileSize) const;

	EAFCErrors GetFilesByPartName(char const *const _pPartOfFileName, std::vector<std::string> &FullFileNames) const;

	EAFCErrors DeleteAFCFile(char const *const _pFileName);

	EAFCErrors Rebuild();
private:
	struct CFileHeader
	{
		size_t dwCompressedFileSize = 0;
		size_t dwOriginalFileSize = 0;
		size_t dwFilePos = 0;
		size_t dwFileHeaderPos = 0;
	};

	struct CInFileFileHeader
	{
		char sFileName[116];
		size_t dwCompressedFileSize = 0;
		size_t dwOriginalFileSize = 0;
		size_t dwFilePos = 0;
	};

	struct CArchiveHeader
	{
		size_t dwNumEntries = 0;
	};

	// filename as key, (pointer to file data, size of file data) as data
	typedef std::unordered_map<std::string, CFileHeader> FILES_BY_NAME;

	std::string			m_sArchiveName;
	FILES_BY_NAME		m_ReadedFiles;
	CArchiveHeader		m_ArchiveHeader;
	size_t				m_dwArchiveSize = 0;

	FILE				*m_pFile = nullptr;
	bool				m_bIsInitialized = false;

public:
	static std::string GetErrorString(EAFCErrors const &_Error);
};

#endif