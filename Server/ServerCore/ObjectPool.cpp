#include "pch.h"
#include "ObjectPool.h"
#include "MemoryPool.h"

ObjectPoolBase::ObjectPoolBase(Int32 dataSize)
{
	blockSize_ = dataSize + static_cast<Int32>(sizeof(MemoryHeader));
	pPool_ = new MemoryPool(blockSize_);
}

ObjectPoolBase::~ObjectPoolBase()
{
	delete pPool_;
}

void* ObjectPoolBase::Alloc()
{
	MemoryHeader* pHeader = pPool_->Pop();
	void* pObject = MemoryHeader::AttachHeader(pHeader, blockSize_);
	return pObject;
}

void ObjectPoolBase::Release(void* pPtr)
{
	MemoryHeader* pHeader = MemoryHeader::DetachHeader(pPtr);
	pPool_->Push(pHeader);
}

Int32 ObjectPoolBase::GetUseCount() const
{
	return pPool_->GetUseCount();
}