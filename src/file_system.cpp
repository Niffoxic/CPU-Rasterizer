#include "file_system.h"
#include <iostream>
#include "helpers.h"

#pragma region Impl_Declaration
class FileSystem::Impl
{
public:
	 Impl() = default;
	~Impl() = default;

	[[nodiscard]] bool OpenForRead (_In_ const std::string& path);
	[[nodiscard]] bool OpenForWrite(_In_ const std::string& path);

	void Close();
	[[nodiscard]] bool ReadBytes(_Out_writes_bytes_all_(size) void*  dest,
							_In_                          size_t size) const;
	[[nodiscard]] bool WriteBytes(_In_reads_bytes_(size) const void* data,
							_In_                     size_t      size) const;

	[[nodiscard]] bool ReadUInt32	 (_Out_      std::uint32_t& value ) const;
	[[nodiscard]] bool WriteUInt32	 (_In_       std::uint32_t  value ) const;
	[[nodiscard]] bool ReadString	 (_Out_      std::string&   outStr) const;
	[[nodiscard]] bool WriteString	 (_In_ const std::string&   str   ) const;
	[[nodiscard]] bool WritePlainText(_In_ const std::string&   str   ) const;

	[[nodiscard]] bool          IsOpen	   () const;
	[[nodiscard]] std::uint64_t GetFileSize() const;

private:
	HANDLE m_fileHandle{ INVALID_HANDLE_VALUE };
	bool   m_bReadMode{ false };
};

#pragma endregion

#pragma region FileSystem_Implementation

FileSystem::FileSystem()
	: m_impl(std::make_unique<FileSystem::Impl>())
{}

FileSystem::~FileSystem() = default;

FileSystem::FileSystem(FileSystem&&) = default;
FileSystem& FileSystem::operator=(FileSystem&&) = default;

_Use_decl_annotations_
bool FileSystem::OpenForRead(const std::string& path)
{
	return m_impl->OpenForRead(path);
}

_Use_decl_annotations_
bool FileSystem::OpenForWrite(const std::string& path)
{
	return m_impl->OpenForWrite(path);
}

void FileSystem::Close()
{
	if (!m_impl) return;
	m_impl->Close();
}

_Use_decl_annotations_
bool FileSystem::ReadBytes(void* dest, size_t size) const
{
	return m_impl->ReadBytes(dest, size);
}

_Use_decl_annotations_
bool FileSystem::WriteBytes(const void* data, size_t size) const
{
	return m_impl->WriteBytes(data, size);
}

_Use_decl_annotations_
bool FileSystem::ReadUInt32(uint32_t& value) const
{
	return m_impl->ReadUInt32(value);
}

_Use_decl_annotations_
bool FileSystem::WriteUInt32(uint32_t value) const
{
	return m_impl->WriteUInt32(value);
}

_Use_decl_annotations_
bool FileSystem::ReadString(std::string& outStr) const
{
	return m_impl->ReadString(outStr);
}

_Use_decl_annotations_
bool FileSystem::WriteString(const std::string& str) const
{
	return m_impl->WriteString(str);
}

_Use_decl_annotations_
bool FileSystem::WritePlainText(const std::string& str) const
{
	return m_impl->WritePlainText(str);
}

_Use_decl_annotations_
uint64_t FileSystem::GetFileSize() const
{
	return m_impl->GetFileSize();
}

_Use_decl_annotations_
bool FileSystem::IsOpen() const
{
	return m_impl->IsOpen();
}

#pragma endregion

#pragma region Impl_Implementation
_Use_decl_annotations_
bool FileSystem::Impl::OpenForRead(const std::string & path)
{
	std::wstring w_path = std::wstring(path.begin(), path.end());
	m_fileHandle = CreateFile(
		w_path.c_str(),
		GENERIC_READ,
		FILE_SHARE_READ,
		nullptr,
		OPEN_EXISTING,
		FILE_ATTRIBUTE_NORMAL,
		nullptr
	);

	m_bReadMode = true;
	return m_fileHandle != INVALID_HANDLE_VALUE;
}

_Use_decl_annotations_
bool FileSystem::Impl::OpenForWrite(const std::string& path)
{
	auto file = helpers::SplitPathFile(path);
	if (!helpers::CreateDirectories(file.DirectoryNames))
	{
		std::cout << "Failed to create dir...\n";
	}

	std::wstring w_path = std::wstring(path.begin(), path.end());
	m_fileHandle = CreateFile(
		w_path.c_str(),
		GENERIC_WRITE,
		0,
		nullptr,
		CREATE_ALWAYS,
		FILE_ATTRIBUTE_NORMAL,
		nullptr
	);

	m_bReadMode = false;
	return m_fileHandle != INVALID_HANDLE_VALUE;
}

void FileSystem::Impl::Close()
{
	if (m_fileHandle != INVALID_HANDLE_VALUE)
	{
		CloseHandle(m_fileHandle);
		m_fileHandle = INVALID_HANDLE_VALUE;
		m_bReadMode = false;
	}
}

_Use_decl_annotations_
bool FileSystem::Impl::ReadBytes(void* dest, size_t size) const
{
	if (!m_bReadMode || m_fileHandle == INVALID_HANDLE_VALUE) return false;

	DWORD bytesRead = 0;
	return ReadFile(m_fileHandle, dest, static_cast<DWORD>(size), &bytesRead, nullptr) && bytesRead == size;
}

_Use_decl_annotations_
bool FileSystem::Impl::WriteBytes(const void* data, size_t size) const
{
	if (m_bReadMode || m_fileHandle == INVALID_HANDLE_VALUE) return false;

	DWORD bytesWritten = 0;
	return WriteFile(m_fileHandle, data, static_cast<DWORD>(size), &bytesWritten, nullptr) && bytesWritten == size;
}

_Use_decl_annotations_
bool FileSystem::Impl::ReadUInt32(uint32_t& value) const
{
	return ReadBytes(&value, sizeof(uint32_t));
}

_Use_decl_annotations_
bool FileSystem::Impl::WriteUInt32(uint32_t value) const
{
	return WriteBytes(&value, sizeof(uint32_t));
}

_Use_decl_annotations_
bool FileSystem::Impl::ReadString(std::string& outStr) const
{
	uint32_t len;
	if (!ReadUInt32(len)) return false;

	std::string buffer(len, '\0');
	if (!ReadBytes(buffer.data(), len)) return false;

	outStr = std::move(buffer);

	return true;
}

_Use_decl_annotations_
bool FileSystem::Impl::WriteString(const std::string& str) const
{
	uint32_t len = static_cast<uint32_t>(str.size());
	return WriteUInt32(len) && WriteBytes(str.data(), len);
}

_Use_decl_annotations_
bool FileSystem::Impl::WritePlainText(const std::string& str) const
{
	if (m_bReadMode || m_fileHandle == INVALID_HANDLE_VALUE) return false;

	DWORD bytesWritten = 0;
	std::string line = str + "\n";
	return WriteFile(m_fileHandle, line.c_str(), static_cast<DWORD>(line.size()), &bytesWritten, nullptr);
}

_Use_decl_annotations_
uint64_t FileSystem::Impl::GetFileSize() const
{
	if (m_fileHandle == INVALID_HANDLE_VALUE) return 0;

	LARGE_INTEGER size{};
	if (!::GetFileSizeEx(m_fileHandle, &size)) return 0;

	return static_cast<uint64_t>(size.QuadPart);
}

_Use_decl_annotations_
bool FileSystem::Impl::IsOpen() const
{
	return m_fileHandle != INVALID_HANDLE_VALUE;
}

#pragma endregion