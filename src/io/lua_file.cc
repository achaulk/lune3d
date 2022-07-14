#include "lua/lua.h"
#include "lua/luabuiltin.h"

#include "file.h"

#include <stdint.h>

namespace lune {


void *ffi_fs_open(const char *name, size_t len, uint32_t mode, uint32_t open)
{
	uint32_t flags = file_flags::kReadOnly;
	OpenMode open_mode = OpenMode::OpenExisting;
	auto f = safe_vfs.OpenFile(Path(name, len), flags, open_mode);
	if(!f) {
		return nullptr;
	}
	auto raw = f.file();
	raw->AddRef();
	return raw;
}

void ffi_fs_close(IoFile *f)
{
	f->Release();
}

uint64_t ffi_fs_size(IoFile *f)
{
	return f->GetFileSize();
}

int ffi_fs_get_error()
{
	return 0;
}

void *fs_read(IoFile *f, uint64_t offset, uint64_t size)
{
	auto b = File(f).ReadToFutureBlob(offset, size);
	if(!b)
		return nullptr;

	b->AddRef();
	return b;
}


void blob_destroy(Blob *b)
{
	b->Release();
}
uint64_t blob_size(Blob *b)
{
	return b->GetSize();
}
void *blob_data(Blob *b)
{
	b->wait();
	return b->GetData();
}


LUA_REGISTER_FFI_FNS("fs", "open", &ffi_fs_open, "close", &ffi_fs_close, "size", &ffi_fs_size, "get_error",
    &ffi_fs_get_error, "read", &fs_read);
LUA_REGISTER_FFI_FNS("blob", "destroy", &blob_destroy, "size", &blob_size, "data", &blob_data);


LUA_REGISTER_SETUP(R"(
local ffi = require "ffi"
local C = ffi.C

ffi.cdef[[
int fs_get_error();
void* fs_open(const char *name, size_t len, uint32_t mode, uint32_t open);
void fs_close(void*);
uint64_t fs_size(void*);

void* fs_read(void*, uint64_t offset, uint64_t size);

void blob_destroy(void*);
uint64_t blob_size(void*);
uint8_t* blob_data(void*);
]]

local internal = internal
local tostring = tostring
local tonumber = tonumber

local blobmap = internal.blobmap
local blob = {}
local blob_mt = {__index=blob, __metatable="Blob", __gc=function(t) C.blob_destroy(blobmap[t]) end,
	__tostring=function(t) return t:getString() end}

function blob:getString(offset)
	local b = blobmap[self]
	local sz = C.blob_size(b)	
	local ptr = C.blob_data(b);
	local ofs = (offset or 0)
	if ofs >= sz then return '' end
	return ffi.string(ptr + ofs, sz - ofs)
end

function blob:getSize()
	local b = blobmap[self]
	local sz = C.blob_size(b)
	return tonumber(sz)
end

function internal.make_blob(b)
	local t = setmetatable({}, blob_mt)
	blobmap[t] = b
	return t
end

local file_errors = {}
local filemap = setmetatable({}, {__mode="k"})
local file = {}
local file_mt = {__index=file, __metatable="File", __gc=function(t) C.fs_close(filemap[t]) end}

function file.type() return "File" end

function file:readToBlob()
	local f = filemap[self]
	local blob = C.fs_read(f, 0, 0);
	if not blob then return nil, file_errors[C.fs_get_error()] end

	return internal.make_blob(blob)
end

function file:writeFromBlob(blob)
	error('NYI')
end

function file:size()
	local f = filemap[self]
	return fs_size(f)
end

lune.fs = lune.fs or {}

function lune.fs.open(name, opts)
	local mode, open = 0, 0
	if opts then
		if opts.rw then mode = mode + 1 end
		if opts.append then mode = mode + 2 end
		if opts.truncate then open = open + 1 end
		if opts.create then open = open + 2 end
	end
	local s = tostring(name)
	local f = C.fs_open(s, #s, mode, open)
	if not f then return nil, file_errors[C.fs_get_error()] end
	local t = setmetatable({offset=0}, file_mt)
	filemap[t] = f
	return t
end

function lune.fs.read(name, size)
	local f, err = lune.fs.open(name)
	if f then return f:readToBlob(size) end
	return nil, err
end

function lune.fs.load(name)
	local contents, err = lune.fs.read(name)
	if not contents then return nil, err end
	local fn, err = loadstring(tostring(contents))
	if fn then setfenv(fn, root_jail) end
	return fn, err
end

)");
}
