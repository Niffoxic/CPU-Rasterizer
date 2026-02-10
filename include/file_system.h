#ifndef DIRECTX12_FILE_SYSTEM_H
#define DIRECTX12_FILE_SYSTEM_H

#include <string>
#include <cstdint>
#include <memory>

class FileSystem
{
public:
	FileSystem();
	~FileSystem();

	FileSystem(const FileSystem&) = delete;
	FileSystem(FileSystem&&);

	FileSystem& operator=(const FileSystem&) = delete;
	FileSystem& operator=(FileSystem&&);

	[[nodiscard]] bool OpenForRead (_In_ const std::string& path);
	[[nodiscard]] bool OpenForWrite(_In_ const std::string& path);

	void Close();
	[[nodiscard]] bool ReadBytes(_Out_writes_bytes_all_(size) void*  dest,
							 _In_                         size_t size) const;
	[[nodiscard]] bool WriteBytes(_In_reads_bytes_(size) const void* data,
							 _In_                    size_t      size) const;

	[[nodiscard]] bool ReadUInt32    (_Out_      std::uint32_t& value) const;
	[[nodiscard]] bool WriteUInt32   (_In_       std::uint32_t value ) const;
	[[nodiscard]] bool ReadString    (_Out_      std::string& outStr ) const;
	[[nodiscard]] bool WriteString   (_In_ const std::string& str    ) const;
	[[nodiscard]] bool WritePlainText(_In_ const std::string& str    ) const;

	[[nodiscard]] bool          IsOpen     () const;
	[[nodiscard]] std::uint64_t GetFileSize() const;

private:
	class Impl;
	std::unique_ptr<Impl> m_impl;
};

#endif //DIRECTX12_FILE_SYSTEM_H