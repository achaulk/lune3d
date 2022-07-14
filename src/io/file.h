#pragma once

#include "aio.h"
#include "blob.h"
#include "future.h"
#include "refptr.h"
#include "memory.h"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <stdio.h>

namespace lune {

class IoFile;
class IoDir;
class IoMount;

class File;
class Dir;

class VFS;

class OutputStream;
class InputStream;

typedef std::string_view Path;

class OutputStream
{
public:
	virtual ~OutputStream() = default;

	virtual void WriteAsync(IoBuffer *buffer) = 0;
	virtual void Write(const void *data, uint32_t size) = 0;
	virtual void Flush() = 0;

	void Write(const std::string &s) { Write(s.data(), (uint32_t)s.size()); }
	void Write(const std::string_view &s) { Write(s.data(), (uint32_t)s.size()); }

	template<size_t N, typename T>
	void Write(const T (&s)[N])
	{
		Write(s, sizeof(T) * N);
	}
};

class SyncOutputStream : public OutputStream
{
public:
	void WriteAsync(IoBuffer *buffer) override
	{
		BufLen bytes;
		void *ptr = buffer->GetValidArea(&bytes);
		Write(ptr, bytes);
	}
};

class StdioOutputStream : public SyncOutputStream
{
public:
	StdioOutputStream(FILE *f, bool owned);
	~StdioOutputStream();

	void Write(const void *data, uint32_t size) override;
	void Flush() override;

private:
	FILE *f_;
	bool owned_;
};

struct ShmRegion
{
	virtual ~ShmRegion() = default;

	uint64_t offset;
	void *ptr;
	size_t size;
};

extern size_t kSystemMappingSize;
extern size_t kSystemMappingSizeMask;

enum class OpenMode
{
	// Open an existing file only.
	// Fails if file does not exist
	OpenExisting,
	// Create a new file only
	// Fails if file exists or could not be created
	CreateIfNotExist,
	// Open existing file, create if it doesn't exist
	// Fails if file could not be created
	OpenOrCreate,
	// Open and truncate existing file, or create new file.
	// Fails if file does not exist and could not be created, or exists and could not be truncated
	CreateOrTruncate,
	// Open existing and truncate.
	// Fails if file does not exist or could not be truncated
	TruncateExisting,
};

// Low level file API, assumes caller is fine with using the asynchronous API
class IoFile : public Refcounted
{
public:
	virtual ~IoFile() = default;

	virtual void BeginRead(AsyncOp *op) = 0;
	virtual void BeginWrite(AsyncOp *op) = 0;

	virtual void Flush() = 0;

	virtual bool AllowWrites() const = 0;

	virtual uint64_t GetFileSize() const = 0;

	virtual bool Sync() { return false; }

	virtual void Truncate(uint64_t bytes = 0) = 0;

	// Size = 0 means map the entire file.
	virtual std::unique_ptr<ShmRegion> MapRegion(void *addr, uint64_t offset, uint64_t size, bool ro) = 0;
};
typedef RefPtr<IoFile> IoFilePtr;

class CIoFile : public IoFile
{
public:
	CIoFile(FILE *f);
};

class IoROFile : public IoFile
{
public:
	void BeginWrite(AsyncOp *op) override
	{
		op->CompleteErr(-1);
	}

	void Flush() override {}

	bool AllowWrites() const { return false; }

	void Truncate(uint64_t bytes) override {}
};

class IoROSubsetFile : public IoROFile
{
public:
	IoROSubsetFile(IoFile *f, uint64_t start, uint64_t size) : f_(f), start_(start), size_(size) {}

	uint64_t GetFileSize() const { return size_; }
	void BeginRead(AsyncOp *op)
	{
		op->offset += start_;
		uint64_t total = 0;
		for(int i = 0; i < op->nsg; i++) {
			if(total + op->sg[i].len > size_) {
				op->sg[i].len = (BufLen)(size_ - total);
			}
			total += op->sg[i].len;
		}
		f_->BeginRead(op);
	}

	std::unique_ptr<ShmRegion> MapRegion(void *addr, uint64_t offset, uint64_t size, bool ro) override
	{
		return f_->MapRegion(addr, offset + start_, std::min(size, size_), ro);
	}

private:
	IoFilePtr f_;
	uint64_t start_;
	uint64_t size_;
};

struct FileInfo
{
	const char *filename;
	uint64_t size;
	uint32_t flags;
};

class IoDir : public Refcounted
{
public:
	virtual ~IoDir() = default;

	std::vector<std::string> EnumerateFiles();

	virtual bool EnumerateFiles(const char *query, std::function<bool(const FileInfo &)>) = 0;

	virtual RefPtr<IoDir> OpenSubdir(const Path &path) = 0;
	virtual RefPtr<IoFile> OpenFile(const Path &path, uint32_t flags, OpenMode mode) = 0;
};
typedef RefPtr<IoDir> IoDirPtr;

class File
{
public:
	File() = default;
	explicit File(IoFile *f);
	explicit File(IoFilePtr f);
	~File();

	File(const File &) = delete;
	void operator=(const File &) = delete;

	File(File &&o) = default;
	File& operator=(File &&o) = default;

	operator bool() const
	{
		return !!file_;
	}

	IoFile *file() { return file_.get(); }

	void Flush() { file_->Flush(); }

	enum class SeekFrom
	{
		Current, Start, End,
	};
	void Seek(SeekFrom from, int64_t n)
	{
		uint64_t end = file_->GetFileSize();
		switch(from) {
		case SeekFrom::Current:
			position_ += n;
			break;
		case SeekFrom::Start:
			position_ = n;
			break;
		case SeekFrom::End:
			position_ = end + n;
			break;
		}
		if(position_ > end)
			position_ = end;
		else
			eof_ = false;
	}

	uint64_t Tell() const { return position_; }

	size_t Read(void *p, size_t n);
	size_t Write(const void *p, size_t n);

	size_t ReadAbs(void *p, size_t n, uint64_t offset);
	size_t WriteAbs(const void *p, size_t n, uint64_t offset);

	bool Append(const void *p, size_t n);

	bool Write(const std::string_view &buf) { return Write(buf.data(), buf.size()) == buf.size(); }

	uint64_t GetPosition() { return position_; }

	bool eof() const { return eof_; }

	std::unique_ptr<OutputStream> CreateOutputStream();

	// Starts an asynchronous read to a blob. The file object can be destroyed before this read completes.
	RefPtr<Blob> ReadToFutureBlob(uint64_t offset = 0, uint64_t size = 0);
	// Functionally equivalent to ReadToFutureBlob and then waiting for the read to finish
	RefPtr<Blob> ReadToImmediateBlob(uint64_t offset = 0, uint64_t size = 0);

	RefPtr<Blob> MapToBlob(uint64_t offset = 0, uint64_t size = 0, bool ro = true);

private:
	RefPtr<Blob> AllocForBlobRead(AsyncOp **op, uint64_t size);

	IoFilePtr file_;

	uint64_t position_ = 0;
	bool eof_ = false;
};
typedef std::unique_ptr<File> FilePtr;

class FileOutputStream : public OutputStream
{
public:
	FileOutputStream(IoFile *f);
	~FileOutputStream();

	void WriteAsync(IoBuffer *buffer) override;
	void Write(const void *data, uint32_t size) override;
	void Flush() override;

private:
	IoFilePtr file_;
};

namespace file_flags {
static constexpr uint32_t kReadOnly = 1;
static constexpr uint32_t kAppendOnly = 2;

static constexpr uint32_t kIsFile = 1U << 31;
static constexpr uint32_t kIsDir = 1U << 30;
} // namespace file_flags

struct StatBuf
{
	uint64_t size;
	uint32_t flags;
};

class VFSImpl
{
public:
	static VFSImpl *GetOsVfs();

	virtual ~VFSImpl() = default;
	virtual IoFilePtr OpenFile(const Path &path, uint32_t flags, OpenMode mode) = 0;
	virtual IoDirPtr OpenDir(const Path &path) = 0;

	virtual bool CreateDirectory(const Path &path) = 0;
	virtual bool Delete(const Path &path) = 0;

	virtual bool Stat(const Path &path, StatBuf *buf) = 0;
	virtual bool CheckAccess(const Path &path, uint32_t flags) = 0;

	virtual uint64_t GetFreeBytesForWriting(const Path &path) = 0;
};

class NullVFSImpl : public VFSImpl
{
	IoFilePtr OpenFile(const Path &path, uint32_t flags, OpenMode mode) override
	{
		return nullptr;
	}
	IoDirPtr OpenDir(const Path &path) override
	{
		return nullptr;
	}

	bool CreateDirectory(const Path &path) override
	{
		return false;
	}
	bool Delete(const Path &path) override
	{
		return false;
	}

	bool Stat(const Path &path, StatBuf *buf) override
	{
		return false;
	}
	bool CheckAccess(const Path &path, uint32_t flags) override
	{
		return false;
	}

	uint64_t GetFreeBytesForWriting(const Path &path) override
	{
		return 0;
	}
};

// This is a VFS that is intended to overlay onto a real directory
class SafeVFSImpl : public VFSImpl
{
public:
	SafeVFSImpl(VFSImpl *real, const Path &root_path);
	~SafeVFSImpl() override;

	IoFilePtr OpenFile(const Path &path, uint32_t flags, OpenMode mode) override;
	IoDirPtr OpenDir(const Path &path) override;

	bool CreateDirectory(const Path &path) override;
	bool Delete(const Path &path) override;

	bool Stat(const Path &path, StatBuf *buf) override;
	bool CheckAccess(const Path &path, uint32_t flags) override;

	uint64_t GetFreeBytesForWriting(const Path &path) override;

private:
	bool CheckPath(const Path &p) const;

	VFSImpl *real_;
	std::string root_path_;
};

// An overlay VFS allows multiple possible VFSes to serve a file
class VFSOverlay : public VFSImpl
{
public:
	IoFilePtr OpenFile(const Path &path, uint32_t flags, OpenMode mode) override;
	IoDirPtr OpenDir(const Path &path) override;

	bool CreateDirectory(const Path &path) override;
	bool Delete(const Path &path) override;

	bool Stat(const Path &path, StatBuf *buf) override;
	bool CheckAccess(const Path &path, uint32_t flags) override;

	uint64_t GetFreeBytesForWriting(const Path &path) override;

	void Add(VFSImpl *impl, std::string root);

private:
	struct Entry
	{
		VFSImpl *impl;
		std::string root;
	};
	std::vector<Entry> entries_;
};

class VFS
{
public:
	explicit VFS(VFSImpl *impl);

	File OpenFile(const Path &path, uint32_t flags, OpenMode mode = OpenMode::OpenExisting);

	bool CreateDirectory(const Path &path);

	bool Delete(const Path &path);

	bool Stat(const Path &path, StatBuf *buf);
	bool CheckAccess(const Path &path, uint32_t flags);

private:
	VFSImpl *impl_;
};

// This defines multiple roots: /save, /temp, /data
// /game consists of either a synonym for /data or baked data
// /data points to whatever the data implementation is
// /save is wherever save files are written out to
// /temp is a writable temporary directory, files do not necessarily persist when writen here
class SafeVFSSplit : public VFSImpl
{
public:
	struct Options
	{
		std::string use_writable_app_dir_if_possible;
		std::string data_dir;
		std::string app_name;
		bool add_lune_subdir = true;
	};
	static bool PreInitialize(const Options& options);
	static bool Initialize(const Options &options);

	SafeVFSSplit(VFSImpl *real, const Path& temp_path, const Path& data_path);
	~SafeVFSSplit() override;

	// This is a writable, persistent location suitable for storing things like save files
	VFSImpl *SaveDir() { return save_vfs_.get(); }
	// This is a writable, nonpersistent, temporary location
	VFSImpl *TempDir() { return &temp_vfs_; }
	// This is a readonly, persistent location containing game data
	VFSImpl *DataDir() { return data_vfs_.get(); }
	// This is a readonly, persistent location containing possible baked game data
	VFSImpl *GameDir() { return game_vfs_.get(); }

	// Dont ever set these to NULL, use null_vfs instead
	void SetData(std::shared_ptr<VFSImpl> vfs)
	{
		data_vfs_ = vfs;
	}
	void SetGame(std::shared_ptr<VFSImpl> vfs)
	{
		game_vfs_ = vfs;
	}

	void SetSave(std::shared_ptr<VFSImpl> vfs)
	{
		save_vfs_ = vfs;
	}

	IoFilePtr OpenFile(const Path &path, uint32_t flags, OpenMode mode) override;
	IoDirPtr OpenDir(const Path &path) override;

	bool CreateDirectory(const Path &path) override;
	bool Delete(const Path &path) override;

	bool Stat(const Path &path, StatBuf *buf) override;
	bool CheckAccess(const Path &path, uint32_t flags) override;

	uint64_t GetFreeBytesForWriting(const Path &path) override;

	void Add(const char *prefix, std::shared_ptr<VFSImpl> vfs);

private:
	VFSImpl *Lookup(Path &p);

	std::shared_ptr<VFSImpl> data_vfs_;
	std::shared_ptr<VFSImpl> game_vfs_;
	std::shared_ptr<VFSImpl> save_vfs_;
	SafeVFSImpl temp_vfs_;

	struct Entry
	{
		char prefix[16];
		size_t len;
		std::shared_ptr<VFSImpl> vfs;
	};
	std::vector<Entry> custom_;
};

// This access system disk from the current path
extern VFS sys_vfs;

// This accesses controlled roots
extern VFS safe_vfs;

// This allows access to change the roots of safe_vfs
extern SafeVFSSplit *safe_vfs_impl;

extern std::shared_ptr<VFSImpl> null_vfs;

} // namespace lune
