#ifndef _INCLUDE_DHOOKS_CLIENT_ENTITY_HACKS_H_
#define _INCLUDE_DHOOKS_CLIENT_ENTITY_HACKS_H_

#include "sdk-hacks.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

// Minimal Black Mesa client ABI. DHooks intentionally builds with
// META_NO_HL2SDK, so only virtual slots used by the client hook bridge live
// here. Keep these declarations in lockstep with the BMS HL2SDK.
class IDHooksClientNetworkable;

class IDHooksHandleEntity
{
public:
	virtual ~IDHooksHandleEntity() {}
	virtual void SetRefEHandle(const CBaseHandle &handle) = 0;
	virtual const CBaseHandle &GetRefEHandle() const = 0;
};

class IDHooksClientUnknown : public IDHooksHandleEntity
{
public:
	virtual void *GetCollideable() = 0;
	virtual IDHooksClientNetworkable *GetClientNetworkable() = 0;
	virtual void *GetClientRenderable() = 0;
	virtual void *GetIClientEntity() = 0;
	virtual void *GetBaseEntity() = 0;
	virtual void *GetClientThinkable() = 0;
};

class IDHooksClientNetworkable
{
public:
	virtual IDHooksClientUnknown *GetIClientUnknown() = 0;
	virtual void Release() = 0;
	virtual void *GetClientClass() = 0;
	virtual void NotifyShouldTransmit(int state) = 0;
	virtual void OnPreDataChanged(int updateType) = 0;
	virtual void OnDataChanged(int updateType) = 0;
	virtual void PreDataUpdate(int updateType) = 0;
	virtual void PostDataUpdate(int updateType) = 0;
	virtual bool IsDormant() = 0;
	virtual int entindex() const = 0;
};

class IDHooksClientEntityList
{
public:
	virtual IDHooksClientNetworkable *GetClientNetworkable(int entnum) = 0;
	virtual IDHooksClientNetworkable *GetClientNetworkableFromHandle(CBaseHandle handle) = 0;
	virtual IDHooksClientUnknown *GetClientUnknownFromHandle(CBaseHandle handle) = 0;
	virtual void *GetClientEntity(int entnum) = 0;
	virtual void *GetClientEntityFromHandle(CBaseHandle handle) = 0;
	virtual int NumberOfEntities(bool includeNonNetworkable) = 0;
	virtual int GetHighestEntityIndex() = 0;
	virtual void SetMaxEntities(int maxents) = 0;
	virtual int GetMaxEntities() = 0;
};

using DHooksToolHandle = uint32_t;
constexpr DHooksToolHandle DHOOKS_TOOL_HANDLE_INVALID = 0;

class IDHooksClientTools
{
public:
	// IClientTools derives from IBaseInterface, whose virtual destructor is the
	// first vtable slot. Omitting it shifts every tool method by one slot.
	virtual ~IDHooksClientTools() {}
	virtual DHooksToolHandle AttachToEntity(void *entity) = 0;
	virtual void DetachFromEntity(void *entity) = 0;
	virtual bool IsValidHandle(DHooksToolHandle handle) = 0;
	virtual int GetNumRecordables() = 0;
	virtual DHooksToolHandle GetRecordable(int index) = 0;
	virtual void *NextEntity(void *currentEntity) = 0;
	void *FirstEntity() { return NextEntity(nullptr); }
	virtual void SetEnabled(DHooksToolHandle handle, bool enabled) = 0;
	virtual void SetRecording(DHooksToolHandle handle, bool recording) = 0;
	virtual bool ShouldRecord(DHooksToolHandle handle) = 0;
	virtual DHooksToolHandle GetToolHandleForEntityByIndex(int entindex) = 0;
	virtual int GetModelIndex(DHooksToolHandle handle) = 0;
	virtual const char *GetModelName(DHooksToolHandle handle) = 0;
	virtual const char *GetClassname(DHooksToolHandle handle) = 0;
};

class IDHooksClientEntityListener
{
public:
	virtual void OnEntityCreated(void *entity) {}
	virtual void OnEntityDeleted(void *entity) {}
};

// The first three IMemAlloc slots are enough to grow CClientEntityList's
// CUtlVector using the same allocator that owns its existing memory.
class IDHooksMemAlloc
{
public:
	virtual void *Alloc(size_t size) = 0;
	virtual void *Realloc(void *memory, size_t size) = 0;
	virtual void Free(void *memory) = 0;
};

// ABI mirror of CUtlVector<IDHooksClientEntityListener *> on 32-bit Source.
// It is a non-owning view over client.dll memory and must never be destroyed.
class DHooksClientListenerVector
{
public:
	int Count() const { return size_; }
	int AllocationCount() const { return allocationCount_; }
	int GrowSize() const { return growSize_; }
	IDHooksClientEntityListener **Base() const { return memory_; }
	bool HasMatchingDebugPointer() const { return elements_ == memory_; }

	int Find(IDHooksClientEntityListener *listener) const
	{
		for (int i = 0; i < size_; ++i)
			if (memory_[i] == listener)
				return i;
		return -1;
	}

	bool AddToTail(IDHooksClientEntityListener *listener, IDHooksMemAlloc *allocator)
	{
		if (size_ >= allocationCount_)
		{
			if (!allocator || growSize_ < 0)
				return false;
			int newCount = allocationCount_;
			if (growSize_ > 0)
				newCount = ((size_ / growSize_) + 1) * growSize_;
			else
			{
				if (!newCount)
					newCount = (31 + static_cast<int>(sizeof(listener))) /
						static_cast<int>(sizeof(listener));
				while (newCount <= size_)
					newCount *= 2;
			}
			void *grown = memory_
				? allocator->Realloc(memory_, static_cast<size_t>(newCount) * sizeof(listener))
				: allocator->Alloc(static_cast<size_t>(newCount) * sizeof(listener));
			if (!grown)
				return false;
			memory_ = static_cast<IDHooksClientEntityListener **>(grown);
			allocationCount_ = newCount;
			elements_ = memory_;
		}
		memory_[size_++] = listener;
		return true;
	}

	bool FindAndRemove(IDHooksClientEntityListener *listener)
	{
		const int index = Find(listener);
		if (index < 0)
			return false;
		if (index + 1 < size_)
			std::memmove(memory_ + index, memory_ + index + 1,
				static_cast<size_t>(size_ - index - 1) * sizeof(listener));
		memory_[--size_] = nullptr;
		return true;
	}

private:
	IDHooksClientEntityListener **memory_;
	int allocationCount_;
	int growSize_;
	int size_;
	IDHooksClientEntityListener **elements_;
};

#if defined(_WIN32) && !defined(_WIN64)
static_assert(sizeof(DHooksClientListenerVector) == 20,
	"CUtlVector listener view must match the Source x86 ABI");
#endif

#endif
