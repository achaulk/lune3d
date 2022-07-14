#include "file.h"

#include <Windows.h>
#include <Shlobj.h>

#undef CreateDirectory

namespace lune {
extern HANDLE g_ioIOCP;

static_assert(sizeof(AsyncOp::overlapped) == sizeof(OVERLAPPED), "Incorrect overlapped size");

namespace {
size_t GetSysMapSize()
{
	SYSTEM_INFO si;
	GetSystemInfo(&si);
	return si.dwAllocationGranularity;
}
} // namespace
size_t kSystemMappingSizeMask = ~(GetSysMapSize() - 1);
size_t kSystemMappingSize = GetSysMapSize();

namespace {
DWORD OpenModeToCreationDisposition(OpenMode mode)
{
	switch(mode) {
	case OpenMode::OpenExisting:
		return OPEN_EXISTING;
	case OpenMode::CreateIfNotExist:
		return CREATE_NEW;
	case OpenMode::CreateOrTruncate:
		return CREATE_ALWAYS;
	case OpenMode::OpenOrCreate:
		return OPEN_ALWAYS;
	case OpenMode::TruncateExisting:
		return TRUNCATE_EXISTING;
	}
	return OPEN_EXISTING;
}

class Win32SHM : public ShmRegion
{
public:
	explicit Win32SHM(void *p, size_t s, uint64_t o) : p(p)
	{
		ptr = p;
		size = s;
		offset = o;
	}
	~Win32SHM()
	{
		if(p)
			UnmapViewOfFile(p);
	}

private:
	void *p;
};

class Win32File : public IoFile
{
public:
	Win32File(HANDLE h, bool writable) : h_(h), writable_(writable)
	{
		CreateIoCompletionPort(h, g_ioIOCP, (ULONG_PTR)this, 0);
		SetFileCompletionNotificationModes(h, FILE_SKIP_SET_EVENT_ON_HANDLE | FILE_SKIP_COMPLETION_PORT_ON_SUCCESS);
	}
	~Win32File()
	{
		CloseHandle(h_);
	}

	void BeginRead(AsyncOp *op) override
	{
		AddRef();
		op->op = &AsyncOp::CompleteOp;
		op->unref = this;
		assert(op->nsg == 1);

		OVERLAPPED *ov = (OVERLAPPED *)&op->overlapped[0];
		ov->Offset = (uint32_t)op->offset;
		ov->OffsetHigh = (uint32_t)(op->offset >> 32);
		ov->Internal = 0;
		ov->InternalHigh = 0;
		BOOL ret = ReadFile(h_, op->sg[0].buf, op->sg[0].len, NULL, ov);
		DWORD err;
		if(ret) {
			assert(false);
		} else if((err = GetLastError()) != ERROR_IO_PENDING) {
			op->CompleteErr((int32_t)err);
		}
	}
	void BeginWrite(AsyncOp *op) override
	{
		AddRef();
		op->op = &AsyncOp::CompleteOp;
		op->unref = this;
		assert(op->nsg == 1);

		OVERLAPPED *ov = (OVERLAPPED *)&op->overlapped[0];
		ov->Pointer = (void *)op->offset;
		BOOL ret = WriteFile(h_, op->sg[0].buf, op->sg[0].len, NULL, ov);
		DWORD err;
		if(ret) {
			assert(false);
		} else if((err = GetLastError()) != ERROR_IO_PENDING) {
			op->CompleteErr((int32_t)err);
		}
	}

	void Flush() override { FlushFileBuffers(h_); }

	bool AllowWrites() const override { return writable_; }

	uint64_t GetFileSize() const override
	{
		LARGE_INTEGER sz;
		auto ret = ::GetFileSizeEx(h_, &sz);
		auto err = GetLastError();
		return sz.QuadPart;
	}

	std::unique_ptr<ShmRegion> MapRegion(void *addr, uint64_t offset, uint64_t size, bool ro) override
	{
		if(!size) {
			size = GetFileSize();
			if(!size) {
				// This will always fail but it's not actually an error to map an empty file
				return std::make_unique<Win32SHM>(nullptr, 0, 0);
			}
		}
		bool wr = (writable_ & !ro);
		DWORD prot = wr ? PAGE_READWRITE : PAGE_READONLY;
		DWORD access = wr ? FILE_MAP_WRITE : FILE_MAP_READ;
		LARGE_INTEGER s;
		s.QuadPart = size;
		LARGE_INTEGER o;
		o.QuadPart = offset;
		HANDLE map = CreateFileMapping(h_, NULL, prot, 0, 0, NULL);
		if(!map) {
			return nullptr;
		}
		void *p = MapViewOfFileEx(map, access, o.HighPart, o.LowPart, size, addr);
		CloseHandle(map);
		if(!p)
			return nullptr;
		return std::make_unique<Win32SHM>(p, size, offset);
	}

	void Truncate(uint64_t bytes) override
	{
		LARGE_INTEGER li;
		li.QuadPart = bytes;
		SetFilePointerEx(h_, li, nullptr, FILE_BEGIN);
		SetEndOfFile(h_);
	}

private:
	HANDLE h_;
	bool writable_;
};

class Win32Dir : public IoDir
{
public:
	Win32Dir(const Path &path) : path_(path) {}

	bool EnumerateFiles(const char *query, std::function<bool(const FileInfo &)> fn) override
	{
		std::string q = path_;
		if(query && *query) {
			q.push_back('/');
			q += query;
		}
		WIN32_FIND_DATAA fd;
		HANDLE h = FindFirstFileA(q.c_str(), &fd);
		if(h == INVALID_HANDLE_VALUE)
			return false;
		FileInfo fi;
		do {
			if(fd.cFileName[0] == '.' && (!fd.cFileName[1] || (fd.cFileName[1] == '.' && !fd.cFileName[2])))
				continue;
			fi.size = fd.nFileSizeLow + ((uint64_t)fd.nFileSizeHigh << 32);
			fi.filename = fd.cFileName;
			fi.flags = 0;
			if(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
				fi.flags |= file_flags::kIsDir;
			else
				fi.flags |= file_flags::kIsFile;
			if(fd.dwFileAttributes & FILE_ATTRIBUTE_READONLY)
				fi.flags |= file_flags::kReadOnly;
			if(!fn(fi))
				break;
		} while(FindNextFileA(h, &fd));
		FindClose(h);
		return true;
	}

	RefPtr<IoDir> OpenSubdir(const Path &path) override;
	RefPtr<IoFile> OpenFile(const Path &path, uint32_t flags, OpenMode mode) override;

	std::string path_;
};

class Win32VFS : public VFSImpl
{
public:
	Win32VFS() {}
	IoFilePtr OpenFile(const Path &path, uint32_t flags, OpenMode mode) override
	{
		bool writable = false;
		DWORD access = GENERIC_READ | FILE_READ_ATTRIBUTES;
		if(!(flags & file_flags::kReadOnly)) {
			access |= FILE_WRITE_DATA;
			writable = true;
		}
		if(flags & file_flags::kAppendOnly)
			access |= FILE_APPEND_DATA;
		DWORD creation = OpenModeToCreationDisposition(mode);

		std::string s(path);
		HANDLE h = CreateFileA(
		    s.data(), access, FILE_SHARE_READ, NULL, creation, FILE_FLAG_OVERLAPPED | FILE_ATTRIBUTE_NORMAL, NULL);
		if(h == INVALID_HANDLE_VALUE) {
			DWORD err = GetLastError();
			return nullptr;
		}
		return new Win32File(h, writable);
	}
	IoDirPtr OpenDir(const Path &path) override
	{
		std::string s(path);
		DWORD attr = GetFileAttributesA(s.data());
		if(attr == INVALID_FILE_ATTRIBUTES)
			return nullptr;
		if(!(attr & FILE_ATTRIBUTE_DIRECTORY))
			return nullptr;
		return new Win32Dir(path);
	}
	bool Delete(const Path &path) override
	{
		std::string s(path);
		return !!::DeleteFileA(s.c_str());
	}

	bool CreateDirectory(const Path &path) override
	{
		std::string s(path);
		return ::CreateDirectoryA(s.c_str(), NULL);
	}

	bool Stat(const Path &path, StatBuf *buf) override
	{
		std::string s(path);
		WIN32_FILE_ATTRIBUTE_DATA attr;
		if(!GetFileAttributesExA(s.data(), GetFileExInfoStandard, &attr))
			return false;
		buf->size = attr.nFileSizeLow + ((uint64_t)attr.nFileSizeHigh << 32);
		buf->flags = 0;
		if(attr.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
			buf->flags |= file_flags::kIsDir;
		else
			buf->flags |= file_flags::kIsFile;
		if(attr.dwFileAttributes & FILE_ATTRIBUTE_READONLY)
			buf->flags |= file_flags::kReadOnly;
		return true;
	}
	bool CheckAccess(const Path &path, uint32_t flags) override
	{
		assert(false);
		return false;
	}
	uint64_t GetFreeBytesForWriting(const Path &path) override
	{
		std::string s(path);
		ULARGE_INTEGER free_avail;
		if(!GetDiskFreeSpaceExA(s.data(), &free_avail, NULL, NULL)) {
			return 0;
		}
		return free_avail.QuadPart;
	}
};
Win32VFS *win32vfs;

RefPtr<IoDir> Win32Dir::OpenSubdir(const Path &path)
{
	if(path.size() >= 2 && path[1] == ':')
		return nullptr;
	std::string p = path_;
	if(p.back() != '\\')
		p.push_back('\\');
	p.append(path);
	return win32vfs->OpenDir(p);
}

RefPtr<IoFile> Win32Dir::OpenFile(const Path &path, uint32_t flags, OpenMode mode)
{
	if(path.size() >= 2 && path[1] == ':')
		return nullptr;
	std::string p = path_;
	if(p.back() != '\\')
		p.push_back('\\');
	p.append(path);
	return win32vfs->OpenFile(p, flags, mode);
}

} // namespace

VFSImpl *VFSImpl::GetOsVfs()
{
	if(!win32vfs)
		win32vfs = new Win32VFS();
	return win32vfs;
}

namespace {

std::string Utf16ToUtf8(wchar_t *str)
{
	DWORD n = WideCharToMultiByte(CP_UTF8, 0, str, -1, nullptr, 0, nullptr, nullptr);

	std::string ret;
	ret.resize(n - 1);
	WideCharToMultiByte(CP_UTF8, 0, str, -1, ret.data(), n, nullptr, nullptr);

	return ret;
}

void MaybeInitSaveDir(std::string& out_path, const char *in_path)
{
	if(!CreateDirectoryA(in_path, NULL)) {
		if(GetLastError() != ERROR_ALREADY_EXISTS)
			return;
	}

	std::string f = in_path;
	f.append("\\TEST_TEMP_FILE");

	HANDLE h = CreateFileA(f.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
	    OPEN_ALWAYS, FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, NULL);
	if(!h)
		return;
	CloseHandle(h);

	out_path = in_path;
}

} // namespace

bool SafeVFSSplit::PreInitialize(const Options &options)
{
	std::string app_path;
	std::string data_path;
	std::string temp_path;

	WCHAR buf[MAX_PATH + 2];
	GetCurrentDirectoryW(sizeof(buf) / sizeof(WCHAR), buf);
	app_path = Utf16ToUtf8(buf);

	data_path = app_path;
	if(!options.data_dir.empty()) {
		data_path.push_back('\\');
		data_path.append(options.data_dir);
	}

	SetCurrentDirectoryA(app_path.c_str());

	if(!GetTempPathW(sizeof(buf) / sizeof(WCHAR), buf)) {
		// Failed to get temp path!
		return false;
	}
	temp_path = Utf16ToUtf8(buf);

	if(safe_vfs_impl)
		delete safe_vfs_impl;
	safe_vfs_impl = new SafeVFSSplit(VFSImpl::GetOsVfs(), temp_path, data_path);
	safe_vfs = VFS(safe_vfs_impl);

	return true;
}

bool SafeVFSSplit::Initialize(const Options &options)
{
	std::string save_path;
	if(!options.use_writable_app_dir_if_possible.empty()) {
		MaybeInitSaveDir(save_path, options.use_writable_app_dir_if_possible.c_str());
	}

	if(save_path.empty() && !options.app_name.empty()) {
		PWSTR app_path_wstr;
		if(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, NULL, &app_path_wstr) == S_OK) {
			save_path = Utf16ToUtf8(app_path_wstr);
			CoTaskMemFree(app_path_wstr);
		} else {
			CoTaskMemFree(app_path_wstr);
			return false;
		}

		save_path.push_back('\\');
		if(!options.add_lune_subdir) {
			save_path.append("Lune\\");
		}
		save_path.append(options.app_name);
		if(!CreateDirectoryA(save_path.c_str(), NULL) && GetLastError() != ERROR_ALREADY_EXISTS)
			return false;
		save_path.push_back('\\');
	}

	if(!save_path.empty())
		safe_vfs_impl->SetSave(std::make_shared<SafeVFSImpl>(VFSImpl::GetOsVfs(), save_path));

	return true;
}

} // namespace lune
