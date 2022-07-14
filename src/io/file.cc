#include "file.h"

namespace lune {

VFS sys_vfs(VFSImpl::GetOsVfs());
std::shared_ptr<VFSImpl> null_vfs = std::make_shared<NullVFSImpl>();
VFS safe_vfs(null_vfs.get());
SafeVFSSplit *safe_vfs_impl = nullptr;

std::vector<std::string> IoDir::EnumerateFiles()
{
	std::vector<std::string> ret;
	if(!EnumerateFiles("", [&ret](const FileInfo &fi) {
		   ret.push_back(fi.filename);
		   return true;
	   })) {
		return std::vector<std::string>();
	}
	return ret;
}

File::File(IoFile *f) : file_(f) {}
File::File(IoFilePtr f) : file_(std::move(f)) {}
File::~File() = default;

size_t File::ReadAbs(void *p, size_t n, uint64_t offset)
{
	OneShotEvent wrev;
	auto op = AsyncOp::AllocForSyncIo(&wrev);
	op->sg = op->default_sg;
	op->nsg = 1;
	op->default_sg[0].len = (BufLen)n;
	op->default_sg[0].buf = p;
	op->offset = offset;
	file_->BeginRead(op);
	wrev.wait();
	auto ret = op->transferred;
	op->Release();
	return ret;
}

size_t File::WriteAbs(const void *p, size_t n, uint64_t offset)
{
	OneShotEvent wrev;
	auto op = AsyncOp::AllocForSyncIo(&wrev);
	op->sg = op->default_sg;
	op->nsg = 1;
	op->default_sg[0].len = (BufLen)n;
	op->default_sg[0].buf = (void*)p;
	op->offset = offset;
	file_->BeginWrite(op);
	wrev.wait();
	auto ret = op->transferred;
	op->Release();
	return ret;
}

size_t File::Read(void *p, size_t n)
{
	OneShotEvent wrev;
	auto op = AsyncOp::AllocForSyncIo(&wrev);
	op->sg = op->default_sg;
	op->nsg = 1;
	op->default_sg[0].len = (BufLen)n;
	op->default_sg[0].buf = p;
	op->offset = position_;
	file_->BeginRead(op);
	wrev.wait();
	auto ret = op->transferred;
	position_ += ret;
	if(op->err == io_err::kEOF)
		eof_ = true;
	op->Release();
	return ret;
}

size_t File::Write(const void *p, size_t n)
{
	OneShotEvent wrev;
	auto op = AsyncOp::AllocForSyncIo(&wrev);
	op->sg = op->default_sg;
	op->nsg = 1;
	op->default_sg[0].len = (BufLen)n;
	op->default_sg[0].buf = const_cast<void*>(p);
	op->offset = position_;
	file_->BeginWrite(op);
	wrev.wait();
	uint32_t ret = op->transferred;
	position_ += ret;
	op->Release();
	return ret;
}

std::unique_ptr<OutputStream> File::CreateOutputStream()
{
	return std::make_unique<FileOutputStream>(file_.get());
}

RefPtr<Blob> File::AllocForBlobRead(AsyncOp **op_out, uint64_t size)
{
	RefPtr<Blob> b = new OwnedMemoryBlob(size);

	auto buf = IoBuffer::WrapEmptyBlob(b);
	auto op = AsyncOp::AllocForMaxWrite(buf);
	op->completion = [](void *ctx, AsyncOp *op) {
		Blob *b = (Blob *)ctx;
		b->Resolved(!!op->err);
	};
	op->completion_context = b;
	*op_out = op;
	return b;
}

RefPtr<Blob> File::ReadToFutureBlob(uint64_t offset, uint64_t size)
{
	uint64_t filesz = file_->GetFileSize();
	if(filesz == 0) {
		auto b = new OwnedMemoryBlob(0);
		b->Resolved(false);
		return b;
	}
	if(size == 0)
		size = filesz;
	AsyncOp *op;
	RefPtr<Blob> b = AllocForBlobRead(&op, size);
	op->offset = offset;

	file_->BeginRead(op);

	return b;
}

RefPtr<Blob> File::ReadToImmediateBlob(uint64_t offset, uint64_t size)
{
	OneShotEvent wrev;
	auto b = ReadToFutureBlob(offset, size);
	b->Then(std::bind(&OneShotEvent::signal, &wrev));
	wrev.wait();

	return b;
}

class MappedBlob : public Blob
{
public:
	MappedBlob(std::unique_ptr<ShmRegion> shm) : shm_(std::move(shm)) {}
	std::pair<void *, size_t> GetContents() override
	{
		return std::make_pair(shm_->ptr, shm_->size);
	}

	bool empty()
	{
		return GetSize() == 0;
	}

private:
	std::unique_ptr<ShmRegion> shm_;
};

RefPtr<Blob> File::MapToBlob(uint64_t offset, uint64_t size, bool ro)
{
	auto map = file_->MapRegion(nullptr, offset, size, ro);
	if(!map)
		return nullptr;
	return new MappedBlob(std::move(map));
}


VFS::VFS(VFSImpl *impl) : impl_(impl) {}

File VFS::OpenFile(const Path &path, uint32_t flags, OpenMode mode)
{
	auto f = impl_->OpenFile(path, flags, mode);
	if(!f)
		return File(nullptr);
	return File(std::move(f));
}

bool VFS::CreateDirectory(const Path &path)
{
	return impl_->CreateDirectory(path);
}

bool VFS::Delete(const Path &path)
{
	return impl_->Delete(path);
}

bool VFS::Stat(const Path &path, StatBuf *buf)
{
	return impl_->Stat(path, buf);
}

bool VFS::CheckAccess(const Path &path, uint32_t flags)
{
	return impl_->CheckAccess(path, flags);
}

FileOutputStream::FileOutputStream(IoFile *f) : file_(f) {}
FileOutputStream ::~FileOutputStream() {}

void FileOutputStream::WriteAsync(IoBuffer *buffer)
{
	auto op = AsyncOp::AllocForMaxRead(buffer);
	op->offset = kAppendOffset;
	file_->BeginWrite(op);
}

void FileOutputStream::Write(const void *data, uint32_t size)
{
	OneShotEvent wrev;
	auto op = AsyncOp::AllocForSyncIo(&wrev);
	op->sg = op->default_sg;
	op->nsg = 1;
	op->default_sg[0].len = size;
	op->default_sg[0].buf = const_cast<void *>(data);
	op->offset = kAppendOffset;
	file_->BeginWrite(op);
	wrev.wait();
	op->Release();
}

void FileOutputStream::Flush()
{
	file_->Flush();
}

StdioOutputStream::StdioOutputStream(FILE *f, bool owned) : f_(f), owned_(owned) {}
StdioOutputStream::~StdioOutputStream()
{
	if(owned_)
		fclose(f_);
}

void StdioOutputStream::Write(const void *data, uint32_t size)
{
	fwrite(data, size, 1, f_);
}

void StdioOutputStream::Flush()
{
	fflush(f_);
}

SafeVFSImpl::SafeVFSImpl(VFSImpl *real, const Path &root_path) : real_(real)
{
	root_path_.assign(root_path);
	for(auto &ch : root_path_)
		if(ch == '\\')
			ch = '/';

	if(!root_path_.empty() && root_path_.back() != '/')
		root_path_.push_back('/');
}
SafeVFSImpl::~SafeVFSImpl() = default;

IoFilePtr SafeVFSImpl::OpenFile(const Path &path, uint32_t flags, OpenMode mode)
{
	if(!CheckPath(path))
		return nullptr;
	std::string p = root_path_;
	p.append(path);
	return real_->OpenFile(p, flags, mode);
}
IoDirPtr SafeVFSImpl::OpenDir(const Path &path)
{
	if(!CheckPath(path))
		return nullptr;
	std::string p = root_path_;
	p.append(path);
	return real_->OpenDir(p);
}

bool SafeVFSImpl::CreateDirectory(const Path &path)
{
	if(!CheckPath(path))
		return false;
	std::string p = root_path_;
	p.append(path);
	return real_->CreateDirectory(p);
}
bool SafeVFSImpl::Delete(const Path &path)
{
	if(!CheckPath(path))
		return false;
	std::string p = root_path_;
	p.append(path);
	return real_->Delete(p);
}

bool SafeVFSImpl::Stat(const Path &path, StatBuf *buf)
{
	if(!CheckPath(path))
		return false;
	std::string p = root_path_;
	p.append(path);
	return real_->Stat(p, buf);
}

bool SafeVFSImpl::CheckAccess(const Path &path, uint32_t flags)
{
	if(!CheckPath(path))
		return false;
	std::string p = root_path_;
	p.append(path);
	return real_->CheckAccess(p, flags);
}

uint64_t SafeVFSImpl::GetFreeBytesForWriting(const Path& path)
{
	if(!CheckPath(path))
		return false;
	std::string p = root_path_;
	p.append(path);
	return real_->GetFreeBytesForWriting(p);
}

bool SafeVFSImpl::CheckPath(const Path &p) const
{
	int s = 1;
	for(auto ch : p) {
		switch(ch) {
		case '\\':
		case '/':
			if(s == 3)
				return false;
			s = 1;
			break;
		case '.':
			if(s > 0)
				s++;
			break;
		default:
			s = 0;
			break;
		}
	}
	return s != 3;
}


SafeVFSSplit::SafeVFSSplit(
    VFSImpl *real, const Path &temp_path, const Path &data_path)
    : data_vfs_(std::make_shared<SafeVFSImpl>(real, data_path)),
	save_vfs_(std::make_shared<NullVFSImpl>()),
	temp_vfs_(real, temp_path)
{
	game_vfs_ = data_vfs_;
}
SafeVFSSplit::~SafeVFSSplit() = default;

IoFilePtr SafeVFSSplit::OpenFile(const Path &path, uint32_t flags, OpenMode mode)
{
	Path p = path;
	auto vfs = Lookup(p);
	if(!vfs)
		return nullptr;
	return vfs->OpenFile(p, flags, mode);
}
IoDirPtr SafeVFSSplit::OpenDir(const Path &path)
{
	Path p = path;
	auto vfs = Lookup(p);
	if(!vfs)
		return nullptr;
	return vfs->OpenDir(p);
}

bool SafeVFSSplit::CreateDirectory(const Path &path)
{
	Path p = path;
	auto vfs = Lookup(p);
	return vfs->CreateDirectory(p);
}
bool SafeVFSSplit::Delete(const Path &path)
{
	Path p = path;
	auto vfs = Lookup(p);
	return vfs->Delete(p);
}

bool SafeVFSSplit::Stat(const Path &path, StatBuf *buf)
{
	Path p = path;
	auto vfs = Lookup(p);
	return vfs->Stat(p, buf);
}
bool SafeVFSSplit::CheckAccess(const Path &path, uint32_t flags)
{
	Path p = path;
	auto vfs = Lookup(p);
	return vfs->CheckAccess(p, flags);
}

uint64_t SafeVFSSplit::GetFreeBytesForWriting(const Path &path)
{
	Path p = path;
	auto vfs = Lookup(p);
	return vfs->GetFreeBytesForWriting(p);
}

VFSImpl *SafeVFSSplit::Lookup(Path &p)
{
	if(p.starts_with("/game/")) {
		p = p.substr(6);
		return game_vfs_.get();
	}
	if(p.starts_with("/data/")) {
		p = p.substr(6);
		return data_vfs_.get();
	}
	if(p.starts_with("/save/")) {
		p = p.substr(6);
		return save_vfs_.get();
	}
	if(p.starts_with("/temp/")) {
		p = p.substr(6);
		return &temp_vfs_;
	}

	for(auto &e : custom_) {
		if(p.starts_with(std::string_view(e.prefix, e.len))) {
			p = p.substr(e.len);
			return e.vfs.get();
		}
	}
	return null_vfs.get();
}

void SafeVFSSplit::Add(const char *prefix, std::shared_ptr<VFSImpl> vfs)
{
	custom_.emplace_back();
	auto& e = custom_.back();

	e.len = strlen(prefix);
	assert(e.len < sizeof(e.prefix));
	strncpy_s(e.prefix, prefix, sizeof(e.prefix));
	e.prefix[sizeof(e.prefix) - 1] = '\0';
	e.vfs = std::move(vfs);
}

IoFilePtr VFSOverlay::OpenFile(const Path &path, uint32_t flags, OpenMode mode)
{
	for(auto &e : entries_) {
		std::string s = e.root;
		s.append(path);
		auto f = e.impl->OpenFile(s, flags, mode);
		if(f)
			return f;
	}
	return nullptr;
}

IoDirPtr VFSOverlay::OpenDir(const Path &path)
{
	for(auto &e : entries_) {
		std::string s = e.root;
		s.append(path);
		auto f = e.impl->OpenDir(s);
		if(f)
			return f;
	}
	return nullptr;
}

bool VFSOverlay::CreateDirectory(const Path &path)
{
	return false;
}
bool VFSOverlay::Delete(const Path &path)
{
	return false;
}

bool VFSOverlay::Stat(const Path &path, StatBuf *buf)
{
	for(auto &e : entries_) {
		std::string s = e.root;
		s.append(path);
		if(e.impl->Stat(s, buf))
			return true;
	}
	return false;
}
bool VFSOverlay::CheckAccess(const Path &path, uint32_t flags)
{
	for(auto &e : entries_) {
		std::string s = e.root;
		s.append(path);
		if(e.impl->CheckAccess(s, flags))
			return true;
	}
	return false;
}

void VFSOverlay::Add(VFSImpl *impl, std::string root)
{
	entries_.emplace_back(Entry{impl, std::move(root)});
}

} // namespace lune
